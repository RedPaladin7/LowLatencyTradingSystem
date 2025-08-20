#include "tcp_socket.h"
using namespace std;

namespace Common {
    auto TCPSocket::connect(const string &ip, const string &iface, int port, bool is_listening)->int{
        const SocketCfg socket_cfg{ip, iface, port, false, is_listening, true};
        socket_fd_ = createSocket(logger_, socket_cfg);

        // telling the server to listen on all interfaces
        socket_attrib_.sin_addr.s_addr = INADDR_ANY;
        // 16 bit number from host byte order to network byte order
        socket_attrib_.sin_port = htons(port);
        socket_attrib_.sin_family = AF_INET;

        return socket_fd_;
    }

    // Ancillary data: non payload data accomapanying normal data sent or received over the sockets
    // cmsghdr is the header for each of the ancillary block

    auto TCPSocket::sendAndRecv() noexcept -> bool {
        // the size of the buffer it timeval, because an ancillary data will be timestamps
        char ctrl[CMSG_SPACE(sizeof(struct timeval))];
        // cmsghdr has fields: len, level (protocol level) and type (type of control message)
        auto cmsg = reinterpret_cast<struct cmsghdr *>(&ctrl);

        // used for scatter gather io
        // reading or writing to multiple non contiguous memory buffers in a single system call
        // fields: starting address, length of buffer
        iovec iov{inbound_data_.data() + next_rcv_valid_index_, TCPBufferSize-next_rcv_valid_index_};
        // store data in inbound_data_, and the timestamp in ctrl
        msghdr msg{&socket_attrib_, sizeof(socket_attrib_), &iov, 1, ctrl, sizeof(ctrl), 0};

        // msghdr: used for main message metadata
        // cmsghdr: used for anicllary message

        // struct msghdr {
        //     void         *msg_name;       
        //     socklen_t     msg_namelen;    
        //     struct iovec *msg_iov;        
        //     size_t        msg_iovlen;     
        //     void         *msg_control;    
        //     size_t        msg_controllen;
        //     int           msg_flags;      
        // };

        const auto read_size = recvmsg(socket_fd_, &msg, MSG_DONTWAIT);
        if(read_size > 0){
            //  updating next valid read index
            next_rcv_valid_index_ += read_size;

            // *
            Nanos kernel_time = 0;
            timeval time_kernel;
            if(cmsg->cmsg_level == SOL_SOCKET && 
                cmsg->cmsg_type == SCM_TIMESTAMP &&
                cmsg->cmsg_len == CMSG_LEN(sizeof(time_kernel))) {
                    // value of cmsg is put in time_kernel
                    memcpy(&time_kernel, CMSG_DATA(cmsg), sizeof(time_kernel));
                    // final time
                    kernel_time = time_kernel.tv_sec * NANOS_TO_SECS + time_kernel.tv_usec * NANOS_TO_MICROS;
                }
            const auto user_time = getCurrentNanos();

            logger_.log("%:% %() % read socket:% len:% utime:% ktime:% diff:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), socket_fd_, next_rcv_valid_index_, user_time, kernel_time, (user_time - kernel_time));
            recv_callback_(this, kernel_time);
        }

        // send pending outbound data if any
        if(next_send_valid_index_ > 0){
            const auto n = ::send(socket_fd_, outbound_data_.data(), next_send_valid_index_, MSG_DONTWAIT | MSG_NOSIGNAL);
            logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), socket_fd_, n);
        }
        next_rcv_valid_index_ = 0;
        return (read_size > 0); 
    }

    auto TCPSocket::send(const void *data, size_t len) noexcept ->void {
        // copies the data into outbound buffer
        memcpy(outbound_data_.data() + next_send_valid_index_, data, len);
        next_send_valid_index_ += true;
    }
}
