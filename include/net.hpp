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
        main_loop();
    }

    void main_loop()
    {
        while(!m_queue.empty())
        {
            std::cout << "Packet in queue!" << std::endl;
            OutgoingPacket op = m_queue.pop_value();
            std::cout << "Sending to " << op.m_receiver.address().to_string()
                << ":" << op.m_receiver.port() << std::endl;

            // Some juggling to achieve correct data lifetime
            auto data = std::move(op.m_packet.data());
            auto buffer = asio::buffer(data);
            m_socket.async_send_to(
                buffer,
                op.m_receiver,
                [capture = std::move(data)](const boost::system::error_code& ec,
                    std::size_t bytes_transferred)
                {
                    std::cout << "Send: ec=" << ec.message()
                        << " bytes=" << bytes_transferred
                        << std::endl;
                }
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
                                << "Subscribe to ch = " << ch.m_channel_id << std::endl;
                            m_subscriptions[ch.m_channel_id].insert(m_peer);
                        }
                    }
                    break;

                case PacketHeader::PacketType::BLOCK:
                    {
                        std::cout
                            << "A block!"
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

                main_loop();
            });
    }

    void do_send(std::size_t length)
    {
        m_socket.async_send_to(
                asio::buffer(m_buffer, length), m_peer,
                [this](boost::system::error_code /*ec*/, std::size_t /*bytes_sent*/)
                {
                    main_loop();
                });
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
