#pragma once

#include <array>
#include <chrono>
#include <iostream>
#include <functional>
#include <memory>
#include <unordered_map>
#include <set>

#include <boost/circular_buffer.hpp>

#include "block.hpp"
#include "stream.hpp"
#include "packet.hpp"
#include "logic.hpp"
#include "wire.hpp"
#include "utility.hpp"
#include "asio.hpp"

int const LOSE_EVERY = 10;

class Node: public AsioNode<Node>
{
public:
    template <class... Args>
    Node(Args&&... args): AsioNode(std::forward<Args>(args)...)
    {
    }

    void handle_packet(Packet p, endpoint_t peer)
    {
        auto const& h = p.header<PacketHeader>();
        switch(h.m_packet_type)
        {
        case PacketHeader::PacketType::CONTROL:
            {
                auto const& ch = static_cast<ControlPacketHeader const &>(h);
                if(ch.m_action == ControlPacketHeader::Action::SUBSCRIBE)
                {
                    std::cout
                        << peer
                        << " subscribed to ch = " << ch.m_channel_id
                        << std::endl;

                    m_subscriptions[ch.m_channel_id].insert_or_assign(
                        peer,
                        make_receiver(
                            peer,
                            ch.m_kbps
                        )
                    );
                }
            }
            break;

        case PacketHeader::PacketType::BLOCK:
            {
                auto const h = p.header<BlockPacketHeader>();
                std::cout
                    << "A block packet!"
                    << " size=" << p.data().size()
                    << " ch=" << h.m_channel_id
                    << " bid=" << h.m_block_id
                    << " bs=" << h.m_block_size
                    << " px=" << h.m_packet_index
                    << std::endl;

                if(packet_seq++ % LOSE_EVERY == 0)
                {
                    std::cout << "Oops, lost!" << std::endl;
                    break;
                }

                auto& block = m_blocks.try_emplace(
                    {h.m_channel_id, h.m_block_id},
                    h.m_block_size
                ).first->second; // pair<iterator, bool>

                bool decoded = block.process_symbol(
                    p.payload<BlockPacketHeader>(),
                    h.m_packet_index
                );

                std::vector<Bytes> packets_to_send = {p.move_data()};

                if(decoded)
                {
                    std::cout << "Full block ready, crc = "
                        << show_crc32{to_sv(block.decoded_data())}
                        << std::endl;

                    for(auto p : block_packet_range(block,
                        h.m_channel_id, h.m_block_id, REDUNDANCY))
                    {
                        packets_to_send.push_back(p.move_data());
                    }
                }

                for(auto& [ep, receiver] : m_subscriptions[h.m_channel_id])
                {
                    std::cout << "Queue to " << ep << std::endl;
                    for(auto const& p : packets_to_send)
                    {
                        receiver.queue_packet(p);
                    }
                }
            }
            break;

        case PacketHeader::PacketType::STREAM:
            {
                auto const& h = p.header<StreamPacketHeader>();
                std::cout
                    << "A stream packet!"
                    << " size=" << p.data().size()
                    << " ch=" << h.m_channel_id
                    << " px=" << h.m_packet_index
                    << std::endl;

                if(packet_seq++ % LOSE_EVERY == 0)
                {
                    std::cout << "Oops, lost!" << std::endl;
                    break;
                }

                auto& stream = m_streams.try_emplace(
                    h.m_channel_id
                ).first->second; // pair<iterator, bool>

                stream.m_decoder.process_symbol(
                    p.payload<StreamPacketHeader>(),
                    h.m_packet_index
                );

                while(stream.m_decoder.has_data())
                {
                    Bytes chunk = stream.m_decoder.get_chunk();
                    std::cout << "Stream chunk: crc=" << show_crc32{to_sv(chunk)} << std::endl;
                    stream.m_encoder.queue_chunk(chunk);
                }

                std::vector<Bytes> packets_to_send;

                while(stream.m_encoder.has_data())
                {
                    Symbol symbol = stream.m_encoder.get_symbol();
                    packets_to_send.push_back(make_stream_packet(
                        h.m_channel_id,
                        symbol.second,
                        to_sv(symbol.first)
                    ).move_data());
                }

                for(auto& [ep, receiver] : m_subscriptions[h.m_channel_id])
                {
                    std::cout << "Queue to " << ep << std::endl;
                    for(auto const& p : packets_to_send)
                    {
                        receiver.queue_packet(p);
                    }
                }
            }
            break;

        default:
            throw std::runtime_error("Bad packet type!");
        }
    }

private:
    int packet_seq = 0;
    std::unordered_map<std::uint32_t,
        std::map<udp::endpoint, AsioReceiver>> m_subscriptions;
    std::unordered_map<std::pair<std::uint32_t, std::uint32_t>, Block,
        boost::hash<std::pair<std::uint32_t, std::uint32_t>>> m_blocks;
    std::unordered_map<std::uint32_t, ContinuousStream> m_streams;
};
