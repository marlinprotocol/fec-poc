#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>
#include <string_view>

// Who cares about endianness. For experimental use only!
struct RawPacketHeader
{
    std::uint8_t version;
    enum class PacketType: std::uint8_t
    {
        BLOCK = 0,
        CONTROL = 1,
    } packet_type;
    std::uint32_t m_channel_id;
    std::uint32_t m_block_id;
    std::uint32_t m_n_packets;
    std::uint32_t m_packet_index;
};

class Packet
{
public:
    static std::size_t const MAX_SIZE = 1400;

    Packet(RawPacketHeader const& header, std::string_view payload):
        m_data(sizeof(RawPacketHeader) + payload.size())
    {
        this->header() = header;
        std::copy(begin(payload), end(payload), m_data.begin() +
            sizeof(RawPacketHeader));
    }

    RawPacketHeader& header()
    {
        return *reinterpret_cast<RawPacketHeader *>(&m_data[0]);
    }

    RawPacketHeader const& header() const
    {
        return const_cast<Packet &>(*this).header();
    }

    std::string_view data(std::size_t offset = 0) const
    {
        return { &m_data[offset], m_data.size() - offset };
    }

    std::string_view payload() const
    {
        return data(sizeof(RawPacketHeader));
    }

private:
    std::vector<char> m_data;
};