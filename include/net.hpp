#pragma once

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <set>

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>

#include "block.hpp"
#include "packet.hpp"
#include "utility.hpp"

namespace asio = boost::asio;
using boost::asio::ip::udp;
using boost::system::error_code;

unsigned const MAX_SIZE = 1500;
unsigned const MAX_PACKET_BURST = 10;
int const LOSE_EVERY = 10;


void enforce_ec(error_code ec)
{
    if(ec)
    {
        throw boost::system::system_error(ec);
    }
}

using Clock = std::chrono::high_resolution_clock;
using Timer = boost::asio::basic_waitable_timer<Clock>;
using time_point_t = std::chrono::time_point<Clock>;

void default_send_bytes_callback(error_code ec, std::size_t bytes_sent)
{
    std::cout << "Send: ec=" << ec.message()
        << " bytes=" << bytes_sent
        << std::endl;
}

template <class Callback = decltype(default_send_bytes_callback)>
void send_bytes(udp::socket& socket, std::vector<char> data,
    udp::endpoint endpoint, Callback&& callback = default_send_bytes_callback)
{
    // Some juggling to achieve correct data lifetime
    auto buffer = asio::buffer(data);  // points to vector contents
    socket.async_send_to(
        buffer,
        endpoint,
        // vector gets move-captured into the lambda,
        // but data is still at the same address
        [capture = std::move(data), &callback](error_code ec, std::size_t bytes_sent)
        {
            callback(ec, bytes_sent);
        } // ~vector()
    );
}

class Receiver
{
public:
    Receiver(asio::io_context& io_context, udp::endpoint endpoint, unsigned kbps):
        m_endpoint{endpoint},
        m_kbps{kbps},
        m_timer(io_context)
    {
    }

    time_point_t can_pop_at() const
    {
        return m_can_pop_at.front();
    }

    Packet pop_packet(time_point_t now)
    {
        Packet packet = pop_value(m_packets);
        unsigned total_bytes = packet.data().size() + IP_UDP_OVERHEAD;
        m_can_pop_at.push_back(now + std::chrono::nanoseconds(
            total_bytes * 8'000'000ull / m_kbps));
        return packet;
    }

    // The above is basically all the business logic. Asio implementation below.
    // TODO:
    // - Decouple one from the other.
    // - Benchmark the performance of setting a timer for each packet.

    void reset_timer(udp::socket& socket)
    {
        m_timer.expires_at(can_pop_at());
        m_timer.async_wait([&](error_code ec) {
            send_one_packet(socket, ec);
        });
    }

    void queue_packet(udp::socket& socket, Packet packet)
    {
        if(m_packets.empty())
        {
            reset_timer(socket);
        }
        m_packets.push(std::move(packet));
    }

    void send_one_packet(udp::socket& socket, error_code ec)
    {
        ENFORCE(!ec);
        ENFORCE(Clock::now() >= can_pop_at());

        send_bytes(socket, pop_packet(Clock::now()).move_data(), m_endpoint);

        if(!m_packets.empty())
        {
            reset_timer(socket);
        }
    }

    friend std::ostream& operator <<(std::ostream& os, Receiver const& r)
    {
        return os << "<Receiver: " << r.m_endpoint
            << " kbps=" << r.m_kbps
            << ">" << std::endl;
    }

    friend bool operator <(Receiver const& x, Receiver const& y)
    {
        // To put into an std::set
        return x.m_endpoint < y.m_endpoint;
    }

private:
    udp::endpoint m_endpoint;
    unsigned m_kbps;

    movable_priority_queue<Packet> m_packets;
    boost::circular_buffer<time_point_t> m_can_pop_at =
        {MAX_PACKET_BURST, time_point_t{}};

    Timer m_timer;  // A wait is pending iff queue not empty.
};

class Node
{
public:
    Node(asio::io_context& io_context, int port):
        m_io_context(io_context),
        m_socket(io_context, udp::endpoint(udp::v4(), port))
    {
    }

    Node(Node const &) = delete;

    void listen()
    {
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
                            m_subscriptions[ch.m_channel_id].insert({
                                m_io_context,
                                m_peer,
                                ch.m_kbps
                            });
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

                        for(auto& cr : m_subscriptions[h.m_channel_id])
                        {
                            auto& receiver = const_cast<Receiver &>(cr);
                            std::cout << "Queue to " << receiver << std::endl;
                            receiver.queue_packet(m_socket, p);
                            if(decoded)
                            {
                                queue_block(block, h.m_channel_id, h.m_block_id,
                                    2, receiver);
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
        std::string_view payload, Receiver& receiver)
    {
        BlockPacketHeader h = {
            { 0, PacketHeader::PacketType::BLOCK },
            channel_id,
            block_id,
            block_size,
            packet_index
        };
        receiver.queue_packet(m_socket, Packet(h, payload));
    }

    void queue_block(Block& block,
        std::uint32_t channel_id, std::uint32_t block_id,
        float redundancy, Receiver& receiver)
    {
        block.generate_unseen_symbols(
            redundancy,
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
        std::uint32_t block_size, char junk, float redundancy, Receiver& receiver)
    {
        std::vector<char> message(block_size, junk);
        std::cout << "Generated a message, crc = " << crc32(to_sv(message)) << std::endl;
        
        Block block(to_sv(message));
        queue_block(block, channel_id, block_id, redundancy, receiver);
    }

    void subscribe(std::uint32_t channel_id, udp::endpoint server, unsigned kbps)
    {
        ControlPacketHeader h = {
            { 0, PacketHeader::PacketType::CONTROL },
            ControlPacketHeader::Action::SUBSCRIBE,
            channel_id,
            kbps
        };
        Packet p(h, {});
        send_bytes(m_socket, p.move_data(), server);
    }

private:
    int packet_seq = 0;
    asio::io_context& m_io_context;
    udp::socket m_socket;
    udp::endpoint m_peer;
    std::vector<char> m_buffer;
    std::unordered_map<std::uint32_t,
        std::set<Receiver>> m_subscriptions;
    std::unordered_map<std::uint32_t, Block> m_blocks;
};
