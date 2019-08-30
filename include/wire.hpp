#pragma once

#include "packet.hpp"
#include "block.hpp"
#include "utility.hpp"

Packet make_block_packet(std::uint32_t channel_id, std::uint32_t block_id,
    std::uint32_t block_size, std::uint32_t packet_index,
    std::string_view payload)
{
    BlockPacketHeader h = {
        { 0, PacketHeader::PacketType::BLOCK },
        channel_id,
        block_id,
        block_size,
        packet_index
    };
    return Packet(h, payload);
}

auto make_subscribe_packet(std::uint32_t channel_id, unsigned kbps)
{
    ControlPacketHeader h = {
        { 0, PacketHeader::PacketType::CONTROL },
        ControlPacketHeader::Action::SUBSCRIBE,
        channel_id,
        kbps
    };
    return Packet(h, {});
}

auto block_packet_range(Block& block,
    std::uint32_t channel_id, std::uint32_t block_id, float redundancy)
{
    return block.unseen_range(block.n_original() * redundancy + 0.5)
        | boost::adaptors::transformed([=, &block](auto pair) {
            auto& [bytes, index] = pair;
            return make_block_packet(channel_id, block_id, block.block_size(),
                index, to_sv(bytes));
        });
}
