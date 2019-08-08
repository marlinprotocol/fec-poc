#pragma once

#include <deque>
#include <optional>
#include <string_view>
#include <vector>
#include <utility>

#include <boost/iterator/indirect_iterator.hpp>

#include "Packet.hpp"

// 
class Block
{
public:
    Block(
        std::uint32_t channel_id,
        std::uint32_t block_id,
        std::uint32_t n_packets,
        std::string_view data
    ):
        m_channel_id(channel_id),
        m_block_id(block_id),
        m_n_packets(n_packets)
    {
    }

    Block(Packet const& p):
        m_channel_id(p.header().m_channel_id),
        m_block_id  (p.header().m_block_id),
        m_n_packets (p.header().m_n_packets)
    {
        process_packet(p);
    }

    bool process_packet(Packet const& p)
    {
    }

private:
    std::uint32_t m_channel_id;
    std::uint32_t m_block_id;
    std::uint32_t m_n_packets;
};