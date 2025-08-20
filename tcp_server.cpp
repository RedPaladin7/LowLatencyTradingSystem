#include "tcp_server.h"
using namespace std;

namespace Common {
    auto TCPServer::addToEpollList(TCPSocket *socket) {
        // tells epoll to monitor a given socket for incoming data
        // struct epoll_event {
        //     uint32_t events;     
        //     epoll_data_t data;
        // };
        // EPOLLET: notify only when new events happen
        // EPOLLIN: data is ready to be read
        epoll_event ev{EPOLLET | EPOLLIN, {reinterpret_cast<void *>(socket)}};
        return !epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket->socket_fd_, &ev);
    }

    auto TCPServer::listen(const string &iface, int port) -> void {
        // server socker set up to listen
        // registers it with epoll monitoring
        epoll_fd_ = epoll_create(1);
        ASSERT(epoll_fd_ >= 0, "epoll_create() failed error:" + string(strerror(errno)));

        ASSERT(listener_socket_.connect("", iface, port, true) >= 0,
            "Listener socket failed to connect. iface:" + iface + " port:" + to_string(port) + " error:" +
            string(strerror(errno)));

        ASSERT(addToEpollList(&listener_socket_), "epoll_ctl() failed. error:" + string(std::strerror(errno)));
    }

    auto TCPServer::sendAndRecv() noexcept -> void{
        auto recv = false;

        // calls sendAndRecv on all the available receiving sockets
        for_each(receive_sockets_.begin(), receive_sockets_.end(), [&recv](auto socket){
            socket->sendAndRecv();
        });
    }

    auto TCPServer::poll() noexcept -> void {
        const int max_events = 1 + send_sockets_.size() + receive_sockets_.size();

        // ask epoll for ready events and store them in events_
        const int n  = epoll_wait(epoll_fd_, events_, max_events, 0);
        bool have_new_connection = false;
        for(int i=0; i<n; ++i){
            const auto &event = events_[i];
            // recover the TCPSocket pointer (addToEpoll function)
            // when we call epoll_wait() we get list of all triggered events
            auto socket = reinterpret_cast<TCPSocket *>(event.data.ptr);

            if(event.events & EPOLLIN){
                if(socket == &listener_socket_){
                    // readable listener
                    // when a listener becomes readable it means that a new client it trying to connect with it
                    logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
                      have_new_connection = true;
                      continue;
                }
                // not a listener, means it is a client readable
                // we can process data whenever the event pool runs, it will not block new clients
                logger_.log("%:% %() % EPOLLIN socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);

                // if not already in receive_sockets_ add it
                if(find(receive_sockets_.begin(), receive_sockets_.end(), socket)==receive_sockets_.end()){
                    receive_sockets_.push_back(socket);
                }
            }
            if(event.events & EPOLLOUT) {
                // writeable socket
                logger_.log("%:% %() % EPOLLOUT socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
                // add to send sockets if not already there
                if (std::find(send_sockets_.begin(), send_sockets_.end(), socket) == send_sockets_.end()){
                    send_sockets_.push_back(socket);
                }
            }

            if (event.events & (EPOLLERR | EPOLLHUP)) {
                // in case of error or hangup
                logger_.log("%:% %() % EPOLLERR socket:%\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), socket->socket_fd_);
                // sent to receive sockets for graceful handling of disconnects or errors
                if (std::find(receive_sockets_.begin(), receive_sockets_.end(), socket) == receive_sockets_.end()){
                    receive_sockets_.push_back(socket);
                }
            }
            // we have figured which socketrs need io work
        }
        while(have_new_connection){
            // enter this in case of readable listener
            logger_.log("%:% %() % have_new_connection\n", __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str_));
            // drain all pending accepts until kernel returns EAGAIN, to get new notification
            sockaddr_storage addr;
            socklen_t addr_len = sizeof(addr);
            // a client finished tcp handshake and waiting to be accepted
            // accepting the new client connection
            int fd = accept(listener_socket_.socket_fd_, reinterpret_cast<sockaddr *>(&addr), &addr_len);
            if(fd == -1){
                break;
            }
            // make the new fd non-blocking and disable nagle
            ASSERT(setNonBlocking(fd) && disableNagle(fd), "Failed to set non-blocking and no-delay on socket:"+to_string(fd));
            logger_.log("%:% %() % accepted socket:%\n", __FILE__, __LINE__, __FUNCTION__,
            Common::getCurrentTimeStr(&time_str_), fd);

            // create new socket for the fd and fire its callback
            auto socket = new TCPSocket(logger_);
            socket->socket_fd_ = fd;
            socket->recv_callback_ = recv_callback_;
            // register the client socket with epoll
            ASSERT(addToEpollList(socket), "Unable to add socket. error:" + string(strerror(errno)));

            // mark it for read handling
            if (find(receive_sockets_.begin(), receive_sockets_.end(), socket) == receive_sockets_.end()){
                receive_sockets_.push_back(socket);
            }
        }
    }
}