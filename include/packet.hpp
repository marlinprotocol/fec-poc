#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>
#include <string_view>

// Who cares about endianness. For experimental use only!
struct PacketHeader
{
    std::uint32_t version;
    enum class PacketType: std::uint32_t
    {
        BLOCK,
        CONTROL,
    } m_packet_type;
};

struct BlockPacketHeader: PacketHeader
{
    std::uint32_t m_channel_id;
    std::uint32_t m_block_id;
    std::uint32_t m_block_size;
    std::uint32_t m_packet_index;
};

struct ControlPacketHeader: PacketHeader
{
    enum class Action: std::uint32_t
    {
        UNSUBSCRIBE,
        SUBSCRIBE,
    } m_action;
    std::uint32_t m_channel_id;
};

class Packet
{
public:
    static std::size_t const MAX_SIZE = 1400;
    static std::size_t const MAX_PAYLOAD_SIZE = MAX_SIZE - sizeof(BlockPacketHeader);

    Packet(std::string_view data): m_data(data.begin(), data.end())
    {
    }

    Packet(std::vector<char>&& data): m_data(std::move(data))
    {
    }

    template <class Header>
    Packet(Header const& header, std::string_view payload):
        m_data(sizeof(Header) + payload.size())
    {
        this->header<Header>() = header;
        std::copy(begin(payload), end(payload), m_data.begin() +
            sizeof(Header));
    }

    template <class Header>
    Header& header()
    {
        return *reinterpret_cast<Header *>(&m_data[0]);
    }

    template <class Header>
    Header const& header() const
    {
        return const_cast<Packet &>(*this).header<Header>();
    }

    std::string_view data(std::size_t offset = 0) const
    {
        return { &m_data[offset], m_data.size() - offset };
    }

    template <class Header>
    std::string_view payload() const
    {
        return data(sizeof(Header));
    }

private:
    std::vector<char> m_data;
};