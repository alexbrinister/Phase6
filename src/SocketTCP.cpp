/**
* \file SocketTCP.cpp
* \details Linux SocketTCP Class - Definitions
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
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

/* Phase 6 SocketTCP library headers */
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

void socksahoy::SocketTCP::Receive(Segment& dest_segment)
{
    // Checks for errors and tracks the actual number of bytes received
    int numBytes = 0;

    socklen_t remoteAddrLen = sizeof(remoteAddr_);

    // Receive a segment of data from the baseSock_ and store
    // the address of the sender so that we can send segments
    // back to them.
    numBytes = recvfrom(baseSock_,
            dest_segment.GetSegment(),
            dest_segment.vectorSize_,
            0, (SockAddr*)&remoteAddr_,
            &remoteAddrLen);

    // Throw an exception with the string corresponding to errno
    if (numBytes == -1)
    {
        throw std::runtime_error(std::strerror(errno));
    }

    // Unpack the segment's header data.
    dest_segment.Deserialize();
}

void socksahoy::SocketTCP::Send(Segment& segment,
        const std::string& destAddr,
        unsigned int destPort,
        unsigned int sendBitErrorPercent,
        unsigned int sendSegmentLoss)
{
    //Checks for errors and tracks the actual number of bytes sent
    int numBytes = 0;

    // Reuse the addressinfo object to send segments
    GetAddressInfo(destPort, destAddr);

    //Pack the header info into the segment.
    segment.Serialize();

    //Calculate the segment's checksum value.
    segment.CalculateChecksum(sendBitErrorPercent);

    //If sendSegmentLoss <= 0, no loss should occur
    if (sendSegmentLoss > 0)
    {
        // Random number engine and distribution
        // Distribution in range [1, 100]
        std::random_device dev;
        std::mt19937 rng(dev());

        using distType = std::mt19937::result_type;
        std::uniform_int_distribution<distType> uniformDist(1, 100);

        unsigned int random_number = uniformDist(rng);

        // Check the random number against the loss percent to see
        // if this segment will be lost, if it's greater than it the
        // segment won't be lost
        if (sendSegmentLoss < random_number)
        {
            printf("Sending to address: %s ", destAddr.c_str());
            printf("With port: %u\n", destPort);

            // Send the segment to the specified address
            numBytes = sendto(baseSock_,
                    segment.GetSegment(),
                    segment.vectorSize_,
                    0, addr_->ai_addr, addr_->ai_addrlen);
        }

        else
        {
            printf("Loss Occurred\n");
        }
    }

    // No loss, sendSegmentLoss <= 0
    else
    {
        printf("Sending to address: %s ", destAddr.c_str());
        printf("With port: %u\n", destPort);

        // Send the segment to the specified address
        numBytes = sendto(baseSock_,
                segment.GetSegment(),
                segment.vectorSize_,
                0, addr_->ai_addr, addr_->ai_addrlen);
    }

    // Throw an exception with the string corresponding to errno
    if (numBytes == -1)
    {
        throw std::runtime_error(std::strerror(errno));
    }

    FreeAddressInfo();
}
bool socksahoy::SocketTCP::CheckReceive()
{
    // Checks for errors and tracks the actual number of bytes received
    int numBytes = 0;

    // Clear the set of file discriptors
    FD_ZERO(&readfds);

    // Add the socket descriptor to the set
    FD_SET(baseSock_, &readfds);

    // Set the n value for the select function
    int n = baseSock_ + 1;

    // Set the time out to be 10us
    tv.tv_sec = 0;
    tv.tv_usec = 10;

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