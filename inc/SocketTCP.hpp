/**
* \file SocketTCP.hpp
* \details Linux SocketTCP Class - Declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
*/

#ifndef __SocketTCP_HPP__
#define __SocketTCP_HPP__

/* C++ Standard Template Library headers */
#include <string>

/* Linux SocketTCP libraries */
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

/* Phase 6 SocketTCP library headers */
#include "Segment.hpp"

/// Group namespace
namespace socksahoy
{
    /*
    * NOTE: Both Windows and Linux inherit from the BSD Socket library.
    * Windows implements some platform-specific extensions of the BSD standard
    * and Linux follows the POSIX standard for Sockes, which descends from
    * the BSD Socket specification.
    */
    /// Alias for general BSD-like SocketTCP address structure
    typedef struct sockaddr SockAddr;

    /// Alias for IPv4 BSD-like SocketTCP address structure
    typedef struct sockaddr_in SockAddrIpv4;

    /// Alias for IPv6 BSD-like SocketTCP address structure
    typedef struct sockaddr_in6 SockAddrIpv6;

    /// Alias for BSD-like IP address info structure
    typedef struct addrinfo SockAddrInfo;

    /// Alias for generic (i.e IPv4 and IPv6) BSD-like SocketTCP data structure
    typedef struct sockaddr_storage SockAddrStorage;

    /// Socket option flag enumeration.
    enum SocketFlag
    {
        SERVER 		= 1 << 0, 	///< SocketTCP flag for server
        CLIENT 		= 1 << 1,	///< SocketTCP flag for client
        TCP 		= 1 << 2,	///< SocketTCP flag for TCP
        UDP 		= 1 << 3,	///< SocketTCP flag for UDP
        BLOCK 		= 1 << 4,	///< SocketTCP flag for a blocking Socket
        NONBLOCK 	= 1 << 5,	///< SocketTCP flag for a nonblocking Socket
    };

    /**
    * \brief THE SocketTCP class.
    * \details Stores Socket structures common to all Socket types. This
    * includes provisions and functions for TCP and UDP
    */
    class SocketTCP
    {
        public:
            /**
            * \brief Constructor for a Socket.
            * \details Initializes a Linux Socket. This creates the
            * file descriptor and prepares the Socket for use. The default
            * is to create a client flag. Note that for a client Socket, the
            * port is the destination port and for the server, this is the
            * port the Socket binds to on the local host.
            * \param port The port to start the SocketTCP on
            * \param flag Flag to set the type of SocketTCP.
            * \param destAddr Destination address to send data to
            */
            SocketTCP(unsigned int port,
                    unsigned int flag = SERVER | UDP | BLOCK,
                    const std::string& destAddr = std::string());

            /// Disable the copy constructor
            SocketTCP(SocketTCP const&) = delete;

            /// Enable the move constructor
            SocketTCP(SocketTCP&&);

            /// Enable the operator= operator for move assignment.
            SocketTCP& operator=(SocketTCP&&);

            /**
            * \brief Destructor
            * \details Closes socket file
            * descriptor.
            */
            ~SocketTCP();

            /**
            * \brief Receive data from a remote socket.
            * \details Wrapper around the recvfrom() function.
            * \param dest_segment The segment to populate with received data.
            */
            void Receive(Segment& dest_segment)
            {
                //Checks for errors and tracks the actual number of bytes received
                int numBytes = 0;

                socklen_t remoteAddrLen = sizeof(remoteAddr_); \

                    // Receive a segment of data from the baseSock_ and store the address
                    // of the sender so that we can send segments back to them.
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

                //Unpack the segment's header data.
                dest_segment.Deserialize();
            }

            /**
            * \brief Send a single segment to a remote host.
            * \details Wrapper around the sendto() function.
            * \param segment The segment to send to the remote socket.
            * \param destAddr The destination address to send the segment to.
            * \param destPort The destination port of the receiving host.
            * \param sendBitErrorPercent Percent of data segments to corrupt.
            * \param recvsegmentLoss Percent of data segments that will be lost.
            */
            void Send(Segment& segment,
                const std::string& destAddr,
                unsigned int destPort,
                int sendBitErrorPercent,
                int sendSegmentLoss)
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

                    int random_number = uniformDist(rng);

                    //Check the random number against the loss percent to see if this
                    //segment will be lost, if it's greater than it the segment won't be
                    //lost
                    if (sendSegmentLoss < random_number)
                    {
                        std::cout << "Sending to address: " << destAddr << std::endl;
                        printf("With port: %d\n", destPort);
                        //Send the segment to the specified address
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

                //No loss, sendSegmentLoss <= 0
                else
                {
                    std::cout << "Sending to address: " << destAddr << std::endl;
                    printf("With port: %d\n", destPort);
                    //Send the segment to the specified address
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

            /**
            * \brief Check if a segment can be received safely without blocking.
            * \details Wrapper around the select() function.
            * \return If the select function timed out or not.
            */
            bool CheckReceive();

            /**
            * \brief Bind a SocketTCP to a port.
            * \details Wrapper around the bind() function, required to recieve segments using the socket.
            */
            void Bind();

            /**
            * \brief Get access to the remote address in string form
            * \return The address of the connecting host.
            */
            std::string GetRemoteAddress();

            /**
            * \brief Get access to the remote port in string form
            * \return The connecting port of the connecting host.
            */
            unsigned int GetRemotePort();

        private:
            /// Base SocketTCP file descriptor.
            int baseSock_;

            /// Set of SocketTCP file discriptors for select().
            fd_set readfds;

            /// Time structure used for setting timeout in select().
            struct timeval tv;

            /// Address hints structure.
            SockAddrInfo hints_;

            /// Address information structure.
            SockAddrInfo* addr_;

            /// Connecting SocketTCP information
            SockAddrStorage remoteAddr_;

            /// The port this SocketTCP connects/binds to
            unsigned int port_;

            /**
            * \brief Helper function to get address info for remote machine.
            * \details Wrapper around the getaddrinfo() function.
            * \param port The port of the address to get information for.
            * \param addrStr The address string to get address info for.
            */
            void GetAddressInfo(unsigned int port,
                    const std::string& addrStr = std::string());


            /**
            * \brief Frees the internal address info structure.
            */
            void FreeAddressInfo();
    };
}

#endif /* end of SocketTCP.hpp */

// vim: set expandtab ts=4 sw=4: