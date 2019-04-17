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
    : connPort_(port), connSocket_(port_) {}

void socksahoy::UdpServerTCP::Send(unsigned int destPort,
                                   const std::string& destAddr,
                                   std::string recieveFileName,
                                   std::string sendFileName,
                                   int bitErrorPercent,
                                   int segmentLoss,
                                   bool ignoreLoss)
{
    int currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    int startingClientSequenceNumber = 0;

    int maxClientSendWindowSize;

    int currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

    int currentServerRecvWindowSize;

    int startingServerSequenceNumber;

    int duplicateAckSequenceNumber;

    int numberOfDuplicateAcks = 0;

    int numberOfBytesInRecieveWindow = 0;

    int nextSendAckNumber = 0;

    bool sendAck = false;

    int numberOfSegmentsInWindow = 0;

    int numberOfAckedBytes = 0;

    int numberOfUnackedBytes = 0;

    int numberOfRecievedBytes = 0;

    int tempSendWindowSize = 0;

    bool recievedFin = false;

    std::streamoff fileSize = 0;

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();

    //The next position in the send ring buffer to add a byte to
    int sendNextPosition = 0;

    //The position in the send buffer to remove a byte from
    int sendBasePosition = 0;
    
    //The next position in the send ring buffer to add a byte to
    int sendSegmentNextPosition = 0;

    //The position in the send buffer to remove a byte from
    int sendSegmentBasePosition = 0;

    //The next position in the recv ring buffer to add a byte to
    int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    int recvTempNextPosition = 0;

    //The position in the recv buffer to remove a byte from
    int recvBasePosition = 0;

    //Connection set up section

    // Random number engine and distribution
        // Distribution in range [1, 100]
    std::random_device dev;
    std::mt19937 rng(dev());

    using distType = std::mt19937::result_type;
    std::uniform_int_distribution<distType> uniformDist(1, 100);

    startingClientSequenceNumber = uniformDist(rng);

    //Make a connection set up segment
    Segment<MAX_EMPTY_SEGMENT_LEN> SynSegment(connPort_, destPort, startingClientSequenceNumber,
        0, false, false, false, false, true, false, currentClientRecvWindowSize, 0, 0);

    //Send the connection set up segment to the server
    connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(SynSegment, destAddr,
        destPort, bitErrorPercent, segmentLoss);

    //Set the starting value of the timer
    startTimer = std::chrono::high_resolution_clock::now();

    while (true)
    {
        //If a packet arrived
        if (connSocket_.CheckReceive())
        {
            //Make a SynAck segment that will be recieved from the server
            Segment<MAX_EMPTY_SEGMENT_LEN> SynAckSegment;

            //Recieve the ack segment from the client
            connSocket_.Receive<MAX_EMPTY_SEGMENT_LEN>(SynAckSegment);

            //If the syn ack segment isn't corrupt, has the correct flags set, and has the correct ack number
            if (SynAckSegment.CalculateChecksum() == 0xff && SynAckSegment.GetSyncFlag() && SynAckSegment.GetAckFlag() && SynAckSegment.GetAckNumber() == startingClientSequenceNumber + 1)
            {
                //Make the max send window the same size of the recieve window
                maxClientSendWindowSize = SynAckSegment.GetReceiveWindow();
                currentServerRecvWindowSize = maxClientSendWindowSize;

                //Resize the send window
                sendWindow_.resize(maxClientSendWindowSize);

                //Resize the segment length window
                sendWindowSegmentLength_.resize(1);

                //Store the starting sequence number of server
                startingServerSequenceNumber = SynAckSegment.GetSequenceNumber();

                nextSendAckNumber = startingServerSequenceNumber + 2;

                //Store the current ack number
                duplicateAckSequenceNumber = SynAckSegment.GetAckNumber();

                //Make an ack segment to send back to the server
                Segment<MAX_EMPTY_SEGMENT_LEN> AckSegment(connPort_, destPort, startingClientSequenceNumber+1,
                    startingServerSequenceNumber, false, false, false, false, true, false, currentClientRecvWindowSize, 0, 0);

                //Send the ack segmet to the server
                connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(AckSegment, destAddr,
                    destPort, bitErrorPercent, segmentLoss);

                //Switch over to using the port that the server sent us for the data transfer
                destPort = SynAckSegment.GetDestPortNumber();

                //Connection established, break out of the loop
                break;
            }
        }

        // Get the current timer value in milliseconds
        currentTimer = std::chrono::high_resolution_clock::now();

        std::chrono::duration<float, std::milli> timermiliSeconds =
            currentTimer - startTimer;

        //If the timeout occurred
        if (timermiliSeconds.count() >= TimoutInterval_)
        {
            //Recalculate the estimated RTT value 
            EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

            //Recalculate the RTT deviation value
            DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

            //Recalculate the Timeout value
            TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

            //Resend the Sync packet
            connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(SynSegment, destAddr,
                destPort, bitErrorPercent, segmentLoss);

            //Restart the timer
            startTimer = std::chrono::high_resolution_clock::now();
        }
    }

    //Data transfer section

    //Make the data port the same as the connection port
    dataPort_ = connPort_;

    //Make a socket to send and receive data from
    dataSocket_(dataPort_);

    //Bind the data socket to it's address and port
    dataSocket_.Bind();

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
        while (numberOfAckedBytes < fileSize)
        {
            //The ring buffer isn't full and the last packet hasn't been sent.
            if ((numberOfUnackedBytes < currentClientSendWindowSize)
                    && (numberOfAckedBytes < fileSize))
            {
                //The length of the segment that will be sent
                int segmentLength;

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
                }

                // Make a segment.
                Segment<segmentLength> segment(dataPort_, destPort, startingClientSequenceNumber+numberOfUnackedBytes+2,
                    nextSendAckNumber, false, sendAck, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                if (sendAck)
                {
                    sendAck = false;
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
                        sendWindow_[sendNextPosition] = byte;
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
                        sendWindow_[sendNextPosition] = byte;
                    }

                    // If we can't get a byte, that means we got EOF; leave the
                    // loop.
                    else
                    {
                        break;
                    }

                    //If we are at, or past, the end of the ring buffer
                    if (sendNextPosition >= currentClientSendWindowSize-1)
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
                dataSocket_.Send<segmentLength>(segment, destAddr, destPort,
                bitErrorPercent, segmentLoss);

                //If the buffer is empty
                if (sendNextPosition == sendBasePosition)
                {
                    //Mark the start time of the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }

                //Add the length of the segment to the second send buffer
                sendWindowSegmentLength_[sendSegmentNextPosition] = segmentLength;

                //If we are at, or past, the end of the ring buffer
                if (sendSegmentNextPosition >= numberOfSegmentsInWindow - 1)
                {
                    //Reset it back to the beginning
                    sendSegmentNextPosition = 0;
                }
                else
                {
                    sendSegmentNextPosition++;
                }

                //Increase the number of segments in flight
                numberOfSegmentsInWindow++;
            }

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //If a timeout has occured, or three duplicate acks arrived
            if (timermiliSeconds.count() >= TimoutInterval_ || numberOfDuplicateAcks >= 3)
            {
                //Make the temp position start at the base position
                int tempPosition = sendBasePosition;

                int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                //Make a temporary buffer to hold the segment's data
                std::vector<char> tempbuffer;
                tempbuffer.resize(segmentLength);

                //Remake the segment
                Segment<segmentLength> resendSegment(dataPort_, destPort, startingClientSequenceNumber+numberOfAckedBytes+2,
                    nextSendAckNumber, false, sendAck, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                if (sendAck)
                {
                    sendAck = false;
                }

                //Populate the segment with all of it's bytes
                for(int i = 0; i < segmentLength; i++)
                {
                    //Get the byte from the send window
                    byte = sendWindow_[tempPosition];

                    //Add it to the temp buffer
                    tempbuffer[i] = byte;

                    //Add it to the segment
                    resendSegment.AddByte(byte);

                    //If we are at, or past, the end of the ring buffer
                    if (tempPosition >= currentClientSendWindowSize-1)
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
                    TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;
                }

                else
                {
                    numberOfDuplicateAcks = 0;
                }

                //Move the segment length back to the beginning of the ring buffer
                sendWindowSegmentLength_[0] = sendWindowSegmentLength_[sendSegmentBasePosition];

                //The segment length base pointer gets reset back to the beginning of the ring buffer
                sendSegmentBasePosition = 0;

                //The segment length next pointer points to just after the base pointer
                sendSegmentNextPosition = 1;

                //The segment length buffer gets resized
                numberOfSegmentsInWindow = 1;
                sendWindowSegmentLength_.resize(numberOfSegmentsInWindow);

                //Move the bytes from the tempbuffer to the beginning of the ring buffer
                for (int i = 0; i < segmentLength; i++)
                {
                    sendWindow_[i] = tempbuffer[i];
                }

                //The slow start threashhold becomes half of the current client window size
                ssthresh = currentClientSendWindowSize / 2;

                //The client window size gets reset back to one full segment.
                currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

                //This segment is the only set of unacked bytes
                numberOfUnackedBytes = currentClientSendWindowSize;

                //Move the base pointer to the beginning of the ring buffer
                sendBasePosition = 0;

                //Move next pointer to just after the the end of the ring buffer, 
                //which is the beginning
                sendNextPosition = 0;

                //Resend the segment
                dataSocket_.Send<segmentLength>(resendSegment, destAddr, destPort,
                    bitErrorPercent, segmentLoss);

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }

            //A packet has arrived
            if (dataSocket_.CheckReceive())
            {
                // Make a segment.
                Segment<MAX_FULL_SEGMENT_LEN> AckSegment;

                // Recieve the segment from the server
                dataSocket_.Receive<MAX_FULL_SEGMENT_LEN>(AckSegment);

                //If the segment is not corrupt
                if (AckSegment.CalculateChecksum() == 0xff)
                {
                    currentServerRecvWindowSize = AckSegment.GetReceiveWindow();

                    //If it's the synack from before
                    if (AckSegment.GetSyncFlag() && AckSegment.GetAckFlag() && AckSegment.GetAckNumber() == startingClientSequenceNumber + 1)
                    {
                        //Make an ack segment to send back to the server
                        Segment<MAX_EMPTY_SEGMENT_LEN> AckSegment(connPort_, destPort, startingClientSequenceNumber + 1,
                            startingServerSequenceNumber, false, false, false, false, true, false, currentClientRecvWindowSize, 0, 0);

                        //Send the ack segmet to the server
                        connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(AckSegment, destAddr,
                            destPort, bitErrorPercent, segmentLoss);

                        continue;
                    }
            
                    //If the segment is an ack segment
                    else if (AckSegment.GetAckFlag())
                    {
                        //If the ack isn't a duplicate
                        if (AckSegment.GetAckNumber() != duplicateAckSequenceNumber)
                        {
                            //Get the length of the acked segment
                            int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                            //Increase the number of acked bytes
                            numberOfAckedBytes += segmentLength;

                            //Decrease the number of unacked bytes in the window
                            numberOfUnackedBytes -= segmentLength;

                            //Decrease the number of segments in the window
                            numberOfSegmentsInWindow--;

                            //Move the base position

                            //If the move would go beyond the end of the ring buffer
                            if (sendBasePosition + segmentLength > currentClientSendWindowSize-1)
                            {
                                sendBasePosition = currentClientSendWindowSize - (1 + sendBasePosition + segmentLength);
                            }

                            //Normal move
                            else
                            {
                                sendBasePosition += segmentLength;
                            }

                            //Move the base pointer of the second ring buffer

                            //If we are at, or past, the end of the ring buffer
                            if (sendSegmentBasePosition >= numberOfSegmentsInWindow - 1)
                            {
                                //Reset it back to the beginning
                                sendSegmentBasePosition = 0;
                            }
                            else
                            {
                                sendSegmentBasePosition++;
                            }

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
                                        sendWindowSegmentLength_.push_back(0);
                                    }
                                }
                                //Must be in SS mode
                                else
                                {
                                    currentClientSendWindowSize += MAX_FULL_SEGMENT_LEN;
                                    sendWindowSegmentLength_.push_back(0);

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

                            //Update the duplicate number
                            duplicateAckSequenceNumber = AckSegment.GetAckNumber();
                            //Reset the number of duplicates
                            numberOfDuplicateAcks = 0;
                        }
                        else
                        {
                            //Increase the number of duplicates
                            numberOfDuplicateAcks++;
                        }
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the segment is a fin segment
                        if (segment.GetFinFlag())
                        {
                            // Make an ackSegment.
                            Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, 0,
                                segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                            //Send the segment to the server
                            dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(segment, destAddr, destPort,
                                0, 0);

                            recievedFin = true;

                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            continue;
                        }

                        //If the data in the ack segment is in order
                        if (AckSegment.GetSequenceNumber() == startingServerSequenceNumber + numberOfRecievedBytes + 2)
                        {
                            //Extract the data from the segment and add it to the recieve window
                            for (int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvNextPosition] = AckSegment.GetData()[i];

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            //If out of order segments have arrived before
                            if (recvNextPosition < recvTempNextPosition)
                            {
                                nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                                recvNextPosition == recvTempNextPosition;
                            }

                            //No out of order segments
                            else
                            {
                                nextSendAckNumber += AckSegment.GetDataLength();
                            }

                            numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                            currentClientRecvWindowSize -= AckSegment.GetDataLength();
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (AckSegment.GetSequenceNumber() >= startingServerSequenceNumber + numberOfRecievedBytes + 2)
                        {
                            recvTempNextPosition = recvNextPosition + AckSegment.GetSequenceNumber() - nextSendAckNumber;

                            //Extract the data from the segment and add it to the recieve window
                            for (int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = AckSegment.GetData()[i];

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                            currentClientRecvWindowSize -= AckSegment.GetDataLength();
                        }

                        //If the recieve buffer is full
                        if (currentClientRecvWindowSize == 0)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            //Reset the recieve window
                            currentClientRecvWindowSize = numberOfBytesInRecieveWindow;
                            numberOfBytesInRecieveWindow = 0;
                            recvTempNextPosition = 0;
                            recvBasePosition = 0;
                            recvNextPosition = 0;
                        }
                    }
                }

                //If a fin segment hasn't been recieved from the server
                if (!recievedFin)
                {
                    //Send an ack with the next data packet
                    sendAck = true;
                }

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }
        }

        // Get the time at the end of the transfer.
        finishTransfer = std::chrono::high_resolution_clock::now();

        inputFile.close();

        int finSequenceNumber = numberOfAckedBytes + 1;

        bool finAcked == false;

        //Make fin segment
        Segment<MAX_EMPTY_SEGMENT_LEN> finSegment(dataPort_, destPort, finSequenceNumber,
            0, false, false, false, false, false, true, currentClientRecvWindowSize, 0, 0);

        //Send the fin segment to the server
        dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(finSegment, destAddr,
            destPort, bitErrorPercent, segmentLoss);

        //Set the starting value of the timer
        startTimer = std::chrono::high_resolution_clock::now();

        //Loop until the reciever completes it's data transfer and sends it's fin segment
        while (true)
        {
            //A packet has arrived
            if (dataSocket_.CheckReceive())
            {
                // Make a segment.
                Segment<MAX_FULL_SEGMENT_LEN> segment;

                // Recieve the segment from the server
                dataSocket_.Receive<MAX_FULL_SEGMENT_LEN>(segment);

                //If the segment is not corrupt
                if (segment.CalculateChecksum() == 0xff)
                {
                    //If the segment has an ack for the fin segment
                    if (segment.GetAckFlag() && segment.GetAckNumber() == finSequenceNumber + 1)
                    {
                        finAcked = true;
                        continue;
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the segment is a fin segment
                        if (segment.GetFinFlag())
                        {
                            // Make an ackSegment.
                            Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, 0,
                                segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                            //Send the segment to the server
                            dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(segment, destAddr, destPort,
                                0, 0);


                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            break;
                        }

                        //If the data in the ack segment is in order
                        if (segment.GetSequenceNumber() == startingServerSequenceNumber + numberOfRecievedBytes + 2)
                        {
                            //Extract the data from the segment and add it to the recieve window
                            for (int i = 0; i < segment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvNextPosition] = segment.GetData()[i];

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            //If out of order segments have arrived before
                            if (recvNextPosition < recvTempNextPosition)
                            {
                                nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                                recvNextPosition == recvTempNextPosition;
                            }

                            //No out of order segments
                            else
                            {
                                nextSendAckNumber += segment.GetDataLength();
                            }

                            numberOfBytesInRecieveWindow += segment.GetDataLength();
                            currentClientRecvWindowSize -= segment.GetDataLength();
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (segment.GetSequenceNumber() >= startingServerSequenceNumber + numberOfRecievedBytes + 2)
                        {
                            recvTempNextPosition = recvNextPosition + segment.GetSequenceNumber() - nextSendAckNumber;

                            //Extract the data from the segment and add it to the recieve window
                            for (int i = 0; i < segment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = segment.GetData()[i];

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            numberOfBytesInRecieveWindow += segment.GetDataLength();
                            currentClientRecvWindowSize -= segment.GetDataLength();
                        }

                        //If the recieve buffer is full
                        if (currentClientRecvWindowSize == 0)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            //Reset the recieve window
                            currentClientRecvWindowSize = numberOfBytesInRecieveWindow;
                            numberOfBytesInRecieveWindow = 0;
                            recvTempNextPosition = 0;
                            recvBasePosition = 0;
                            recvNextPosition = 0;
                        }
                    }

                    else
                    {
                        break;
                    }
                }

                if (!recievedFin)
                {
                    // Make an ackSegment.
                    Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, 0,
                        nextSendAckNumber, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                    //Send the segment to the server
                    dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(ackSegment, destAddr, destPort,
                        sendBitErrorPercent, sendsegmentLoss);
                }                

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }
            //If the fin segment wasn't acked
            if (!finAcked)
            {
                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                //If the timeout occurred
                if (timermiliSeconds.count() >= TimoutInterval_)
                {
                    //Recalculate the estimated RTT value 
                    EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                    //Recalculate the RTT deviation value
                    DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                    //Recalculate the Timeout value
                    TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                    //Resend the fin segment
                    connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(finSegment, destAddr,
                        destPort, bitErrorPercent, segmentLoss);

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
    int currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    int startingServerSequenceNumber = 0;

    int maxServerSendWindowSize;

    int currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

    int currentClientRecvWindowSize;

    int startingClientSequenceNumber;

    int duplicateAckSequenceNumber;

    int numberOfDuplicateAcks = 0;

    int numberOfBytesInRecieveWindow = 0;

    int nextSendAckNumber = 0;

    int numberOfSegmentsInWindow = 0;

    int numberOfAckedBytes = 0;

    int numberOfUnackedBytes = 0;

    int numberOfRecievedBytes = 0;

    int tempSendWindowSize = 0;

    int clientPort = 0;

    bool finAcked = false;

    std::streamoff fileSize = 0;

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();

    //The next position in the send ring buffer to add a byte to
    int sendNextPosition = 0;

    //The position in the send buffer to remove a byte from
    int sendBasePosition = 0;

    //The next position in the send ring buffer to add a byte to
    int sendSegmentNextPosition = 0;

    //The position in the send buffer to remove a byte from
    int sendSegmentBasePosition = 0;

    //The next position in the recv ring buffer to add a byte to
    int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    int recvTempNextPosition = 0;

    //The position in the recv buffer to remove a byte from
    int recvBasePosition = 0;

    bool recievedAck = false;

    //Connection setup section

    //Bind the connection socket to it's address and port
    connSocket_.Bind();

    //Make a Sync packet that will be recieved from the connecting client
    Segment<MAX_EMPTY_SEGMENT_LEN> SynSegment;

    //Wait for a client to connect
    connSocket_.Receive<MAX_EMPTY_SEGMENT_LEN>(SynSegment);

    //If the sync packet isn't corrupt and it's syn flag is set
    if (SynSegment.CalculateChecksum() == 0xff && SynSegment.GetSyncFlag())
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

        //Resize the segment length window
        sendWindowSegmentLength_.resize(1);

        // Random number engine and distribution
        // Distribution in range [1, 100]
        std::random_device dev;
        std::mt19937 rng(dev());

        using distType = std::mt19937::result_type;
        std::uniform_int_distribution<distType> uniformDist(1, 100);

        startingServerSequenceNumber = uniformDist(rng);

        nextSendAckNumber = startingClientSequenceNumber + 2;

        //Make a data port for this client
        dataPort_ = connPort_ + clientNumber_;

        //Make a Sync Ack packet that will be sent back to the client with the port number of the data socket
        Segment<MAX_EMPTY_SEGMENT_LEN> SynAckSegment(dataPort_, clientPort, startingServerSequenceNumber,
        startingClientSequenceNumber+1, false, true, false, false, true, false, currentServerRecvWindowSize, 0, 0);

        //Send the Sync Ack packet
        connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(SynAckSegment, connSocket_.GetRemoteAddress(),
            SynSegment.GetSourcePortNumber(), bitErrorPercent, segmentLoss);

        clientNumber_++;

        //Set the starting value of the timer
        startTimer = std::chrono::high_resolution_clock::now();

        while (true)
        {
            //If a packet arrived
            if (connSocket_.CheckReceive())
            {
                //Make a Ack packet that will be recieved from the connecting client
                Segment<MAX_EMPTY_SEGMENT_LEN> AckSegment;

                //Recieve the ack packet from the client
                connSocket_.Receive<MAX_EMPTY_SEGMENT_LEN>(AckSegment);
                
                //If the ack packet isn't corrupt, has the correct flags set, and has the correct ack number
                if (AckSegment.CalculateChecksum() == 0xff && AckSegment.GetSyncFlag() && AckSegment.GetAckFlag() && AckSegment.GetAckNumber() == startingServerSequenceNumber + 1)
                {
                    //Connection established, break out of the loop
                    break;
                }
            }

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //If the timeout occurred
            if (timermiliSeconds.count() >= TimoutInterval_)
            {                
                //Recalculate the estimated RTT value 
                EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                //Recalculate the RTT deviation value
                DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                //Recalculate the Timeout value
                TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                //Resend the Sync Ack packet without bit errors or loss
                connSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(SynAckSegment, connSocket_.GetRemoteAddress(),
                    SynSegment.GetSourcePortNumber(), bitErrorPercent, segmentLoss);

                //Restart the timer
                startTimer = std::chrono::high_resolution_clock::now();
            }
        }
    }

    //Data transfer section

    //Set up a new socket the uses the dataPort for this client.
    dataSocket_(dataPort_);

    //Bind the data socket to it's address and port
    dataSocket_.Bind();

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
            Segment<MAX_FULL_SEGMENT_LEN> segment;

            // Recieve the segment from the client
            dataSocket_.Receive<MAX_FULL_SEGMENT_LEN>(segment);

            //If the segment is not corrupt
            if (segment.CalculateChecksum() == 0xff)
            {
                //If the segment is an ack segment
                if (segment.GetAckFlag())
                {
                    recievedAck = true;

                    //If the ack isn't a duplicate
                    if (segment.GetAckNumber() != duplicateAckSequenceNumber)
                    {
                        //If the ack is for the fin segment
                        if (numberOfAckedBytes >= fileSize)
                        {
                            finAcked = true;
                        }

                        else 
                        {
                            //Get the length of the acked segment
                            int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                            //Increase the number of acked bytes
                            numberOfAckedBytes += segmentLength;

                            //Decrease the number of unacked bytes in the window
                            numberOfUnackedBytes -= segmentLength;

                            //Decrease the number of segments in the window
                            numberOfSegmentsInWindow--;

                            //Update the duplicate number
                            duplicateAckSequenceNumber = segment.GetAckNumber();
                            //Reset the number of duplicates
                            numberOfDuplicateAcks = 0;
                        }
                    }
                    else
                    {
                        //Increase the number of duplicates
                        numberOfDuplicateAcks++;
                    }
                }

                //If the segment is a fin segment
                if (segment.GetFinFlag())
                {
                    // Make an segment.
                    Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, 0,
                        segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Send the segment to the client
                    dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(segment, destAddr, destPort,
                        0, 0);

                    recievedFin = true;

                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                    break;
                }

                //If the data in the segment is in order
                if (segment.GetSequenceNumber() == startingClientSequenceNumber + numberOfRecievedBytes + 2)
                {
                    //Extract the data from the segment and add it to the recieve window
                    for (int i = 0; i < segment.GetDataLength(); i++)
                    {
                        //Add a byte of data to the recieve window
                        recvWindow_[recvNextPosition] = segment.GetData()[i];

                        //Move the recieve window write position
                        recvNextPosition++;
                    }

                    //If out of order segments have arrived before
                    if (recvNextPosition < recvTempNextPosition)
                    {
                        nextSendAckNumber += recvTempNextPosition - recvNextPosition;
                        recvNextPosition == recvTempNextPosition;
                    }

                    //No out of order segments
                    else
                    {
                        nextSendAckNumber += segment.GetDataLength();
                    }

                    numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                    currentClientRecvWindowSize -= AckSegment.GetDataLength();
                }

                //If the data in the segment is out of order, but not data that's already been recieved
                else if (segment.GetSequenceNumber() >= startingClientSequenceNumber + numberOfRecievedBytes + 2)
                {
                    recvTempNextPosition = recvNextPosition + AckSegment.GetSequenceNumber() - nextSendAckNumber;

                    //Extract the data from the segment and add it to the recieve window
                    for (int i = 0; i < AckSegment.GetDataLength(); i++)
                    {
                        //Add a byte of data to the recieve window
                        recvWindow_[recvTempNextPosition] = AckSegment.GetData()[i];

                        //Move the recieve window
                        recvTempNextPosition++;
                    }

                    numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                    currentClientRecvWindowSize -= AckSegment.GetDataLength();
                }

                //If the recieve buffer is full
                if (currentServerRecvWindowSize == 0)
                {
                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                    //Reset the recieve window
                    currentServerRecvWindowSize = numberOfBytesInRecieveWindow;
                    numberOfBytesInRecieveWindow = 0;
                    recvTempNextPosition = 0;
                    recvBasePosition = 0;
                    recvNextPosition = 0;
                }

            }

            //If the transfer of the input file isn't complete, and we got an ack
            if (numberOfAckedBytes < fileSize && recievedAck == true)
            {
                //If the recieved ack wasn't a duplicate
                if (numberOfDuplicateAcks == 0)
                {
                    //The length of the segment that will be sent
                    int segmentLength = segment.GetReceiveWindow();

                    if (segmentLength < MAX_FULL_SEGMENT_LEN)
                    {
                        segmentLength = MAX_FULL_SEGMENT_LEN;
                    }

                    // Make a segment.
                    Segment<segmentLength> ackSegment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
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
                            sendWindow_[sendNextPosition] = byte;
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
                            sendWindow_[sendNextPosition] = byte;
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
                    dataSocket_.Send<segmentLength>(segment, dataSocket_.GetRemoteAddress(), clientPort,
                        sendBitErrorPercent, sendsegmentLoss);

                    //Add the length of the segment to the second send buffer
                    sendWindowSegmentLength_[sendSegmentNextPosition] = segmentLength;
                }

                else
                {
                    //Populate the ack segment with old data

                    //Make the temp position start at the base position
                    int tempPosition = sendBasePosition;

                    int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                    //Remake the segment
                    Segment<segmentLength> resendSegment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Populate the segment with all of it's bytes
                    for (int i = 0; i < segmentLength; i++)
                    {
                        //Get the byte from the send window
                        byte = sendWindow_[tempPosition];

                        //Add it to the segment
                        resendSegment.AddByte(byte);
                            
                        tempPosition++;
                    }
                }

                //Send the segment
                dataSocket_.Send<segmentLength>(resendSegment, dataSocket_.GetRemoteAddress(), clientPort,
                    sendBitErrorPercent, sendsegmentLoss);
            }

            else
            {
                //If the file transfer is complete and the fin segment hasn't acked
                if (numberOfAckedBytes >= fileSize && !finAcked)
                {
                    //Make a fin ack segment
                    Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                        nextSendAckNumber, false, true, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                    //Send the fin ack segment to the client
                    dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(ackSegment, dataSocket_.GetRemoteAddress(), clientPort,
                        sendBitErrorPercent, sendsegmentLoss);
                }

                else
                {
                    //Make an empty ack segment
                    Segment<MAX_EMPTY_SEGMENT_LEN> ackSegment(dataPort_, destPort, 0,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Send the ack packet to the client
                    dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(ackSegment, dataSocket_.GetRemoteAddress(), clientPort,
                        sendBitErrorPercent, sendsegmentLoss);
                }
            }

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            //Recalculate the estimated RTT value 
            EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

            //Recalculate the RTT deviation value
            DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

            //Recalculate the Timeout value
            TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

            //Mark the start time of the timer
            startTimer = std::chrono::high_resolution_clock::now();
        }

        outputFile.close();
        
        if (!finAcked)
        {
            while (true)
            {
                //If the input file hasn't been fully transferred
                if (numberOfAckedBytes < fileSize)
                {
                    //The ring buffer isn't full
                    if ((numberOfUnackedBytes < currentServerSendWindowSize))
                    {
                        //The length of the segment that will be sent
                        int segmentLength;

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
                        }

                        // Make a segment.
                        Segment<segmentLength> segment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                            0, false, false, false, false, false, false, currentServerRecvWindowSize, 0, 0);

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
                                sendWindow_[sendNextPosition] = byte;
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
                                sendWindow_[sendNextPosition] = byte;
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
                                //Reset it back to the beginning
                                sendNextPosition = 0;
                            }
                            else
                            {
                                sendNextPosition++;
                            }
                        }
                        //Send the segment
                        dataSocket_.Send<segmentLength>(segment, dataSocket_.GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                        //If the buffer is empty
                        if (sendNextPosition == sendBasePosition)
                        {
                            //Mark the start time of the timer
                            startTimer = std::chrono::high_resolution_clock::now();
                        }

                        //Add the length of the segment to the second send buffer
                        sendWindowSegmentLength_[sendSegmentNextPosition] = segmentLength;

                        //If we are at, or past, the end of the ring buffer
                        if (sendSegmentNextPosition >= numberOfSegmentsInWindow - 1)
                        {
                            //Reset it back to the beginning
                            sendSegmentNextPosition = 0;
                        }
                        else
                        {
                            sendSegmentNextPosition++;
                        }

                        //Increase the number of segments in flight
                        numberOfSegmentsInWindow++;
                    }

                    // Get the current timer value in milliseconds
                    currentTimer = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<float, std::milli> timermiliSeconds =
                        currentTimer - startTimer;

                    //If a timeout has occured, or three duplicate acks arrived
                    if (timermiliSeconds.count() >= TimoutInterval_ || numberOfDuplicateAcks >= 3)
                    {
                        //Make the temp position start at the base position
                        int tempPosition = sendBasePosition;

                        int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                        //Make a temporary buffer to hold the segment's data
                        std::vector<char> tempbuffer;
                        tempbuffer.resize(segmentLength);

                        //Remake the segment
                        Segment<segmentLength> resendSegment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                            0, false, false, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                        //Populate the segment with all of it's bytes
                        for (int i = 0; i < segmentLength; i++)
                        {
                            //Get the byte from the send window
                            byte = sendWindow_[tempPosition];

                            //Add it to the temp buffer
                            tempbuffer[i] = byte;

                            //Add it to the segment
                            resendSegment.AddByte(byte);

                            //If we are at, or past, the end of the ring buffer
                            if (tempPosition >= currentServerSendWindowSize - 1)
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
                            TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;
                        }

                        else
                        {
                            numberOfDuplicateAcks = 0;
                        }

                        //Move the segment length back to the beginning of the ring buffer
                        sendWindowSegmentLength_[0] = sendWindowSegmentLength_[sendSegmentBasePosition];

                        //The segment length base pointer gets reset back to the beginning of the ring buffer
                        sendSegmentBasePosition = 0;

                        //The segment length next pointer points to just after the base pointer
                        sendSegmentNextPosition = 1;

                        //The segment length buffer gets resized
                        numberOfSegmentsInWindow = 1;
                        sendWindowSegmentLength_.resize(numberOfSegmentsInWindow);

                        //Move the bytes from the tempbuffer to the beginning of the ring buffer
                        for (int i = 0; i < segmentLength; i++)
                        {
                            sendWindow_[i] = tempbuffer[i];
                        }

                        //The slow start threashhold becomes half of the current client window size
                        ssthresh = currentServerSendWindowSize / 2;

                        //The server window size gets reset back to one full segment.
                        currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

                        //This segment is the only set of unacked bytes
                        numberOfUnackedBytes = currentServerSendWindowSize;

                        //Move the base pointer to the beginning of the ring buffer
                        sendBasePosition = 0;

                        //Move next pointer to just after the the end of the ring buffer, 
                        //which is the beginning
                        sendNextPosition = 0;

                        //Resend the segment
                        dataSocket_.Send<segmentLength>(resendSegment, dataSocket_.GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                        //Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }

                    //A packet has arrived
                    if (dataSocket_.CheckReceive())
                    {
                        // Make a segment.
                        Segment<MAX_FULL_SEGMENT_LEN> AckSegment;

                        // Recieve the segment from the server
                        dataSocket_.Receive<MAX_FULL_SEGMENT_LEN>(AckSegment);

                        //If the segment is not corrupt
                        if (AckSegment.CalculateChecksum() == 0xff)
                        {
                            currentClientRecvWindowSize = AckSegment.GetReceiveWindow();                        

                            //If the segment is an ack segment
                            if (AckSegment.GetAckFlag())
                            {
                                //If the ack isn't a duplicate
                                if (AckSegment.GetAckNumber() != duplicateAckSequenceNumber)
                                {
                                    //Get the length of the acked segment
                                    int segmentLength = sendWindowSegmentLength_[sendSegmentBasePosition];

                                    //Increase the number of acked bytes
                                    numberOfAckedBytes += segmentLength;

                                    //Decrease the number of unacked bytes in the window
                                    numberOfUnackedBytes -= segmentLength;

                                    //Decrease the number of segments in the window
                                    numberOfSegmentsInWindow--;

                                    //Move the base position

                                    //If the move would go beyond the end of the ring buffer
                                    if (sendBasePosition + segmentLength > currentServerSendWindowSize - 1)
                                    {
                                        sendBasePosition = currentServerSendWindowSize - (1 + sendBasePosition + segmentLength);
                                    }

                                    //Normal move
                                    else
                                    {
                                        sendBasePosition += segmentLength;
                                    }

                                    //Move the base pointer of the second ring buffer

                                    //If we are at, or past, the end of the ring buffer
                                    if (sendSegmentBasePosition >= numberOfSegmentsInWindow - 1)
                                    {
                                        //Reset it back to the beginning
                                        sendSegmentBasePosition = 0;
                                    }
                                    else
                                    {
                                        sendSegmentBasePosition++;
                                    }

                                    //If the window size is currently smaller than the max window size
                                    if (currentServerSendWindowSize < maxClientSendWindowSize)
                                    {
                                        //If we are in CA mode
                                        if (currentServerSendWindowSize >= ssthresh)
                                        {
                                            tempSendWindowSize += MAX_FULL_SEGMENT_LEN * ((float)MAX_FULL_SEGMENT_LEN / (float)currentServerSendWindowSize);

                                            if (tempSendWindowSize >= MAX_FULL_SEGMENT_LEN)
                                            {
                                                currentServerSendWindowSize += MAX_FULL_SEGMENT_LEN;
                                                tempSendWindowSize = 0;
                                                sendWindowSegmentLength_.push_back(0);
                                            }
                                        }
                                        //Must be in SS mode
                                        else
                                        {
                                            currentServerSendWindowSize += MAX_FULL_SEGMENT_LEN;
                                            sendWindowSegmentLength_.push_back(0);

                                            //If we've gone from SS to CA mode
                                            if (currentServerSendWindowSize > ssthresh)
                                            {
                                                currentServerSendWindowSize = ssthresh;
                                            }
                                        }

                                        //If the new client window size is greater than the max set by the reciever
                                        if (currentServerSendWindowSize > maxServerSendWindowSize)
                                        {
                                            currentServerSendWindowSize = maxServerSendWindowSize;
                                        }
                                    }

                                    //Update the duplicate number
                                    duplicateAckSequenceNumber = AckSegment.GetAckNumber();
                                    //Reset the number of duplicates
                                    numberOfDuplicateAcks = 0;
                                }
                                else
                                {
                                    //Increase the number of duplicates
                                    numberOfDuplicateAcks++;
                                }
                            }
                        }

                        //If the file transfer is complete
                        if (numberOfAckedBytes > fileSize)
                        {
                            //Make a fin segment
                            Segment<MAX_EMPTY_SEGMENT_LEN> finSegment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                                0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                            //Send the fin segment to the client
                            dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(finSegment, dataSocket_.GetRemoteAddress(), clientPort,
                                sendBitErrorPercent, sendsegmentLoss);
                        }

                        std::chrono::duration<float, std::milli> timermiliSeconds =
                            currentTimer - startTimer;

                        //Recalculate the estimated RTT value 
                        EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                        //Recalculate the RTT deviation value
                        DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                        //Recalculate the Timeout value
                        TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                        //Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
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
                    if (timermiliSeconds.count() >= TimoutInterval_)
                    {
                        //Make a fin segment
                        Segment<MAX_EMPTY_SEGMENT_LEN> finSegment(dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + 2,
                            0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                        //Send the fin segment to the client
                        dataSocket_.Send<MAX_EMPTY_SEGMENT_LEN>(finSegment, dataSocket_.GetRemoteAddress(), clientPort,
                            sendBitErrorPercent, sendsegmentLoss);

                        //Recalculate the estimated RTT value 
                        EstimatedRTT_ = (1 - ALPHA)*EstimatedRTT_ + ALPHA * timermiliSeconds.count();

                        //Recalculate the RTT deviation value
                        DevRTT_ = (1 - BETA)*DevRTT_ + BETA * fabs(timermiliSeconds.count() - EstimatedRTT_);

                        //Recalculate the Timeout value
                        TimoutInterval_ = EstimatedRTT_ + 4 * DevRTT_;

                        //Set the starting value of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }
                    
                    //A packet has arrived
                    if (dataSocket_.CheckReceive())
                    {
                        // Make a segment.
                        Segment<MAX_FULL_SEGMENT_LEN> segment;

                        // Recieve the segment from the client
                        dataSocket_.Receive<MAX_FULL_SEGMENT_LEN>(segment);

                        //If the segment is not corrupt
                        if (segment.CalculateChecksum() == 0xff)
                        {
                            //If the segment has an ack for the fin segment
                            if (segment.GetAckFlag() && segment.GetAckNumber() == startingServerSequenceNumber + numberOfUnackedBytes + 3)
                            {
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