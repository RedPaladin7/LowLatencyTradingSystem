#pragma once 

#include <iostream>
#include <string>
#include <unordered_set>
#include <sstream>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <string>

#include "logging.h"
#include "macros.h"

namespace Common {
    struct SocketCfg {
        string ip_;
        string iface_;
        int port_ = -1;
        bool is_udp_ = false;
        bool is_listening_ = false;
        bool needs_so_timestamp_ = false;

        auto toString() const {
            stringstream ss;
            ss << "SocketCfg[ip:" << ip_
            << " iface:" << iface_
            << " port:" << port_
            << " is_udp:" << is_udp_
            << " is_listening:" << is_listening_
            << " needs_SO_timestamp:" << needs_so_timestamp_
            << "]";

            return ss.str();
        }
    };

    constexpr int MaxTCPServerBacklog = 1024;

    inline auto getIfaceIP(const string &iface) -> string {
        // looks for the passed in interface in the list of all available interfaces
        char buf[NI_MAXHOST] = {'\0'};
        ifaddrs *ifaddr = nullptr;

        // function takes in pointer to pointer
        // fills the given address with link list of available network interfaces
        if(getifaddrs(&ifaddr)!=-1){
            for(ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next){
                if(ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && iface == ifa->ifa_name){
                    // converts ip address to host name
                    getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
                    break;
                }
            }
            freeifaddrs(ifaddr);
        }
        return buf;
    }

    // socket returns immediately if not data is available
    inline auto setNonBlocking(int fd) -> bool {
        // gets the current flags of the file descriptor provided
        const auto flags = fcntl(fd, F_GETFL, 0);
        if(flags & O_NONBLOCK){
            return true;
        }
        return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
    }

    // disabling Nagle's algorithm
    inline auto disableNagle(int fd) -> bool {
        int one = 1;
        // void * is a generic pointer (cannot be deferenced because compiler does not know how many bytes to read)
        return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&one), sizeof(one))!=-1);
    }

    inline auto setSOTimestamp(int fd) -> bool {
        int one = 1;
        return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, reinterpret_cast<void *>(&one), sizeof(one))!=-1);
    }

    inline auto join(int fd, const string& ip) -> bool {
        // mreq has two fields
        // the multicast ip you want to join and the interface you want to join it from
        const ip_mreq mreq{{inet_addr(ip.c_str())}, {htonl(INADDR_ANY)}};
        return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))!=-1);
    }

    [[nodiscard]] inline auto createSocket (Logger& logger, const SocketCfg& socket_cfg) -> int {
        string time_str;

        const auto ip = socket_cfg.ip_.empty() ? getIfaceIP(socket_cfg.iface_) : socket_cfg.ip_;
        logger.log("%:% %() % cfg:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), socket_cfg.toString());

        // merging flags
        const int input_flags = (socket_cfg.is_listening_ ? AI_PASSIVE : 0) | (AI_NUMERICHOST | AI_NUMERICSERV);
        const addrinfo hints{
            input_flags,
            AF_INET,
            socket_cfg.is_udp_ ? SOCK_DGRAM : SOCK_STREAM,
            socket_cfg.is_udp_ ? IPPROTO_UDP : IPPROTO_TCP,
            0, 0, nullptr, nullptr
        };

        addrinfo *result = nullptr;
        // converts host name to ip address
        // result is stored in result variable
        const auto rc = getaddrinfo(ip.c_str(), to_string(socket_cfg.port_).c_str(), &hints, &result);
        ASSERT(!rc, "getaddrinfo() failed. error:" + string(gai_strerror(rc)) + "errno:" + strerror(errno));

        int socket_fd = -1;
        int one = 1;
        for(addrinfo *rp = result; rp; rp=rp->ai_next){
            ASSERT((socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) != -1, "socket() failed. errno:" + string(strerror(errno)));
            ASSERT(setNonBlocking(socket_fd), "setNonBlocking() failed. errno:" + string(strerror(errno)));

            if (!socket_cfg.is_udp_) { // disable Nagle for TCP sockets.
                ASSERT(disableNagle(socket_fd), "disableNagle() failed. errno:" + string(strerror(errno)));
            }

            if (!socket_cfg.is_listening_) { // establish connection to specified address.
                ASSERT(connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != 1, "connect() failed. errno:" + string(strerror(errno)));
            }

            if (socket_cfg.is_listening_) { // allow re-using the address in the call to bind()
                // multiple addresses can bind to same port
                ASSERT(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one)) == 0, "setsockopt() SO_REUSEADDR failed. errno:" + string(strerror(errno)));
            }

            // for the client the port is assigned to by the OS
            // for the server we have to define the port to bind to
            if (socket_cfg.is_listening_) {
                // bind to the specified port number.
                const sockaddr_in addr{AF_INET, htons(socket_cfg.port_), {htonl(INADDR_ANY)}, {}};
                ASSERT(bind(socket_fd, socket_cfg.is_udp_ ? reinterpret_cast<const struct sockaddr *>(&addr) : rp->ai_addr, sizeof(addr)) == 0, "bind() failed. errno:%" + string(strerror(errno)));
            }

            if (!socket_cfg.is_udp_ && socket_cfg.is_listening_) { // listen for incoming TCP connections.
                ASSERT(listen(socket_fd, MaxTCPServerBacklog) == 0, "listen() failed. errno:" + string(strerror(errno)));
            }

            if (socket_cfg.needs_so_timestamp_) { // enable software receive timestamps.
                ASSERT(setSOTimestamp(socket_fd), "setSOTimestamp() failed. errno:" + string(strerror(errno)));
            }
        }
    }
}