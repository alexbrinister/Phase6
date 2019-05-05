/**
* \file UdpServerTCP.cpp
* \details TCP server class - definitions
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
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
    
    //The current number of bytes that the client can recieve before it's recieve window becomes full
    unsigned int currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    //The starting sequence number of the client
    uint32_t startingClientSequenceNumber = 0;

    //The maximum size that the client's send window can be
    unsigned int maxClientSendWindowSize;

    //The current size of the client's send window
    unsigned int currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

    //The advertized size of the server's recieve window
    unsigned int currentServerRecvWindowSize;

    //The starting sequence number of the server
    uint32_t startingServerSequenceNumber;

    //The ack number of the last, in order, ack recieved
    uint32_t duplicateAckSequenceNumber;

    //The number of duplicate acks recieved
    int numberOfDuplicateAcks = 0;

    //The current number of bytes in the client's recieve window
    uint32_t numberOfBytesInRecieveWindow = 0;

    //The next number of the next that will be send ack
    uint32_t nextSendAckNumber = 0;

    //If the next data packet will contain an ack
    bool sendAck = true;

    //The total number of bytes that the server has acked
    uint32_t numberOfAckedBytes = 0;

    //The current number of un-acked bytes in the client's recieve window
    uint32_t numberOfUnackedBytes = 0;

    //A timorary size for increasing the size of the client's recieve window in CA mode
    uint32_t tempSendWindowSize = 0;

    //If we have recieved a fin from the server or not
    bool recievedFin = false;

    //The size of the input file in bytes
    std::streamoff fileSize = 0;
    
    //Make all bytes in the recieve window invalid
    for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
    {
        recvWindowVaild_[i] = false;
    }

    //The start time of the timer for segments not put in the send window, like the fin and syn segments
    auto startTimer = std::chrono::high_resolution_clock::now();
    
    //The current time for the timer
    auto currentTimer = std::chrono::high_resolution_clock::now();
    
    //The start time of the transfer
    auto startTransfer = std::chrono::high_resolution_clock::now();
    
    //The finish time for the transfer
    auto finishTransfer = std::chrono::high_resolution_clock::now();

    //The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    unsigned int recvTempNextPosition = 0;

    //Connection set up section

    //Bind the connection socket to it's address and port
    connSocket_->Bind();

    // Random number engine and distribution
        // Distribution in range [1, 100]
    std::random_device dev;
    std::mt19937 rng(dev());

    using distType = std::mt19937::result_type;
    std::uniform_int_distribution<distType> uniformDist(1, 100);

    //Set the starting sequence number of the client to a random number between 1 and 100
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
                //Make the max send window the same size of the recieve window of the server
                maxClientSendWindowSize = SynAckSegment.GetReceiveWindow();
                currentServerRecvWindowSize = maxClientSendWindowSize;

                //Store the starting sequence number of the server
                startingServerSequenceNumber = SynAckSegment.GetSequenceNumber();

                //The next ack sent by the client will have an ack number that is one past the starting sequence number of the server
                nextSendAckNumber = startingServerSequenceNumber + 1;

                //Store the ack number as the duplicate
                duplicateAckSequenceNumber = SynAckSegment.GetAckNumber();

                //Make an ack segment to send back to the server
                Segment AckSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort, 0,
                    nextSendAckNumber, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                //Send the ack segmet to the server
                connSocket_->Send(AckSegment, destAddr,
                    destPort, bitErrorPercent, segmentLoss);

                printf("Sending ack segment\n");
                printf("Ack number %d\n", AckSegment.GetAckNumber());
                printf("%d bytes long\n\n", AckSegment.GetDataLength());

                //Make the data port the same as the connection port
                dataPort_ = connPort_;

                //Store the old port number just in case we need to resend the ack for the syn ack segment
                connPort_ = destPort;

                //Switch over to using the new port that the server sent us for the data transfer
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

    //Delete the old socket.
    delete connSocket_;

    //Make the old socket null
    connSocket_ = NULL;

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

        // Open a file to input from in binary mode with the stream at the end of the file.
        std::ifstream inputFile(sendFileName,
            std::ios::binary | std::ios::ate);

        // Find out how many bytes are in the file.
        fileSize = inputFile.tellg();

        //Reset the file stream back to the begining of the file
        inputFile.seekg(0);

        // Open a file to output to
        std::ofstream outputFile(recieveFileName, std::ios::binary);

        //Stores an overflow byte from a previous segment
        char byte = 0;
        //Indicates if there is an overflow byte from a previous segment
        bool queuedByte = false;

        // Get the time at the start of the transfer.
        startTransfer = std::chrono::high_resolution_clock::now();

        // Loop until all segments are sent and acked
        while (numberOfAckedBytes < (unsigned int)fileSize)
        {
            //The ring buffer isn't full and the last segment hasn't been sent, and the server can handle more bytes.
            if ((numberOfUnackedBytes < currentClientSendWindowSize)
                    && (numberOfUnackedBytes < (unsigned int)fileSize) 
                    && numberOfUnackedBytes < currentServerRecvWindowSize)
            {
                printf("Timeout time: %f\n\n", TimeoutInterval_);

                //The length of the segment that will be sent
                uint16_t segmentLength;

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
                if (segmentLength + numberOfUnackedBytes > currentServerRecvWindowSize)
                {
                    //Make it fit
                    segmentLength = currentServerRecvWindowSize - numberOfUnackedBytes;
                }

                //If there is even a segment to send
                if (segmentLength > 0)
                {
                    //Get the sequence number of the segment that will be sent
                    unsigned int sequenceNumber = startingClientSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make the segment.
                    Segment segment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, destPort, sequenceNumber,
                        nextSendAckNumber, false, sendAck, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                    //Mark the time that this segment was sent
                    startTimer = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<float, std::milli> currentTime =
                        startTransfer - startTimer;

                    //Increase the number of unacked bytes in the send window by the length of the segment
                    numberOfUnackedBytes += segmentLength;

                    //Add bytes to the segment until it's full, or we reach EOF
                    for (;;)
                    {
                        // We have to make sure we have put every byte into a segment
                        // Check if we have a left-over byte... this means we are in
                        // a new segment
                        if (queuedByte)
                        {
                            segment.AddByte(byte);
                            queuedByte = false;
                            //Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber, false, sendAck, false, false, false, false, 0, segmentLength, 0, currentTime.count() });
                        }

                        // Get a byte and try to put it into the packet.
                        // If after this we have a queuedByte, this means we go to
                        // another packet.
                        if (inputFile.get(byte))
                        {
                            queuedByte = !segment.AddByte(byte);

                            //The segment is already full, leave the loop
                            if (queuedByte)
                            {
                                break;
                            }

                            //Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber, false, sendAck, false, false, false, false, 0, segmentLength, 0, currentTime.count() });
                        }

                        // If we can't get a byte, that means we got EOF; leave the
                        // loop.
                        else
                        {
                            break;
                        }
                    }

                    //The next segment won't have an ack, unless a segment arrives from the server
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

            //If the send window isn't empty
            if (!sendWindow_.empty())
            {
                // Get the current time value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> currentTime =
                    currentTimer - startTransfer ;

                printf("Current time: %f\n\n", currentTime.count() - sendWindow_.begin()->timeSent);

                //If a timeout has occured, or three duplicate acks arrived
                if (currentTime.count() - sendWindow_.begin()->timeSent >= TimeoutInterval_ || numberOfDuplicateAcks >= 3)
                {
                    printf("Timeout time: %f\n\n", TimeoutInterval_);
                    std::cout << "Send Window Number of Bytes: " << sendWindow_.size() << "\n";

                    //Make a list iterator that starts at the begining of the send buffer
                    std::list<sendWindowByte>::iterator it = sendWindow_.begin();

                    //Get the length of the segment that will be re-sent
                    uint16_t segmentLength = it->dataLength;

                    //Remake the segment
                    Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, destPort, it->sequenceNumber,
                        nextSendAckNumber, it->urg, it->ack, it->psh,
                        it->rst, it->syn, it->fin, currentClientRecvWindowSize,
                        it->urgDataPointer, it->options);

                    //Populate the segment with all of it's bytes
                    for (unsigned int i = 0; i < segmentLength; i++)
                    {
                        //Get and add the byte it to the segment
                        resendSegment.AddByte(it->byte);

                        //Update the send time of the segment
                        it->timeSent = currentTime.count();

                        //Move the iterator up
                        it++;
                    }

                    //If three duplicate acks arrived, reset the number back to zero
                    if (numberOfDuplicateAcks > 3)
                    {
                        numberOfDuplicateAcks = 0;
                    }

                    //The slow start threashhold becomes half of the current client window size
                    ssthresh = currentClientSendWindowSize / 2;

                    printf("ssthresh: %d\n", ssthresh);

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
                }
            }

            //A segment has arrived
            if (dataSocket_->CheckReceive())
            {
                printf("Timeout time: %f\n\n", TimeoutInterval_);

                // Get the current time value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> currentTime =
                    currentTimer - startTransfer ;

                //Get the a sample RTT for the oldest unacked segment
                float_t RTTSample = currentTime.count() - sendWindow_.begin()->timeSent;

                // Make a segment.
                Segment AckSegment(MAX_FULL_SEGMENT_LEN);

                // Recieve the segment from the server
                dataSocket_->Receive(AckSegment);

                printf("Recieved segment\n");
                printf("Segment number %d\n", AckSegment.GetSequenceNumber());
                printf("Ack number %d\n", AckSegment.GetAckNumber());
                printf("%d bytes long\n", AckSegment.GetDataLength());

                //If a fin segment hasn't been recieved from the server
                if (!recievedFin)
                {
                    //Send an ack in the next data packet
                    sendAck = true;
                }

                //If the segment is not corrupt
                if (AckSegment.CalculateChecksum(0) == 0x0000)
                {
                    //Store the advertized recieve window of the server 
                    currentServerRecvWindowSize = AckSegment.GetReceiveWindow();

                    //If it's the synack from before
                    if (AckSegment.GetSyncFlag() && AckSegment.GetAckFlag() && AckSegment.GetAckNumber() == startingClientSequenceNumber + 1)
                    {
                        printf("It's a syn ack segment\n");

                        //Make an ack segment to send back to the server
                        Segment AckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, connPort_, 0,
                            nextSendAckNumber, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                        //Send the ack segmet to the server
                        dataSocket_->Send(AckSegment, destAddr,
                            connPort_, bitErrorPercent, segmentLoss);

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

                            //Find out how many bytes were acked by this ack segment
                            unsigned int tempNumberOfAckedBytes = AckSegment.GetAckNumber() - duplicateAckSequenceNumber;

                            printf("%d bytes acked\n", tempNumberOfAckedBytes);

                            //Increase the total number of acked bytes
                            numberOfAckedBytes += tempNumberOfAckedBytes;

                            //Decrease the number of unacked bytes in the send window
                            numberOfUnackedBytes -= tempNumberOfAckedBytes;

                            printf("%d total bytes acked\n", numberOfAckedBytes);

                            printf("%d Un-acked bytes\n", numberOfUnackedBytes);

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

                            printf("currentClientSendWindowSize: %d\n", currentClientSendWindowSize);

                            //Pop the acked bytes from the front of the list
                            for (unsigned int i = 0; i < tempNumberOfAckedBytes && !sendWindow_.empty(); i++)
                            {
                                sendWindow_.pop_front();
                            }

                            //Update the duplicate ack number
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

                    //If the segment is a fin segment
                    if (AckSegment.GetFinFlag() && !recievedFin)
                    {
                        printf("It's a fin segment\n\n");
                        //Send an ack in the next data packet
                        nextSendAckNumber = AckSegment.GetSequenceNumber() + 1;

                        //Indicated that a fin was received from the server
                        recievedFin = true;

                        //Write the data in the recieve window to the file
                        outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the data in the ack segment is in order
                        if (AckSegment.GetSequenceNumber() == nextSendAckNumber)
                        {
                            printf("It's data is in order\n");
                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvNextPosition] = AckSegment.GetData()[i];

                                //Mark the byte as valid
                                recvWindowVaild_[recvNextPosition] = true;

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            nextSendAckNumber += AckSegment.GetDataLength();
                            numberOfBytesInRecieveWindow += AckSegment.GetDataLength();

                            //If all holes have been closed
                            if (recvNextPosition > recvTempNextPosition)
                            {
                                //Decrease the advertised number of free bytes in the client's recieve window
                                currentClientRecvWindowSize -= AckSegment.GetDataLength();
                            }

                            //If the byte after the byte that was just writing is valid, that means a hole was just closed
                            //itterate until an invalid byte is found
                            while (recvWindowVaild_[recvNextPosition] && ((recvNextPosition < recvTempNextPosition) || (recvNextPosition < numberOfBytesInRecieveWindow)))
                            {
                                //Move the next pointer increase the next ack number that will be sent
                                recvNextPosition++;
                                nextSendAckNumber++;
                            }
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (AckSegment.GetSequenceNumber() > nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");

                            //Get the starting position in the recieve buffer for the bytes in the segment 
                            recvTempNextPosition = recvNextPosition+(AckSegment.GetSequenceNumber()-nextSendAckNumber);

                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < AckSegment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = AckSegment.GetData()[i];

                                //Mark the byte as valid
                                recvWindowVaild_[recvTempNextPosition] = true;

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            //Only bytes after the ending index of this considered free space, holes are not free space.
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE - recvTempNextPosition;

                            //If loss is ingnored
                            if (ignoreLoss)
                            {
                                //Make the ack next number be for the byte after this segment
                                nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                                //Increase the number of bytes in the recieve window
                                numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                                //Move the next position to the temp position
                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                //Increase the number of bytes in the recieve window
                                numberOfBytesInRecieveWindow += AckSegment.GetDataLength();
                            }
                        }

                        //If the recieve buffer is full and there are no holes
                        if (currentClientRecvWindowSize == 0 && recvNextPosition >= recvTempNextPosition && !recievedFin)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            //Make all bytes in the recieve window invalid
                            for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
                            {
                                recvWindowVaild_[i] = false;
                            }

                            printf("Emptying the recv buffer\n");

                            //Reset the recieve window
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                            numberOfBytesInRecieveWindow = 0;
                            recvTempNextPosition = 0;
                            recvNextPosition = 0;
                        }
                        printf("currentClientRecvWindowSize: %d\n", currentClientRecvWindowSize);
                    }
                }

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = ((1 - ALPHA)*EstimatedRTT_) + (ALPHA * RTTSample);

                //Recalculate the RTT deviation value
                DevRTT_ = ((1 - BETA)*DevRTT_) + (BETA * (fabs(RTTSample - EstimatedRTT_)));

                //Recalculate the Timeout value
                TimeoutInterval_ = EstimatedRTT_ + (4 * DevRTT_);

                printf("\n");
            }
        }

        // Get the time at the end of the transfer.
        finishTransfer = std::chrono::high_resolution_clock::now();

        //Close the input file
        inputFile.close();

        //Store the sequence number of the fin that is going to be sent to the server
        unsigned int finSequenceNumber = duplicateAckSequenceNumber + 1;

        //The fin hasn't been acked yet
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

        //Loop until the reciever completes it's data transfer, sends it's fin segment, and acks the client's fin segment
        while (!recievedFin || !finAcked)
        {
            printf("Timeout time: %f\n\n", TimeoutInterval_);

            //A packet has arrived
            if (dataSocket_->CheckReceive())
            {
                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

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
                        printf("Ack number %d\n", segment.GetAckNumber());

                        //The client's fin has been acked 
                        finAcked = true;
                    }

                    //If a fin segment hasn't been recieved from the server
                    if (!recievedFin)
                    {
                        //If the segment is a fin segment
                        if (segment.GetFinFlag() && !recievedFin)
                        {
                            printf("It's a fin segment\n\n");
                            // Make an ackSegment.
                            Segment ackSegment(MAX_EMPTY_SEGMENT_LEN,dataPort_, destPort, 0,
                                segment.GetSequenceNumber() + 1, false, true, false, false, false, false, currentClientRecvWindowSize, 0, 0);

                            //Send the segment to the server
                            dataSocket_->Send(ackSegment, destAddr, destPort,
                                0, 0);

                            printf("Sending ack segment\n");
                            printf("Ack number %d\n", ackSegment.GetAckNumber());
                            printf("%d bytes long\n\n", ackSegment.GetDataLength());

                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            recievedFin = true;
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

                                recvWindowVaild_[recvNextPosition] = true;

                                //Move the recieve window
                                recvNextPosition++;
                            }

                            nextSendAckNumber += segment.GetDataLength();
                            numberOfBytesInRecieveWindow += segment.GetDataLength();

                            //If all holes have been closed
                            if (recvNextPosition > recvTempNextPosition)
                            {
                                currentClientRecvWindowSize -= segment.GetDataLength();
                            }

                            //If the byte after the byte that was just writing is valid, that means a hole was just closed
                            //itterate until an invalid byte is found
                            while (recvWindowVaild_[recvNextPosition] && ((recvNextPosition < recvTempNextPosition) || (recvNextPosition < numberOfBytesInRecieveWindow)))
                            {
                                recvNextPosition++;
                                nextSendAckNumber++;
                            }
                        }

                        //If the data in the segment is out of order, but not data that's already been recieved
                        else if (segment.GetSequenceNumber() > nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");

                            recvTempNextPosition = recvNextPosition + (segment.GetSequenceNumber() - nextSendAckNumber);

                            //Extract the data from the segment and add it to the recieve window
                            for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                            {
                                //Add a byte of data to the recieve window
                                recvWindow_[recvTempNextPosition] = segment.GetData()[i];

                                recvWindowVaild_[recvTempNextPosition] = true;

                                //Move the recieve window
                                recvTempNextPosition++;
                            }

                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE - recvTempNextPosition;

                            if (ignoreLoss)
                            {
                                nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                                numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                numberOfBytesInRecieveWindow += segment.GetDataLength();
                            }
                        }

                        //If the recieve buffer is full and there are no holes
                        if (currentClientRecvWindowSize == 0 && recvNextPosition >= recvTempNextPosition && !recievedFin)
                        {
                            //Write the data in the recieve window to the file
                            outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                            //Make all bytes in the recieve window invalid
                            for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
                            {
                                recvWindowVaild_[i] = false;
                            }

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

                    printf("\n");
                }

                //If we havn't recieved a fin from the server
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

                //Recalculate the estimated RTT value 
                EstimatedRTT_ = ((1 - ALPHA)*EstimatedRTT_) + (ALPHA * timermiliSeconds.count());

                //Recalculate the RTT deviation value
                DevRTT_ = ((1 - BETA)*DevRTT_) + (BETA * (fabs(timermiliSeconds.count() - EstimatedRTT_)));

                //Recalculate the Timeout value
                TimeoutInterval_ = EstimatedRTT_ + (4 * DevRTT_);

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

        //Close the output file
        outputFile.close();

        //Delete the data socket.
        delete dataSocket_;

        //Make it null
        dataSocket_ = NULL;
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

    std::cout << "Time for the server to transfer the file in milliseconds: ";
    std::cout << miliSeconds.count() << std::endl;
}

void socksahoy::UdpServerTCP::Listen(std::string recieveFileName,
                                     std::string sendFileName,
                                     int bitErrorPercent,
                                     int segmentLoss,
                                     bool ignoreLoss)
{
    printf("\n");

    //The current number of bytes that the server can recieve before it's recieve window becomes full
    unsigned int currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    //The starting sequence number of the server
    unsigned int startingServerSequenceNumber = 0;

    //The max size of the server's send window
    unsigned int maxServerSendWindowSize;

    //The current size of the server's send window
    unsigned int currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

    //The advertized size of the client's recieve window
    unsigned int currentClientRecvWindowSize;

    //The starting sequence number of the client
    unsigned int startingClientSequenceNumber;

    //The ack number of the last, in order, ack recieved
    uint32_t duplicateAckSequenceNumber = 0;

    //The ack number of duplicate acks recieved
    unsigned int numberOfDuplicateAcks = 0;

    //The number of bytes in the server's recieve window
    uint32_t numberOfBytesInRecieveWindow = 0;

    //The number of the next ack segment that will be sent
    uint32_t nextSendAckNumber = 0;

    //The total number of bytes acked
    uint32_t numberOfAckedBytes = 0;

    //The number of un-acked bytes in the server's send window
    uint32_t numberOfUnackedBytes = 0;

    //A timorary size for increasing the size of the server's recieve window in CA mode
    uint32_t tempSendWindowSize = 0;

    //The port number of the connecting client
    unsigned int clientPort = 0;

    //If the fin has been acked
    bool finAcked = false;

    //The size of the input file
    std::streamoff fileSize = 0;

    //Make all bytes in the recieve window invalid
    for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
    {
        recvWindowVaild_[i] = false;
    }

    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = std::chrono::high_resolution_clock::now();
    auto startTransfer = std::chrono::high_resolution_clock::now();
    auto finishTransfer = std::chrono::high_resolution_clock::now();

    //The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    //The temporary next position in the recv ring buffer, for out of order segments
    unsigned int recvTempNextPosition = 0;

    //If an ack has been recieved
    bool recievedAck = true;

    //If a fin segment has been recieved
    bool recievedFin = false;

    //Connection setup section

    //Bind the connection socket to it's address and port
    connSocket_->Bind();

    //Make a Sync packet that will be recieved from the connecting client
    Segment SynSegment(MAX_EMPTY_SEGMENT_LEN);

    while (true)
    {
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

            //Store the advertized size of the client's recieve window
            currentClientRecvWindowSize = maxServerSendWindowSize;

            //Store the port number of the client
            clientPort = SynSegment.GetSourcePortNumber();

            // Random number engine and distribution
            // Distribution in range [1, 100]
            std::random_device dev;
            std::mt19937 rng(dev());

            using distType = std::mt19937::result_type;
            std::uniform_int_distribution<distType> uniformDist(1, 100);

            //Set the starting sequence number of the server to a random number between 1 and 100
            startingServerSequenceNumber = uniformDist(rng);

            //The next ack sent by the server will have an ack number that is one past the starting sequence number of the client
            nextSendAckNumber = startingClientSequenceNumber + 1;

            //Make a data port for this client
            dataPort_ = connPort_ + clientNumber_;

            //Make a Sync Ack packet that will be sent back to the client with the port number of the data socket
            Segment SynAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber,
                nextSendAckNumber, false, true, false, false, true, false, currentServerRecvWindowSize, 0, 0);

            printf("Sending syn ack segment\n");

            //Send the Sync Ack packet
            connSocket_->Send(SynAckSegment, connSocket_->GetRemoteAddress(),
                clientPort, bitErrorPercent, segmentLoss);

            printf("Segment number %d\n", SynAckSegment.GetSequenceNumber());
            printf("Ack number %d\n", SynAckSegment.GetAckNumber());
            printf("%d bytes long\n\n", SynAckSegment.GetDataLength());

            //Increase the number of clients connected
            clientNumber_++;

            //Set the starting value of the timer
            startTimer = std::chrono::high_resolution_clock::now();

            while (true)
            {
                //If a packet arrived
                if (connSocket_->CheckReceive())
                {
                    // Get the current timer value in milliseconds
                    currentTimer = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<float, std::milli> timermiliSeconds =
                        currentTimer - startTimer;

                    //Recalculate the estimated RTT value 
                    EstimatedRTT_ = ((1 - ALPHA)*EstimatedRTT_) + (ALPHA * timermiliSeconds.count());

                    //Recalculate the RTT deviation value
                    DevRTT_ = ((1 - BETA)*DevRTT_) + (BETA * (fabs(timermiliSeconds.count() - EstimatedRTT_)));

                    //Recalculate the Timeout value
                    TimeoutInterval_ = EstimatedRTT_ + (4 * DevRTT_);

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

            //Connection established, break out of the loop
            break;
        }
    }

    //Data transfer section

    //Delete the old socket.
    delete connSocket_;

    //Make it null
    connSocket_ = NULL;

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
        
        uint32_t finSequenceNumber;

        //Loop until we recieve a fin segment from the client
        while (!recievedFin)
        {
            printf("Timeout time: %f\n\n", TimeoutInterval_);
            // Make a segment.
            Segment segment(MAX_FULL_SEGMENT_LEN);

            // Recieve the segment from the client
            dataSocket_->Receive(segment);

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;
            
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
                        if (duplicateAckSequenceNumber == 0)
                        {
                            duplicateAckSequenceNumber = segment.GetAckNumber();
                        }

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

                            //Pop the acked bytes from the front of the list
                            for (unsigned int i = 0; i < tempNumberOfAckedBytes && !sendWindow_.empty(); i++)
                            {
                                sendWindow_.pop_front();
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
                if (segment.GetFinFlag() && !recievedFin)
                {
                    printf("It's a fin\n\n");

                    recievedFin = true;

                    nextSendAckNumber = segment.GetSequenceNumber() + 1;
                    
                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);
                    printf("Emptying the recv buffer\n");

                    //Reset the recieve window
                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                    numberOfBytesInRecieveWindow = 0;
                    recvTempNextPosition = 0;
                    recvNextPosition = 0;
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

                        recvWindowVaild_[recvNextPosition] = true;

                        //Move the recieve window
                        recvNextPosition++;
                    }

                    nextSendAckNumber += segment.GetDataLength();
                    numberOfBytesInRecieveWindow += segment.GetDataLength();

                    //If all holes have been closed
                    if (recvNextPosition > recvTempNextPosition)
                    {
                        currentServerRecvWindowSize -= segment.GetDataLength();
                    }

                    //If the byte after the byte that was just writing is valid, that means a hole was just closed
                    //itterate until an invalid byte is found
                    while (recvWindowVaild_[recvNextPosition] && ((recvNextPosition < recvTempNextPosition) || (recvNextPosition < numberOfBytesInRecieveWindow)))
                    {
                        recvNextPosition++;
                        nextSendAckNumber++;
                    }
                }

                //If the data in the segment is out of order, but not data that's already been recieved
                else if (segment.GetSequenceNumber() > nextSendAckNumber)
                {
                    printf("It's data is out of order\n");

                    recvTempNextPosition = recvNextPosition + (segment.GetSequenceNumber() - nextSendAckNumber);

                    //Extract the data from the segment and add it to the recieve window
                    for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                    {
                        //Add a byte of data to the recieve window
                        recvWindow_[recvTempNextPosition] = segment.GetData()[i];

                        recvWindowVaild_[recvTempNextPosition] = true;

                        //Move the recieve window
                        recvTempNextPosition++;
                    }

                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE - recvTempNextPosition;

                    if (ignoreLoss)
                    {
                        nextSendAckNumber += recvNextPosition - recvTempNextPosition;
                        numberOfBytesInRecieveWindow += recvNextPosition - recvTempNextPosition;
                        recvNextPosition = recvTempNextPosition;
                    }

                    else
                    {
                        numberOfBytesInRecieveWindow += segment.GetDataLength();
                    }
                }

                //If the recieve buffer is full and there are no holes
                if (currentServerRecvWindowSize == 0 && recvNextPosition >= recvTempNextPosition && !recievedFin)
                {
                    //Write the data in the recieve window to the file
                    outputFile.write(recvWindow_, numberOfBytesInRecieveWindow);

                    //Make all bytes in the recieve window invalid
                    for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
                    {
                        recvWindowVaild_[i] = false;
                    }

                    printf("Emptying the recv buffer\n");

                    //Reset the recieve window
                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                    numberOfBytesInRecieveWindow = 0;
                    recvTempNextPosition = 0;
                    recvNextPosition = 0;
                }
                printf("currentServerRecvWindowSize: %d\n", currentServerRecvWindowSize);

            }

            printf("\n");

            //If the transfer of the input file isn't complete
            if (numberOfAckedBytes < (unsigned int)fileSize)
            {
                printf("Unacked bytes: %d\n", numberOfUnackedBytes);
                //If the recieved ack wasn't a duplicate
                if (numberOfDuplicateAcks == 0 && recievedAck == true)
                {
                    //The length of the segment that will be sent
                    uint16_t segmentLength = segment.GetReceiveWindow();

                    if (segmentLength > MAX_SEGMENT_DATA_LEN)
                    {
                        segmentLength = MAX_SEGMENT_DATA_LEN;
                    }

                    unsigned int sequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make a segment.
                    Segment ackSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sequenceNumber,
                        nextSendAckNumber, false, true, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                    //Mark the start time of the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<float, std::milli> currentTime =
                        startTransfer - startTimer;

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
                            //Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber, false, true, false, false, false, false, 0, segmentLength, 0, currentTime.count()});
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
                            //Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber, false, true, false, false, false, false, 0, segmentLength, 0, currentTime.count()});
                        }

                        // If we can't get a byte, that means we got EOF; leave the
                        // loop.
                        else
                        {
                            break;
                        }
                    }

                    //Send the segment
                    dataSocket_->Send(ackSegment, dataSocket_->GetRemoteAddress(), clientPort,
                        bitErrorPercent, segmentLoss);

                    numberOfUnackedBytes += ackSegment.GetDataLength();
                    printf("Sending data segment\n");
                    printf("Segment number %d\n", ackSegment.GetSequenceNumber());
                    printf("Ack number %d\n", ackSegment.GetAckNumber());
                    printf("RecvWindow: %d\n", ackSegment.GetReceiveWindow());
                    printf("Unacked bytes: %d\n", numberOfUnackedBytes);
                    printf("%d bytes long\n\n", ackSegment.GetDataLength());
                }

                else
                {
                    if (numberOfDuplicateAcks >= 3)
                    {
                        numberOfDuplicateAcks = 0;
                    }
                    //Populate the ack segment with old data
                    
                    //Make a list iterator that starts at the begining of the send buffer
                    std::list<sendWindowByte>::iterator it = sendWindow_.begin();

                    uint16_t segmentLength = it->dataLength;

                    //Remake the segment
                    Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, it->sequenceNumber,
                        nextSendAckNumber, it->urg, it->ack, it->psh,
                        it->rst, it->syn, it->fin, currentServerRecvWindowSize,
                        it->urgDataPointer, it->options);

                    //Populate the segment with all of it's bytes
                    for (unsigned int i = 0; i < segmentLength; i++)
                    {
                        //Get and add the byte it to the segment
                        resendSegment.AddByte(it->byte);

                        it++;
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
                    finSequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    //Make a fin ack segment
                    Segment finAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1,
                        nextSendAckNumber, false, true, false, false, false, true, currentServerRecvWindowSize, 0, 0);
                    
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
                    if ((numberOfUnackedBytes < currentServerSendWindowSize)
                        && numberOfUnackedBytes < (unsigned int)fileSize
                        && numberOfUnackedBytes < currentClientRecvWindowSize)
                    {
                        printf("Timeout time: %f\n\n", TimeoutInterval_);
                        printf("Unacked bytes: %d\n", numberOfUnackedBytes);
                        //The length of the segment that will be sent
                        uint16_t segmentLength;

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
                        if (segmentLength + numberOfUnackedBytes > currentClientRecvWindowSize)
                        {
                            segmentLength = currentClientRecvWindowSize - numberOfUnackedBytes;
                        }

                        if (segmentLength > 0)                            
                        {
                            unsigned int sequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                            // Make a segment.
                            Segment segment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, sequenceNumber,
                                0, false, false, false, false, false, false, currentServerRecvWindowSize, 0, 0);

                            //Mark the start time of the timer
                            startTimer = std::chrono::high_resolution_clock::now();
                            std::chrono::duration<float, std::milli> currentTime =
                                startTransfer - startTimer;

                            for (;;)
                            {
                                // We have to make sure we have put every byte into a segment
                                // Check if we have a left-over byte... this means we are in
                                // a new segment
                                if (queuedByte)
                                {
                                    segment.AddByte(byte);
                                    queuedByte = false;
                                    //Add the byte to the back of the send window
                                    sendWindow_.push_back({ byte, sequenceNumber, false, false, false, false, false, false, 0, segmentLength, 0, currentTime.count() });
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
                                    //Add the byte to the back of the send window
                                    sendWindow_.push_back({ byte, sequenceNumber, false, false, false, false, false, false, 0, segmentLength, 0, currentTime.count() });
                                }

                                // If we can't get a byte, that means we got EOF; leave the
                                // loop.
                                else
                                {
                                    break;
                                }
                            }

                            //Send the segment
                            dataSocket_->Send(segment, dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                            numberOfUnackedBytes += segment.GetDataLength();

                            printf("Sending data segment\n");
                            printf("Segment number %d\n", segment.GetSequenceNumber());
                            printf("RecvWindow: %d\n", segment.GetReceiveWindow());
                            printf("Unacked bytes: %d\n", numberOfUnackedBytes);
                            printf("%d bytes long\n\n", segment.GetDataLength());
                        }                        
                    }

                    if (!sendWindow_.empty())
                    {
                        // Get the current timer value in milliseconds
                        currentTimer = std::chrono::high_resolution_clock::now();

                        std::chrono::duration<float, std::milli> currentTime =
                            currentTimer - startTransfer;

                        printf("Current time: %f\n\n", currentTime.count() - sendWindow_.begin()->timeSent);

                        //If a timeout has occured, or three duplicate acks arrived
                        if (currentTime.count() - sendWindow_.begin()->timeSent >= TimeoutInterval_ || numberOfDuplicateAcks >= 3)
                        {
                            printf("Timeout time: %f\n\n", TimeoutInterval_);
                            //Make a list iterator that starts at the begining of the send buffer
                            std::list<sendWindowByte>::iterator it = sendWindow_.begin();

                            uint16_t segmentLength = it->dataLength;

                            //Remake the segment
                            Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN, dataPort_, clientPort, it->sequenceNumber,
                                nextSendAckNumber, it->urg, it->ack, it->psh,
                                it->rst, it->syn, it->fin, currentServerRecvWindowSize,
                                it->urgDataPointer, it->options);

                            //Populate the segment with all of it's bytes
                            for (unsigned int i = 0; i < segmentLength; i++)
                            {
                                //Get and add the byte it to the segment
                                resendSegment.AddByte(it->byte);

                                it->timeSent = currentTime.count();

                                it++;
                            }

                            //Only recalculate the timeout interval if a timeout occured
                            if (numberOfDuplicateAcks > 3)
                            {
                                numberOfDuplicateAcks = 0;
                            }

                            //The slow start threashhold becomes half of the current client window size
                            ssthresh = currentServerSendWindowSize / 2;

                            //The client window size gets reset back to one full segment.
                            currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

                            //Resend the segment
                            dataSocket_->Send(resendSegment, dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                            printf("Re-Sending data segment\n");
                            printf("Segment number %d\n", resendSegment.GetSequenceNumber());
                            printf("%d bytes long\n\n", resendSegment.GetDataLength());
                        }
                    
                    }

                    //A packet has arrived
                    if (dataSocket_->CheckReceive())
                    {
                        printf("Timeout time: %f\n\n", TimeoutInterval_);

                        // Get the current timer value in milliseconds
                        currentTimer = std::chrono::high_resolution_clock::now();

                        std::chrono::duration<float, std::milli> currentTime =
                            currentTimer - startTransfer ;

                        float_t RTTSample = currentTime.count() - sendWindow_.begin()->timeSent;

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
                                if (AckSegment.GetAckNumber() > duplicateAckSequenceNumber)
                                {
                                    printf("It's an ack\n");
                                    printf("Ack number %d\n", AckSegment.GetAckNumber());
                                    printf("It's a new ack segment\n");
                                    printf("duplicateAckSequenceNumber: %d\n", duplicateAckSequenceNumber);

                                    unsigned int tempNumberOfAckedBytes = AckSegment.GetAckNumber() - duplicateAckSequenceNumber;

                                    //Increase the number of acked bytes
                                    numberOfAckedBytes += tempNumberOfAckedBytes;

                                    printf("Bytes acked: %d\n", tempNumberOfAckedBytes);

                                    printf("total bytes acked: %d\n", numberOfAckedBytes);

                                    printf("File Size: %d\n", (unsigned int)fileSize);

                                    printf("Unacked bytes: %d\n", numberOfUnackedBytes);

                                    printf("currentServerSendWindowSize: %d\n", currentServerSendWindowSize);

                                    printf("currentClientRecvWindowSize: %d\n", currentClientRecvWindowSize);

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

                                    printf("currentServerSendWindowSize: %d\n", currentServerSendWindowSize);
                                    
                                    //Pop the acked bytes from the send window

                                    for (unsigned int i = 0; i < tempNumberOfAckedBytes && !sendWindow_.empty(); i++)
                                    {
                                        sendWindow_.pop_front();
                                    }

                                    //Update the duplicate number
                                    duplicateAckSequenceNumber = AckSegment.GetAckNumber();
                                    //Reset the number of duplicates
                                    numberOfDuplicateAcks = 0;
                                }
                                else
                                {
                                    printf("It's a duplicate ack segment\n");
                                    printf("Ack number %d\n", AckSegment.GetAckNumber());
                                    //Increase the number of duplicates
                                    numberOfDuplicateAcks++;
                                }
                            }
                        }

                        //If the file transfer is complete
                        if (numberOfAckedBytes >= (unsigned int)fileSize)
                        {
                            finSequenceNumber = startingServerSequenceNumber + numberOfUnackedBytes + numberOfAckedBytes + 1;

                            //Make a fin segment
                            Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, finSequenceNumber,
                                0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                            //Send the fin segment to the client
                            dataSocket_->Send(finSegment, dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                            printf("Sending fin segment\n");
                            printf("Segment number %d\n", finSegment.GetSequenceNumber());
                            printf("%d bytes long\n\n", finSegment.GetDataLength());
                        }

                        //Recalculate the estimated RTT value 
                        EstimatedRTT_ = ((1 - ALPHA)*EstimatedRTT_) + (ALPHA * RTTSample);

                        //Recalculate the RTT deviation value
                        DevRTT_ = ((1 - BETA)*DevRTT_) + (BETA * (fabs(RTTSample - EstimatedRTT_)));

                        //Recalculate the Timeout value
                        TimeoutInterval_ = EstimatedRTT_ + (4 * DevRTT_);

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
                        printf("Timeout time: %f\n\n", TimeoutInterval_);
                        //Make a fin segment
                        Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort, finSequenceNumber,
                            0, false, false, false, false, false, true, currentServerRecvWindowSize, 0, 0);

                        //Send the fin segment to the client
                        dataSocket_->Send(finSegment, dataSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                        printf("Re-Sending fin segment\n");
                        printf("Segment number %d\n", finSegment.GetSequenceNumber());
                        printf("%d bytes long\n\n", finSegment.GetDataLength());

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
                        printf("Ack number %d\n", segment.GetAckNumber());

                        //If the segment is not corrupt
                        if (segment.CalculateChecksum(0) == 0x0000)
                        {
                            //If the segment has an ack for the fin segment
                            if (segment.GetAckFlag() && segment.GetAckNumber() == finSequenceNumber+1)
                            {
                                printf("It's an ack for the fin\n\n");
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

        //Delete the data socket.
        delete dataSocket_;

        dataSocket_ = NULL;

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