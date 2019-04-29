/**
* \file UdpServerTCP.hpp
* \details TCP server class - declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

#ifndef __TCP_SERVER_HPP__
#define __TCP_SERVER_HPP__

#include "SocketTCP.hpp"

namespace socksahoy
{
    /**
    * \class UdpServerTCP
    * \brief UDP server object.
    * \details This server implementation keeps a non-blocking socket open
    * until the programmer tells the server to stop. This allows for using the
    * socket for multiple transfers.
    */
    class UdpServerTCP
    {
        public:
            /**
            * \brief UDP Server constructor.
            * \details Initializes the port and internal socket object.
            * \param port The port to bind the server to.
            */
            UdpServerTCP(unsigned int port);

            /**
            * \brief UDP Server destructor.
            */
            ~UdpServerTCP() 
            {
                delete dataSocket_;
            }

            /**
            * \brief Function that the client calls to connect to and then communicate with a server.
            * \param destPort Destination port number.
            * \param destAddr Destination address string.
            * \param recieveFileName String containing name of input file.
            * \param sendFileName The name of the file to send.
            * \param bitErrorPercent Percent of segment to corrupt.
            * \param segmentLoss Percent of segment to lose.
            * \param ignoreLoss If segment lost will be ignored.
            */
            void Send(unsigned int destPort,
                const std::string& destAddr,
                std::string recieveFileName,
                std::string sendFileName,
                int bitErrorPercent,
                int segmentLoss,
                bool ignoreLoss);

            /**
            * \brief Function that the server calls to listen for, connect to, and then communicate with clients.
            * \param recieveFileName String containing name of input file.
            * \param sendFileName The name of the file to send.
            * \param bitErrorPercent Percent of segment to corrupt.
            * \param segmentLoss Percent of segment to lose.
            * \param ignoreLoss If segment lost will be ignored.
            */
            void Listen(std::string recieveFileName,
                std::string sendFileName,
                int bitErrorPercent,
                int segmentLoss,
                bool ignoreLoss);

        private:

            struct sendWindowByte
            {
                char byte;
                uint32_t sequenceNumber;
                uint32_t ackNumber;
                bool urg;
                bool ack;
                bool psh;
                bool rst;
                bool syn;
                bool fin;
                uint16_t urgDataPointer; 
                uint16_t dataLength;
                uint16_t options;
            };

            /// Port the server is listening for connection requests on
            unsigned int connPort_;

            /// Internal TCP connection socket
            SocketTCP * connSocket_;

            /// Port the server sending/recieving data on
            unsigned int dataPort_;

            /// Internal TCP data socket
            SocketTCP * dataSocket_;

            int clientNumber_ = 1;

            //An ring buffer holding all unacked bytes.
            std::vector<sendWindowByte> sendWindow_;

            //An ring buffer holding all recieved bytes.
            char recvWindow_[MAX_RECV_WINDOW_SIZE];

            //The current sample round trip time.
            float_t SampleRTT_ = 0.0;

            //The current estimated round trip time.
            float_t EstimatedRTT_ = STARTING_TIMEOUT_VALUE;

            //The current deviation in the round trip time
            float_t DevRTT_ = 0.0;

            //The current timeout interval
            float_t TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

            unsigned int ssthresh = 11 * MAX_FULL_SEGMENT_LEN;

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

#endif /* End of UdpServerTCP.hpp */

// vim: set expandtab ts=4 sw=4: