/**
* \file UdpServerGBN.hpp
* \details UDP server class - declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

#ifndef __UDP_SERVER_HPP__
#define __UDP_SERVER_HPP__

#include "SocketGBN.hpp"

namespace socksahoy
{
    /**
    * \class UdpServerGBN
    * \brief UDP server object.
    * \details This server implementation keeps a non-blocking socket open
    * until the programmer tells the server to stop. This allows for using the
    * socket for multiple transfers.
    */
    class UdpServerGBN
    {
        public:
            /**
            * \brief UDP Server constructor.
            * \details Initializes the port and internal socket object.
            * \param port The port to bind the server to.
            */
            UdpServerGBN(unsigned int port);

            /**
            * \brief UDP Server destructor.
            */
            ~UdpServerGBN() {}

            /**
            * \brief Run the server in receive mode first then send mode.
            * \details
            * ## Steps for running the server-then-client module:
            * 1. Bind the socket
            * 2. Open the destination file
            * 3. Start the receiver
            * 4. Consume packets and save them to file
            * 5. Reopen destination file as read
            * 6. Send packets one by one from file
            * \param recieveFileName The file name of the destination file.
            * \param sendFileName The name of the file to be sent.
            * \param sendBitErrorPercent Percent of data packets to corrupt.
            * \param ackBitErrorPercent Percent of ack packets to corrupt.
            * \param sendPacketLoss Percent of data packets that will be lost.
            * \param ackPacketLoss Percent of ack packets to lose.
            * \return The time it took to receive and send the file.
            */
            float RunListenSend(std::string recieveFileName,
                    std::string sendFileName,
                    int sendBitErrorPercent,
                    int ackBitErrorPercent,
                    int sendPacketLoss,
                    int ackPacketLoss,
                    bool IgnoreLoss);

            /**
            * \brief Run the server in send mode first then receive mode.
            * \details
            * ## Steps for running the client-then-server module:
            * 1. Bind the socket
            * 2. Open the input file
            * 3. Send packets one by one from file
            * 4. Open destination file
            * 5. Start the receiver
            * 6. Consume packets and save them to file
            * \param destPort Destination port number.
            * \param destAddr Destination address string.
            * \param recieveFileName String containing name of input file.
            * \param sendFileName The name of the file to send.
            * \param sendBitErrorPercent Percent of data packets to corrupt.
            * \param ackBitErrorPercent Percent of ack packets to corrupt.
            * \param sendPacketLoss Percent of data packets that will be lost.
            * \param ackPacketLoss Percent of ack packets to lose.
            * \return The time it took to send and receive the file.
            */
            float RunSendListen(unsigned int destPort,
                    const std::string& destAddr,
                    std::string recieveFileName,
                    std::string sendFileName,
                    int sendBitErrorPercent,
                    int ackBitErrorPercent,
                    int sendPacketLoss,
                    int ackPacketLoss,
                    bool IgnoreLoss);
        private:
            /// Port the server is running on
            unsigned int port_;

            /// Internal UDP socket
            SocketGBN socket_;

            //An ring buffer holding all sent unacked packets.
            Packet<MAX_PACKET_LEN> send_window_[MAX_SEND_WINDOW_SIZE];

            /**
            * \brief Wrapper around the Socket class' Send function.
            * \param destPort Destination port.
            * \param destAddr Destination address.
            * \param sendFileName The name of the file to send.
            * \param sendBitErrorPercent Percent of data packets to corrupt.
            * \param sendPacketLoss Percent of data packets that will be lost.
            * \return The time it took to send.
            */
            float Send(unsigned int destPort,
                    const std::string& destAddr,
                    std::string sendFileName,
                    int sendBitErrorPercent,
                    int sendPacketLoss);

            /**
            * \brief Wrapper around the Socket class' Receive function.
            * \param recieveFileName The name of the file to save packets to.
            * \param ackBitErrorPercent Percent of ack packets to corrupt.
            * \param ackPacketLoss Percent of ack packets to lose.
            */
            void Listen(std::string recieveFileName,
                    int ackBitErrorPercent,
                    int ackPacketLoss,
                    bool IngonreLoss);

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

#endif /* End of UdpServerGBN.hpp */

// vim: set expandtab ts=4 sw=4: