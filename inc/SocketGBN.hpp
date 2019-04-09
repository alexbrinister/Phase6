/**
* \file SocketGBN.hpp
* \details Linux/Windows SocketGBN Class - Declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

#ifndef __SocketGBN_HPP__
#define __SocketGBN_HPP__

/* C++ Standard Template Library headers */
#include <string>

/* Linux SocketGBN libraries */
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

/* Phase 2 SocketGBN library headers */
#include "Packet.hpp"

/// Group namespace
namespace socksahoy
{
    /*
    * NOTE: Both Windows and Linux inherit from the BSD Socket library.
    * Windows implements some platform-specific extensions of the BSD standard
    * and Linux follows the POSIX standard for Sockes, which descends from
    * the BSD Socket specification.
    */
    /// Alias for general BSD-like SocketGBN address structure
    typedef struct sockaddr SockAddr;

    /// Alias for IPv4 BSD-like SocketGBN address structure
    typedef struct sockaddr_in SockAddrIpv4;

    /// Alias for IPv6 BSD-like SocketGBN address structure
    typedef struct sockaddr_in6 SockAddrIpv6;

    /// Alias for BSD-like IP address info structure
    typedef struct addrinfo SockAddrInfo;

    /// Alias for generic (i.e IPv4 and IPv6) BSD-like SocketGBN data structure
    typedef struct sockaddr_storage SockAddrStorage;

    /// Socket option flag enumeration.
    enum SocketFlag
    {
        SERVER 		= 1 << 0, 	///< SocketGBN flag for server
        CLIENT 		= 1 << 1,	///< SocketGBN flag for client
        TCP 		= 1 << 2,	///< SocketGBN flag for TCP
        UDP 		= 1 << 3,	///< SocketGBN flag for UDP
        BLOCK 		= 1 << 4,	///< SocketGBN flag for a blocking Socket
        NONBLOCK 	= 1 << 5,	///< SocketGBN flag for a nonblocking Socket
    };

    /**
    * \brief THE SocketGBN class.
    * \details Stores Socket structures common to all Socket types. This
    * includes provisions and functions for TCP and UDP
    */
    class SocketGBN
    {
        public:
            /**
            * \brief Constructor for a Socket.
            * \details Initializes a Windows/Linux Socket. This creates the
            * file descriptor and prepares the Socket for use. The default
            * is to create a client flag. Note that for a client Socket, the
            * port is the destination port and for the server, this is the
            * port the Socket binds to on the local host.
            * \param port The port to start the SocketGBN on
            * \param flag Flag to set the type of SocketGBN.
            * \param destAddr Destination address to send data to
            */
            SocketGBN(unsigned int port,
                    unsigned int flag = SERVER | UDP | BLOCK,
                    const std::string& destAddr = std::string());

            /// Disable the copy constructor
            SocketGBN(SocketGBN const&) = delete;

            /// Enable the move constructor
            SocketGBN(SocketGBN&&);

            /// Enable the operator= operator for move assignment.
            SocketGBN& operator=(SocketGBN&&);

            /**
            * \brief Destructor
            * \details Closes SocketGBN file
            * descriptor.
            */
            ~SocketGBN();

            /**
            * \brief Receive data from a remote SocketGBN.
            * \details Wrapper around the recvfrom() function.
            * \param dest_packet The packet to populate with received data.
            */
            template <std::size_t Size>
            void Receive(Packet<Size>& dest_packet)
            {
                //Checks for errors and tracks the actual number of bytes received
                int numBytes = 0;

                socklen_t remoteAddrLen = sizeof(remoteAddr_); \

                    // Receive a packet of data from the base_SocketGBN and store the address
                    // of the sender so that we can send packets back to them.
                    numBytes = recvfrom(baseSock_,
                        dest_packet.GetPacket(),
                        Size,
                        0, (SockAddr*)&remoteAddr_,
                        &remoteAddrLen);

                // Throw an exception with the string corresponding to errno
                if (numBytes == -1)
                {
                    throw std::runtime_error(std::strerror(errno));
                }

                //Unpack the packet's header data.
                dest_packet.Deserialize();
            }

            /**
            * \brief Send a single packet to a remote host.
            * \details Wrapper around the sendto() function.
            * \param packet The packet to send to the remote SocketGBN.
            * \param destAddr The destination address to send packet to.
            * \param destPort The destination port of the receiving host.
            * \param sendBitErrorPercent Percent of data packets to corrupt.
            * \param recvPacketLoss Percent of data packets that will be lost.
            */
            template <std::size_t Size>
            void Send(Packet<Size>& packet,
                const std::string& destAddr,
                unsigned int destPort,
                int sendBitErrorPercent,
                int sendPacketLoss)
            {
                //Checks for errors and tracks the actual number of bytes sent
                int numBytes = 0;

                // Reuse the addressinfo object to send packets
                GetAddressInfo(destPort, destAddr);

                //Pack the header info into the packet.
                packet.Serialize();

                //Calculate the packet's checksum value.
                packet.checksum_ = packet.CalculateChecksum(sendBitErrorPercent);

                //If sendPacketLoss <= 0, no loss should occur
                if (sendPacketLoss > 0)
                {
                    // Random number engine and distribution
                    // Distribution in range [1, 100]
                    std::random_device dev;
                    std::mt19937 rng(dev());

                    using distType = std::mt19937::result_type;
                    std::uniform_int_distribution<distType> uniformDist(1, 100);

                    int random_number = uniformDist(rng);

                    //Check the random number against the loss percent to see if this
                    //packet will be lost, if it's greater than it the packet won't be
                    //lost
                    if (sendPacketLoss < random_number)
                    {
                        //Send the packet to the specified address
                        numBytes = sendto(baseSock_,
                            packet.GetPacket(),
                            packet.GetPacketSize() + PACKET_HEADER_LEN,
                            0, addr_->ai_addr, addr_->ai_addrlen);
                    }
                }

                //No loss, sendPacketLoss <= 0
                else
                {
                    //Send the packet to the specified address
                    numBytes = sendto(baseSock_,
                        packet.GetPacket(),
                        packet.GetPacketSize() + PACKET_HEADER_LEN,
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
            * \brief Check if a packet can be received.
            * \details Wrapper around the select() function.
            * \return If the select function timed out.
            */
            bool CheckReceive();

            /**
            * \brief Bind a SocketGBN to a port.
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
            /// Base SocketGBN file descriptor.
            int baseSock_;

            /// Set of SocketGBN file discriptors for select().
            fd_set readfds;

            /// Time structure used for setting timeout in select().
            struct timeval tv;

            /// Address hints structure.
            SockAddrInfo hints_;

            /// Address information structure.
            SockAddrInfo* addr_;

            /// Connecting SocketGBN information
            SockAddrStorage remoteAddr_;

            /// The port this SocketGBN connects/binds to
            unsigned int port_;

            /// The sequence number of the next expected packet on the recieve side.
            int NextExpectedPacket_;

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

#endif /* end of SocketGBN.hpp */

// vim: set expandtab ts=4 sw=4: