#pragma once

#include "packet.hpp"
#include "block.hpp"
#include "utility.hpp"

auto block_packet_range(Block& block,
    std::uint32_t channel_id, std::uint32_t block_id, float redundancy)
{
    return block.unseen_range(block.n_original() * redundancy + 0.5)
        | boost::adaptors::transformed([=, &block](auto pair) {
            auto& [bytes, index] = pair;
            return Packet::make<BlockPacketHeader>(to_sv(bytes),
                channel_id, block_id, block.block_size(), index);
        });
}
