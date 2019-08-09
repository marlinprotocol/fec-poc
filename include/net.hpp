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

    void listen()
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
                                << m_peer.address() << ":" << m_peer.port()
                                << " subscribed to ch = " << ch.m_channel_id
                                << std::endl;
                            m_subscriptions[ch.m_channel_id].insert(m_peer);
                        }
                    }
                    break;

                case PacketHeader::PacketType::BLOCK:
                    {
                        std::cout
                            << "A packet!"
                            << " br=" << bytes_read
                            << " ch=" << p.header<BlockPacketHeader>().m_channel_id
                            << " bid=" << p.header<BlockPacketHeader>().m_block_id
                            << " bs=" << p.header<BlockPacketHeader>().m_block_size
                            << " px=" << p.header<BlockPacketHeader>().m_packet_index
                            << std::endl;

                        auto& block = m_blocks.try_emplace(
                            p.header<BlockPacketHeader>().m_block_id,
                            p.header<BlockPacketHeader>().m_block_size
                        ).first->second; // pair<iterator, bool>

                        bool res = block.process_chunk(
                            p.payload<BlockPacketHeader>(),
                            p.header<BlockPacketHeader>().m_packet_index
                        );
                        std::cout << "process_chunk: " << std::boolalpha << res << std::endl;

                        for(auto endpoint : m_subscriptions[
                            p.header<BlockPacketHeader>().m_channel_id])
                        {
                            std::cout
                                << "Subscriber: "
                                << endpoint.address().to_string() << ":"
                                << endpoint.port()
                                << std::endl;
                            m_queue.push({
                                std::move(p),
                                endpoint,
                                {}
                            });
                        }
                    }
                    break;

                default:
                    throw std::runtime_error("Bad packet type!");
                }

                listen();
            });
    }

    void send_random_block(std::uint32_t channel_id, std::uint32_t block_id,
        std::uint32_t block_size, char junk, int packets, udp::endpoint receiver)
    {
        std::vector<char> message(block_size, junk);
        Block<Packet::MAX_PAYLOAD_SIZE> block(v2sv(message));

        block.generate_unseen_chunks(packets, [=](std::string_view payload, std::uint32_t i) {
            BlockPacketHeader h = {
                { 0, PacketHeader::PacketType::BLOCK },
                channel_id,
                block_id,
                block_size,
                i
            };
            Packet p(h, payload);
            //std::cout << "Send " << payload.size() << " bytes of packet #" << i << "?" << std::endl;
            send_bytes(p.move_data(), receiver);            
        });

        // for(std::uint32_t i = 0; i < block_count; ++i)
        // {
        //     BlockPacketHeader h = {
        //         { 0, PacketHeader::PacketType::BLOCK },
        //         channel_id,
        //         block_id,
        //         block_size,
        //         i
        //     };
        //     std::uint32_t payload_size = std::min(Packet::MAX_PAYLOAD_SIZE,
        //         block_size - i * Packet::MAX_PAYLOAD_SIZE);
        //     std::vector<char> payload(payload_size, junk);
        //     Packet p(h, v2sv(payload));
        //     send_bytes(p.move_data(), receiver);
        // }
    }

private:
    udp::socket m_socket;
    udp::endpoint m_peer;
    std::vector<char> m_buffer;
    std::unordered_map<std::uint32_t,
        std::set<udp::endpoint>> m_subscriptions;
    std::unordered_map<std::uint32_t,
        Block<Packet::MAX_PAYLOAD_SIZE>> m_blocks;
    PacketQueue m_queue;
};
