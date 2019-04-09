/**
* \file Constants.hpp
* \details Constants for this project
* \author Alex Brinister
* \author Colin Rockwood
* \author Mike Geoffroy
* \date February 16, 2019
*/

#ifndef __CONSTANTS_HPP__
#define __CONSTANTS_HPP__

/*
* Linux includes the POSIX HOST_NAME_MAX because POSIX adds constants to
* limits.h
*/
#include <climits>

// Include this for PATH_MAX
#include <linux/limits.h>

namespace socksahoy
{
    /**
    * \brief The longest a hostname/IP can be.
    * \details Windows does not have a standard header for hostname maximum
    * length so we have to define it to some sane number without help from the
    * operating system. According to the internet, Windows has a 64 character
    * limit. On my Linux system, HOST_NAME_MAX is equal to 64 as well. To
    * account for changes in this value in the future on Linux, the
    * system-global constant HOST_NAME_MAX is used.
    */
    const std::size_t MAX_HOSTNAME_LEN = HOST_NAME_MAX;

    /**
    * \brief Maximum data to be sent/received in a packet.
    * \details RFC 1122 Section 3.3.2 states that the lowest "Effective MTU
    * for Sending" supported at the IP layer is 576 bytes. IP packets have a
    * maximum header size of 60 bytes and UDP has a header of 8 bytes. Hence,
    * the full packet will be sized at:
    *
    * \f$size = length_{max}+header_{ip_{max}}+header_{udp} = 508+60+8 = 576\f$
    *
    * Since this is at the minimum guaranteed value, the packet is
    * guaranteed to not suffer fragmentation and thus no loss.
    */
    const std::size_t MAX_PACKET_LEN = 508;

    /// The length of the header
    const std::size_t PACKET_HEADER_LEN = 11;

    /**
    * \brief Maximum amount of data you can stuff in a packet.
    * \details Every packet is sent with a size of type size_t. Therefore, the
    * maximum amount of data is the maximum packet length minus the size.
    */
    const std::size_t MAX_PACKET_DATA_LEN = MAX_PACKET_LEN - PACKET_HEADER_LEN;

    /**
    * \brief Maximum size of an ack packet.
    * \details An ack packet's size is equal to the size of the header
    */
    const std::size_t MAX_ACK_PACKET_LEN = PACKET_HEADER_LEN;

    /**
    * \brief The value for an ack type of packet
    * \details
    */
    const uint8_t ACK_TYPE_PACKET = 0;

    /**
    * \brief The value for an data type of packet
    * \details
    */
    const uint8_t DATA_TYPE_PACKET = 1;

    /// Maximum length of a file path
    const std::size_t MAX_FILE_PATH_LEN = PATH_MAX;

    //The number of packets that can be in the sender window
    const std::size_t MAX_SEND_WINDOW_SIZE = 20;

    //The timeout value in mS
    const std::float_t TIMEOUT_VALUE = 50;
}

#endif /* End of Constants.hpp */

// vim: set expandtab ts=4 sw=4: