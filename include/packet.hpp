#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>
#include <string_view>

unsigned const IP_UDP_OVERHEAD = 20 + 8;

// Who cares about endianness. For experimental use only!
struct PacketHeader
{
    std::uint32_t version;
    enum class PacketType: std::uint32_t
    {
        // Ordered by priority, starting with lowest
        STREAM,
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

struct StreamPacketHeader: PacketHeader
{
    std::uint32_t m_channel_id;
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
    std::uint32_t m_kbps;
};

static_assert(MAX_BLOCK_PACKET_SIZE + sizeof(BlockPacketHeader) == MAX_PACKET_SIZE);
static_assert(MAX_STREAM_PACKET_SIZE + sizeof(StreamPacketHeader) == MAX_PACKET_SIZE);

class Packet
{
public:
    static std::size_t constexpr MAX_SIZE = MAX_PACKET_SIZE;
    static std::size_t constexpr MAX_PAYLOAD_SIZE = MAX_BLOCK_PACKET_SIZE;

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

    std::vector<char>&& move_data()
    {
        return std::move(m_data);
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

    friend bool operator <(Packet const& x, Packet const& y)
    {
        int const d =
            (int)x.header<PacketHeader>().m_packet_type -
            (int)y.header<PacketHeader>().m_packet_type
        ;
        if(d < 0)
            return true;
        if(d > 0)
            return false;

        if(x.header<PacketHeader>().m_packet_type == PacketHeader::PacketType::BLOCK)
        {
            return
                y.header<BlockPacketHeader>().m_packet_index <
                x.header<BlockPacketHeader>().m_packet_index
            ;
        }
        return false;
    }

private:
    std::vector<char> m_data;
};