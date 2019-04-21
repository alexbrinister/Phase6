/**
* \file SocketTCP.cpp
* \details Linux/Windows SocketTCP Class - Definitions
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

/* Standard C++ Library headers */
#include <string>
#include <sstream>
#include <iostream>
#include <exception>
#include <random>

/* Standard C Library Headers */
#include <cstring>
#include <cerrno>

/* Linux-specific C library headers */
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
/* Linux SocketTCP/network library headers */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// For setting to non-blocking. Works on both Win and Lin
#include <fcntl.h>

/* Phase 2 SocketTCP library headers */
#include "Constants.hpp"
#include "SocketTCP.hpp"
#include "Segment.hpp"

socksahoy::SocketTCP::SocketTCP(unsigned int port,
                                unsigned int flag,
                                const std::string& destAddr)
    : baseSock_(0), addr_(nullptr), port_(port)
{
    // Clear the hints struct
    std::memset(&hints_, 0, sizeof(hints_));

    // Use IPv4
    hints_.ai_family = AF_INET;

    // Flag processing
    if (flag & socksahoy::TCP)
    {
        hints_.ai_socktype = SOCK_STREAM;
    }

    if (flag & socksahoy::UDP)
    {
        hints_.ai_socktype = SOCK_DGRAM;
    }

    if (flag & socksahoy::SERVER)
    {
        hints_.ai_flags = AI_PASSIVE;
    }

    GetAddressInfo(port_, destAddr);

    // Create the SocketTCP file descriptor (or kernel object in Windows)
    baseSock_ = socket(addr_->ai_family,
                       addr_->ai_socktype,
                       addr_->ai_protocol);

    if (baseSock_ == -1)
    {
        throw std::runtime_error(std::strerror(errno));
    }

    if (flag & socksahoy::NONBLOCK)
    {
        // States for the nonblocking case
        int error = 0;
        int flags = fcntl(baseSock_, F_GETFL, 0);

        if (flags == -1)
        {
            throw std::runtime_error(std::strerror(errno));
        }

        flags = flags | O_NONBLOCK;
        error = fcntl(baseSock_, F_SETFL, flags);

        if (error == -1)
        {
            throw std::runtime_error(std::strerror(errno));
        }
    }
}

socksahoy::SocketTCP::~SocketTCP()
{
    // Close the SocketTCP file descriptor
    if (baseSock_ != 0)
    {
        close(baseSock_);
    }
}

bool socksahoy::SocketTCP::CheckReceive()
{
    //Checks for errors and tracks the actual number of bytes received
    int numBytes = 0;

    // Clear the set of file discriptors
    FD_ZERO(&readfds);

    // Add the socket descriptor to the set
    FD_SET(baseSock_, &readfds);

    // Set the n value for the select function
    int n = baseSock_ + 1;

    // Set the time out to be 10us
    tv.tv_sec = 0;
    tv.tv_usec = 1000;

    numBytes = select(n, &readfds, NULL, NULL, &tv);

    // The select function returned an error
    if (numBytes == -1)
    {
        throw std::runtime_error(std::strerror(errno));
    }

    // The select function timed out
    else if (numBytes == 0)
    {
        return false;
    }

    //A packet arrived
    else
    {
        return true;
    }
}

void
socksahoy::SocketTCP::Bind()
{
    int error = bind(baseSock_, addr_->ai_addr, addr_->ai_addrlen);

    // Bind is the last function we really need this address info for. We can
    // get rid of it now.
    FreeAddressInfo();

    // Throw an exception with the string corresponding to errno
    if (error == -1)
    {
        throw std::runtime_error(std::strerror(errno));
    }
}

std::string
socksahoy::SocketTCP::GetRemoteAddress()
{
    // String to capture the connecting IP address
    std::string remoteaddrStr(INET_ADDRSTRLEN, '\0');

    void* rcvd_addr = &(((SockAddrIpv4*)&remoteAddr_)->sin_addr);

    // Convert binary address to human-readable format
    inet_ntop(remoteAddr_.ss_family, rcvd_addr,
              &remoteaddrStr[0], INET_ADDRSTRLEN);

    return remoteaddrStr;
}

unsigned int
socksahoy::SocketTCP::GetRemotePort()
{
    in_port_t rcvdPort = ((SockAddrIpv4*)&remoteAddr_)->sin_port;

    // Convert binary address to human-readable format
    return ntohs(rcvdPort);
}

void
socksahoy::SocketTCP::GetAddressInfo(unsigned int port,
                                     const std::string& addrStr)
{
    std::string port_str(std::to_string(port));

    /*
    * NOTE: getaddressinfo limits length of hostnames internally and nullptr
    * is a valid value for addr, so no checking is needed
    *
    * Also note the use of the ternary operator. This is to decide whether to
    * use the underlying C string in the addrStr or to pass a nullptr. This
    * is because an empty std::string never holds a nullptr.
    */
    int error = getaddrinfo(addrStr.empty() ? nullptr : addrStr.c_str(),
                            port_str.c_str(),
                            &hints_, &addr_);

    // Catch errors
    if (error != 0)
    {
        throw std::runtime_error(gai_strerror(error));
    }
}

void socksahoy::SocketTCP::FreeAddressInfo()
{
    // Can't free if addr_ is null...
    if (addr_ != nullptr)
    {
        freeaddrinfo(addr_);
    }
}

// vim: set expandtab ts=4 sw=4: