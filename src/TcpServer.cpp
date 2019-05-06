/**
* \file TcpServer.cpp
* \details TCP server class - definitions
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
*/

/* C++ STL headers */
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>
#include <random>

/* Linux library headers */
#include <sys/stat.h>

/* Phase 6 library header */
#include "TcpServer.hpp"
#include "SocketTCP.hpp"
#include "Segment.hpp"

socksahoy::TcpServer::TcpServer(unsigned int port)
    : connPort_(port), connSocket_(new SocketTCP(connPort_)), sampleRtt_(0.0),
    estimatedRtt_(STARTING_TIMEOUT_VALUE), devRtt_(0.0),
    timeoutInterval_(estimatedRtt_ + 4 * devRtt_)
{}

socksahoy::TcpServer::~TcpServer()
{
    if (connSocket_ != NULL)
    {
        //Delete the conn socket.
        delete connSocket_;

        connSocket_ = NULL;
    }

    if (dataSocket_ != NULL)
    {
        //Delete the conn socket.
        delete dataSocket_;

        dataSocket_ = NULL;
    }
}

void socksahoy::TcpServer::Send(unsigned int destPort,
                                    const std::string& destAddr,
                                    const std::string& receiveFileName,
                                    const std::string& sendFileName,
                                    unsigned int bitErrorPercent,
                                    unsigned int segmentLoss,
                                    bool ignoreLoss)
{
    printf("\n");

    // The current number of bytes that the client can receive before it's
    // receive window becomes full
    unsigned int currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    // The starting sequence number of the client
    uint32_t startingClientSequenceNumber = 0;

    // The maximum size that the client's send window can be
    unsigned int maxClientSendWindowSize;

    // The current size of the client's send window
    unsigned int currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

    // The advertized size of the server's receive window
    unsigned int currentServerRecvWindowSize;

    // The starting sequence number of the server
    uint32_t startingServerSequenceNumber;

    // The ack number of the last, in order, ack received
    uint32_t duplicateAckSequenceNumber;

    // The number of duplicate acks received
    int numberOfDuplicateAcks = 0;

    // The current number of bytes in the client's receive window
    uint32_t numberOfBytesInReceiveWindow = 0;

    // The next number of the next that will be send ack
    uint32_t nextSendAckNumber = 0;

    // If the next data packet will contain an ack
    bool sendAck = false;

    // The total number of bytes that the server has acked
    uint32_t numberOfAckedBytes = 0;

    // The current number of un-acked bytes in the client's receive window
    uint32_t numberOfUnackedBytes = 0;

    // A timorary size for increasing the size of the client's receive window
    // in CA mode
    uint32_t tempSendWindowSize = 0;

    // If we have received a fin from the server or not
    bool receivedFin = false;

    // The size of the input file in bytes
    std::streamoff fileSize = 0;

    // Make all bytes in the receive window invalid
    for (auto& validByte: recvWindowValid_)
    {
        validByte = false;
    }

    // The start time of the timer for segments not put in the send window,
    // like the fin and syn segments
    auto startTimer = std::chrono::high_resolution_clock::now();

    // The current time for the timer
    auto currentTimer = std::chrono::high_resolution_clock::now();

    // The start time of the transfer
    auto startTransfer = currentTimer;

    // The finish time for the transfer
    auto finishTransfer = currentTimer;

    // The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    // The temporary next position in the recv ring buffer, for out of order
    // segments
    unsigned int recvTempNextPosition = 0;

    /************* C O N N E C T I O N  S E T U P  S E C T I O N *************/

    // Bind the connection socket to it's address and port.
    connSocket_->Bind();

    // Random number engine and distribution in range [1, 100]
    std::random_device dev;
    std::mt19937 rng(dev());

    using distType = std::mt19937::result_type;
    std::uniform_int_distribution<distType> uniformDist(1, 100);

    // Set the starting sequence number of the client to a random number
    // between 1 and 100
    startingClientSequenceNumber = uniformDist(rng);

    // Make a connection set up segment
    Segment synSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort,
            startingClientSequenceNumber, 0, false, false, false, false, true,
            false, currentClientRecvWindowSize, 0, 0);

    // Send the connection set up segment to the server.
    connSocket_->Send(synSegment, destAddr, destPort, bitErrorPercent,
            segmentLoss);

    printf("Sending syn segment\n");
    printf("Segment number %u\n", synSegment.GetSequenceNumber());
    printf("%u bytes long\n\n", synSegment.GetDataLength());

    // Set the starting value of the timer
    startTimer = std::chrono::high_resolution_clock::now();

    while (true)
    {
        // If a packet arrived
        if (connSocket_->CheckReceive())
        {
            // Make a SynAck segment that will be received from the server
            Segment synAckSegment(MAX_EMPTY_SEGMENT_LEN);

            //Receive the ack segment from the server
            connSocket_->Receive(synAckSegment);

            printf("Received syn ack segment\n");
            printf("Segment number %u\n", synAckSegment.GetSequenceNumber());
            printf("Ack number %u\n", synAckSegment.GetAckNumber());
            printf("%u bytes long\n\n", synAckSegment.GetDataLength());

            // If the syn ack segment isn't corrupt, has the correct flags set,
            // and has the correct ack number
            if (synAckSegment.CalculateChecksum(0) == 0x0000 &&
                    synAckSegment.GetSyncFlag() && synAckSegment.GetAckFlag()
                    && synAckSegment.GetAckNumber() ==
                    startingClientSequenceNumber + 1)
            {
                // Make the max send window the same size of the receive window
                // of the server
                maxClientSendWindowSize = synAckSegment.GetReceiveWindow();
                currentServerRecvWindowSize = maxClientSendWindowSize;

                // Store the starting sequence number of the server
                startingServerSequenceNumber =
                    synAckSegment.GetSequenceNumber();

                // The next ack sent by the client will have an ack number that
                // is one past the starting sequence number of the server
                nextSendAckNumber = startingServerSequenceNumber + 1;

                // Store the ack number as the duplicate
                duplicateAckSequenceNumber = synAckSegment.GetAckNumber();

                // Make an ack segment to send back to the server
                Segment ackSegment(MAX_EMPTY_SEGMENT_LEN, connPort_, destPort,
                        0, nextSendAckNumber, false, true, false, false, false,
                        false, currentClientRecvWindowSize, 0, 0);

                // Send the ack segmet to the server
                connSocket_->Send(ackSegment, destAddr, destPort,
                        bitErrorPercent, segmentLoss);

                printf("Sending ack segment\n");
                printf("Ack number %u\n", ackSegment.GetAckNumber());
                printf("%u bytes long\n\n", ackSegment.GetDataLength());

                // Make the data port the same as the connection port
                dataPort_ = connPort_;

                // Store the old port number just in case we need to resend the
                // ack for the syn ack segment
                connPort_ = destPort;

                // Switch over to using the new port that the server sent us
                // for the data transfer
                destPort = synAckSegment.GetSourcePortNumber();

                // Connection established, break out of the loop
                break;
            }
        }

        // Get the current timer value in milliseconds
        currentTimer = std::chrono::high_resolution_clock::now();

        std::chrono::duration<float, std::milli> timermiliSeconds =
            currentTimer - startTimer;

        // If the timeout occurred
        if (timermiliSeconds.count() >= timeoutInterval_)
        {
            // Resend the Sync packet
            connSocket_->Send(synSegment, destAddr, destPort, bitErrorPercent,
                    segmentLoss);

            printf("Re-Sending syn segment\n");
            printf("Segment number %u\n", synSegment.GetSequenceNumber());
            printf("%u bytes long\n\n", synSegment.GetDataLength());

            // Restart the timer
            startTimer = std::chrono::high_resolution_clock::now();
        }
    }
    /********** E N D  C O N N E C T I O N  S E T U P  S E C T I O N *********/

    /*************** D A T A  T R A N S F E R  S E C T I O N *****************/
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

        // Open a file to input from in binary mode with the stream at the end
        // of the file.
        std::ifstream inputFile(sendFileName,
            std::ios::binary | std::ios::ate);

        // Find out how many bytes are in the file.
        fileSize = inputFile.tellg();

        // Reset the file stream back to the begining of the file
        inputFile.seekg(0);

        // Open a file to output to
        std::ofstream outputFile(receiveFileName, std::ios::binary);

        // Stores an overflow byte from a previous segment
        char byte = 0;

        // Indicates if there is an overflow byte from a previous segment
        bool queuedByte = false;

        // Get the time at the start of the transfer.
        startTransfer = std::chrono::high_resolution_clock::now();

        // Loop until all segments are sent and acked
        while (numberOfAckedBytes < (unsigned int)fileSize)
        {
            // The ring buffer isn't full and the last segment hasn't been
            // sent, and the server can handle more bytes.
            if ((numberOfUnackedBytes < currentClientSendWindowSize)
                    && (numberOfUnackedBytes < (unsigned int)fileSize)
                    && numberOfUnackedBytes < currentServerRecvWindowSize)
            {
                printf("Timeout time: %f\n\n", timeoutInterval_);

                // The length of the segment that will be sent
                uint16_t segmentLength;

                printf("currentServerRecvWindowSize: %u\n",
                        currentServerRecvWindowSize);

                // If the segment won't fit in the send buffer
                if (numberOfUnackedBytes + MAX_SEGMENT_DATA_LEN >
                        currentClientSendWindowSize)
                {
                    // Make it smaller so that it will fit
                    segmentLength =
                        currentClientSendWindowSize - numberOfUnackedBytes;
                }

                // A fill size segment will fit
                else
                {
                    segmentLength = MAX_SEGMENT_DATA_LEN;
                }

                // If the segment length is longer than what the server can
                // receive
                if (segmentLength + numberOfUnackedBytes >
                        currentServerRecvWindowSize)
                {
                    // Make it fit
                    segmentLength =
                        currentServerRecvWindowSize - numberOfUnackedBytes;
                }

                // If there is even a segment to send
                if (segmentLength > 0)
                {
                    // Get the sequence number of the segment that will be sent
                    unsigned int sequenceNumber = startingClientSequenceNumber
                        + numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make the segment.
                    Segment segment(segmentLength + SEGMENT_HEADER_LEN,
                            dataPort_, destPort, sequenceNumber,
                            nextSendAckNumber, false, sendAck, false, false,
                            false, false, currentClientRecvWindowSize, 0, 0);

                    // Mark the time that this segment was sent
                    startTimer = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<float, std::milli> currentTime =
                        startTransfer - startTimer;

                    // Increase the number of unacked bytes in the send window
                    // by the length of the segment
                    numberOfUnackedBytes += segmentLength;

                    // Add bytes to the segment until it's full, or we reach
                    // EOF
                    for (;;)
                    {
                        // We have to make sure we have put every byte into a
                        // segment Check if we have a left-over byte... this
                        // means we are in a new segment
                        if (queuedByte)
                        {
                            segment.AddByte(byte);
                            queuedByte = false;

                            // Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber,
                                    false, sendAck, false, false, false, false,
                                    0, segmentLength, 0, currentTime.count()
                                    });
                        }

                        // Get a byte and try to put it into the packet.  If
                        // after this we have a queuedByte, this means we go to
                        // another packet.
                        if (inputFile.get(byte))
                        {
                            queuedByte = !segment.AddByte(byte);

                            // The segment is already full, leave the loop
                            if (queuedByte)
                            {
                                break;
                            }

                            // Add the byte to the back of the send window
                            sendWindow_.push_back({ byte, sequenceNumber,
                                    false, sendAck, false, false, false, false,
                                    0, segmentLength, 0, currentTime.count()
                                    });
                        }

                        // If we can't get a byte, that means we got EOF; leave
                        // the loop.
                        else
                        {
                            break;
                        }
                    }

                    // The next segment won't have an ack, unless a segment
                    // arrives from the server
                    if (sendAck)
                    {
                        sendAck = false;
                    }

                    // Send the segment
                    dataSocket_->Send(segment, destAddr, destPort,
                            bitErrorPercent, segmentLoss);

                    printf("Sending data segment\n");
                    printf("Segment number %u\n", segment.GetSequenceNumber());

                    if (segment.GetAckFlag())
                    {
                        printf("Ack number %u\n", segment.GetAckNumber());
                    }

                    printf("%u bytes long\n\n", segment.GetDataLength());
                }
            }

            // If the send window isn't empty
            if (!sendWindow_.empty())
            {
                // Get the current time value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> currentTime =
                    currentTimer - startTransfer ;

                printf("Current time: %f\n\n",
                        currentTime.count() - sendWindow_.begin()->timeSent);

                // If a timeout has occured, or three duplicate acks arrived
                if (currentTime.count() - sendWindow_.begin()->timeSent >=
                        timeoutInterval_ || numberOfDuplicateAcks >= 3)
                {
                    printf("Timeout time: %f\n\n", timeoutInterval_);
                    printf("Send Window Number of Bytes: %lu\n",
                            sendWindow_.size());

                    // Make a list iterator that starts at the begining of the
                    // send buffer
                    auto it = sendWindow_.begin();

                    //Get the length of the segment that will be re-sent
                    uint16_t segmentLength = it->dataLength;

                    // Remake the segment
                    Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN,
                        dataPort_, destPort, it->sequenceNumber,
                        nextSendAckNumber, it->urg, it->ack, it->psh,
                        it->rst, it->syn, it->fin, currentClientRecvWindowSize,
                        it->urgDataPointer, it->options);

                    // Populate the segment with all of it's bytes
                    for (unsigned int i = 0; i < segmentLength; i++)
                    {
                        // Get and add the byte it to the segment
                        resendSegment.AddByte(it->byte);

                        // Update the send time of the segment
                        it->timeSent = currentTime.count();

                        // Move the iterator up
                        ++it;
                    }

                    // If three duplicate acks arrived, reset the number back
                    // to zero
                    if (numberOfDuplicateAcks > 3)
                    {
                        numberOfDuplicateAcks = 0;
                    }

                    // The slow start threshold becomes half of the current
                    // client window size
                    ssthresh = currentClientSendWindowSize / 2;

                    printf("ssthresh: %u\n", ssthresh);

                    // The client window size gets reset back to one full
                    // segment.
                    currentClientSendWindowSize = MAX_SEGMENT_DATA_LEN;

                    // Resend the segment
                    dataSocket_->Send(resendSegment, destAddr, destPort,
                            bitErrorPercent, segmentLoss);

                    printf("Re-Sending data segment\n");
                    printf("Segment number %u\n",
                            resendSegment.GetSequenceNumber());

                    if (resendSegment.GetAckFlag())
                    {
                        printf("Ack number %u\n",
                                resendSegment.GetAckNumber());
                    }

                    printf("%u bytes long\n\n", resendSegment.GetDataLength());
                }
            }

            // A segment has arrived
            if (dataSocket_->CheckReceive())
            {
                printf("Timeout time: %f\n\n", timeoutInterval_);

                // Get the current time value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> currentTime =
                    currentTimer - startTransfer ;

                // Get the a sample RTT for the oldest unacked segment
                float_t rttSample =
                    currentTime.count() - sendWindow_.begin()->timeSent;

                // Make a segment.
                Segment ackSegment(MAX_FULL_SEGMENT_LEN);

                // Receive the segment from the server
                dataSocket_->Receive(ackSegment);

                printf("Received segment\n");
                printf("Segment number %u\n", ackSegment.GetSequenceNumber());
                printf("Ack number %u\n", ackSegment.GetAckNumber());
                printf("%u bytes long\n", ackSegment.GetDataLength());

                // If a fin segment hasn't been received from the server
                if (!receivedFin)
                {
                    // Send an ack in the next data packet
                    sendAck = true;
                }

                // If the segment is not corrupt
                if (ackSegment.CalculateChecksum(0) == 0x0000)
                {
                    // Store the advertized receive window of the server
                    currentServerRecvWindowSize =
                        ackSegment.GetReceiveWindow();

                    // If it's the synack from before
                    if (ackSegment.GetSyncFlag() && ackSegment.GetAckFlag() &&
                            ackSegment.GetAckNumber() ==
                            startingClientSequenceNumber + 1)
                    {
                        printf("It's a syn ack segment\n");

                        // Make an ack segment to send back to the server
                        Segment ackSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_,
                                connPort_, 0, nextSendAckNumber, false, true,
                                false, false, false, false,
                                currentClientRecvWindowSize, 0, 0);

                        // Send the ack segmet to the server
                        dataSocket_->Send(ackSegment, destAddr, connPort_,
                                bitErrorPercent, segmentLoss);

                        printf("Re-Sending ack segment\n");
                        printf("Ack number %u\n", ackSegment.GetAckNumber());
                        printf("%u bytes long\n\n",
                                ackSegment.GetDataLength());

                        continue;
                    }

                    // If the segment is an ack segment
                    else if (ackSegment.GetAckFlag())
                    {
                        // If the ack isn't a duplicate
                        if (ackSegment.GetAckNumber() >
                                duplicateAckSequenceNumber)
                        {
                            printf("It's a new ack segment\n");

                            // Find out how many bytes were acked by this ack
                            // segment
                            unsigned int tempNumberOfAckedBytes =
                                ackSegment.GetAckNumber() -
                                duplicateAckSequenceNumber;

                            printf("%u bytes acked\n", tempNumberOfAckedBytes);

                            // Increase the total number of acked bytes
                            numberOfAckedBytes += tempNumberOfAckedBytes;

                            // Decrease the number of unacked bytes in the send
                            // window
                            numberOfUnackedBytes -= tempNumberOfAckedBytes;

                            printf("%u total bytes acked\n",
                                    numberOfAckedBytes);

                            printf("%u Un-acked bytes\n",
                                    numberOfUnackedBytes);

                            // Increase the windowsize
                            // If the window size is currently smaller than the
                            // max window size
                            if (currentClientSendWindowSize <
                                    maxClientSendWindowSize)
                            {
                                // If we are in CA mode
                                if (currentClientSendWindowSize >= ssthresh)
                                {
                                    float windowSizeScale =
                                        (float)MAX_FULL_SEGMENT_LEN /
                                        (float)currentClientSendWindowSize;

                                        tempSendWindowSize +=
                                        MAX_FULL_SEGMENT_LEN * windowSizeScale;

                                    if (tempSendWindowSize >=
                                            MAX_FULL_SEGMENT_LEN)
                                    {
                                        currentClientSendWindowSize +=
                                            MAX_FULL_SEGMENT_LEN;

                                        tempSendWindowSize = 0;
                                    }
                                }

                                // Must be in SS mode
                                else
                                {
                                    currentClientSendWindowSize +=
                                        MAX_FULL_SEGMENT_LEN;

                                    // If we've gone from SS to CA mode
                                    if (currentClientSendWindowSize > ssthresh)
                                    {
                                        currentClientSendWindowSize = ssthresh;
                                    }
                                }

                                // If the new client window size is greater
                                // than the max set by the receiver
                                if (currentClientSendWindowSize >
                                        maxClientSendWindowSize)
                                {
                                    currentClientSendWindowSize =
                                        maxClientSendWindowSize;
                                }
                            }

                            printf("currentClientSendWindowSize: %u\n",
                                    currentClientSendWindowSize);

                            // Pop the acked bytes from the front of the list
                            for (unsigned int i = 0; i < tempNumberOfAckedBytes
                                    && !sendWindow_.empty(); i++)
                            {
                                sendWindow_.pop_front();
                            }

                            // Update the duplicate ack number
                            duplicateAckSequenceNumber =
                                ackSegment.GetAckNumber();

                            // Reset the number of duplicates
                            numberOfDuplicateAcks = 0;
                        }

                        // Else, it's a duplicate ack
                        else
                        {
                            printf("It's a duplicate ack segment\n");

                            // Increase the number of duplicates
                            numberOfDuplicateAcks++;
                        }
                    }

                    // If the segment is a fin segment
                    if (ackSegment.GetFinFlag() && !receivedFin)
                    {
                        printf("It's a fin segment\n\n");
                        // Send an ack in the next data packet
                        nextSendAckNumber = ackSegment.GetSequenceNumber() + 1;

                        // Indicated that a fin was received from the server
                        receivedFin = true;

                        // Send an ack in the next data packet
                        sendAck = true;

                        // Write the data in the receive window to the file
                        outputFile.write(recvWindow_.data(),
                                numberOfBytesInReceiveWindow);
                    }

                    // If a fin segment hasn't been received from the server
                    if (!receivedFin)
                    {
                        // If the data in the ack segment is in order
                        if (ackSegment.GetSequenceNumber() ==
                                nextSendAckNumber)
                        {
                            printf("It's data is in order\n");

                            // Extract the data from the segment and add it to
                            // the receive window
                            for (unsigned int i = 0; i <
                                    ackSegment.GetDataLength(); i++)
                            {
                                // Add a byte of data to the receive window
                                recvWindow_[recvNextPosition] =
                                    ackSegment.GetData()[i];

                                // Mark the byte as valid
                                recvWindowValid_[recvNextPosition] = true;

                                // Move the receive window
                                recvNextPosition++;
                            }

                            nextSendAckNumber += ackSegment.GetDataLength();
                            numberOfBytesInReceiveWindow +=
                                ackSegment.GetDataLength();

                            // If all holes have been closed
                            if (recvNextPosition > recvTempNextPosition)
                            {
                                // Decrease the advertised number of free bytes
                                // in the client's receive window
                                currentClientRecvWindowSize -=
                                    ackSegment.GetDataLength();
                            }

                            // If the byte after the byte that was just writing
                            // is valid, that means a hole was just closed

                            // Iterate until an invalid byte is found
                            while (recvWindowValid_[recvNextPosition] &&
                                    ((recvNextPosition < recvTempNextPosition)
                                    || (recvNextPosition <
                                        numberOfBytesInReceiveWindow)))
                            {
                                // Move the next pointer increase the next ack
                                // number that will be sent
                                recvNextPosition++;
                                nextSendAckNumber++;
                            }
                        }

                        // If the data in the segment is out of order, but not
                        // data that's already been received
                        else if (ackSegment.GetSequenceNumber() >
                                nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");

                            // Get the starting position in the receive buffer
                            // for the bytes in the segment
                            recvTempNextPosition = recvNextPosition +
                                (ackSegment.GetSequenceNumber()
                                - nextSendAckNumber);

                            // Extract the data from the segment and add it to
                            // the receive window
                            for (unsigned int i = 0; i <
                                    ackSegment.GetDataLength(); i++)
                            {
                                // Add a byte of data to the receive window
                                recvWindow_[recvTempNextPosition] =
                                    ackSegment.GetData()[i];

                                // Mark the byte as valid
                                recvWindowValid_[recvTempNextPosition] = true;

                                // Move the receive window
                                recvTempNextPosition++;
                            }

                            // Only bytes after the ending index of this
                            // considered free space, holes are not free space.
                            currentClientRecvWindowSize =
                                MAX_RECV_WINDOW_SIZE - recvTempNextPosition;

                            // If loss is ingnored
                            if (ignoreLoss)
                            {
                                // Make the ack next number be for the byte
                                // after this segment
                                nextSendAckNumber += recvNextPosition
                                    - recvTempNextPosition;

                                // Increase the number of bytes in the receive
                                // window
                                numberOfBytesInReceiveWindow +=
                                    recvNextPosition - recvTempNextPosition;

                                // Move the next position to the temp position
                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                // Increase the number of bytes in the receive
                                // window
                                numberOfBytesInReceiveWindow +=
                                    ackSegment.GetDataLength();
                            }
                        }

                        // If the receive buffer is full and there are no holes
                        if (currentClientRecvWindowSize == 0 &&
                                recvNextPosition >= recvTempNextPosition &&
                                !receivedFin)
                        {
                            // Write the data in the receive window to the file
                            outputFile.write(recvWindow_.data(),
                                    numberOfBytesInReceiveWindow);

                            // Make all bytes in the receive window invalid
                            for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE;
                                    i++)
                            {
                                recvWindowValid_[i] = false;
                            }

                            printf("Emptying the recv buffer\n");

                            // Reset the receive window
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                            numberOfBytesInReceiveWindow = 0;
                            recvTempNextPosition = 0;
                            recvNextPosition = 0;
                        }
                        printf("currentClientRecvWindowSize: %u\n",
                                currentClientRecvWindowSize);
                    }
                }

                // Recalculate the estimated RTT value
                estimatedRtt_ = ((1 - ALPHA)*estimatedRtt_)
                    + (ALPHA * rttSample);

                // Recalculate the RTT deviation value
                devRtt_ = ((1 - BETA) * devRtt_)
                    + (BETA * (fabs(rttSample - estimatedRtt_)));

                // Recalculate the Timeout value
                timeoutInterval_ = estimatedRtt_ + (4 * devRtt_);

                printf("\n");
            }
        }

        // Get the time at the end of the transfer.
        finishTransfer = std::chrono::high_resolution_clock::now();

        // Close the input file
        inputFile.close();

        // Store the sequence number of the fin that is going to be sent to the
        // server
        unsigned int finSequenceNumber = duplicateAckSequenceNumber + 1;

        // The fin hasn't been acked yet
        bool finAcked = false;

        // Make fin segment
        Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, destPort,
                finSequenceNumber, nextSendAckNumber, false, true, false,
                false, false, true, currentClientRecvWindowSize, 0, 0);

        // Send the fin segment to the server
        dataSocket_->Send(finSegment, destAddr, destPort, bitErrorPercent,
                segmentLoss);

        printf("Sending fin segment\n");
        printf("Segment number %u\n", finSegment.GetSequenceNumber());
        if (finSegment.GetAckFlag())
        {
            printf("Ack number %u\n", finSegment.GetAckNumber());
        }
        printf("%u bytes long\n\n", finSegment.GetDataLength());

        // Set the starting value of the timer
        startTimer = std::chrono::high_resolution_clock::now();

        // Loop until the receiver completes it's data transfer, sends it's fin
        // segment, and acks the client's fin segment
        while (!receivedFin || !finAcked)
        {
            printf("Timeout time: %f\n\n", timeoutInterval_);

            // A packet has arrived
            if (dataSocket_->CheckReceive())
            {
                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                // Make a segment.
                Segment segment(MAX_FULL_SEGMENT_LEN);

                // Receive the segment from the server
                dataSocket_->Receive(segment);

                printf("Received segment\n");
                printf("Segment number %u\n", segment.GetSequenceNumber());
                printf("%u bytes long\n", segment.GetDataLength());

                //If the segment is not corrupt
                if (segment.CalculateChecksum(0) == 0x0000)
                {
                    // If the segment has an ack for the fin segment
                    if (segment.GetAckFlag() && segment.GetAckNumber() ==
                            finSequenceNumber + 1)
                    {
                        printf("It's an ack for the fin segment\n");
                        printf("Ack number %u\n", segment.GetAckNumber());

                        // The client's fin has been acked
                        finAcked = true;
                    }

                    //If a fin segment hasn't been received from the server
                    if (!receivedFin)
                    {
                        //If the segment is a fin segment
                        if (segment.GetFinFlag() && !receivedFin)
                        {
                            printf("It's a fin segment\n\n");

                            // Make an ackSegment.
                            Segment ackSegment(MAX_EMPTY_SEGMENT_LEN,dataPort_,
                                    destPort, 0, segment.GetSequenceNumber() +
                                    1, false, true, false, false, false, false,
                                    currentClientRecvWindowSize, 0, 0);

                            // Send the segment to the server
                            dataSocket_->Send(ackSegment, destAddr, destPort,
                                    0, 0);

                            printf("Sending ack segment\n");
                            printf("Ack number %u\n",
                                    ackSegment.GetAckNumber());
                            printf("%u bytes long\n\n",
                                    ackSegment.GetDataLength());

                            // Write the data in the receive window to the file
                            outputFile.write(recvWindow_.data(),
                                    numberOfBytesInReceiveWindow);

                            receivedFin = true;
                        }

                        // If the data in the segment is in order
                        if (segment.GetSequenceNumber() == nextSendAckNumber)
                        {
                            printf("It's data is in order\n");

                            // Extract the data from the segment and add it to
                            // the receive window
                            for (unsigned int i = 0; i <
                                    segment.GetDataLength(); i++)
                            {
                                // Add a byte of data to the receive window
                                recvWindow_[recvNextPosition] =
                                    segment.GetData()[i];

                                recvWindowValid_[recvNextPosition] = true;

                                // Move the receive window
                                recvNextPosition++;
                            }

                            nextSendAckNumber += segment.GetDataLength();
                            numberOfBytesInReceiveWindow +=
                                segment.GetDataLength();

                            // If all holes have been closed
                            if (recvNextPosition > recvTempNextPosition)
                            {
                                currentClientRecvWindowSize -=
                                    segment.GetDataLength();
                            }

                            // If the byte after the byte that was just writing
                            // is valid, that means a hole was just closed

                            // Iterate until an invalid byte is found
                            while (recvWindowValid_[recvNextPosition] &&
                                    ((recvNextPosition < recvTempNextPosition)
                                    || (recvNextPosition <
                                        numberOfBytesInReceiveWindow)))
                            {
                                recvNextPosition++;
                                nextSendAckNumber++;
                            }
                        }

                        // If the data in the segment is out of order, but not
                        // data that's already been received
                        else if (segment.GetSequenceNumber() >
                                nextSendAckNumber)
                        {
                            printf("It's data is out of order\n");

                            recvTempNextPosition = recvNextPosition +
                                (segment.GetSequenceNumber() -
                                nextSendAckNumber);

                            // Extract the data from the segment and add it to
                            // the receive window
                            for (unsigned int i = 0; i <
                                    segment.GetDataLength(); i++)
                            {
                                // Add a byte of data to the receive window
                                recvWindow_[recvTempNextPosition] =
                                    segment.GetData()[i];

                                recvWindowValid_[recvTempNextPosition] = true;

                                // Move the receive window
                                recvTempNextPosition++;
                            }

                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE
                                - recvTempNextPosition;

                            if (ignoreLoss)
                            {
                                nextSendAckNumber += recvNextPosition -
                                    recvTempNextPosition;

                                numberOfBytesInReceiveWindow +=
                                    recvNextPosition - recvTempNextPosition;

                                recvNextPosition = recvTempNextPosition;
                            }

                            else
                            {
                                numberOfBytesInReceiveWindow +=
                                    segment.GetDataLength();
                            }
                        }

                        // If the receive buffer is full and there are no holes
                        if (currentClientRecvWindowSize == 0 &&
                                recvNextPosition >= recvTempNextPosition &&
                                !receivedFin)
                        {
                            // Write the data in the receive window to the file
                            outputFile.write(recvWindow_.data(),
                                    numberOfBytesInReceiveWindow);

                            // Make all bytes in the receive window invalid
                            for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE;
                                    i++)
                            {
                                recvWindowValid_[i] = false;
                            }

                            printf("Emptying the recv buffer\n");

                            // Reset the receive window
                            currentClientRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                            numberOfBytesInReceiveWindow = 0;
                            recvTempNextPosition = 0;
                            recvNextPosition = 0;
                        }
                        printf("currentClientRecvWindowSize: %u\n",
                                currentClientRecvWindowSize);
                    }

                    printf("\n");
                }

                // If we haven't received a fin from the server
                if (!receivedFin)
                {
                    // Make an ackSegment.
                    Segment ackSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_,
                            destPort, 0, nextSendAckNumber, false, true, false,
                            false, false, false, currentClientRecvWindowSize,
                            0, 0);

                    // Send the segment to the server
                    dataSocket_->Send(ackSegment, destAddr, destPort,
                        bitErrorPercent, segmentLoss);

                    printf("Sending ack segment\n");
                    printf("Ack number %u\n", ackSegment.GetAckNumber());
                    printf("%u bytes long\n\n", ackSegment.GetDataLength());
                }

                // Recalculate the estimated RTT value
                estimatedRtt_ = ((1 - ALPHA)*estimatedRtt_)
                    + (ALPHA * timermiliSeconds.count());

                // Recalculate the RTT deviation value
                devRtt_ = ((1 - BETA) * devRtt_) +
                    (BETA * (fabs(timermiliSeconds.count() - estimatedRtt_)));

                // Recalculate the Timeout value
                timeoutInterval_ = estimatedRtt_ + (4 * devRtt_);

                // Mark the start time of the timer
                startTimer = std::chrono::high_resolution_clock::now();

                printf("\n");
            }

            // If the fin segment wasn't acked
            if (!finAcked)
            {
                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                // If the timeout occurred
                if (timermiliSeconds.count() >= timeoutInterval_)
                {
                    // Resend the fin segment
                    dataSocket_->Send(finSegment, destAddr, destPort,
                            bitErrorPercent, segmentLoss);

                    printf("Re-Sending fin segment\n");
                    printf("Segment number %u\n",
                            finSegment.GetSequenceNumber());
                    if (finSegment.GetAckFlag())
                    {
                        printf("Ack number %u\n", finSegment.GetAckNumber());
                    }

                    printf("%u bytes long\n\n", finSegment.GetDataLength());

                    // Restart the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }
            }
        }

        // Close the output file
        outputFile.close();

        // Delete the data socket.
        delete dataSocket_;

        // Make it null
        dataSocket_ = NULL;

        // Get the time it took to send the file in milliseconds
        std::chrono::duration<float, std::milli> miliSeconds = finishTransfer -
            startTransfer;

        printf("Percent of segments with bit errors: %u%%\n",
                bitErrorPercent);

        printf("Percent of segments lost: %u%%\n", segmentLoss);

        printf("Time for the server to transfer the file in milliseconds: ");
        printf("%g\n", miliSeconds.count());

    }
    catch (std::runtime_error& e)
    {
        throw e;
    }

}

void socksahoy::TcpServer::Listen(const std::string& receiveFileName,
        const std::string& sendFileName,
        unsigned int bitErrorPercent,
        unsigned int segmentLoss,
        bool ignoreLoss)
{
    printf("\n");

    // The current number of bytes that the server can receive before it's
    // receive window becomes full
    unsigned int currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;

    // The starting sequence number of the server
    unsigned int startingServerSequenceNumber = 0;

    // The max size of the server's send window
    unsigned int maxServerSendWindowSize;

    // The current size of the server's send window
    unsigned int currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

    // The advertized size of the client's receive window
    unsigned int currentClientRecvWindowSize;

    // The starting sequence number of the client
    unsigned int startingClientSequenceNumber;

    // The ack number of the last, in order, ack received
    uint32_t duplicateAckSequenceNumber = 0;

    // The ack number of duplicate acks received
    unsigned int numberOfDuplicateAcks = 0;

    // The number of bytes in the server's receive window
    uint32_t numberOfBytesInReceiveWindow = 0;

    // The number of the next ack segment that will be sent
    uint32_t nextSendAckNumber = 0;

    // The total number of bytes acked
    uint32_t numberOfAckedBytes = 0;

    // The number of un-acked bytes in the server's send window
    uint32_t numberOfUnackedBytes = 0;

    // A temporary size for increasing the size of the server's receive window
    // in CA mode
    uint32_t tempSendWindowSize = 0;

    // The port number of the connecting client
    unsigned int clientPort = 0;

    // If the fin has been acked
    bool finAcked = false;

    // The size of the input file
    std::streamoff fileSize = 0;

    // Make all bytes in the receive window invalid
    for (auto& validByte: recvWindowValid_)
    {
        validByte = false;
    }

    // Initialize timers
    auto startTimer = std::chrono::high_resolution_clock::now();
    auto currentTimer = startTimer;
    auto startTransfer = startTimer;
    auto finishTransfer = startTimer;

    // The next position in the recv ring buffer to add a byte to
    unsigned int recvNextPosition = 0;

    // The temporary next position in the recv ring buffer, for out of order
    // segments
    unsigned int recvTempNextPosition = 0;

    // If an ack has been received
    bool receivedAck = false;

    // If a fin segment has been received
    bool receivedFin = false;

    /************* C O N N E C T I O N  S E T U P  S E C T I O N *************/

    // Bind the connection socket to it's address and port
    connSocket_->Bind();

    // Make a Sync packet that will be received from the connecting client
    Segment synSegment(MAX_EMPTY_SEGMENT_LEN);

    while (true)
    {
        // Wait for a client to connect
        connSocket_->Receive(synSegment);

        printf("Received syn segment\n");
        printf("Segment number %u\n", synSegment.GetSequenceNumber());
        printf("%u bytes long\n\n", synSegment.GetDataLength());

        // If the sync packet isn't corrupt and it's syn flag is set
        if (synSegment.CalculateChecksum(0) == 0x0000 &&
                synSegment.GetSyncFlag())
        {
            // Store the starting sequence number of the client
            startingClientSequenceNumber = synSegment.GetSequenceNumber();

            // Make the max send window the same size of the receive window of
            // the client
            maxServerSendWindowSize = synSegment.GetReceiveWindow();

            // Store the advertized size of the client's receive window
            currentClientRecvWindowSize = maxServerSendWindowSize;

            // Store the port number of the client
            clientPort = synSegment.GetSourcePortNumber();

            // Random number engine and distribution
            // Distribution in range [1, 100]
            std::random_device dev;
            std::mt19937 rng(dev());

            using distType = std::mt19937::result_type;
            std::uniform_int_distribution<distType> uniformDist(1, 100);

            // Set the starting sequence number of the server to a random
            // number between 1 and 100
            startingServerSequenceNumber = uniformDist(rng);

            // The next ack sent by the server will have an ack number that is
            // one past the starting sequence number of the client
            nextSendAckNumber = startingClientSequenceNumber + 1;

            // Make a data port for this client
            dataPort_ = connPort_ + clientNumber_;

            // Make a Sync Ack packet that will be sent back to the client with
            // the port number of the data socket
            Segment synAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_, clientPort,
                    startingServerSequenceNumber, nextSendAckNumber, false,
                    true, false, false, true, false,
                    currentServerRecvWindowSize, 0, 0);

            printf("Sending syn ack segment\n");

            // Send the Sync Ack packet
            connSocket_->Send(synAckSegment, connSocket_->GetRemoteAddress(),
                    clientPort, bitErrorPercent, segmentLoss);

            printf("Segment number %u\n", synAckSegment.GetSequenceNumber());
            printf("Ack number %u\n", synAckSegment.GetAckNumber());
            printf("%u bytes long\n\n", synAckSegment.GetDataLength());

            // Increase the number of clients connected
            clientNumber_++;

            // Set the starting value of the timer
            startTimer = std::chrono::high_resolution_clock::now();

            while (true)
            {
                // If a packet arrived
                if (connSocket_->CheckReceive())
                {
                    // Make a Ack packet that will be received from the
                    // connecting client
                    Segment ackSegment(MAX_EMPTY_SEGMENT_LEN);

                    // Receive the ack packet from the client
                    connSocket_->Receive(ackSegment);

                    printf("Received segment\n");
                    printf("Segment number %u\n",
                            ackSegment.GetSequenceNumber());

                    printf("%u bytes long\n", ackSegment.GetDataLength());

                    // If the ack packet isn't corrupt, has the correct flags
                    // set, and has the correct ack number
                    if (ackSegment.CalculateChecksum(0) == 0x0000 &&
                            ackSegment.GetAckFlag() &&
                            ackSegment.GetAckNumber() ==
                            startingServerSequenceNumber + 1)
                    {
                        printf("It's an ack\n");
                        printf("Ack number %u\n\n", ackSegment.GetAckNumber());

                        duplicateAckSequenceNumber = ackSegment.GetAckNumber();
                        receivedAck = true;
                        numberOfDuplicateAcks = 0;

                        // Connection established, break out of the loop
                        break;
                    }
                }

                // Get the current timer value in milliseconds
                currentTimer = std::chrono::high_resolution_clock::now();

                std::chrono::duration<float, std::milli> timermiliSeconds =
                    currentTimer - startTimer;

                // If the timeout occurred
                if (timermiliSeconds.count() >= timeoutInterval_)
                {
                    // Resend the Sync Ack packet without bit errors or loss
                    connSocket_->Send(synAckSegment,
                            connSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                    printf("Re-Sending syn ack segment\n");
                    printf("Segment number %u\n",
                            synAckSegment.GetSequenceNumber());

                    printf("Ack number %u\n", synAckSegment.GetAckNumber());
                    printf("%u bytes long\n\n", synAckSegment.GetDataLength());

                    // Restart the timer
                    startTimer = std::chrono::high_resolution_clock::now();
                }
            }

            // Connection established, break out of the loop
            break;
        }
    }

    /********** E N D  C O N N E C T I O N  S E T U P  S E C T I O N *********/

    /*************** D A T A  T R A N S F E R  S E C T I O N *****************/

    // Delete the old socket.
    delete connSocket_;

    // Make it null
    connSocket_ = NULL;

    // Set up a new socket the uses the dataPort for this client.
    dataSocket_ = new SocketTCP(dataPort_);

    // Bind the data socket to it's address and port
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
        std::ofstream outputFile(receiveFileName, std::ios::binary);

        char byte = 0;
        bool queuedByte = false;

        // Mark the start time of the file transfer
        startTransfer = std::chrono::high_resolution_clock::now();

        uint32_t finSequenceNumber;

        // Loop until we receive a fin segment from the client
        while (!receivedFin)
        {
            printf("Timeout time: %f\n\n", timeoutInterval_);
            // Make a segment.
            Segment segment(MAX_FULL_SEGMENT_LEN);

            // Receive the segment from the client
            dataSocket_->Receive(segment);

            // Get the current timer value in milliseconds
            currentTimer = std::chrono::high_resolution_clock::now();

            std::chrono::duration<float, std::milli> timermiliSeconds =
                currentTimer - startTimer;

            printf("Received segment\n");
            printf("Segment number %u\n", segment.GetSequenceNumber());
            printf("%u bytes long\n", segment.GetDataLength());

            // If the segment is not corrupt
            if (segment.CalculateChecksum(0) == 0x0000)
            {
                currentClientRecvWindowSize = segment.GetReceiveWindow();

                // If the segment is an ack segment
                if (segment.GetAckFlag())
                {
                    receivedAck = true;

                    // If the ack is for the fin segment
                    if (segment.GetAckFlag() && segment.GetAckNumber() ==
                            finSequenceNumber + 1)
                    {
                        printf("It's an ack for the fin\n\n");
                        printf("Ack number %u\n", segment.GetAckNumber());
                        finAcked = true;
                    }

                    // If the ack isn't a duplicate
                    else if (segment.GetAckNumber() >
                            duplicateAckSequenceNumber)
                    {
                        printf("It's an ack\n");
                        printf("Ack number %u\n", segment.GetAckNumber());

                        printf("It's a new ack segment\n");

                        unsigned int tempNumberOfAckedBytes =
                            segment.GetAckNumber() -
                            duplicateAckSequenceNumber;

                        // Increase the number of acked bytes
                        numberOfAckedBytes += tempNumberOfAckedBytes;

                        // Decrease the number of unacked bytes in the
                        // window
                        numberOfUnackedBytes -= tempNumberOfAckedBytes;

                        // Pop the acked bytes from the front of the list
                        for (unsigned int i = 0; i < tempNumberOfAckedBytes
                                && !sendWindow_.empty(); i++)
                        {
                            sendWindow_.pop_front();
                        }

                        // Update the duplicate number
                        duplicateAckSequenceNumber =
                            segment.GetAckNumber();

                        // Reset the number of duplicates
                        numberOfDuplicateAcks = 0;
                    }
                    else
                    {
                        printf("It's a duplicate ack\n");
                        printf("Ack number %u\n", segment.GetAckNumber());

                        // Increase the number of duplicates
                        numberOfDuplicateAcks++;
                    }
                }

                // If the segment is a fin segment
                if (segment.GetFinFlag() && !receivedFin)
                {
                    printf("It's a fin\n\n");

                    receivedFin = true;

                    nextSendAckNumber = segment.GetSequenceNumber() + 1;

                    // Write the data in the receive window to the file
                    outputFile.write(recvWindow_.data(),
                            numberOfBytesInReceiveWindow);

                    printf("Emptying the recv buffer\n");

                    // Reset the receive window
                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                    numberOfBytesInReceiveWindow = 0;
                    recvTempNextPosition = 0;
                    recvNextPosition = 0;
                }

                // If the data in the segment is in order
                if (segment.GetSequenceNumber() == nextSendAckNumber)
                {
                    printf("It's data is in order\n");

                    // Extract the data from the segment and add it to the
                    // receive window
                    for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                    {
                        // Add a byte of data to the receive window
                        recvWindow_[recvNextPosition] = segment.GetData()[i];

                        recvWindowValid_[recvNextPosition] = true;

                        // Move the receive window
                        recvNextPosition++;
                    }

                    nextSendAckNumber += segment.GetDataLength();
                    numberOfBytesInReceiveWindow += segment.GetDataLength();

                    // If all holes have been closed
                    if (recvNextPosition > recvTempNextPosition)
                    {
                        currentServerRecvWindowSize -= segment.GetDataLength();
                    }

                    // If the byte after the byte that was just writing is
                    // valid, that means a hole was just closed
                    // Iterate until an invalid byte is found
                    while (recvWindowValid_[recvNextPosition] &&
                            ((recvNextPosition < recvTempNextPosition) ||
                            (recvNextPosition <
                            numberOfBytesInReceiveWindow)))
                    {
                        recvNextPosition++;
                        nextSendAckNumber++;
                    }
                }

                // If the data in the segment is out of order, but not data
                // that's already been received
                else if (segment.GetSequenceNumber() > nextSendAckNumber)
                {
                    printf("It's data is out of order\n");

                    recvTempNextPosition = recvNextPosition +
                        (segment.GetSequenceNumber() - nextSendAckNumber);

                    // Extract the data from the segment and add it to the
                    // receive window
                    for (unsigned int i = 0; i < segment.GetDataLength(); i++)
                    {
                        // Add a byte of data to the receive window
                        recvWindow_[recvTempNextPosition] =
                            segment.GetData()[i];

                        recvWindowValid_[recvTempNextPosition] = true;

                        // Move the receive window
                        recvTempNextPosition++;
                    }

                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE -
                        recvTempNextPosition;

                    if (ignoreLoss)
                    {
                        nextSendAckNumber += recvNextPosition -
                            recvTempNextPosition;

                        numberOfBytesInReceiveWindow += recvNextPosition -
                            recvTempNextPosition;

                        recvNextPosition = recvTempNextPosition;
                    }

                    else
                    {
                        numberOfBytesInReceiveWindow +=
                            segment.GetDataLength();
                    }
                }

                // If the receive buffer is full and there are no holes
                if (currentServerRecvWindowSize == 0 && recvNextPosition >=
                        recvTempNextPosition && !receivedFin)
                {
                    // Write the data in the receive window to the file
                    outputFile.write(recvWindow_.data(),
                            numberOfBytesInReceiveWindow);

                    // Make all bytes in the receive window invalid
                    for (unsigned int i = 0; i < MAX_RECV_WINDOW_SIZE; i++)
                    {
                        recvWindowValid_[i] = false;
                    }

                    printf("Emptying the recv buffer\n");

                    // Reset the receive window
                    currentServerRecvWindowSize = MAX_RECV_WINDOW_SIZE;
                    numberOfBytesInReceiveWindow = 0;
                    recvTempNextPosition = 0;
                    recvNextPosition = 0;
                }
                printf("currentServerRecvWindowSize: %u\n",
                        currentServerRecvWindowSize);

            }

            printf("\n");

            // If the transfer of the input file isn't complete
            if (numberOfAckedBytes < (unsigned int)fileSize)
            {
                printf("Unacked bytes: %u\n", numberOfUnackedBytes);

                // If the received ack wasn't a duplicate
                if (numberOfDuplicateAcks == 0 && receivedAck == true)
                {
                    // The length of the segment that will be sent
                    uint16_t segmentLength = segment.GetReceiveWindow();

                    // If the segment won't fit in the send buffer
                    if (numberOfUnackedBytes + MAX_SEGMENT_DATA_LEN >
                            currentServerSendWindowSize)
                    {
                        // Make it smaller so that it will fit
                        segmentLength = currentServerSendWindowSize -
                            numberOfUnackedBytes;
                    }

                    // A full size segment will fit
                    else
                    {
                        segmentLength = MAX_SEGMENT_DATA_LEN;
                    }

                    // If the segment length is longer than what the client can
                    // receive
                    if (segmentLength + numberOfUnackedBytes >
                            currentClientRecvWindowSize)
                    {
                        segmentLength = currentClientRecvWindowSize -
                            numberOfUnackedBytes;
                    }

                    if (segmentLength > 0)
                    {
                        unsigned int sequenceNumber =
                            startingServerSequenceNumber + numberOfUnackedBytes
                            + numberOfAckedBytes + 1;

                        // Make a segment.
                        Segment ackSegment(segmentLength + SEGMENT_HEADER_LEN,
                                dataPort_, clientPort, sequenceNumber,
                                nextSendAckNumber, false, true, false, false,
                                false, false, currentServerRecvWindowSize, 0,
                                0);

                        // Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<float, std::milli> currentTime =
                            startTransfer - startTimer;

                        // Populate the ack segment with new data from the file
                        for (;;)
                        {
                            // We have to make sure we have put every byte into
                            // a segment Check if we have a left-over byte...
                            // this means we are in a new segment
                            if (queuedByte)
                            {
                                ackSegment.AddByte(byte);
                                queuedByte = false;

                                // Add the byte to the back of the send window
                                sendWindow_.push_back({ byte, sequenceNumber,
                                        false, true, false, false, false, false,
                                        0, segmentLength, 0, currentTime.count()
                                        });
                            }

                            // Get a byte and try to put it into the packet.  If
                            // after this we have a queuedByte, this means we go
                            // to another packet.
                            if (inputFile.get(byte))
                            {
                                queuedByte = !ackSegment.AddByte(byte);

                                if (queuedByte)
                                {
                                    break;
                                }

                                // Add the byte to the back of the send window
                                sendWindow_.push_back({ byte, sequenceNumber,
                                        false, true, false, false, false, false,
                                        0, segmentLength, 0, currentTime.count()
                                        });
                            }

                            // If we can't get a byte, that means we got EOF;
                            // leave the loop.
                            else
                            {
                                break;
                            }
                        }

                        // Send the segment
                        dataSocket_->Send(ackSegment,
                                dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                        numberOfUnackedBytes += ackSegment.GetDataLength();
                        printf("Sending data segment\n");
                        printf("Segment number %u\n",
                                ackSegment.GetSequenceNumber());

                        printf("Ack number %u\n", ackSegment.GetAckNumber());
                        printf("RecvWindow: %u\n",
                                ackSegment.GetReceiveWindow());

                        printf("Unacked bytes: %u\n", numberOfUnackedBytes);
                        printf("%u bytes long\n\n", ackSegment.GetDataLength());
                    }
                }

                else if (!sendWindow_.empty())
                {
                    if (numberOfDuplicateAcks >= 3)
                    {
                        numberOfDuplicateAcks = 0;
                    }

                    // Populate the ack segment with old data

                    // Make a list iterator that starts at the begining of the
                    // send buffer
                    auto it = sendWindow_.begin();

                    uint16_t segmentLength = it->dataLength;

                    // Remake the segment
                    Segment resendSegment(segmentLength + SEGMENT_HEADER_LEN,
                            dataPort_, clientPort, it->sequenceNumber,
                            nextSendAckNumber, it->urg, it->ack, it->psh,
                            it->rst, it->syn, it->fin,
                            currentServerRecvWindowSize, it->urgDataPointer,
                            it->options);

                    // Populate the segment with all of it's bytes
                    for (unsigned int i = 0; i < segmentLength; i++)
                    {
                        // Get and add the byte it to the segment
                        resendSegment.AddByte(it->byte);

                        it++;
                    }

                    // Re-send the segment
                    dataSocket_->Send(resendSegment,
                            dataSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                    printf("Re-Sending data segment\n");
                    printf("Segment number %u\n",
                            resendSegment.GetSequenceNumber());

                    printf("Ack number %u\n", resendSegment.GetAckNumber());
                    printf("RecvWindow: %u\n",
                            resendSegment.GetReceiveWindow());

                    printf("%u bytes long\n\n", resendSegment.GetDataLength());
                }
            }

            else
            {
                // If the file transfer is complete and the fin segment hasn't
                // acked
                if (numberOfAckedBytes >= (unsigned int)fileSize && !finAcked)
                {
                    finSequenceNumber = startingServerSequenceNumber +
                        numberOfUnackedBytes + numberOfAckedBytes + 1;

                    // Make a fin ack segment
                    Segment finAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_,
                            clientPort, startingServerSequenceNumber +
                            numberOfUnackedBytes + numberOfAckedBytes + 1,
                            nextSendAckNumber, false, true, false, false,
                            false, true, currentServerRecvWindowSize, 0, 0);

                    // Send the fin ack segment to the client
                    dataSocket_->Send(finAckSegment,
                            dataSocket_->GetRemoteAddress(), clientPort,
                            bitErrorPercent, segmentLoss);

                    printf("Sending fin segment\n");
                    printf("Segment number %u\n",
                            finAckSegment.GetSequenceNumber());

                    printf("Ack number %u\n", finAckSegment.GetAckNumber());
                    printf("RecvWindow: %u\n",
                            finAckSegment.GetReceiveWindow());

                    printf("%u bytes long\n\n", finAckSegment.GetDataLength());
                }

                else
                {
                    // Make an empty ack segment
                    Segment emptyAckSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_,
                            clientPort, 0, nextSendAckNumber, false, true,
                            false, false, false, false,
                            currentServerRecvWindowSize, 0, 0);

                    // Send the ack packet to the client
                    if (!receivedFin)
                    {
                        // Send the ack packet to the client
                        dataSocket_->Send(emptyAckSegment,
                                dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);
                    }

                    else
                    {
                        // Send the ack packet to the client
                        dataSocket_->Send(emptyAckSegment,
                                dataSocket_->GetRemoteAddress(), clientPort, 0,
                                0);
                    }

                    printf("Sending ack segment\n");
                    printf("Ack number %u\n", emptyAckSegment.GetAckNumber());
                    printf("RecvWindow: %u\n",
                            emptyAckSegment.GetReceiveWindow());

                    printf("%u bytes long\n\n",
                            emptyAckSegment.GetDataLength());
                }
            }

            receivedAck = false;
        }

        outputFile.close();

        if (!finAcked)
        {
            while (true)
            {
                // If the input file hasn't been fully transferred
                if (numberOfAckedBytes < (unsigned int)fileSize)
                {
                    // The ring buffer isn't full
                    if ((numberOfUnackedBytes < currentServerSendWindowSize)
                        && numberOfUnackedBytes < (unsigned int)fileSize
                        && numberOfUnackedBytes < currentClientRecvWindowSize)
                    {
                        printf("Timeout time: %f\n\n", timeoutInterval_);
                        printf("Unacked bytes: %u\n", numberOfUnackedBytes);

                        // The length of the segment that will be sent
                        uint16_t segmentLength;

                        // If the segment won't fit in the send buffer
                        if (numberOfUnackedBytes + MAX_SEGMENT_DATA_LEN >
                                currentServerSendWindowSize)
                        {
                            // Make it smaller so that it will fit
                            segmentLength = currentServerSendWindowSize -
                                numberOfUnackedBytes;
                        }

                        // A fill size segment will fit
                        else
                        {
                            segmentLength = MAX_SEGMENT_DATA_LEN;
                        }

                        // If the segment length is longer than what the client
                        // can receive
                        if (segmentLength + numberOfUnackedBytes >
                                currentClientRecvWindowSize)
                        {
                            segmentLength = currentClientRecvWindowSize -
                                numberOfUnackedBytes;
                        }

                        if (segmentLength > 0)
                        {
                            unsigned int sequenceNumber =
                                startingServerSequenceNumber +
                                numberOfUnackedBytes + numberOfAckedBytes + 1;

                            // Make a segment.
                            Segment segment(segmentLength + SEGMENT_HEADER_LEN,
                                    dataPort_, clientPort, sequenceNumber, 0,
                                    false, false, false, false, false, false,
                                    currentServerRecvWindowSize, 0, 0);

                            // Mark the start time of the timer
                            startTimer =
                                std::chrono::high_resolution_clock::now();

                            std::chrono::duration<float, std::milli>
                                currentTime = startTransfer - startTimer;

                            for (;;)
                            {
                                // We have to make sure we have put every byte
                                // into a segment Check if we have a left-over
                                // byte... this means we are in a new segment
                                if (queuedByte)
                                {
                                    segment.AddByte(byte);
                                    queuedByte = false;

                                    // Add the byte to the back of the send
                                    // window
                                    sendWindow_.push_back({ byte,
                                            sequenceNumber, false, false,
                                            false, false, false, false, 0,
                                            segmentLength, 0,
                                            currentTime.count() });
                                }

                                // Get a byte and try to put it into the
                                // packet.  If after this we have a queuedByte,
                                // this means we go to another packet.
                                if (inputFile.get(byte))
                                {
                                    queuedByte = !segment.AddByte(byte);

                                    if (queuedByte)
                                    {
                                        break;
                                    }

                                    // Add the byte to the back of the send
                                    // window
                                    sendWindow_.push_back({ byte,
                                            sequenceNumber, false, false,
                                            false, false, false, false, 0,
                                            segmentLength, 0,
                                            currentTime.count() });
                                }

                                // If we can't get a byte, that means we got
                                // EOF; leave the loop.
                                else
                                {
                                    break;
                                }
                            }

                            // Send the segment
                            dataSocket_->Send(segment,
                                    dataSocket_->GetRemoteAddress(),
                                    clientPort, bitErrorPercent, segmentLoss);

                            numberOfUnackedBytes += segment.GetDataLength();

                            printf("Sending data segment\n");
                            printf("Segment number %u\n",
                                    segment.GetSequenceNumber());

                            printf("RecvWindow: %u\n",
                                    segment.GetReceiveWindow());

                            printf("Unacked bytes: %u\n",
                                    numberOfUnackedBytes);

                            printf("%u bytes long\n\n",
                                    segment.GetDataLength());
                        }
                    }

                    if (!sendWindow_.empty())
                    {
                        // Get the current timer value in milliseconds
                        currentTimer =
                            std::chrono::high_resolution_clock::now();

                        std::chrono::duration<float, std::milli> currentTime =
                            currentTimer - startTransfer;

                        printf("Current time: %f\n\n", currentTime.count() -
                                sendWindow_.begin()->timeSent);

                        // If a timeout has occured, or three duplicate acks
                        // arrived
                        if (currentTime.count() - sendWindow_.begin()->timeSent
                                >= timeoutInterval_ || numberOfDuplicateAcks >=
                                3)
                        {
                            printf("Timeout time: %f\n\n", timeoutInterval_);

                            // Make a list iterator that starts at the begining
                            // of the send buffer
                            auto it = sendWindow_.begin();

                            uint16_t segmentLength = it->dataLength;

                            // Remake the segment
                            Segment resendSegment(segmentLength +
                                    SEGMENT_HEADER_LEN, dataPort_, clientPort,
                                    it->sequenceNumber, nextSendAckNumber,
                                    it->urg, it->ack, it->psh, it->rst,
                                    it->syn, it->fin,
                                    currentServerRecvWindowSize,
                                    it->urgDataPointer, it->options);

                            // Populate the segment with all of it's bytes
                            for (unsigned int i = 0; i < segmentLength; i++)
                            {
                                // Get and add the byte it to the segment
                                resendSegment.AddByte(it->byte);

                                it->timeSent = currentTime.count();

                                it++;
                            }

                            // Only recalculate the timeout interval if a
                            // timeout occured
                            if (numberOfDuplicateAcks > 3)
                            {
                                numberOfDuplicateAcks = 0;
                            }

                            // The slow start threashhold becomes half of the
                            // current client window size
                            ssthresh = currentServerSendWindowSize / 2;

                            // The client window size gets reset back to one
                            // full segment.
                            currentServerSendWindowSize = MAX_SEGMENT_DATA_LEN;

                            // Resend the segment
                            dataSocket_->Send(resendSegment,
                                    dataSocket_->GetRemoteAddress(),
                                    clientPort, bitErrorPercent, segmentLoss);

                            printf("Re-Sending data segment\n");
                            printf("Segment number %u\n",
                                    resendSegment.GetSequenceNumber());

                            printf("%u bytes long\n\n",
                                    resendSegment.GetDataLength());
                        }

                    }

                    // A packet has arrived
                    if (dataSocket_->CheckReceive())
                    {
                        printf("Timeout time: %f\n\n", timeoutInterval_);

                        // Get the current timer value in milliseconds
                        currentTimer =
                            std::chrono::high_resolution_clock::now();

                        std::chrono::duration<float, std::milli> currentTime =
                            currentTimer - startTransfer ;

                        float rttSample = currentTime.count() -
                            sendWindow_.begin()->timeSent;

                        // Make a segment.
                        Segment ackSegment(MAX_FULL_SEGMENT_LEN);

                        // Receive the segment from the server
                        dataSocket_->Receive(ackSegment);

                        printf("Received segment\n");
                        printf("Segment number %u\n",
                                ackSegment.GetSequenceNumber());

                        printf("%u bytes long\n", ackSegment.GetDataLength());

                        // If the segment is not corrupt
                        if (ackSegment.CalculateChecksum(0) == 0x0000)
                        {
                            currentClientRecvWindowSize =
                                ackSegment.GetReceiveWindow();

                            // If the segment is an ack segment
                            if (ackSegment.GetAckFlag())
                            {
                                // If the ack isn't a duplicate
                                if (ackSegment.GetAckNumber() >
                                        duplicateAckSequenceNumber)
                                {
                                    printf("It's an ack\n");
                                    printf("Ack number %u\n",
                                            ackSegment.GetAckNumber());

                                    printf("It's a new ack segment\n");
                                    printf("duplicateAckSequenceNumber: %u\n",
                                            duplicateAckSequenceNumber);

                                    unsigned int tempNumberOfAckedBytes =
                                        ackSegment.GetAckNumber() -
                                        duplicateAckSequenceNumber;

                                    // Increase the number of acked bytes
                                    numberOfAckedBytes +=
                                        tempNumberOfAckedBytes;

                                    printf("Bytes acked: %u\n",
                                            tempNumberOfAckedBytes);

                                    printf("total bytes acked: %u\n",
                                            numberOfAckedBytes);

                                    printf("File Size: %u\n",
                                            (unsigned int)fileSize);

                                    printf("Unacked bytes: %u\n",
                                            numberOfUnackedBytes);

                                    printf("currentServerSendWindowSize: %u\n",
                                            currentServerSendWindowSize);

                                    printf("currentClientRecvWindowSize: %u\n",
                                            currentClientRecvWindowSize);

                                    // Decrease the number of unacked bytes in
                                    // the window
                                    numberOfUnackedBytes -=
                                        tempNumberOfAckedBytes;

                                    // Increase the windowsize

                                    // If the window size is currently smaller
                                    // than the max window size
                                    if (currentServerSendWindowSize <
                                            maxServerSendWindowSize)
                                    {
                                        // If we are in CA mode
                                        if (currentServerSendWindowSize >=
                                                ssthresh)
                                        {
                                            float curWinSizeFloat =
                                                static_cast<float>
                                                (currentServerSendWindowSize);

                                            float windowSizeScale =
                                                (float)MAX_FULL_SEGMENT_LEN /
                                                curWinSizeFloat;

                                            tempSendWindowSize +=
                                                MAX_FULL_SEGMENT_LEN *
                                                windowSizeScale;

                                            if (tempSendWindowSize >=
                                                    MAX_FULL_SEGMENT_LEN)
                                            {
                                                currentServerSendWindowSize +=
                                                    MAX_FULL_SEGMENT_LEN;
                                                tempSendWindowSize = 0;
                                            }
                                        }

                                        // Must be in SS mode
                                        else
                                        {
                                            currentServerSendWindowSize +=
                                                MAX_FULL_SEGMENT_LEN;

                                            // If we've gone from SS to CA mode
                                            if (currentServerSendWindowSize >
                                                    ssthresh)
                                            {
                                                currentServerSendWindowSize =
                                                    ssthresh;
                                            }
                                        }

                                        // If the new server window size is
                                        // greater than the max set by the
                                        // client
                                        if (currentServerSendWindowSize >
                                                maxServerSendWindowSize)
                                        {
                                            currentServerSendWindowSize =
                                                maxServerSendWindowSize;
                                        }
                                    }

                                    printf("currentServerSendWindowSize: %u\n",
                                            currentServerSendWindowSize);

                                    // Pop the acked bytes from the send window

                                    for (unsigned int i = 0; i <
                                            tempNumberOfAckedBytes &&
                                            !sendWindow_.empty(); i++)
                                    {
                                        sendWindow_.pop_front();
                                    }

                                    // Update the duplicate number
                                    duplicateAckSequenceNumber =
                                        ackSegment.GetAckNumber();

                                    // Reset the number of duplicates
                                    numberOfDuplicateAcks = 0;
                                }
                                else
                                {
                                    printf("It's a duplicate ack segment\n");
                                    printf("Ack number %u\n",
                                            ackSegment.GetAckNumber());

                                    // Increase the number of duplicates
                                    numberOfDuplicateAcks++;
                                }
                            }
                        }

                        // If the file transfer is complete
                        if (numberOfAckedBytes >= (unsigned int)fileSize)
                        {
                            finSequenceNumber = startingServerSequenceNumber +
                                numberOfUnackedBytes + numberOfAckedBytes + 1;

                            // Make a fin segment
                            Segment finSegment(MAX_EMPTY_SEGMENT_LEN,
                                    dataPort_, clientPort, finSequenceNumber,
                                    0, false, false, false, false, false, true,
                                    currentServerRecvWindowSize, 0, 0);

                            // Send the fin segment to the client
                            dataSocket_->Send(finSegment,
                                    dataSocket_->GetRemoteAddress(),
                                    clientPort, bitErrorPercent, segmentLoss);

                            printf("Sending fin segment\n");
                            printf("Segment number %u\n",
                                    finSegment.GetSequenceNumber());
                            printf("%u bytes long\n\n",
                                    finSegment.GetDataLength());
                        }

                        // Recalculate the estimated RTT value
                        estimatedRtt_ = ((1 - ALPHA)*estimatedRtt_)
                            + (ALPHA * rttSample);

                        // Recalculate the RTT deviation value
                        devRtt_ = ((1 - BETA)*devRtt_) +
                            (BETA * (fabs(rttSample - estimatedRtt_)));

                        // Recalculate the Timeout value
                        timeoutInterval_ = estimatedRtt_ + (4 * devRtt_);

                        // Mark the start time of the timer
                        startTimer = std::chrono::high_resolution_clock::now();

                        printf("\n");
                    }
                }

                // Wait for the fin segment to be acked
                else
                {
                    // Get the current timer value in milliseconds
                    currentTimer = std::chrono::high_resolution_clock::now();

                    std::chrono::duration<float, std::milli> timermiliSeconds =
                        currentTimer - startTimer;

                    // If a timeout has occured
                    if (timermiliSeconds.count() >= timeoutInterval_)
                    {
                        printf("Timeout time: %f\n\n", timeoutInterval_);
                        // Make a fin segment
                        Segment finSegment(MAX_EMPTY_SEGMENT_LEN, dataPort_,
                                clientPort, finSequenceNumber, 0, false, false,
                                false, false, false, true,
                                currentServerRecvWindowSize, 0, 0);

                        // Send the fin segment to the client
                        dataSocket_->Send(finSegment,
                                dataSocket_->GetRemoteAddress(), clientPort,
                                bitErrorPercent, segmentLoss);

                        printf("Re-Sending fin segment\n");
                        printf("Segment number %u\n",
                                finSegment.GetSequenceNumber());

                        printf("%u bytes long\n\n",
                                finSegment.GetDataLength());

                        // Set the starting value of the timer
                        startTimer = std::chrono::high_resolution_clock::now();
                    }

                    // A packet has arrived
                    if (dataSocket_->CheckReceive())
                    {
                        // Make a segment.
                        Segment segment(MAX_FULL_SEGMENT_LEN);

                        // Receive the segment from the client
                        dataSocket_->Receive(segment);

                        printf("Received segment\n");
                        printf("Segment number %u\n",
                                segment.GetSequenceNumber());

                        printf("%u bytes long\n", segment.GetDataLength());
                        printf("Ack number %u\n", segment.GetAckNumber());

                        // If the segment is not corrupt
                        if (segment.CalculateChecksum(0) == 0x0000)
                        {
                            // If the segment has an ack for the fin segment
                            if (segment.GetAckFlag() && segment.GetAckNumber()
                                    == finSequenceNumber+1)
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

        // Mark the end time of the file transfer
        finishTransfer = std::chrono::high_resolution_clock::now();

        inputFile.close();

        // Delete the data socket.
        delete dataSocket_;

        dataSocket_ = NULL;

        // Get the time it took to send the file in milliseconds
        std::chrono::duration<float, std::milli> miliSeconds = finishTransfer -
            startTransfer;

        printf("Percent of segments with bit errors: %u%%\n", bitErrorPercent);

        printf("Percent of segments lost: %u%%\n", segmentLoss);

        printf("Time for the client to transfer the file in milliseconds: ");
        printf("%g\n", miliSeconds.count());
    }

    catch (std::runtime_error& e)
    {
        throw e;
    }
}

bool socksahoy::TcpServer::FileExists(const std::string& fileName) const
{
    struct stat fileBuffer;
    int exists = stat(fileName.c_str(), &fileBuffer);

    return (exists == 0);
}

// vim: set expandtab ts=4 sw=4: