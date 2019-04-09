/**
* \file UdpServerGBN.cpp
* \details UDP server class - definitions
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>

#include <sys/stat.h>

#include "UdpServerGBN.hpp"
#include "SocketGBN.hpp"
#include "Packet.hpp"

socksahoy::UdpServerGBN::UdpServerGBN(unsigned int port)
    : port_(port), socket_(port_) {}

float socksahoy::UdpServerGBN::RunListenSend(std::string recieveFileName,
        std::string sendFileName,
        int sendBitErrorPercent,
        int ackBitErrorPercent,
        int sendPacketLoss,
        int ackPacketLoss,
        bool IgnoreLoss)
{
    // Bind to a socket
    socket_.Bind();

    Listen(recieveFileName, ackBitErrorPercent, ackPacketLoss, IgnoreLoss);

    std::string destAddr = socket_.GetRemoteAddress();
    unsigned int destPort = socket_.GetRemotePort();

    float transferTime = Send(destPort, destAddr, sendFileName,
                              sendBitErrorPercent, sendPacketLoss);

    std::cout << "Percent of Data packets with biterrors: ";
    std::cout << sendBitErrorPercent << "%" << std::endl;

    std::cout << "Percent of Ack packets with biterrors: ";
    std::cout << ackBitErrorPercent << "%" << std::endl;

    std::cout << "Percent of Data packets lost: ";
    std::cout << sendPacketLoss << "%" << std::endl;

    std::cout << "Percent of Ack packets lost: ";
    std::cout << ackPacketLoss << "%" << std::endl;

    std::cout << "Time for the server to transfer the file in milliseconds: ";
    std::cout << transferTime << std::endl;

    return transferTime;
}

float socksahoy::UdpServerGBN::RunSendListen(unsigned int destPort,
        const std::string& destAddr,
        std::string recieveFileName,
        std::string sendFileName,
        int sendBitErrorPercent,
        int ackBitErrorPercent,
        int sendPacketLoss,
        int ackPacketLoss,
        bool IgnoreLoss)
{
    // Bind to a socket
    socket_.Bind();

    float transferTime = Send(destPort, destAddr, sendFileName,
                              sendBitErrorPercent, sendPacketLoss);

    Listen(recieveFileName, ackBitErrorPercent, ackPacketLoss, IgnoreLoss);

    std::cout << "Percent of Data packets with bit errors: ";
    std::cout << sendBitErrorPercent << "%" << std::endl;

    std::cout << "Percent of Ack packets with bit errors: ";
    std::cout << ackBitErrorPercent << "%" << std::endl;

    std::cout << "Percent of Data packets lost: ";
    std::cout << sendPacketLoss << "%" << std::endl;

    std::cout << "Percent of Ack packets lost: ";
    std::cout << ackPacketLoss << "%" << std::endl;

    std::cout << "Time for the client to transfer the file in milliseconds: ";
    std::cout << transferTime << std::endl;
    return transferTime;
}

float socksahoy::UdpServerGBN::Send(unsigned int destPort,
                                    const std::string& destAddr,
                                    std::string sendFileName,
                                    int sendBitErrorPercent,
                                    int sendPacketLoss)
{
    std::size_t i = 0;
    std::streamoff fileSize = 0;
    uint16_t numberOfPackets = 0;

    auto startTransfer = std::chrono::high_resolution_clock::now();
    auto finishTransfer = std::chrono::high_resolution_clock::now();

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();

    try
    {
        if (!FileExists(sendFileName))
        {
            throw std::runtime_error("Input file does not exist");
        }

        // Open a file to input from.
        std::ifstream inputFile(sendFileName,
                                std::ios::binary | std::ios::ate);

        // Find out how many bytes are in the file.
        fileSize = inputFile.tellg();

        inputFile.seekg(0);

        // Calculate the number of packets required to send that many bytes.
        numberOfPackets = fileSize / MAX_PACKET_DATA_LEN;

        // If the division resulted in a remainder, add one to the number of
        // packets.
        if (fileSize % MAX_PACKET_DATA_LEN > 0)
        {
            numberOfPackets++;
        }

        //The next position in the ring buffer to add a packet to
        int nextPosition = 0;

        //The position in the ring buffer to remove a packet from
        int basePosition = 0;

        //The number of elements in the ring buffer
        size_t numberOfElements = 0;

        uint16_t lastAckNumber = -1;

        // Get the time at the start of the transfer.
        startTransfer = std::chrono::high_resolution_clock::now();

        char byte = 0;
        bool queuedByte = false;

        // Loop through until we reach EOF
        while (true)
        {
            //The ring buffer isn't full and the last packet hasn't been sent.
            if ((numberOfElements < MAX_SEND_WINDOW_SIZE)
                    && (i < numberOfPackets))
            {
                // Make a packet.
                Packet<MAX_PACKET_LEN> packet(i, numberOfPackets);

                for (;;)
                {
                    // We have to make sure we have put every byte into a packet
                    // Check if we have a left-over byte... this means we are in
                    // a new packet
                    if (queuedByte)
                    {
                        packet.AddByte(byte);
                        queuedByte = false;
                    }

                    // Get a byte and try to put it into the packet.
                    // If after this we have a queuedByte, this means we go to
                    // another packet.
                    if (inputFile.get(byte))
                    {
                        queuedByte = !packet.AddByte(byte);

                        if (queuedByte)
                        {
                            break;
                        }

                    }

                    // If we can't get a byte, that means we got EOF; leave the
                    // loop.
                    else
                    {
                        break;
                    }

                }
                //Send the packet
                socket_.Send<MAX_PACKET_LEN>(packet, destAddr, destPort,
                        sendBitErrorPercent, sendPacketLoss);

                //Add the packet to the ring buffer
                send_window_[nextPosition] = packet;

                if (nextPosition == basePosition)
                {
                    //Mark the start time of the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }

                //Is the nextPosition at the last element of the ring buffer?
                if (nextPosition == MAX_SEND_WINDOW_SIZE - 1)
                {
                    //Reset it back to the first element
                    nextPosition = 0;
                }
                else
                {
                    nextPosition++;
                }

                //Increase the number of packets sent
                i++;

                //Increase the number of elements in the ring buffer
                numberOfElements++;
            }

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //If a timeout has occured
            if (timermiliSeconds.count() >= TIMEOUT_VALUE)
            {
                //Make the temp position start at the base position
                int tempPosition = basePosition;

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();

                //Resend all packets in between the basePosition and the nextPosition
                do
                {
                    socket_.Send<MAX_PACKET_LEN>(send_window_[tempPosition],
                            destAddr, destPort, sendBitErrorPercent,
                            sendPacketLoss);

                    //Is the tempPosition at the last element of the ring buffer?
                    if (tempPosition == MAX_SEND_WINDOW_SIZE - 1)
                    {
                        //Reset it back to the first element
                        tempPosition = 0;
                    }
                    else
                    {
                        tempPosition++;
                    }

                    //A packet has arrived
                    if (socket_.CheckReceive())
                    {
                        // Make a ack packet.
                        Packet<MAX_ACK_PACKET_LEN> ackPacket;

                        // Recieve the ack packet
                        socket_.Receive<MAX_ACK_PACKET_LEN>(ackPacket);

                        //If the ack packet isn't corrupt and isn't a duplicate
                        if (ackPacket.GetChecksum() == ackPacket.CalculateChecksum(0)
                                && lastAckNumber != ackPacket.GetSequenceNumber())
                        {
                            lastAckNumber = ackPacket.GetSequenceNumber();

                            //Calulate how much the base position of the ring buffer is going to increase by
                            int increase_number = ackPacket.GetSequenceNumber() -
                                                  send_window_[basePosition].GetSequenceNumber() + 1;

                            //Increase the base position of the ring buffer
                            while (increase_number > 0)
                            {
                                //Is the basePosition at the last element of the ring buffer?
                                if (basePosition == MAX_SEND_WINDOW_SIZE - 1)
                                {
                                    //Reset it back to the first element
                                    basePosition = 0;
                                }
                                else
                                {
                                    basePosition++;
                                }

                                //Decrease the number of elements in the ring buffer
                                numberOfElements--;

                                increase_number--;
                            }

                            //Mark the start time of the timer
                            startTimer = std::chrono::high_resolution_clock::now();
                        }
                    }

                }
                while (tempPosition != nextPosition);
            }

            //A packet has arrived
            else if (socket_.CheckReceive())
            {
                // Make a ack packet.
                Packet<MAX_ACK_PACKET_LEN> ackPacket;

                // Recieve the ack packet
                socket_.Receive<MAX_ACK_PACKET_LEN>(ackPacket);

                //If the ack packet isn't corrupt and isn't a duplicate
                if (ackPacket.GetChecksum() == ackPacket.CalculateChecksum(0)
                        && lastAckNumber != ackPacket.GetSequenceNumber())
                {
                    lastAckNumber = ackPacket.GetSequenceNumber();

                    //Calulate how much the base position of the ring buffer is going to increase by
                    int increase_number = ackPacket.GetSequenceNumber() -
                                          send_window_[basePosition].GetSequenceNumber() + 1;

                    //Increase the base position of the ring buffer
                    while (increase_number > 0)
                    {
                        //Is the basePosition at the last element of the ring buffer?
                        if (basePosition == MAX_SEND_WINDOW_SIZE - 1)
                        {
                            //Reset it back to the first element
                            basePosition = 0;
                        }
                        else
                        {
                            basePosition++;
                        }

                        //Decrease the number of elements in the ring buffer
                        numberOfElements--;

                        increase_number--;
                    }

                    //Mark the start time of the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }
            }

            //Recieved the last ack packet?
            if ((numberOfElements == 0) && (i == numberOfPackets))
            {
                break;
            }
        }

        // Get the time at the end of the transfer.
        finishTransfer = std::chrono::high_resolution_clock::now();

        inputFile.close();
    }

    catch (std::runtime_error& e)
    {
        throw e;
    }

    // Get the time it took to send the file in milliseconds
    std::chrono::duration<float, std::milli> miliSeconds = finishTransfer -
            startTransfer;

    // Returns a float value.
    return miliSeconds.count();
}

void socksahoy::UdpServerGBN::Listen(std::string recieveFileName,
                                     int ackBitErrorPercent,
                                     int ackPacketLoss,
                                     bool IgnoreLoss)
{

    uint16_t LastRecievedPacket = -1;
    uint16_t NextExpectedPacket = 0;
    uint16_t numberOfPackets = 1;

    try
    {
        // Open a file to output to
        std::ofstream outputFile(recieveFileName, std::ios::binary);

        do
        {
            // Doesn't matter what values we start the packet with, they will
            // be overridden by the correct values when the data packet is
            // received.
            Packet<MAX_PACKET_LEN> packet;

            //Receive and unpack a packet of data.
            socket_.Receive<MAX_PACKET_LEN>(packet);

            //If the packet is not corrupt
            if ((packet.GetChecksum() == packet.CalculateChecksum(0)))
            {
                numberOfPackets = packet.GetPacketNumber();

                //If the packet has correct sequence number or  the program is set to ingnore loss.
                if ((NextExpectedPacket == packet.GetSequenceNumber()) || (IgnoreLoss
                        && (NextExpectedPacket != 0)))
                {
                    if (IgnoreLoss)
                    {
                        outputFile.seekp(MAX_PACKET_DATA_LEN * packet.GetSequenceNumber());
                    }

                    //Write the packet data to the file
                    outputFile.write(packet.GetData(), packet.GetPacketSize());

                    //Increase the number of the next expected packet
                    NextExpectedPacket = packet.GetSequenceNumber() + 1;

                    LastRecievedPacket = packet.GetSequenceNumber();
                }

                // Make an ack packet
                Packet<MAX_ACK_PACKET_LEN> ackpacket(LastRecievedPacket, 1);

                /*
                * The RDT 3.0 protocol breaks if the last ACK packet is lost or
                * corrupted. It is a core fault of the protocol. To work around
                * this, we make sure the last packet is not lost or corrupted.
                */

                //Is the last ack
                if (NextExpectedPacket == numberOfPackets)
                {
                    //Send the ack packet without bit errors or loss
                    socket_.Send<MAX_ACK_PACKET_LEN>(ackpacket, socket_.GetRemoteAddress(),
                                                     socket_.GetRemotePort(), 0, 0);
                }

                //Not the last ack
                else
                {
                    //Send the ack packet with bit errors and loss
                    socket_.Send<MAX_ACK_PACKET_LEN>(ackpacket, socket_.GetRemoteAddress(),
                                                     socket_.GetRemotePort(), ackBitErrorPercent, ackPacketLoss);
                }
            }

            //Loop until all of the packets have arrived.
        }
        while (NextExpectedPacket < numberOfPackets);

        outputFile.close();
    }

    catch (std::runtime_error& e)
    {
        throw e;
    }
}

bool socksahoy::UdpServerGBN::FileExists(const std::string& fileName) const
{
    struct stat fileBuffer;
    int exists = stat(fileName.c_str(), &fileBuffer);

    return (exists == 0);
}

// vim: set expandtab ts=4 sw=4: