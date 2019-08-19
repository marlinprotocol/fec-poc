#pragma once

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <set>

#include <boost/asio.hpp>

#include "block.hpp"
#include "packet.hpp"
#include "utility.hpp"

namespace asio = boost::asio;
using boost::asio::ip::udp;
using boost::system::error_code;

unsigned const MAX_SIZE = 1500;
int const LOSE_EVERY = 3;

void enforce_ec(error_code ec)
{
    if(ec)
    {
        throw boost::system::system_error(ec);
    }
}

using Clock = std::chrono::high_resolution_clock;

struct OutgoingPacket
{
    Packet m_packet;
    udp::endpoint m_receiver;
    Clock::time_point m_time;

    friend bool operator <(OutgoingPacket const& x, OutgoingPacket const& y)
    {
        return y.m_time < x.m_time;  // sic! so earliest goes on top of queue
    }
};

using PacketQueue = movable_priority_queue<OutgoingPacket>;

class Node
{
public:
    Node(asio::io_context& io_context, int port):
        m_socket(io_context, udp::endpoint(udp::v4(), port))
    {
    }

    Node(Node const &) = delete;

    void send_bytes(std::vector<char>&& data, udp::endpoint endpoint)
    {
        // Some juggling to achieve correct data lifetime
        auto buffer = asio::buffer(data);  // points to vector contents
        m_socket.async_send_to(
            buffer,
            endpoint,
            // vector gets move-captured into the lambda,
            // but data is still at the same address
            [capture = std::move(data)](const boost::system::error_code& ec,
                std::size_t bytes_transferred)
            {
                std::cout << "Send: ec=" << ec.message()
                    << " bytes=" << bytes_transferred
                    << std::endl;
            } // ~vector()
        );       
    }

    void send_queued()
    {
        while(!m_queue.empty())
        {
            std::cout << "Packet in queue!" << std::endl;
            OutgoingPacket op = m_queue.pop_value();
            std::cout << "Sending to " << op.m_receiver.address().to_string()
                << ":" << op.m_receiver.port() << std::endl;

            send_bytes(
                op.m_packet.move_data(),
                op.m_receiver
            );
        }
    }

    void listen()
    {
        send_queued();

        m_buffer.resize(MAX_SIZE);
        m_socket.async_receive_from(
            asio::buffer(m_buffer), m_peer,
            [this](error_code ec, std::size_t bytes_read)
            {
                enforce_ec(ec);
                m_buffer.resize(bytes_read);

                std::cout << "Got " << bytes_read << " bytes" << std::endl;
                Packet p(std::move(m_buffer));
                auto const& h = p.header<PacketHeader>();
                switch(h.m_packet_type)
                {
                case PacketHeader::PacketType::CONTROL:
                    {
                        auto const& ch = static_cast<ControlPacketHeader const &>(h);
                        if(ch.m_action == ControlPacketHeader::Action::SUBSCRIBE)
                        {
                            std::cout
                                << m_peer
                                << " subscribed to ch = " << ch.m_channel_id
                                << std::endl;
                            m_subscriptions[ch.m_channel_id].insert(m_peer);
                        }
                    }
                    break;

                case PacketHeader::PacketType::BLOCK:
                    {
                        auto const& h = p.header<BlockPacketHeader>();
                        std::cout
                            << "A packet!"
                            << " br=" << bytes_read
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
                            h.m_block_id,
                            h.m_block_size
                        ).first->second; // pair<iterator, bool>

                        bool decoded = block.process_symbol(
                            p.payload<BlockPacketHeader>(),
                            h.m_packet_index
                        );

                        if(decoded)
                        {
                            std::cout << "Full block ready, crc = "
                                << crc32(to_sv(block.decoded_data()))
                                << std::endl;
                        }

                        for(auto endpoint : m_subscriptions[h.m_channel_id])
                        {
                            std::cout << "Queue to " << endpoint << std::endl;
                            m_queue.push({
                                p,
                                endpoint,
                                {}
                            });
                            if(decoded)
                            {
                                queue_block(block, h.m_channel_id, h.m_block_id,
                                    2, endpoint);
                            }
                        }
                    }
                    break;

                default:
                    throw std::runtime_error("Bad packet type!");
                }

                listen();
            });
    }

    void queue_block_packet(std::uint32_t channel_id, std::uint32_t block_id,
        std::uint32_t block_size, std::uint32_t packet_index,
        std::string_view payload, udp::endpoint receiver)
    {
        BlockPacketHeader h = {
            { 0, PacketHeader::PacketType::BLOCK },
            channel_id,
            block_id,
            block_size,
            packet_index
        };
        m_queue.push({Packet(h, payload), receiver, {}});
        //send_bytes(p.move_data(), receiver);            
    }

    void queue_block(Block& block,
        std::uint32_t channel_id, std::uint32_t block_id,
        int n_packets, udp::endpoint receiver)
    {
        block.generate_unseen_symbols(
            n_packets,
            [&](std::string_view payload, std::uint32_t i) {
                queue_block_packet(
                    channel_id,
                    block_id,
                    block.block_size(),
                    i,
                    payload,
                    receiver
                );
            }
        );
    }

    void queue_random_block(std::uint32_t channel_id, std::uint32_t block_id,
        std::uint32_t block_size, char junk, int n_packets, udp::endpoint receiver)
    {
        std::vector<char> message(block_size, junk);
        std::cout << "Generated a message, crc = " << crc32(to_sv(message)) << std::endl;
        
        Block block(to_sv(message));
        queue_block(block, channel_id, block_id, n_packets, receiver);
    }

    void subscribe(std::uint32_t channel_id, udp::endpoint server)
    {
        ControlPacketHeader h = {
            { 0, PacketHeader::PacketType::CONTROL },
            ControlPacketHeader::Action::SUBSCRIBE,
            channel_id
        };
        Packet p(h, {});
        send_bytes(p.move_data(), server);
    }

private:
    int packet_seq = 0;
    udp::socket m_socket;
    udp::endpoint m_peer;
    std::vector<char> m_buffer;
    std::unordered_map<std::uint32_t,
        std::set<udp::endpoint>> m_subscriptions;
    std::unordered_map<std::uint32_t, Block> m_blocks;
    PacketQueue m_queue;
};
