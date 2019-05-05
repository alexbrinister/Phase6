/**
* \file Segment.hpp
* \details Segment class - declarations
* \author Alex Brinister
* \author Colin Rockwood
* \author Yonatan Genao Baez
* \date May 4, 2019
*/

#pragma once

/* C++ Standard Template Library headers */
#include <array>
#include <iterator>
#include <iostream>
#include <random>
#include <cstring>

/* Phase 6 Socket library headers */
#include "Constants.hpp"
#include "SocketTCP.hpp"

namespace socksahoy
{
    /**
    * \class Segment
    * \brief Class representing a segment holding binary data.
    */
    class Segment
    {
        /// SocketTCP can use Segment private members through friendship.
        friend class SocketTCP;

        public:

        /**
        * \brief Constructor
        * \details Creates a new segement.
        * \param size The size of the segment
        * \param sourcePortNumber The source port of the segment
        * \param destPortNumber The destination port of the segment
        * \param sequenceNumber The sequence number of the segment
        * \param ackNumber The ack number of the segment
        * \param urg The urgent flag of the segment
        * \param ack The acknowledgment flag of the segment
        * \param psh The push flag of the segment
        * \param rst The reset flag of the segment
        * \param syn The sync flag of the segment
        * \param fin The finish flag of the segment
        * \param rwnd The receive window size number of the segment's sender
        * \param urgDataPtr The urgent data pointer of the segment
        * \param options The options field of the segment
        */
        Segment(size_t size, uint16_t sourcePortNumber, uint16_t destPortNumber,
                uint32_t sequenceNumber, uint32_t ackNumber, bool urg, bool ack,
                bool psh, bool rst, bool syn, bool fin, uint16_t rwnd,
                uint16_t urgDataPtr, uint16_t options)
            : sourcePortNumber_(sourcePortNumber),
            destPortNumber_(destPortNumber), sequenceNumber_(sequenceNumber),
            ackNumber_(ackNumber), headerLength_(SEGMENT_HEADER_LEN), urg_(urg),
            ack_(ack), psh_(psh), rst_(rst), syn_(syn), fin_(fin), rwnd_(rwnd),
            checksum_(0), urgDataPointer_(urgDataPtr), dataLength_(0),
            options_(options), vectorSize_(size)
        {
            data_.resize(vectorSize_);
        }


        /**
        * \brief Constructor
        * \details Creates a new, empty segement or recieveing.
        * \param size The size of the segment
        */
        Segment(std::size_t size)
            : sourcePortNumber_(0), destPortNumber_(0), sequenceNumber_(0),
            ackNumber_(0), headerLength_(0), urg_(false), ack_(false),
            psh_(false), rst_(false), syn_(false), fin_(false), rwnd_(0),
            checksum_(0), urgDataPointer_(0), dataLength_(0), options_(0),
            vectorSize_(size)
            {
                data_.resize(vectorSize_);
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
        * \brief Calculates the checksum value of the segment.
        * \details This function also packs the checksum value into the
        * segment's data array.
        * \param bitErrorPercent Percent of segments that will have bit errors.
        * \return The calculated checksum value.
        */
        uint16_t CalculateChecksum(unsigned int bitErrorPercent)
        {
            checksum_ = 0;

            //Loop counter
            unsigned int i = 0;

            //The two 8 bit values concatinated into a 16 bit value
            uint16_t tempval = 0;

            // Add 8 bit values from the packet to checksum_ until there
            // aren't anymore 8 bit values
            for (i = 0; i < (vectorSize_-1); i+=2)
            {
                //Concatinate the next two bytes into a 16 bit value
                tempval = (data_[i] << 8) | (uint8_t)data_[i+1];

                //Add the 16 bit value to tempsum
                checksum_ += tempval;
            }

            //If the last byte wasn't added
            if (i == vectorSize_-1)
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

                unsigned int random_number = uniformDist(rng);

                // Check the random number aganst the error percent to see if
                // this packet will have bit errors
                if (bitErrorPercent >= random_number)
                {
                    //Simuate bit errors by sending an incorrect checksum value
                    checksum_++;

                    printf("Bit Errors Occurred\n");
                }
            }

            // Perform a one's complement on checksum.
            checksum_ = ~checksum_;

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
            // If the segment not full?
            if (dataLength_ < (vectorSize_ - SEGMENT_HEADER_LEN))
            {
                // Add a byte to it
                data_[dataLength_ + SEGMENT_HEADER_LEN] = byte;
                dataLength_++;

                // Indicate that the add succeeded
                return true;
            }

            // The segment must be full
            // Indicate that the add failed
            return false;
        }

        private:

        /**
        * \brief Packs the contents of the segment header into the byte array.
        * \details
        */
        void Serialize()
        {
            // The source port number is stored in the first 2 bytes of the data
            // array
            data_[0] = (sourcePortNumber_ >> 8) & 0xFF;
            data_[1] = sourcePortNumber_ & 0xFF;

            // The destination port number is stored in the 3rd and 4th bytes of
            // the data array
            data_[2] = (destPortNumber_ >> 8) & 0xFF;
            data_[3] = destPortNumber_ & 0xFF;

            // The sequence number is stored in the 5th, 6th, 7th, and 8th bytes
            // of the data array
            data_[4] = (sequenceNumber_ >> 24) & 0xFF;
            data_[5] = (sequenceNumber_ >> 16) & 0xFF;
            data_[6] = (sequenceNumber_ >> 8) & 0xFF;
            data_[7] = sequenceNumber_ & 0xFF;

            // The acknowledgement number is stored in the 9th, 10th, 11th, and
            // 12th bytes of the data array
            data_[8] = (ackNumber_ >> 24) & 0xFF;
            data_[9] = (ackNumber_ >> 16) & 0xFF;
            data_[10] = (ackNumber_ >> 8) & 0xFF;
            data_[11] = ackNumber_ & 0xFF;

            // The header length is the 13th byte
            data_[12] = headerLength_;

            // The flags are stored in the 14th byte
            data_[13] = 0x00;

            if (fin_)
            {
                data_[13] = data_[13] | 0b00000001;
            }
            if (syn_)
            {
                data_[13] = data_[13] | 0b00000010;
            }
            if (rst_)
            {
                data_[13] = data_[13] | 0b00000100;
            }
            if (psh_)
            {
                data_[13] = data_[13] | 0b00001000;
            }
            if (ack_)
            {
                data_[13] = data_[13] | 0b00010000;
            }
            if (urg_)
            {
                data_[13] = data_[13] | 0b00100000;
            }

            // The receive window is stored in the 15th and 16th bytes
            data_[14] = (rwnd_ >> 8) & 0xFF;
            data_[15] = rwnd_ & 0xFF;

            // The checksum value is stored in the 17th and 18th bytes,
            // added by the calculate checksum function, zeroed out here.
            data_[16] = 0;
            data_[17] = 0;

            // The urgent data pointer is stored in the 19th and 20th bytes
            data_[18] = (urgDataPointer_ >> 8) & 0xFF;
            data_[19] = urgDataPointer_ & 0xFF;

            // The length of the data is stored in the 21st and 22nd bytes
            data_[20] = (dataLength_ >> 8) & 0xFF;
            data_[21] = dataLength_ & 0xFF;

            // The options field is stored in the 23rd and 24th bytes
            data_[22] = (options_ >> 8) & 0xFF;
            data_[23] = options_ & 0xFF;
        }

        /**
        * \brief Unpacks the contents of the byte array into the segment header.
        */
        void Deserialize()
        {
            // The source port number is stored in the first 2 bytes of the data
            // array
            sourcePortNumber_ = (uint8_t)data_[0];
            sourcePortNumber_ = (sourcePortNumber_ << 8) | ((uint8_t)data_[1]);

            // The destination port number is stored in the 3rd and 4th bytes of
            // the data array
            destPortNumber_ = (uint8_t)data_[2];
            destPortNumber_ = (destPortNumber_ << 8) | ((uint8_t)data_[3]);

            // The sequence number is stored in the 5th, 6th, 7th, and 8th bytes
            // of the data array
            sequenceNumber_ = (uint8_t)data_[4];
            sequenceNumber_ = (sequenceNumber_ << 8) | ((uint8_t)data_[5]);
            sequenceNumber_ = (sequenceNumber_ << 8) | ((uint8_t)data_[6]);
            sequenceNumber_ = (sequenceNumber_ << 8) | ((uint8_t)data_[7]);

            // The acknowledgement number is stored in the 9th, 10th, 11th, and
            // 12th bytes of the data array
            ackNumber_ = (uint8_t)data_[8];
            ackNumber_ = (ackNumber_ << 8) | ((uint8_t)data_[9]);
            ackNumber_ = (ackNumber_ << 8) | ((uint8_t)data_[10]);
            ackNumber_ = (ackNumber_ << 8) | ((uint8_t)data_[11]);

            // The header length is the 13th byte
            headerLength_ = data_[12];

            // The flags are stored in the 14th byte
            if (((data_[13] >> 5) & 0x01) == 1)
            {
                urg_ = true;
            }
            else
            {
                urg_ = false;
            }

            if (((data_[13] >> 4) & 0x01) == 1)
            {
                ack_ = true;
            }
            else
            {
                ack_ = false;
            }

            if (((data_[13] >> 3) & 0x01) == 1)
            {
                psh_ = true;
            }
            else
            {
                psh_ = false;
            }

            if (((data_[13] >> 2) & 0x01) == 1)
            {
                rst_ = true;
            }
            else
            {
                rst_ = false;
            }

            if (((data_[13] >> 1) & 0x01) == 1)
            {
                syn_ = true;
            }
            else
            {
                syn_ = false;
            }

            if ((data_[13] & 0x01) == 1)
            {
                fin_ = true;
            }
            else
            {
                fin_ = false;
            }

            // The receive window is stored in the 15th and 16th bytes
            rwnd_ = (uint8_t)data_[14];
            rwnd_ = (rwnd_ << 8) | ((uint8_t)data_[15]);

            // The checksum is stored in the 17th and 18th bytes
            checksum_ = (uint8_t)data_[16];
            checksum_ = (checksum_ << 8) | ((uint8_t)data_[17]);

            // The urgent data pointer is stored in the 19th and 20th bytes
            urgDataPointer_ = (uint8_t)data_[18];
            urgDataPointer_ = (urgDataPointer_ << 8) | ((uint8_t)data_[18]);

            // The length of the data is stored in the 21st and 22nd bytes
            dataLength_ = (uint8_t)data_[20];
            dataLength_ = (dataLength_ << 8) | ((uint8_t)data_[21]);

            // The options field is stored in the 23rd and 24th bytes
            options_ = (uint8_t)data_[22];
            options_ = (options_ << 8) | ((uint8_t)data_[23]);

            vectorSize_ = headerLength_ + dataLength_;

            data_.resize(vectorSize_);
        }

        /**
        * \brief Returns a pointer to the begining of the byte array.
        * \return Pointer to the begining of the packet's byte array.
        */
        char* GetSegment()
        {
            // Return a pointer to the byte array so that it can be used
            return static_cast<char*>(&data_[0]);
        }

        uint16_t sourcePortNumber_; ///< Source port of the segment.
        uint16_t destPortNumber_;   ///< Destination port of the segment.
        uint32_t sequenceNumber_;   ///< Sequence number of the segment.
        uint32_t ackNumber_;        ///< ACK sequence number of the segment.
        uint8_t headerLength_;      ///< The length of the header in bytes.
        bool urg_;                  ///< The urgent data flag
        bool ack_;                  ///< The ack flag
        bool psh_;                  ///< The push data now flag
        bool rst_;                  ///< The Connection Reset flag
        bool syn_;                  ///< The Connection Sync flag
        bool fin_;                  ///< The Connection Fin flag
        uint16_t rwnd_;             ///< # of bytes open in the receive window.
        uint16_t checksum_;         ///< Standard UDP checksum.
        uint16_t urgDataPointer_;   ///< Pointer to urgent data.
        uint16_t dataLength_;       ///< The length of the data in the segment.
        uint16_t options_;          ///< The options field.

        std::vector<char> data_;    ///< The data array.
        std::size_t vectorSize_;    ///< The total size of the segment
    };
}

// vim: set expandtab ts=4 sw=4: