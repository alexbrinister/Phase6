/**
* \file Packet.hpp
* \details Packet class
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \author Yonatan Genao Baez
* \date March 24, 2019
*/

#ifndef __PACKET_HPP__
#define __PACKET_HPP__

/* C++ Standard Template Library headers */
#include <array>
#include <iterator>
#include <iostream>
#include <random>

#include <cstring>

/* Phase 2 Socket library headers */
#include "Constants.hpp"
#include "SocketGBN.hpp"

namespace socksahoy
{
    /**
    * \class Packet
    * \brief Struct representing a packet holding binary data.
    */
    template <std::size_t Size> class Packet
    {
        /// Packet is friends with SocketGBN so SocketGBN can use Packet members.
        friend class SocketGBN;

        public:

        /**
        * \brief Constructor
        * \details Creates a new packet.
        * \param sequence The sequence number of the packet.
        * \param packets The total number of packets that will be sent.
        */
        Packet(uint16_t sequence, uint16_t packets)
        {
            sequenceNum_ = sequence;
            numOfPackets_ = packets;
            packetSize_ = 0;
            checksum_ = 0;

            // Check the packet size and set what type it is accordingly
            if (Size == MAX_ACK_PACKET_LEN)
            {
                // Ack packet type
                packetType_ = ACK_TYPE_PACKET;
            }
            else
            {
                // Data packet type
                packetType_ = DATA_TYPE_PACKET;
            }
        }

        Packet()
        {
            packetSize_ = 0;
            checksum_ = 0;

            // Check the packet size and set what type it is accordingly
            if (Size == MAX_ACK_PACKET_LEN)
            {
                // Ack packet type
                packetType_ = ACK_TYPE_PACKET;
            }
            else
            {
                // Data packet type
                packetType_ = DATA_TYPE_PACKET;
            }
        }

        /**
        * \brief Gets and returns the actual size of the packet.
        * \details Wrapper around the packetSize_ variable.
        * \return The size of the packet.
        */
        uint32_t GetPacketSize()
        {
            return packetSize_;
        }

        /**
        * \brief Gets and returns the type of packet.
        * \details Wrapper around the packetType_ variable.
        * \return The type of the packet.
        */
        uint8_t GetPacketType()
        {
            return packetType_;
        }

        /**
        * \brief Gets and returns the the squence number of the packet.
        * \details Wrapper around the sequenceNum_ variable.
        * \return The sequence number of the packet.
        */
        uint16_t GetSequenceNumber()
        {
            return sequenceNum_;
        }

        /**
        * \brief Gets and returns the number of packets that will be sent.
        * \details Wrapper around the packetSize_ variable.
        * \return The number of packets that will be sent.
        */
        uint16_t GetPacketNumber()
        {
            return numOfPackets_;
        }

        /**
        * \brief Gets and returns the max size that a packet can be.
        * \details
        * \return The max size of a packet
        */
        uint32_t GetMaxPacketSize()
        {
            return data_.max_size();
        }

        /**
        * \brief Gets the checksum value that was included in the packet.
        * \details Wrapper around the checksum_ variable.
        * \return The checksum value.
        */
        uint16_t GetChecksum()
        {
            return checksum_;
        }

        /**
        * \brief Calculates the checksum value of the packet and returns it.
        * \param bitErrorPercent Percent of packets that will have bit errors.
        * \return The calculated checksum value.
        */
        uint16_t CalculateChecksum(int bitErrorPercent)
        {
            // Temporary sum, makes the code look cleaner
            uint16_t tempsum = 0;

            // Clear the checksum value if it's already set.
            data_[4] = 0;
            data_[5] = 0;

            //Loop counter
            unsigned int i = 0;

            // If the packet size is larger than the maximum data size, that
            // means we are corrupted. Return an incorrect checksum value.
            if (packetSize_ > MAX_PACKET_LEN)
            {
                return tempsum++;
            }

            // Add 8 bit values from the packet to checksum_ until there
            // aren't anymore 8 bit values
            for (i = 0; i < (packetSize_ + PACKET_HEADER_LEN); i++)
            {
                //Add the 8 bit value to checksum
                tempsum += data_[i];
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
                    tempsum++;
                }
            }

            // Perform a one's complement on checksum.
            tempsum ^= 0xFFFF;

            // The checksum value is stored in bytes 5 and 6
            data_[4] = (tempsum & 0xFF00) >> 8;
            data_[5] = tempsum & 0xFF;

            return tempsum;
        }

        /**
        * \brief Adds a byte to the data segment of the byte array
        * \param byte The byte to add to the packet.
        * \return A bool value indicating if the add succeeded
        */
        bool AddByte(char byte)
        {
            //If the packet not full, and not a ack packet?
            if ((packetSize_ < (GetMaxPacketSize() - PACKET_HEADER_LEN))
                    && (packetType_ == DATA_TYPE_PACKET))
            {
                //Add a byte to it
                data_[packetSize_ + PACKET_HEADER_LEN] = byte;
                packetSize_++;
                //Indicate that the add succeeded
                return true;
            }

            //The packet must be full
            //Indicate that the add failed
            return false;
        }

        /**
        * \brief Access the data segment of the underlying byte array.
        * \return Pointer to the data segment of the underlying byte array.
        */
        const char* GetData() const
        {
            //Is this a data packet?
            if (packetType_ == DATA_TYPE_PACKET)
            {
                // Return a pointer to the data segment of the byte array
                return static_cast<const char*>(&data_[PACKET_HEADER_LEN]);
            }

            // This must be a ack packet
            // Return a null pointer
            return nullptr;
        }

        private:

        /**
        * \brief Packs the contents of the packet into the byte array.
        * \details
        */
        void Serialize()
        {
            // The packet size is stored in the first 4 bytes of the data array
            data_[0] = (packetSize_ & 0xFF000000) >> 24;
            data_[1] = (packetSize_ & 0x00FF0000) >> 16;
            data_[2] = (packetSize_ & 0x0000FF00) >> 8;
            data_[3] = (packetSize_ & 0x000000FF) ;

            // The packetype is the 7th byte
            data_[6] = packetType_;

            // The sequence number is stored in the 8th and 9th bytes
            data_[7] = (sequenceNum_ & 0xFF00) >> 8;
            data_[8] = sequenceNum_ & 0xFF;

            // And finally the number of packets that will be sent is stored in
            // the 10th and 11th bytes
            data_[9] = (numOfPackets_ & 0xFF00) >> 8;
            data_[10] = numOfPackets_ & 0xFF;
        }

        /**
        * \brief Unpacks the contents of the byte array.
        */
        void
            Deserialize()
            {
                packetSize_ = data_[0] << 24;
                packetSize_ |= data_[1] << 16;
                packetSize_ |= data_[2] << 8;
                packetSize_ |= (uint8_t)data_[3];

                checksum_ = data_[4] << 8;
                checksum_ |= (uint8_t) data_[5];

                packetType_ = data_[6];

                sequenceNum_ = data_[7] << 8;
                sequenceNum_ |= (uint8_t) data_[8];

                numOfPackets_ = data_[9] << 8;
                numOfPackets_ |= (uint8_t) data_[10];
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

        uint32_t packetSize_;           ///< Number of data elements in packet.
        uint16_t checksum_;             ///< The checksum of the packet.
        uint8_t packetType_;            ///< The type of packet.
        uint16_t sequenceNum_;          ///< The sequence number of the packet.
        uint16_t numOfPackets_;         ///< Number of packets in transmission.
        std::array<char, Size> data_;   ///< The data array.
    };
}

#endif /* End of Packet.hpp */

// vim: set expandtab ts=4 sw=4: