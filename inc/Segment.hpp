/**
* \file Segment.hpp
* \details Segment class
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date April 10, 2019
*/

#ifndef __SEGMENT_HPP__
#define __SEGMENT_HPP__

/* C++ Standard Template Library headers */
#include <array>
#include <iterator>
#include <iostream>
#include <random>

#include <cstring>

/* Phase 2 Socket library headers */
#include "Constants.hpp"
#include "SocketTCP.hpp"

namespace socksahoy
{
    /**
    * \class Segment
    * \brief Struct representing a segment holding binary data.
    */
    template <std::size_t Size> class Segment
    {
        /// Segment is friends with SocketTCP so SocketTCP can use Segment members.
        friend class SocketTCP;

        public:

        /**
        * \brief Constructor
        * \details Creates a new packet.
        * \param sourcePortNumber the source port number of the segment
        * \param packets The total number of packets that will be sent.
        */
        Segment(uint16_t sourcePortNumber, uint16_t destPortNumber, uint32_t sequenceNumber,
               uint32_t ackNumber, bool urg, bool ack, bool psh, bool rst, bool syn, bool fin, 
               uint16_t rwnd, uint16_t urg_data_pointer, uint16_t options)
        {
            sourcePortNumber_ = sourcePortNumber;
            destPortNumber_ = destPortNumber;
            sequenceNumber_ = sequenceNumber;
            ackNumber_ = ackNumber;
            headerLength_ = SEGMENT_HEADER_LEN;
            urg_ = urg;
            ack_ = ack;
            psh_ = psh;
            rst_ = rst;
            syn_ = syn;
            fin_ = fin;
            rwnd_ = rwnd;
            checksum_ = 0;
            urgDataPointer_ = urg_data_pointer;
            dataLength_ = 0;
            options_ = options;
        }

        Segment()
        {
            sourcePortNumber_ = 0;
            destPortNumber_ = 0;
            sequenceNumber_ = 0;
            ackNumber_ = 0;
            headerLength_ = 0;
            urg_ = false;
            ack_ = false;
            psh_ = false;
            rst_ = false;
            syn_ = false;
            fin_ = false;
            rwnd_ = 0;
            checksum_ = 0;
            urgDataPointer_ = 0;
            options_ = 0;
        }

        /**
        * \brief Gets and returns the source port number of the segment.
        * \details Wrapper around the sourcePortNumber_ variable.
        * \return The source port number of the segment.
        */
        uint16_t GetSourcePortNumber()
        {
            return sourcePortNumber_;
        }

        /**
        * \brief Gets and returns the destination port number of the segment.
        * \details Wrapper around the destPortNumber_ variable.
        * \return The destination port number of the segment.
        */
        uint16_t GetDestPortNumber()
        {
            return destPortNumber_;
        }

        /**
        * \brief Gets and returns the sequence number of the segment.
        * \details Wrapper around the sequenceNumber_ variable.
        * \return The sequence number of the segment.
        */
        uint32_t GetSequenceNumber()
        {
            return sequenceNumber_;
        }

        /**
        * \brief Gets and returns the acknowledgement number of the segment.
        * \details Wrapper around the ackNumber_ variable.
        * \return The acknowledgement number of the segment.
        */
        uint32_t GetAckNumber()
        {
            return ackNumber_;
        }

        /**
        * \brief Gets and returns the header length of the segment.
        * \details Wrapper around the headerLength_ variable.
        * \return The header length of the segment.
        */
        uint8_t GetHeaderLength()
        {
            return headerLength_;
        }

        /**
        * \brief Gets and returns the urgent data flag of the segment.
        * \details Wrapper around the urg_ variable.
        * \return The urgent data flag of the segment.
        */
        bool GetUrgentDataFlag()
        {
            return urg_;
        }

        /**
        * \brief Gets and returns the acknowledgement flag of the segment.
        * \details Wrapper around the ack_ variable.
        * \return The acknowledgement flag of the segment.
        */
        bool GetAckFlag()
        {
            return ack_;
        }

        /**
        * \brief Gets and returns the push flag of the segment.
        * \details Wrapper around the psh_ variable.
        * \return The push flag of the segment.
        */
        bool GetPushFlag()
        {
            return psh_;
        }

        /**
        * \brief Gets and returns the reset flag of the segment.
        * \details Wrapper around the rst_ variable.
        * \return The reset flag of the segment.
        */
        bool GetResetFlag()
        {
            return rst_;
        }

        /**
        * \brief Gets and returns the sync flag of the segment.
        * \details Wrapper around the syn_ variable.
        * \return The sync flag of the segment.
        */
        bool GetSyncFlag()
        {
            return syn_;
        }

        /**
        * \brief Gets and returns the finish flag of the segment.
        * \details Wrapper around the fin_ variable.
        * \return The finish flag of the segment.
        */
        bool GetFinFlag()
        {
            return fin_;
        }
        
        /**
        * \brief Gets and returns the receive window of the segment.
        * \details Wrapper around the rwnd_ variable.
        * \return The receive window of the segment.
        */
        uint16_t GetReceiveWindow()
        {
            return rwnd_;
        }

        /**
        * \brief Gets the checksum value of the segment.
        * \details Wrapper around the checksum_ variable.
        * \return The checksum value.
        */
        uint16_t GetChecksum()
        {
            return checksum_;
        }

        /**
        * \brief Gets the urgent data pointer value of the segment.
        * \details Wrapper around the urg_data_pointer_ variable.
        * \return The urgent data pointer value.
        */
        uint16_t GetUrgentDataPointer()
        {
            return urgDataPointer_;
        }

        /**
        * \brief Gets the length of the payload of the segment in bytes.
        * \details Wrapper around the data_length_ variable.
        * \return The length of the payload.
        */
        uint16_t GetDataLength()
        {
            return dataLength_;
        }

        /**
        * \brief Gets the options field of the segment.
        * \details Wrapper around the data_length_ variable.
        * \return The options field.
        */
        uint16_t GetOptions()
        {
            return options_;
        }

        /**
        * \brief Access the payload of the segment.
        * \return Pointer to the payload of the segment.
        */
        const char* GetData() const
        {
            //Is the segment not empty?
            if (dataLength_ > 0)
            {
                // Return a pointer to the data segment of the byte array
                return static_cast<const char*>(&data_[SEGMENT_HEADER_LEN]);
            }

            // The packet must be empty
            // Return a null pointer
            return nullptr;
        }

        /**
        * \brief Gets and returns the current size of the data array.
        * \details
        * \return The current size of the data array
        */
        uint32_t GetDataArraySize()
        {
            return data_.max_size();
        }

        /**
        * \brief Calculates the checksum value of the segment, packet it into the data array, and returns it.
        * \param bitErrorPercent Percent of segments that will have bit errors.
        * \return The calculated checksum value.
        */
        uint16_t CalculateChecksum(int bitErrorPercent)
        {
            checksum_ = 0;

            //Loop counter
            unsigned int i = 0;

            //The two 8 bit values concatinated into a 16 bit value
            uint16_t tempval = 0;

            // Add 8 bit values from the packet to checksum_ until there
            // aren't anymore 8 bit values
            for (i = 0; i < (dataLength_ + SEGMENT_HEADER_LEN-1); i+=2)
            {
                //Concatinate the next two bytes into a 16 bit value
                tempval = (data_[i] << 8) | (uint8_t)data_[i+1];

                //Add the 16 bit value to tempsum
                checksum_ += tempval;
            }

            //If the last byte wasn't added
            if (i == dataLength_ + SEGMENT_HEADER_LEN)
            {
                //Make the last byte the upper half of a 16 bit value
                tempval = (data_[i] << 8) | 0x00;

                //Add the 16 bit value to tempsum
                checksum_ += tempval;
            }

            // Induce bit errors
            // If bitErrorPercent <= 0, no bit errors should occur
            if (bitErrorPercent > 0)
            {
                // Random number engine and distribution
                // Distribution in range [1, 100]
                std::random_device dev;
                std::mt19937 rng(dev());

                using distType = std::mt19937::result_type;
                using dist = std::uniform_int_distribution<distType>;
                dist uniformDist(1, 100);

                int random_number = uniformDist(rng);

                // Check the random number aganst the error percent to see if
                // this packet will have bit errors
                if (bitErrorPercent >= random_number)
                {
                    //Simuate bit errors by sending an incorrect checksum value
                    checksum_++;
                }
            }

            // Perform a one's complement on checksum.
            checksum_ ^= 0xFFFF;
                       
            // The checksum value is stored in the 17th and 18th bytes
            data_[16] = (checksum_ & 0xFF00) >> 8;
            data_[17] = checksum_ & 0xFF;

            //The checksum value should be all ones on the receiver side
            return checksum_;
        }

        /**
        * \brief Adds a byte to the payload of the segment
        * \param byte The byte to add to the segment.
        * \return A bool value indicating if the add succeeded
        */
        bool AddByte(char byte)
        {
            //If the packet not full, and not a ack packet?
            if (dataLength_ < (GetMaxPacketSize() - SEGMENT_HEADER_LEN))
            {
                //Add a byte to it
                data_[dataLength_ + SEGMENT_HEADER_LEN] = byte;
                dataLength_++;
                //Indicate that the add succeeded
                return true;
            }

            //The packet must be full
            //Indicate that the add failed
            return false;
        }

        private:

        /**
        * \brief Packs the contents of the segment header into the byte array.
        * \details
        */
        void Serialize()
        {
            // The source port number is stored in the first 2 bytes of the data array
            data_[0] = (sourcePortNumber_ & 0xFF00) >> 8;
            data_[1] = sourcePortNumber_ & 0xFF;

            // The destination port number is stored in the 3rd and 4th bytes of the data array
            data_[2] = (destPortNumber_ & 0xFF00) >> 8;
            data_[3] = destPortNumber_ & 0xFF;

            // The sequence number is stored in the 5th, 6th, 7th, and 8th bytes of the data array
            data_[4] = (sequenceNumber_ & 0xFF000000) >> 24;
            data_[5] = (sequenceNumber_ & 0x00FF0000) >> 16;
            data_[6] = (sequenceNumber_ & 0x0000FF00) >> 8;
            data_[7] = (sequenceNumber_ & 0x000000FF);

            // The acknowledgement number is stored in the 9th, 10th, 11th, and 12th bytes of the data array
            data_[8] = (ackNumber_ & 0xFF000000) >> 24;
            data_[9] = (ackNumber_ & 0x00FF0000) >> 16;
            data_[10] = (ackNumber_ & 0x0000FF00) >> 8;
            data_[11] = (ackNumber_ & 0x000000FF);

            // The header length is the 13th byte
            data_[12] = headerLength_;

            // The flags are stored in the 14th byte
            data_[13] = 0x00;
            data_[13] = (urg_ << 5) || data_[13];
            data_[13] = (ack_ << 4) || data_[13];
            data_[13] = (psh_ << 3) || data_[13];
            data_[13] = (rst_ << 2) || data_[13];
            data_[13] = (syn_ << 1) || data_[13];
            data_[13] = (fin_ << 0) || data_[13];

            // The receive window is stored in the 15th and 16th bytes
            data_[14] = (rwnd_ & 0xFF00) >> 8;
            data_[15] = rwnd_ & 0xFF;

            // The checksum value is stored in the 17th and 18th bytes
            data_[16] = (checksum_ & 0xFF00) >> 8;
            data_[17] = checksum_ & 0xFF;

            // The urgent data pointer is stored in the 19th and 20th bytes
            data_[18] = (urgDataPointer_ & 0xFF00) >> 8;
            data_[19] = urgDataPointer_ & 0xFF;

            // The length of the data is stored in the 21st and 22nd bytes
            data_[20] = (dataLength_ & 0xFF00) >> 8;
            data_[21] = dataLength_ & 0xFF;

            // The options field is stored in the 23rd and 24th bytes
            data_[22] = (options_ & 0xFF00) >> 8;
            data_[23] = options_ & 0xFF;
        }

        /**
        * \brief Unpacks the contents of the byte array into the segment header.
        */
        void
            Deserialize()
            {
                // The source port number is stored in the first 2 bytes of the data array
                sourcePortNumber_ = data_[0] << 8;
                sourcePortNumber_ |= (uint8_t)data_[1];

                // The destination port number is stored in the 3rd and 4th bytes of the data array
                destPortNumber_ = data_[2] << 8;
                destPortNumber_ |= (uint8_t)data_[3];

                // The sequence number is stored in the 5th, 6th, 7th, and 8th bytes of the data array
                sequenceNumber_ = data_[4] << 24;
                sequenceNumber_ |= data_[5] << 16;
                sequenceNumber_ |= data_[6] << 8;
                sequenceNumber_ |= (uint8_t)data_[7];

                // The acknowledgement number is stored in the 9th, 10th, 11th, and 12th bytes of the data array
                ackNumber_ = data_[8] << 24;
                ackNumber_ |= data_[9] << 16;
                ackNumber_ |= data_[10] << 8;
                ackNumber_ |= (uint8_t)data_[11];

                // The header length is the 13th byte
                headerLength_ = data_[12];

                // The flags are stored in the 14th byte
                urg_ = (data_[13] >> 5) & 0x01;
                ack_ = (data_[13] >> 4) & 0x01;
                psh_ = (data_[13] >> 3) & 0x01;
                rst_ = (data_[13] >> 2) & 0x01;
                syn_ = (data_[13] >> 1) & 0x01;
                fin_ = data_[13] & 0x01;

                // The receive window is stored in the 15th and 16th bytes
                rwnd_ = data_[14] << 8;
                rwnd_ |= (uint8_t)data_[15];

                // The checksum is stored in the 17th and 18th bytes
                checksum_ = data_[16] << 8;
                checksum_ |= (uint8_t)data_[17];

                // The urgent data pointer is stored in the 19th and 20th bytes
                urgDataPointer_ = data_[18] << 8;
                urgDataPointer_ |= (uint8_t)data_[19];

                // The length of the data is stored in the 21st and 22nd bytes
                dataLength_ = data_[20] << 8;
                dataLength_ |= (uint8_t)data_[21];

                // The options field is stored in the 23rd and 24th bytes
                options_ = data_[22] << 8;
                options_ |= (uint8_t)data_[23];
            }

        /**
        * \brief Returns a pointer to the begining of the byte array.
        * \return Pointer to the begining of the packet's byte array.
        */
        char* GetPacket()
        {
            // Return a pointer to the byte array so that it can be used
            return static_cast<char*>(&data_[0]);
        }

        uint16_t sourcePortNumber_;     ///< Number of the source port of the segment.
        uint16_t destPortNumber_;       ///< Number of the destination port of the segment.
        uint32_t sequenceNumber_;       ///< Number of the first byte of data that is in the segment.
        uint32_t ackNumber_;             ///< Number of the next byte of data that the sender is waiting for.
        uint8_t headerLength_;           ///< The length of the header in bytes.
        bool urg_;                      ///< The urgent data flag
        bool ack_;                      ///< The ack flag
        bool psh_;                      ///< The push data now flag
        bool rst_;                      ///< The Connection Reset flag
        bool syn_;                      ///< The Connection Sync flag
        bool fin_;                      ///< The Connection Fin flag
        uint16_t rwnd_;                 ///< Number of bytes left open in the recieve window.
        uint16_t checksum_;             ///< Standard UDP checksum.
        uint16_t urgDataPointer_;     ///< A pointer to the location of the urgent data in the segment.
        uint16_t dataLength_;           ///< The length of the data, in bytes, in the segment.
        uint16_t options_;              ///< The options field.
        std::array<char, Size> data_;   ///< The data array.
    };
}

#endif /* End of Packet.hpp */

// vim: set expandtab ts=4 sw=4: