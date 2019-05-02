/**
* \file UdpServerTCP.cpp
* \details TCP server class - definitions
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
#include <random>

#include <sys/stat.h>

#include "UdpServerTCP.hpp"
#include "SocketTCP.hpp"
#include "Segment.hpp"

socksahoy::UdpServerTCP::UdpServerTCP(unsigned int port)
{
    connPort_ = port;

    connSocket_ = new SocketTCP(connPort_);
}

void socksahoy::UdpServerTCP::Send(unsigned int destPort,
                                   const std::string& destAddr,
                                   std::string recieveFileName,
                                   std::string sendFileName,
                                   int bitErrorPercent,
                                   int segmentLoss,
                                   bool ignoreLoss)
{
    printf("\n");
    
    unsigned int currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    uint32_t startingClientSequenceNumber = 0;

    unsigned int maxClientSendWindowSize;

    unsigned int currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

    unsigned int currentServerRecvWindowSize;

    uint32_t startingServerSequenceNumber;

    uint32_t duplicateAckSequenceNumber;

    int numberOfDuplicateAcks = 0;

    uint32_t numberOfBytesInRecieveWindow = 0;

    uint32_t nextSendAckNumber = 0;

    bool sendAck = true;

    uint32_t numberOfAckedBytes = 0;

    uint32_t numberOfUnackedBytes = 0;

    uint32_t tempSendWindowSize = 0;

    bool recievedFin = false;

    std::streamoff fileSize = 0;

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();
    auto startTransfer = std::chrono::high_resolution_clock::now();
    auto finishTransfer = std::chrono::high_resolution_clock::now();

    //The next position in the send ring buffer to add a byte to
    unsigned int sendNextPosition = 0;

    //The position in the send buffer to remove a byte from
    unsigned int sendBasePosition = 0;

    //The point where the base position loops back to the begining of the ring buffer
    unsigned int sendLoopBackPosition = currentClientSendWindowSize;

    //The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    unsigned int recvTempNextPosition = 0;

    unsigned int holeSize = 0;

    //Connection set up section

    //Bind the connection socket to it's address and port
    connSocket_->Bind();

    // Random number engine and distribution
        // Distribution in range [1, 100]
    std::random_device dev;
    std::mt19937 rng(dev());

    using distType = std::mt19937::result_type;
    std::uniform_int_distribution<distType> uniformDist(1, 100);

    startingClientSequenceNumber = uniformDist(rng);

    //Make a connection set up segment
    Segment SynSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort, startingClientSequenceNumber,
        0, false, false, false, false, true, false, currentClientRecvWindowSize, 0, 0);

    //Send the connection set up segment to the server
    connSocket_->Send(SynSegment, destAddr,
        destPort, bitErrorPercent, segmentLoss);

    printf("Sending syn segment\n");
    printf("Segment number %d\n", SynSegment.GetSequenceNumber());
    printf("%d bytes long\n\n", SynSegment.GetDataLength());

    //Set the starting value of the timer
    startTimer = std::chrono::high_resolution_clock::now();

    while (true)
    {
        //If a packet arrived
        if (connSocket_->CheckReceive())
        {
            //Make a SynAck segment that will be recieved from the server
            Segment SynAckSegment(MAX_EMPTY_SEGMENT_LEN);

            //Recieve the ack segment from the server
            connSocket_->Receive(SynAckSegment);

            printf("Recieved syn ack segment\n");
            printf("Segment number %d\n", SynAckSegment.GetSequenceNumber());
            printf("Ack number %d\n", SynAckSegment.GetAckNumber());
            printf("%d bytes long\n\n", SynAckSegment.GetDataLength());

            //If the syn ack segment isn't corrupt, has the correct flags set, and has the correct ack number
            if (SynAckSegment.CalculateChecksum(0) == 0x0000 && SynAckSegment.GetSyncFlag() && SynAckSegment.GetAckFlag() && SynAckSegment.GetAckNumber() == startingClientSequenceNumber + 1)
            {
                //Make the max send window the same size of the recieve window
                maxClientSendWindowSize = SynAckSegment.GetReceiveWindow();
                currentServerRecvWindowSize = maxClientSendWindowSize;

                //Resize the send window
                sendWindow_.resize(maxClientSendWindowSize);

                //Store the starting sequence number of server
                startingServerSequenceNumber = SynAckSegment.GetSequenceNumber();

                nextSendAckNumber = startingServerSequenceNumber + 1;

                //Store the current ack number
                duplicateAckSequenceNumber = SynAckSegment.GetAckNumber();

                //Make an ack segment to send back to the server
                Segment AckSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort, 0,
                    nextSendAckNumber, false, true, false, false, true, false, currentClientRecvWindowSize, 0, 0);

                //Send the ack segmet to the server
                connSocket_->Send(AckSegment, destAddr,
                    destPort, bitErrorPercent, segmentLoss);

                printf("Sending ack segment\n");
                printf("Ack number %d\n", AckSegment.GetAckNumber());
                printf("%d bytes long\n\n", AckSegment.GetDataLength());

                //Switch over to using the port that the server sent us for the data transfer
                destPort = SynAckSegment.GetSourcePortNumber();

                //Connection established, break out of the loop
                break;
            }
        }

        // Get the current timer value in milliseconds
        currentTimer = std::chrono::high_resolution_clock::now();

        std::chrono::duration<float, std::milli> timermiliSeconds =
            currentTimer - startTimer;

        //If the timeout occurred
        if (timermiliSeconds.count() >= TimeoutInterval_)
        {
            //Recalculate the estimated RTT value 
            EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

            //Recalculate the RTT deviation value
            DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

            //Recalculate the Timeout value
            TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

            //Resend the Sync packet
            connSocket_->Send(SynSegment, destAddr,
                destPort, bitErrorPercent, segmentLoss);

            printf("Re-Sending syn segment\n");
            printf("Segment number %d\n", SynSegment.GetSequenceNumber());
            printf("%d bytes long\n\n", SynSegment.GetDataLength());

            //Restart the timer
            startTimer = std::chrono::high_resolution_clock::now();
        }
    }

    //Data transfer section

    //Make the data port the same as the connection port
    dataPort_ = connPort_;

    //Delete the old socket.
    delete connSocket_;

    //Make a socket to send and receive data from
    dataSocket_ = new SocketTCP(dataPort_);

    //Bind the data socket to it's address and port
    dataSocket_->Bind();

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

        // Open a file to output to
        std::ofstream outputFile(recieveFileName, std::ios::binary);

        char byte = 0;
        bool queuedByte = false;

        // Get the time at the start of the transfer.
        startTransfer = std::chrono::high_resolution_clock::now();

        // Loop until all segments are sent and acked
        while (numberOfAckedBytes < (unsigned int)fileSize)
        {
            //The ring buffer isn't full and the last packet hasn't been sent.
            if ((numberOfUnackedBytes < currentClientSendWindowSize)
                    && (numberOfUnackedBytes < (unsigned int)fileSize) 
                    && numberOfUnackedBytes < currentServerRecvWindowSize)
            {
                //The length of the segment that will be sent
                unsigned int segmentLength;

                printf("currentServerRecvWindowSize: %d\n", currentServerRecvWindowSize);

                //If the segment won't fit in the send buffer
                if (numberOfUnackedBytes + MAX_SEGMENT_DATA_LEN > currentClientSendWindowSize)
                {
                    //Make it smaller so that it will fit
                    segmentLength = currentClientSendWindowSize - numberOfUnackedBytes;
                }
                //A fill size segment will fit
                else
                {
                    segmentLength = MAX_SEGMENT_DATA_LEN;
                }

                //If the segment length is longer than what the server can recieve
                if (segmentLength > currentServerRecvWindowSize)
                {
                    segmentLength = currentServerRecvWindowSize;
                    currentServerRecvWindowSize = 0;
                }

                if (segmentLength > 0)
                {
                    unsigned int sequenceNumber = startingClientSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make a segment.
                    Segment segment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, destPort, sequenceNumber,
                        nextSendAckNumber, false, sendAck, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                    //If the buffer is empty
                    if (sendNextPosition == sendBasePosition)
                    {
                        //Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }

                    for (;;)
                    {
                        // We have to make sure we have put every byte into a segment
                        // Check if we have a left-over byte... this means we are in
                        // a new segment
                        if (queuedByte)
                        {
                            segment.AddByte(byte);
                            queuedByte = false;
                            numberOfUnackedBytes++;
                            sendWindow_[sendNextPosition].byte = byte;
                            sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                            sendWindow_[sendNextPosition].ackNumber = nextSendAckNumber;
                            sendWindow_[sendNextPosition].urg = false;
                            sendWindow_[sendNextPosition].ack = sendAck;
                            sendWindow_[sendNextPosition].psh = false;
                            sendWindow_[sendNextPosition].rst = false;
                            sendWindow_[sendNextPosition].syn = false;
                            sendWindow_[sendNextPosition].fin = false;
                            sendWindow_[sendNextPosition].urgDataPointer = 0;
                            sendWindow_[sendNextPosition].dataLength = segmentLength;
                            sendWindow_[sendNextPosition].options = 0;
                        }

                        // Get a byte and try to put it into the packet.
                        // If after this we have a queuedByte, this means we go to
                        // another packet.
                        if (inputFile.get(byte))
                        {
                            queuedByte = !segment.AddByte(byte);

                            if (queuedByte)
                            {
                                break;
                            }

                            numberOfUnackedBytes++;
                            sendWindow_[sendNextPosition].byte = byte;
                            sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                            sendWindow_[sendNextPosition].ackNumber = nextSendAckNumber;
                            sendWindow_[sendNextPosition].urg = false;
                            sendWindow_[sendNextPosition].ack = sendAck;
                            sendWindow_[sendNextPosition].psh = false;
                            sendWindow_[sendNextPosition].rst = false;
                            sendWindow_[sendNextPosition].syn = false;
                            sendWindow_[sendNextPosition].fin = false;
                            sendWindow_[sendNextPosition].urgDataPointer = 0;
                            sendWindow_[sendNextPosition].dataLength = segmentLength;
                            sendWindow_[sendNextPosition].options = 0;
                        }

                        // If we can't get a byte, that means we got EOF; leave the
                        // loop.
                        else
                        {
                            break;
                        }

                        //If we are at, or past, the end of the ring buffer
                        if (sendNextPosition >= currentClientSendWindowSize - 1)
                        {
                            sendLoopBackPosition = currentClientSendWindowSize;
                            //Reset it back to the beginning
                            sendNextPosition = 0;
                        }
                        else
                        {
                            sendNextPosition++;
                        }
                    }

                    if (sendAck)
                    {
                        sendAck = false;
                    }

                    //Send the segment
                    dataSocket_->Send(segment, destAddr, destPort,
                        bitErrorPercent, segmentLoss);


                    printf("Sending data segment\n");
                    printf("Segment number %d\n", segment.GetSequenceNumber());
                    if (segment.GetAckFlag())
                    {
                        printf("Ack number %d\n", segment.GetAckNumber());
                    }
                    printf("%d bytes long\n\n", segment.GetDataLength());
                }                
            }

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //If a timeout has occured, or three duplicate acks arrived
            if (timermiliSeconds.count() >= TimeoutInterval_ || numberOfDuplicateAcks >= 3)
            {
                //Make the temp position start at the base position
                unsigned int tempPosition = sendBasePosition;

                unsigned int segmentLength = sendWindow_[tempPosition].dataLength;

                //Remake the segment
                Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, destPort, sendWindow_[tempPosition].sequenceNumber,
                    nextSendAckNumber, sendWindow_[tempPosition].urg, sendWindow_[tempPosition].ack, sendWindow_[tempPosition].psh,
                    sendWindow_[tempPosition].rst, sendWindow_[tempPosition].syn, sendWindow_[tempPosition].fin, currentClientRecvWindowSize, 
                    sendWindow_[tempPosition].urgDataPointer, sendWindow_[tempPosition].options);

                //Populate the segment with all of it's bytes
                for(unsigned int i = 0; i < segmentLength; i++)
                {
                    //Get the byte from the send window
                    byte = sendWindow_[tempPosition].byte;

                    //Add it to the segment
                    resendSegment.AddByte(byte);

                    //If we are at, or past, the end of the ring buffer loop back
                    if (tempPosition >= sendLoopBackPosition - 1)
                    {
                        //Reset it back to the beginning
                        tempPosition = 0;
                    }
                    else
                    {
                        tempPosition++;
                    }
                }

                //Only recalculate the timeout interval if a timeout occured
                if (numberOfDuplicateAcks < 3) 
                {
                    //Recalculate the estimated RTT value 
                    EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                    //Recalculate the RTT deviation value
                    DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                    //Recalculate the Timeout value
                    TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;
                }

                else
                {
                    numberOfDuplicateAcks = 0;
                }
                
                //The slow start threashhold becomes half of the current client window size
                ssthresh = currentClientSendWindowSize / 2;

                //The client window size gets reset back to one full segment.
                currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

                //Resend the segment
                dataSocket_->Send(resendSegment, destAddr, destPort,
                    bitErrorPercent, segmentLoss);

                printf("Re-Sending data segment\n");
                printf("Segment number %d\n", resendSegment.GetSequenceNumber());
                if (resendSegment.GetAckFlag())
                {
                    printf("Ack number %d\n", resendSegment.GetAckNumber());
                }
                printf("%d bytes long\n\n", resendSegment.GetDataLength());

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }

            //A packet has arrived
            if (dataSocket_->CheckReceive())
            {
                // Make a segment.
                Segment AckSegment(MAX_FULL_SEGMENT_LEN);

                // Recieve the segment from the server
                dataSocket_->Receive(AckSegment);

                printf("Recieved segment\n");
                printf("Segment number %d\n", AckSegment.GetSequenceNumber());
                printf("Ack number %d\n", AckSegment.GetAckNumber());
                printf("%d bytes long\n", AckSegment.GetDataLength());

                //If the segment is not corrupt
                if (AckSegment.CalculateChecksum(0) == 0x0000)
                {
                    currentServerRecvWindowSize = AckSegment.GetReceiveWindow();

                    //If it's the synack from before
                    if (AckSegment.GetSyncFlag() && AckSegment.GetAckFlag() && AckSegment.GetAckNumber() == startingClientSequenceNumber + 1)
                    {
                        printf("It's a syn ack segment\n");
                        //Make an ack segment to send back to the server
                        Segment AckSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort, startingClientSequenceNumber + 1,
                            startingServerSequenceNumber, false, false, false, false, true, false, currentClientRecvWindowSize, 0, 0);

                        //Send the ack segmet to the server
                        dataSocket_->Send(AckSegment, destAddr,
                            destPort, bitErrorPercent, segmentLoss);

                        printf("Re-Sending ack segment\n");
                        printf("Ack number %d\n", AckSegment.GetAckNumber());
                        printf("%d bytes long\n\n", AckSegment.GetDataLength());

                        continue;
                    }
            
                    //If the segment is an ack segment
                    else if (AckSegment.GetAckFlag())
                    {
                        //If the ack isn't a duplicate
                        if (AckSegment.GetAckNumber() > duplicateAckSequenceNumber)
                        {
                            printf("It's a new ack segment\n");

                            unsigned int tempNumberOfAckedBytes = AckSegment.GetAckNumber() - duplicateAckSequenceNumber;

                            printf("%d bytes acked\n", tempNumberOfAckedBytes);

                            //Increase the number of acked bytes
                            numberOfAckedBytes += tempNumberOfAckedBytes;

                            //Decrease the number of unacked bytes in the window
                            numberOfUnackedBytes -= tempNumberOfAckedBytes;

                            //Increase the windowsize

                            //If the window size is currently smaller than the max window size
                            if (currentClientSendWindowSize < maxClientSendWindowSize)
                            {
                                //If we are in CA mode
                                if (currentClientSendWindowSize >= ssthresh)
                                {
                                    tempSendWindowSize += MAX_FULL_SEGMENT_LEN * ((float)MAX_FULL_SEGMENT_LEN / (float)currentClientSendWindowSize);

                                    if (tempSendWindowSize >= MAX_FULL_SEGMENT_LEN)
                                    {
                                        currentClientSendWindowSize += MAX_FULL_SEGMENT_LEN;
                                        tempSendWindowSize = 0;
                                    }
                                }
                                //Must be in SS mode
                                else
                                {
                                    currentClientSendWindowSize += MAX_FULL_SEGMENT_LEN;

                                    //If we've gone from SS to CA mode
                                    if (currentClientSendWindowSize > ssthresh)
                                    {
                                        currentClientSendWindowSize = ssthresh;
                                    }
                                }

                                //If the new client window size is greater than the max set by the reciever
                                if (currentClientSendWindowSize > maxClientSendWindowSize)
                                {
                                    currentClientSendWindowSize = maxClientSendWindowSize;
                                }
                            }

                            //Move the base position

                            for (unsigned int i; i < tempNumberOfAckedBytes; i++)
                            {
                                //If we are at, or past, the end of the ring buffer loop back
                                if (sendBasePosition >= sendLoopBackPosition - 1)
                                {
                                    //Reset it back to the beginning
                                    sendBasePosition = 0;
                                    sendLoopBackPosition = currentClientSendWindowSize;
                                }
                                else
                                {
                                    sendBasePosition++;
                                }
                            }

                            //Update the duplicate number
                            duplicateAckSequenceNumber = AckSegment.GetAckNumber();
                            //Reset the number of duplicates
                            numberOfDuplicateAcks = 0;
                        }
                        else
                        {
                            printf("It's a duplicate ack segment\n");
                            //Increase the number of duplicates
                            numberOfDuplicateAcks++;
                        }
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the segment is a fin segment
                        if (AckSegment.GetFinFlag())
                        {
                            printf("It's a fin segment\n");
                            //Send an ack in the next data packet
                            nextSendAckNumber = AckSegment.GetSequenceNumber() + 1;

                            recievedFin = true;

                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            continue;
                        }

                        //If the data in the ack segment is in order
                        if (AckSegment.GetSequenceNumber() == nextSendAckNumber)
                        {
                            printf("It's data is in order\n");
                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvNextPosition] = AckSegment.GetData()[i];

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            //If the hole has been closed
                            if (recvNextPosition < recvTempNextPosition && holeSize == 0)
                            {
                                nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                                recvNextPosition = recvTempNextPosition;
                            }

                            //No hole exists, or the hole isn't closed
                            else
                            {
                                nextSendAckNumber += AckSegment.GetDataLength();
                                if (holeSize != 0)
                                {
                                    holeSize -= AckSegment.GetDataLength();
                                }
                            }

                            numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                            currentClientRecvWindowSize -= AckSegment.GetDataLength();
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (AckSegment.GetSequenceNumber() >= nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");
                            recvTempNextPosition = recvNextPosition + AckSegment.GetSequenceNumber() - nextSendAckNumber;

                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = AckSegment.GetData()[i];

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            if (ignoreLoss)
                            {
                                nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                                numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                                currentClientRecvWindowSize -= recvNextPosition - recvTempNextPosition;
                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                                currentClientRecvWindowSize -= AckSegment.GetDataLength();

                                holeSize = recvTempNextPosition - recvNextPosition - AckSegment.GetDataLength();
                            }
                        }

                        //If the recieve buffer is full and there are no holes
                        if (currentClientRecvWindowSize <= MAX_SEGMENT_DATA_LEN && recvNextPosition >= recvTempNextPosition)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);
                            printf("Emptying the recv buffer\n");

                            //Reset the recieve window
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                            numberOfBytesInRecieveWindow = 0;
                            recvTempNextPosition = 0;
                            recvNextPosition = 0;
                        }

                        sendAck = true;
                        printf("currentClientRecvWindowSize: %d\n", currentClientRecvWindowSize);
                    }
                }

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();

                printf("\n");
            }
        }

        // Get the time at the end of the transfer.
        finishTransfer = std::chrono::high_resolution_clock::now();

        inputFile.close();

        unsigned int finSequenceNumber = numberOfAckedBytes + 1;

        bool finAcked = false;

        //Make fin segment
        Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, destPort, finSequenceNumber,
            nextSendAckNumber, false, true, false, false, false, true, currentClientRecvWindowSize, 0, 0);

        //Send the fin segment to the server
        dataSocket_->Send(finSegment, destAddr,
            destPort, bitErrorPercent, segmentLoss);

        printf("Sending fin segment\n");
        printf("Segment number %d\n", finSegment.GetSequenceNumber());
        if (finSegment.GetAckFlag())
        {
            printf("Ack number %d\n", finSegment.GetAckNumber());
        }
        printf("%d bytes long\n\n", finSegment.GetDataLength());

        //Set the starting value of the timer
        startTimer = std::chrono::high_resolution_clock::now();

        //Loop until the reciever completes it's data transfer and sends it's fin segment
        while (true)
        {
            //A packet has arrived
            if (dataSocket_->CheckReceive() | finAcked)
            {
                // Make a segment.
                Segment segment(MAX_FULL_SEGMENT_LEN);

                // Recieve the segment from the server
                dataSocket_->Receive(segment);

                printf("Recieved segment\n");
                printf("Segment number %d\n", segment.GetSequenceNumber());
                printf("%d bytes long\n", segment.GetDataLength());

                //If the segment is not corrupt
                if (segment.CalculateChecksum(0) == 0x0000)
                {
                    //If the segment has an ack for the fin segment
                    if (segment.GetAckFlag() && segment.GetAckNumber() == finSequenceNumber + 1)
                    {
                        printf("It's an ack for the fin segment\n");
                        printf("Ack number %d\n", finSegment.GetAckNumber());
                        finAcked = true;
                        continue;
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the segment is a fin segment
                        if (segment.GetFinFlag())
                        {
                            printf("It's a fin segment");
                            // Make an ackSegment.
                            Segment ackSegment(MAX_EMPTY_SEGMENT_LEN,dataPort_, destPort, 0,
                                segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                            //Send the segment to the server
                            dataSocket_->Send(segment, destAddr, destPort,
                                0, 0);

                            printf("Sending ack segment\n");
                            printf("Ack number %d\n", ackSegment.GetAckNumber());
                            printf("%d bytes long\n\n", ackSegment.GetDataLength());

                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            break;
                        }

                        //If the data in the segment is in order
                        if (segment.GetSequenceNumber() == nextSendAckNumber)
                        {
                            printf("It's data is in order\n");
                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvNextPosition] = segment.GetData()[i];

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            //If the hole has been closed
                            if (recvNextPosition < recvTempNextPosition && holeSize == 0)
                            {
                                nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                                recvNextPosition = recvTempNextPosition;
                            }

                            //No hole exists, or the hole isn't closed
                            else
                            {
                                nextSendAckNumber += segment.GetDataLength();
                                if (holeSize != 0)
                                {
                                    holeSize -= segment.GetDataLength();
                                }
                            }

                            numberOfBytesInRecieveWindow += segment.GetDataLength();
                            currentClientRecvWindowSize -= segment.GetDataLength();
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (segment.GetSequenceNumber() >= nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");
                            recvTempNextPosition = recvNextPosition + segment.GetSequenceNumber() - nextSendAckNumber;

                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = segment.GetData()[i];

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            if (ignoreLoss)
                            {
                                nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                                numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                                currentClientRecvWindowSize -= recvNextPosition - recvTempNextPosition;
                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                numberOfBytesInRecieveWindow += segment.GetDataLength();
                                currentClientRecvWindowSize -= segment.GetDataLength();

                                holeSize = recvTempNextPosition - recvNextPosition - segment.GetDataLength();
                            }
                        }

                        //If the recieve buffer is full and there are no holes
                        if (currentClientRecvWindowSize <= MAX_SEGMENT_DATA_LEN && recvNextPosition >= recvTempNextPosition)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);
                            printf("Emptying the recv buffer\n");

                            //Reset the recieve window
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                            numberOfBytesInRecieveWindow = 0;
                            recvTempNextPosition = 0;
                            recvNextPosition = 0;
                        }
                        printf("currentClientRecvWindowSize: %d\n", currentClientRecvWindowSize);
                    }

                    else
                    {
                        break;
                    }
                }

                if (!recievedFin)
                {
                    // Make an ackSegment.
                    Segment ackSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, destPort, 0,
                        nextSendAckNumber, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                    //Send the segment to the server
                    dataSocket_->Send(ackSegment, destAddr, destPort,
                        bitErrorPercent, segmentLoss);

                    printf("Sending ack segment\n");
                    printf("Ack number %d\n", ackSegment.GetAckNumber());
                    printf("%d bytes long\n\n", ackSegment.GetDataLength());
                }                

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();

                printf("\n");
            }
            //If the fin segment wasn't acked
            if (!finAcked)
            {
                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //If the timeout occurred
                if (timermiliSeconds.count() >= TimeoutInterval_)
                {
                    //Recalculate the estimated RTT value 
                    EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                    //Recalculate the RTT deviation value
                    DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                    //Recalculate the Timeout value
                    TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                    //Resend the fin segment
                    dataSocket_->Send(finSegment, destAddr,
                        destPort, bitErrorPercent, segmentLoss);

                    printf("Re-Sending fin segment\n");
                    printf("Segment number %d\n", finSegment.GetSequenceNumber());
                    if (finSegment.GetAckFlag())
                    {
                        printf("Ack number %d\n", finSegment.GetAckNumber());
                    }
                    printf("%d bytes long\n\n", finSegment.GetDataLength());

                    //Restart the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }
            }
        }

        outputFile.close();
    }

    catch (std::runtime_error& e)
    {
        throw e;
    }

    // Get the time it took to send the file in milliseconds
    std::chrono::duration<float, std::milli> miliSeconds = finishTransfer -
            startTransfer;

    std::cout << "Percent of segments with bit errors: ";
    std::cout << bitErrorPercent << "%" << std::endl;

    std::cout << "Percent of segments lost: ";
    std::cout << segmentLoss << "%" << std::endl;

    std::cout << "Time for the client to transfer the file in milliseconds: ";
    std::cout << miliSeconds.count() << std::endl;
}

void socksahoy::UdpServerTCP::Listen(std::string recieveFileName,
                                     std::string sendFileName,
                                     int bitErrorPercent,
                                     int segmentLoss,
                                     bool ignoreLoss)
{
    printf("\n");

    unsigned int currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    unsigned int startingServerSequenceNumber = 0;

    unsigned int maxServerSendWindowSize;

    unsigned int currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

    unsigned int currentClientRecvWindowSize;

    unsigned int startingClientSequenceNumber;

    unsigned int duplicateAckSequenceNumber;

    unsigned int numberOfDuplicateAcks = 0;

    uint32_t numberOfBytesInRecieveWindow = 0;

    uint32_t nextSendAckNumber = 0;

    uint32_t numberOfAckedBytes = 0;

    uint32_t numberOfUnackedBytes = 0;

    uint32_t tempSendWindowSize = 0;

    unsigned int clientPort = 0;

    bool finAcked = false;

    std::streamoff fileSize = 0;

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();
    auto startTransfer = std::chrono::high_resolution_clock::now();
    auto finishTransfer = std::chrono::high_resolution_clock::now();

    //The next position in the send ring buffer to add a byte to
    unsigned int sendNextPosition = 0;

    //The position in the send buffer to remove a byte from
    unsigned int sendBasePosition = 0;

    //The point where the base position loops back to the begining of the ring buffer
    unsigned int sendLoopBackPosition = currentServerSendWindowSize;

    //The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    unsigned int recvTempNextPosition = 0;

    unsigned int holeSize = 0;

    bool recievedAck = true;

    //Connection setup section

    //Bind the connection socket to it's address and port
    connSocket_->Bind();

    //Make a Sync packet that will be recieved from the connecting client
    Segment SynSegment(MAX_EMPTY_SEGMENT_LEN);

    //Wait for a client to connect
    connSocket_->Receive(SynSegment);

    printf("Recieved syn segment\n");
    printf("Segment number %d\n", SynSegment.GetSequenceNumber());
    printf("%d bytes long\n\n", SynSegment.GetDataLength());

    //If the sync packet isn't corrupt and it's syn flag is set
    if (SynSegment.CalculateChecksum(0) == 0x0000 && SynSegment.GetSyncFlag())
    {
        //Store the starting sequence number of the client
        startingClientSequenceNumber = SynSegment.GetSequenceNumber();

        //Make the max send window the same size of the recieve window of the client
        maxServerSendWindowSize = SynSegment.GetReceiveWindow();

        currentClientRecvWindowSize = maxServerSendWindowSize;

        //Store the port number of the client
        clientPort = SynSegment.GetSourcePortNumber();

        //Resize the send window
        sendWindow_.resize(maxServerSendWindowSize);

        // Random number engine and distribution
        // Distribution in range [1, 100]
        std::random_device dev;
        std::mt19937 rng(dev());

        using distType = std::mt19937::result_type;
        std::uniform_int_distribution<distType> uniformDist(1, 100);

        startingServerSequenceNumber = uniformDist(rng);

        nextSendAckNumber = startingClientSequenceNumber + 1;

        //Make a data port for this client
        dataPort_ = connPort_ + clientNumber_;

        //Make a Sync Ack packet that will be sent back to the client with the port number of the data socket
        Segment SynAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber,
            nextSendAckNumber, false, true, false, false, true, false, currentServerRecvWindowSize, 0, 0);

        //Send the Sync Ack packet
        connSocket_->Send(SynAckSegment, connSocket_->GetRemoteAddress(),
            clientPort, bitErrorPercent, segmentLoss);

        printf("Sending syn ack segment\n");
        printf("Segment number %d\n", SynAckSegment.GetSequenceNumber());
        printf("Ack number %d\n", SynAckSegment.GetAckNumber());
        printf("%d bytes long\n\n", SynAckSegment.GetDataLength());

        clientNumber_++;

        //Set the starting value of the timer
        startTimer = std::chrono::high_resolution_clock::now();

        while (true)
        {
            //If a packet arrived
            if (connSocket_->CheckReceive())
            {
                //Make a Ack packet that will be recieved from the connecting client
                Segment AckSegment(MAX_EMPTY_SEGMENT_LEN);

                //Recieve the ack packet from the client
                connSocket_->Receive(AckSegment);

                printf("Recieved segment\n");
                printf("Segment number %d\n", AckSegment.GetSequenceNumber());
                printf("%d bytes long\n", AckSegment.GetDataLength());
                
                //If the ack packet isn't corrupt, has the correct flags set, and has the correct ack number
                if (AckSegment.CalculateChecksum(0) == 0x0000 && AckSegment.GetAckFlag() && AckSegment.GetAckNumber() == startingServerSequenceNumber + 1)
                {
                    printf("It's an ack\n");
                    printf("Ack number %d\n\n", AckSegment.GetAckNumber());
                    //Connection established, break out of the loop
                    break;
                }
            }

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //If the timeout occurred
            if (timermiliSeconds.count() >= TimeoutInterval_)
            {                
                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Resend the Sync Ack packet without bit errors or loss
                connSocket_->Send(SynAckSegment, connSocket_->GetRemoteAddress(),
                    clientPort, bitErrorPercent, segmentLoss);

                printf("Re-Sending syn ack segment\n");
                printf("Segment number %d\n", SynAckSegment.GetSequenceNumber());
                printf("Ack number %d\n", SynAckSegment.GetAckNumber());
                printf("%d bytes long\n\n", SynAckSegment.GetDataLength());

                //Restart the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }
        }
    }

    //Data transfer section

    //Delete the old socket.
    delete connSocket_;

    //Set up a new socket the uses the dataPort for this client.
    dataSocket_ = new SocketTCP(dataPort_);

    //Bind the data socket to it's address and port
    dataSocket_->Bind();

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

        // Open a file to output to
        std::ofstream outputFile(recieveFileName, std::ios::binary);

        char byte = 0;
        bool queuedByte = false;

        //Mark the start time of the file transfer
        startTransfer = std::chrono::high_resolution_clock::now();

        //Loop until we recieve a fin segment from the client
        while (true)
        {
            // Make a segment.
            Segment segment(MAX_FULL_SEGMENT_LEN);

            // Recieve the segment from the client
            dataSocket_->Receive(segment); 
            
            printf("Recieved segment\n");
            printf("Segment number %d\n", segment.GetSequenceNumber());
            printf("%d bytes long\n", segment.GetDataLength());

            //If the segment is not corrupt
            if (segment.CalculateChecksum(0) == 0x0000)
            {
                //If the segment is an ack segment
                if (segment.GetAckFlag())
                {
                    recievedAck = true;

                    //If the ack isn't a duplicate
                    if (segment.GetAckNumber() > duplicateAckSequenceNumber)
                    {
                        printf("It's an ack\n");
                        printf("Ack number %d\n", segment.GetAckNumber());
                        //If the ack is for the fin segment
                        if (numberOfAckedBytes >= (unsigned int)fileSize)
                        {
                            finAcked = true;
                        }

                        else
                        {
                            printf("It's a new ack segment\n");

                            unsigned int tempNumberOfAckedBytes = segment.GetAckNumber() - duplicateAckSequenceNumber;

                            //Increase the number of acked bytes
                            numberOfAckedBytes += tempNumberOfAckedBytes;

                            //Decrease the number of unacked bytes in the window
                            numberOfUnackedBytes -= tempNumberOfAckedBytes;

                            //Move the base position

                            for (unsigned int i; i < tempNumberOfAckedBytes; i++)
                            {
                                //If we are at, or past, the end of the ring buffer loop back
                                if (sendBasePosition >= sendLoopBackPosition - 1)
                                {
                                    //Reset it back to the beginning
                                    sendBasePosition = 0;
                                    sendLoopBackPosition = currentServerRecvWindowSize;
                                }
                                else
                                {
                                    sendBasePosition++;
                                }
                            }

                            //Update the duplicate number
                            duplicateAckSequenceNumber = segment.GetAckNumber();
                            //Reset the number of duplicates
                            numberOfDuplicateAcks = 0;
                        }
                    }
                    else
                    {
                        printf("It's a duplicate ack\n");
                        printf("Ack number %d\n", segment.GetAckNumber());
                        //Increase the number of duplicates
                        numberOfDuplicateAcks++;
                    }
                }

                //If the segment is a fin segment
                if (segment.GetFinFlag())
                {
                    printf("It's a fin\n");
                    // Make an segment.
                    Segment ackSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, 0,
                        segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Send the segment to the client
                    dataSocket_->Send(segment, dataSocket_->GetRemoteAddress(), clientPort,
                        0, 0);

                    printf("Sending ack segment\n");
                    printf("Ack number %d\n", segment.GetAckNumber());
                    printf("%d bytes long\n\n", segment.GetDataLength());

                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                    break;
                }

                //If the data in the segment is in order
                if (segment.GetSequenceNumber() == nextSendAckNumber)
                {
                    printf("It's data is in order\n");
                    //Extract the data from the segment and add it to the recieve window
                    for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                    {
                        //Add a byte of data to the recieve window
                        recvWindow_[recvNextPosition] = segment.GetData()[i];

                        //Move the recieve window
                        recvNextPosition++;
                    }

                    //If the hole has been closed
                    if (recvNextPosition < recvTempNextPosition && holeSize == 0)
                    {
                        nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                        recvNextPosition = recvTempNextPosition;
                    }

                    //No hole exists, or the hole isn't closed
                    else
                    {
                        nextSendAckNumber += segment.GetDataLength();
                        if (holeSize != 0)
                        {
                            holeSize -= segment.GetDataLength();
                        }
                    }

                    numberOfBytesInRecieveWindow += segment.GetDataLength();
                    currentServerRecvWindowSize -= segment.GetDataLength();
                }

                //If the data in the segment is out of order, but not data that's already been recieved
                else if (segment.GetSequenceNumber() >= nextSendAckNumber)
                {
                    printf("It's data is out of order\n");
                    recvTempNextPosition = recvNextPosition + segment.GetSequenceNumber() - nextSendAckNumber;

                    //Extract the data from the segment and add it to the recieve window
                    for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                    {
                        //Add a byte of data to the recieve window
                        recvWindow_[recvTempNextPosition] = segment.GetData()[i];

                        //Move the recieve window
                        recvTempNextPosition++;
                    }

                    if (ignoreLoss)
                    {
                        nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                        numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                        currentServerRecvWindowSize -= recvNextPosition - recvTempNextPosition;
                        recvNextPosition = recvTempNextPosition;
                    }

                    else
                    {
                        numberOfBytesInRecieveWindow += segment.GetDataLength();
                        currentServerRecvWindowSize -= segment.GetDataLength();

                        holeSize = recvTempNextPosition - recvNextPosition - segment.GetDataLength();
                    }
                }

                //If the recieve buffer is full and there are no holes
                if (currentServerRecvWindowSize <= MAX_SEGMENT_DATA_LEN && recvNextPosition >= recvTempNextPosition)
                {
                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);
                    printf("Emptying the recv buffer\n");

                    //Reset the recieve window
                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                    numberOfBytesInRecieveWindow = 0;
                    recvTempNextPosition = 0;
                    recvNextPosition = 0;
                }
                printf("currentServerRecvWindowSize: %d\n", currentServerRecvWindowSize);
                
                printf("\n");

            }

            //If the transfer of the input file isn't complete
            if (numberOfAckedBytes < (unsigned int)fileSize)
            {
                //If the recieved ack wasn't a duplicate
                if (numberOfDuplicateAcks == 0 && recievedAck == true)
                {
                    //The length of the segment that will be sent
                    unsigned int segmentLength = segment.GetReceiveWindow();

                    if (segmentLength > MAX_SEGMENT_DATA_LEN)
                    {
                        segmentLength = MAX_SEGMENT_DATA_LEN;
                    }

                    unsigned int sequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make a segment.
                    Segment ackSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sequenceNumber,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Populate the ack segment with new data from the file
                    for (;;)
                    {
                        // We have to make sure we have put every byte into a segment
                        // Check if we have a left-over byte... this means we are in
                        // a new segment
                        if (queuedByte)
                        {
                            ackSegment.AddByte(byte);
                            queuedByte = false;
                            numberOfUnackedBytes++;
                            sendWindow_[sendNextPosition].byte = byte;
                            sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                            sendWindow_[sendNextPosition].ackNumber = nextSendAckNumber;
                            sendWindow_[sendNextPosition].urg = false;
                            sendWindow_[sendNextPosition].ack = true;
                            sendWindow_[sendNextPosition].psh = false;
                            sendWindow_[sendNextPosition].rst = false;
                            sendWindow_[sendNextPosition].syn = false;
                            sendWindow_[sendNextPosition].fin = false;
                            sendWindow_[sendNextPosition].urgDataPointer = 0;
                            sendWindow_[sendNextPosition].dataLength = segmentLength;
                            sendWindow_[sendNextPosition].options = 0;
                        }

                        // Get a byte and try to put it into the packet.
                        // If after this we have a queuedByte, this means we go to
                        // another packet.
                        if (inputFile.get(byte))
                        {
                            queuedByte = !ackSegment.AddByte(byte);

                            if (queuedByte)
                            {
                                break;
                            }

                            numberOfUnackedBytes++;
                            sendWindow_[sendNextPosition].byte = byte;
                            sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                            sendWindow_[sendNextPosition].ackNumber = nextSendAckNumber;
                            sendWindow_[sendNextPosition].urg = false;
                            sendWindow_[sendNextPosition].ack = true;
                            sendWindow_[sendNextPosition].psh = false;
                            sendWindow_[sendNextPosition].rst = false;
                            sendWindow_[sendNextPosition].syn = false;
                            sendWindow_[sendNextPosition].fin = false;
                            sendWindow_[sendNextPosition].urgDataPointer = 0;
                            sendWindow_[sendNextPosition].dataLength = segmentLength;
                            sendWindow_[sendNextPosition].options = 0;
                        }

                        // If we can't get a byte, that means we got EOF; leave the
                        // loop.
                        else
                        {
                            break;
                        }

                        //If we are at, or past, the end of the ring buffer
                        if (sendNextPosition >= currentServerSendWindowSize - 1)
                        {
                            //Reset it back to the beginning
                            sendNextPosition = 0;
                            sendLoopBackPosition = currentServerSendWindowSize;
                        }
                        else
                        {
                            sendNextPosition++;
                        }
                    }

                    //Send the segment
                    dataSocket_->Send(ackSegment, dataSocket_->GetRemoteAddress(), clientPort,
                        bitErrorPercent, segmentLoss);

                    printf("Sending data segment\n");
                    printf("Segment number %d\n", ackSegment.GetSequenceNumber());
                    printf("Ack number %d\n", ackSegment.GetAckNumber());
                    printf("RecvWindow: %d\n", ackSegment.GetReceiveWindow());
                    printf("%d bytes long\n\n", ackSegment.GetDataLength());
                }

                else
                {

                    if (numberOfDuplicateAcks >= 3)
                    {
                        numberOfDuplicateAcks = 0;
                    }
                    //Populate the ack segment with old data

                    //Make the temp position start at the base position
                    int tempPosition = sendBasePosition;

                    unsigned int segmentLength = sendWindow_[tempPosition].dataLength;

                    //Remake the segment
                    Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sendWindow_[tempPosition].sequenceNumber,
                        sendWindow_[tempPosition].ackNumber, sendWindow_[tempPosition].urg, sendWindow_[tempPosition].ack, 
                        sendWindow_[tempPosition].psh, sendWindow_[tempPosition].rst, sendWindow_[tempPosition].syn, 
                        sendWindow_[tempPosition].fin, currentServerRecvWindowSize, sendWindow_[tempPosition].urgDataPointer, sendWindow_[tempPosition].options);

                    //Populate the segment with all of it's bytes
                    for (unsigned int i = 0; i < segmentLength; i++)
                    {
                        //Get the byte from the send window
                        byte = sendWindow_[tempPosition].byte;

                        //Add it to the segment
                        resendSegment.AddByte(byte);
                            
                        tempPosition++;
                    }

                    //Re-send the segment
                    dataSocket_->Send(resendSegment, dataSocket_->GetRemoteAddress(), clientPort,
                        bitErrorPercent, segmentLoss);

                    printf("Re-Sending data segment\n");
                    printf("Segment number %d\n", resendSegment.GetSequenceNumber());
                    printf("Ack number %d\n", resendSegment.GetAckNumber());
                    printf("RecvWindow: %d\n", resendSegment.GetReceiveWindow());
                    printf("%d bytes long\n\n", resendSegment.GetDataLength());
                }
            }

            else
            {
                //If the file transfer is complete and the fin segment hasn't acked
                if (numberOfAckedBytes >= (unsigned int)fileSize && !finAcked)
                {
                    //Make a fin ack segment
                    Segment finAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);
                    
                    //Send the fin ack segment to the client
                    dataSocket_->Send(finAckSegment, dataSocket_->GetRemoteAddress(), clientPort,
                        bitErrorPercent, segmentLoss);

                    printf("Sending fin segment\n");
                    printf("Segment number %d\n", finAckSegment.GetSequenceNumber());
                    printf("Ack number %d\n", finAckSegment.GetAckNumber());
                    printf("RecvWindow: %d\n", finAckSegment.GetReceiveWindow());
                    printf("%d bytes long\n\n", finAckSegment.GetDataLength());
                }

                else
                {
                    //Make an empty ack segment
                    Segment emptyAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, 0,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);                    

                    //Send the ack packet to the client
                    dataSocket_->Send(emptyAckSegment, dataSocket_->GetRemoteAddress(), clientPort,
                        bitErrorPercent, segmentLoss);

                    printf("Sending ack segment\n");
                    printf("Ack number %d\n", emptyAckSegment.GetAckNumber());
                    printf("RecvWindow: %d\n", emptyAckSegment.GetReceiveWindow());
                    printf("%d bytes long\n\n", emptyAckSegment.GetDataLength());
                }
            }

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //Recalculate the estimated RTT value 
            EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

            //Recalculate the RTT deviation value
            DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

            //Recalculate the Timeout value
            TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

            //Mark the start time of the timer
            startTimer = std::chrono::high_resolution_clock::now();
            
            recievedAck = false;
        }

        outputFile.close();
        
        if (!finAcked)
        {
            while (true)
            {
                //If the input file hasn't been fully transferred
                if (numberOfAckedBytes < (unsigned int)fileSize)
                {
                    //The ring buffer isn't full
                    if ((numberOfUnackedBytes < currentServerSendWindowSize))
                    {
                        //The length of the segment that will be sent
                        unsigned int segmentLength;

                        //If the segment won't fit in the send buffer
                        if (numberOfUnackedBytes + MAX_SEGMENT_DATA_LEN > currentServerSendWindowSize)
                        {
                            //Make it smaller so that it will fit
                            segmentLength = currentServerSendWindowSize - numberOfUnackedBytes;
                        }
                        //A fill size segment will fit
                        else
                        {
                            segmentLength = MAX_SEGMENT_DATA_LEN;
                        }

                        //If the segment length is longer than what the client can recieve
                        if (segmentLength > currentClientRecvWindowSize)
                        {
                            segmentLength = currentClientRecvWindowSize;
                            currentClientRecvWindowSize = 0;
                        }

                        if (segmentLength > 0)                            
                        {
                            unsigned int sequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                            // Make a segment.
                            Segment segment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sequenceNumber,
                                0, false, false, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                            //If the buffer is empty
                            if (sendNextPosition == sendBasePosition)
                            {
                                //Mark the start time of the timer
                                startTimer = std::chrono::high_resolution_clock::now();
                            }

                            for (;;)
                            {
                                // We have to make sure we have put every byte into a segment
                                // Check if we have a left-over byte... this means we are in
                                // a new segment
                                if (queuedByte)
                                {
                                    segment.AddByte(byte);
                                    queuedByte = false;
                                    numberOfUnackedBytes++;
                                    sendWindow_[sendNextPosition].byte = byte;
                                    sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                                    sendWindow_[sendNextPosition].ackNumber = 0;
                                    sendWindow_[sendNextPosition].urg = false;
                                    sendWindow_[sendNextPosition].ack = false;
                                    sendWindow_[sendNextPosition].psh = false;
                                    sendWindow_[sendNextPosition].rst = false;
                                    sendWindow_[sendNextPosition].syn = false;
                                    sendWindow_[sendNextPosition].fin = false;
                                    sendWindow_[sendNextPosition].urgDataPointer = 0;
                                    sendWindow_[sendNextPosition].dataLength = segmentLength;
                                    sendWindow_[sendNextPosition].options = 0;
                                }

                                // Get a byte and try to put it into the packet.
                                // If after this we have a queuedByte, this means we go to
                                // another packet.
                                if (inputFile.get(byte))
                                {
                                    queuedByte = !segment.AddByte(byte);

                                    if (queuedByte)
                                    {
                                        break;
                                    }

                                    numberOfUnackedBytes++;
                                    sendWindow_[sendNextPosition].byte = byte;
                                    sendWindow_[sendNextPosition].sequenceNumber = sequenceNumber;
                                    sendWindow_[sendNextPosition].ackNumber = 0;
                                    sendWindow_[sendNextPosition].urg = false;
                                    sendWindow_[sendNextPosition].ack = false;
                                    sendWindow_[sendNextPosition].psh = false;
                                    sendWindow_[sendNextPosition].rst = false;
                                    sendWindow_[sendNextPosition].syn = false;
                                    sendWindow_[sendNextPosition].fin = false;
                                    sendWindow_[sendNextPosition].urgDataPointer = 0;
                                    sendWindow_[sendNextPosition].dataLength = segmentLength;
                                    sendWindow_[sendNextPosition].options = 0;
                                }

                                // If we can't get a byte, that means we got EOF; leave the
                                // loop.
                                else
                                {
                                    break;
                                }

                                //If we are at, or past, the end of the ring buffer
                                if (sendNextPosition >= currentServerSendWindowSize - 1)
                                {
                                    //Reset it back to the beginning
                                    sendNextPosition = 0;
                                }
                                else
                                {
                                    sendNextPosition++;
                                }
                            }
                            //Send the segment
                            dataSocket_->Send(segment, dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                            printf("Sending data segment\n");
                            printf("Segment number %d\n", segment.GetSequenceNumber());
                            printf("RecvWindow: %d\n", segment.GetReceiveWindow());
                            printf("%d bytes long\n\n", segment.GetDataLength());
                        }                        
                    }

                    // Get the current timer value in milliseconds
                    currentTimer = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<float, std::milli> timermiliSeconds =
                        currentTimer - startTimer;

                    //If a timeout has occured, or three duplicate acks arrived
                    if (timermiliSeconds.count() >= TimeoutInterval_ || numberOfDuplicateAcks >= 3)
                    {
                        //Make the temp position start at the base position
                        unsigned int tempPosition = sendBasePosition;

                        unsigned int segmentLength = sendWindow_[tempPosition].dataLength;

                        //Remake the segment
                        Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sendWindow_[tempPosition].sequenceNumber,
                            nextSendAckNumber, sendWindow_[tempPosition].urg, sendWindow_[tempPosition].ack, sendWindow_[tempPosition].psh,
                            sendWindow_[tempPosition].rst, sendWindow_[tempPosition].syn, sendWindow_[tempPosition].fin, currentServerRecvWindowSize, sendWindow_[tempPosition].urgDataPointer, sendWindow_[tempPosition].options);

                        //Populate the segment with all of it's bytes
                        for (unsigned int i = 0; i < segmentLength; i++)
                        {
                            //Get the byte from the send window
                            byte = sendWindow_[tempPosition].byte;

                            //Add it to the segment
                            resendSegment.AddByte(byte);

                            //If we are at, or past, the end of the ring buffer
                            if (tempPosition >= sendLoopBackPosition - 1)
                            {
                                //Reset it back to the beginning
                                tempPosition = 0;
                            }
                            else
                            {
                                tempPosition++;
                            }
                        }

                        //Only recalculate the timeout interval if a timeout occured
                        if (numberOfDuplicateAcks < 3)
                        {
                            //Recalculate the estimated RTT value 
                            EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                            //Recalculate the RTT deviation value
                            DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                            //Recalculate the Timeout value
                            TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;
                        }

                        else
                        {
                            numberOfDuplicateAcks = 0;
                        }

                        //The slow start threashhold becomes half of the current client window size
                        ssthresh = currentServerSendWindowSize / 2;

                        //The server window size gets reset back to one full segment.
                        currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

                        //Resend the segment
                        dataSocket_->Send(resendSegment, dataSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                        printf("Re-Sending data segment\n");
                        printf("Segment number %d\n", resendSegment.GetSequenceNumber());
                        printf("RecvWindow: %d\n", resendSegment.GetReceiveWindow());
                        printf("%d bytes long\n\n", resendSegment.GetDataLength());

                        //Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }

                    //A packet has arrived
                    if (dataSocket_->CheckReceive())
                    {
                        // Make a segment.
                        Segment AckSegment(MAX_FULL_SEGMENT_LEN);

                        // Recieve the segment from the server
                        dataSocket_->Receive(AckSegment);

                        printf("Recieved segment\n");
                        printf("Segment number %d\n", AckSegment.GetSequenceNumber());
                        printf("%d bytes long\n", AckSegment.GetDataLength());

                        //If the segment is not corrupt
                        if (AckSegment.CalculateChecksum(0) == 0x0000)
                        {
                            currentClientRecvWindowSize = AckSegment.GetReceiveWindow();                        

                            //If the segment is an ack segment
                            if (AckSegment.GetAckFlag())
                            {
                                //If the ack isn't a duplicate
                                if (AckSegment.GetAckNumber() != duplicateAckSequenceNumber)
                                {
                                    printf("It's an ack");
                                    printf("Ack number %d\n", AckSegment.GetAckNumber());
                                    printf("It's a new ack segment\n");

                                    unsigned int tempNumberOfAckedBytes = AckSegment.GetAckNumber() - duplicateAckSequenceNumber;

                                    //Increase the number of acked bytes
                                    numberOfAckedBytes += tempNumberOfAckedBytes;

                                    //Decrease the number of unacked bytes in the window
                                    numberOfUnackedBytes -= tempNumberOfAckedBytes;

                                    //Increase the windowsize

                                    //If the window size is currently smaller than the max window size
                                    if (currentServerSendWindowSize < maxServerSendWindowSize)
                                    {
                                        //If we are in CA mode
                                        if (currentServerSendWindowSize >= ssthresh)
                                        {
                                            tempSendWindowSize += MAX_FULL_SEGMENT_LEN * ((float)MAX_FULL_SEGMENT_LEN / (float)currentServerSendWindowSize);

                                            if (tempSendWindowSize >= MAX_FULL_SEGMENT_LEN)
                                            {
                                                currentServerSendWindowSize += MAX_FULL_SEGMENT_LEN;
                                                tempSendWindowSize = 0;
                                            }
                                        }
                                        //Must be in SS mode
                                        else
                                        {
                                            currentServerSendWindowSize += MAX_FULL_SEGMENT_LEN;

                                            //If we've gone from SS to CA mode
                                            if (currentServerSendWindowSize > ssthresh)
                                            {
                                                currentServerSendWindowSize = ssthresh;
                                            }
                                        }

                                        //If the new server window size is greater than the max set by the client
                                        if (currentServerSendWindowSize > maxServerSendWindowSize)
                                        {
                                            currentServerSendWindowSize = maxServerSendWindowSize;
                                        }
                                    }
                                    
                                    //Move the base position

                                    for (unsigned int i; i < tempNumberOfAckedBytes; i++)
                                    {
                                        //If we are at, or past, the end of the ring buffer loop back
                                        if (sendBasePosition >= sendLoopBackPosition - 1)
                                        {
                                            //Reset it back to the beginning
                                            sendBasePosition = 0;
                                            sendLoopBackPosition = currentServerRecvWindowSize;
                                        }
                                        else
                                        {
                                            sendBasePosition++;
                                        }
                                    }

                                    //Update the duplicate number
                                    duplicateAckSequenceNumber = AckSegment.GetAckNumber();
                                    //Reset the number of duplicates
                                    numberOfDuplicateAcks = 0;
                                }
                                else
                                {
                                    printf("It's a duplicate ack segment\n");
                                    //Increase the number of duplicates
                                    numberOfDuplicateAcks++;
                                }
                            }
                        }

                        //If the file transfer is complete
                        if (numberOfAckedBytes > (unsigned int)fileSize)
                        {
                            //Make a fin segment
                            Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1,
                                0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                            //Send the fin segment to the client
                            dataSocket_->Send(finSegment, dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                            printf("Sending fin segment\n");
                            printf("Segment number %d\n", finSegment.GetSequenceNumber());
                            printf("%d bytes long\n\n", finSegment.GetDataLength());
                        }

                        std::chrono::duration<float, std::milli> timermiliSeconds =
                            currentTimer - startTimer;

                        //Recalculate the estimated RTT value 
                        EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                        //Recalculate the RTT deviation value
                        DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                        //Recalculate the Timeout value
                        TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                        //Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();

                        printf("\n");
                    }
                }

                //Wait for the fin segment to be acked
                else
                {
                    // Get the current timer value in milliseconds
                    currentTimer = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<float, std::milli> timermiliSeconds =
                        currentTimer - startTimer;

                    //If a timeout has occured
                    if (timermiliSeconds.count() >= TimeoutInterval_)
                    {
                        //Make a fin segment
                        Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1,
                            0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                        //Send the fin segment to the client
                        dataSocket_->Send(finSegment, dataSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                        printf("Re-Sending fin segment\n");
                        printf("Segment number %d\n", finSegment.GetSequenceNumber());
                        printf("%d bytes long\n\n", finSegment.GetDataLength());

                        //Recalculate the estimated RTT value 
                        EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                        //Recalculate the RTT deviation value
                        DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                        //Recalculate the Timeout value
                        TimeoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                        //Set the starting value of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }
                    
                    //A packet has arrived
                    if (dataSocket_->CheckReceive())
                    {
                        // Make a segment.
                        Segment segment(MAX_FULL_SEGMENT_LEN);

                        // Recieve the segment from the client
                        dataSocket_->Receive(segment);

                        printf("Recieved segment\n");
                        printf("Segment number %d\n", segment.GetSequenceNumber());
                        printf("%d bytes long\n", segment.GetDataLength());

                        //If the segment is not corrupt
                        if (segment.CalculateChecksum(0) == 0x0000)
                        {
                            //If the segment has an ack for the fin segment
                            if (segment.GetAckFlag() && segment.GetAckNumber() == startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1)
                            {
                                printf("It's an ack for the fin");
                                printf("Ack number %d\n", segment.GetAckNumber());
                                finAcked = true;
                                break;
                            }
                        }
                    }
                }
            }            
        }

        //Mark the end time of the file transfer
        finishTransfer = std::chrono::high_resolution_clock::now();

        inputFile.close();

        // Get the time it took to send the file in milliseconds
        std::chrono::duration<float, std::milli> miliSeconds = finishTransfer -
            startTransfer;

        std::cout << "Percent of segments with bit errors: ";
        std::cout << bitErrorPercent << "%" << std::endl;

        std::cout << "Percent of segments lost: ";
        std::cout << segmentLoss << "%" << std::endl;

        std::cout << "Time for the client to transfer the file in milliseconds: ";
        std::cout << miliSeconds.count() << std::endl;
    }

    catch (std::runtime_error& e)
    {
        throw e;
    }
}

bool socksahoy::UdpServerTCP::FileExists(const std::string& fileName) const
{
    struct stat fileBuffer;
    int exists = stat(fileName.c_str(), &fileBuffer);

    return (exists == 0);
}

// vim: set expandtab ts=4 sw=4: