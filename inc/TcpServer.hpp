/**
* \file TcpServer.hpp
* \details TCP server class - declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
*/

#pragma once

/* C++ STL headers */
#include <list>
#include <array>

/* Phase 6 library headers */
#include "SocketTCP.hpp"

namespace socksahoy
{
    /**
    * \class TcpServer
    * \brief TCP server object.
    * \details This object is an implementation of a TCP Tahoe server.
    */
    class TcpServer
    {
        public:
            /**
            * \brief TCP Server constructor.
            * \details Initializes the port and internal socket object.
            * \param port The port to bind the server to.
            */
            TcpServer(unsigned int port);

            /**
            * \brief TCP Server destructor.
            */
            ~TcpServer();

            /**
            * \brief Send data packets to a TCP Tahoe server.
            * \param destPort Destination port number.
            * \param destAddr Destination address string.
            * \param receiveFileName String containing name of input file.
            * \param sendFileName The name of the file to send.
            * \param bitErrorPercent Percent of segment to corrupt.
            * \param segmentLoss Percent of segment to lose.
            * \param ignoreLoss If segment lost will be ignored.
            */
            void Send(unsigned int destPort,
                const std::string& destAddr,
                const std::string& receiveFileName,
                const std::string& sendFileName,
                unsigned int bitErrorPercent,
                unsigned int segmentLoss,
                bool ignoreLoss);

            /**
            * \brief Listens for TCP Tahoe connections.
            * \details Function that the server calls to listen for, connect
            * to, and then communicate with clients.
            * \param receiveFileName String containing name of input file.
            * \param sendFileName The name of the file to send.
            * \param bitErrorPercent Percent of segment to corrupt.
            * \param segmentLoss Percent of segment to lose.
            * \param ignoreLoss If segment lost will be ignored.
            */
            void Listen(const std::string& receiveFileName,
                const std::string& sendFileName,
                unsigned int bitErrorPercent,
                unsigned int segmentLoss,
                bool ignoreLoss);

        private:

            /**
            * \struct SendWindowByte
            * \brief Structure used to hold a byte of data in the sender
            * sliding window.
            */
            struct SendWindowByte
            {
                char byte;                  ///< The data byte
                uint32_t sequenceNumber;    ///< Seq. num. byte belongs to.
                bool urg;                   ///< Urgent flag.
                bool ack;                   ///< ACK flag.
                bool psh;                   ///< Push flag.
                bool rst;                   ///< Connection reset flag.
                bool syn;                   ///< Connection SYN flag.
                bool fin;                   ///< Connection FIN flag.
                uint16_t urgDataPointer;    ///< Pointer to urgent data.
                uint16_t dataLength;        ///< Length of data.
                uint16_t options;           ///< Options.
                float timeSent;             ///< Time to send byte.
            };

            /// Port the server is listening for connection requests on
            unsigned int connPort_;

            /// Internal TCP connection socket
            SocketTCP * connSocket_;

            /// Port the server sending/recieving data on
            unsigned int dataPort_;

            /// Internal TCP data socket
            SocketTCP * dataSocket_;

            /// Client number.
            int clientNumber_ = 1;

            /// A list holding all unacked bytes.
            std::list<SendWindowByte> sendWindow_;

            /// Buffer indicating if the bytes in the receive window are valid.
            std::array<bool, MAX_RECV_WINDOW_SIZE> recvWindowValid_;

            /// A buffer holding all received bytes.
            std::array<char, MAX_RECV_WINDOW_SIZE> recvWindow_;

            /// The current sample round trip time.
            float sampleRtt_;

            /// The current estimated round trip time.
            float estimatedRtt_;

            /// The current deviation in the round trip time
            float devRtt_;

            /// The current timeout interval
            float timeoutInterval_;

            /// The slow start window threshold.
            unsigned int ssthresh; // 11 * MAX_FULL_SEGMENT_LEN

            /**
            * \brief Helper function to check if a file exists.
            * \details This method was taken from
            * [StackOverflow](https://stackoverflow.com/a/12774387).
            * \param fileName The file name to check for existence.
            * \returns Whether the file exists or not.
            */
            bool FileExists(const std::string& fileName) const;
    };
}

// vim: set expandtab ts=4 sw=4:
