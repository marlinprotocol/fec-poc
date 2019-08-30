#pragma once

#include <boost/asio.hpp>

#include "logic.hpp"
#include "utility.hpp"

unsigned const NETWORK_BUFFER_SIZE = 5000;
unsigned const MAX_SIZE = 1500;

namespace asio = boost::asio;
using boost::asio::ip::udp;
using boost::system::error_code;

void enforce_ec(error_code ec)
{
    if(ec)
    {
        throw boost::system::system_error(ec);
    }
}

using Timer = boost::asio::basic_waitable_timer<Clock>;

void default_send_bytes_callback(error_code ec, std::size_t bytes_sent)
{
    std::cout << "Send: ec=" << ec.message()
        << " bytes=" << bytes_sent
        << std::endl;
}

template <class Callback = decltype(default_send_bytes_callback)>
void send_bytes(udp::socket& socket, udp::endpoint endpoint,
    std::vector<char> data, Callback&& callback = default_send_bytes_callback)
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

class AsioReceiver
{
public:
    AsioReceiver(asio::io_context& io_context, std::function<void(Bytes)> send, unsigned kbps):
        m_send(send),
        m_queue(kbps, NETWORK_BUFFER_SIZE),
        m_timer(io_context)
    {
    }

    void queue_packet(Bytes packet)
    {
        bool was_empty = m_queue.empty();
        m_queue.push(std::move(packet));
        if(was_empty)
        {
            maybe_send();
        }
    }

    void maybe_send()
    {
        time_point_t now = Clock::now();
        time_point_t next;

        while((next = m_queue.when_can_pop()) <= now)
        {
            m_send(m_queue.pop_value(now));
        }

        if(next == FAR_FUTURE)
        {
            return;
        }

        m_timer.expires_at(next);
        m_timer.async_wait([&](error_code ec) {
            enforce_ec(ec);
            maybe_send();
        });
    }

private:
    std::function<void(Bytes)> m_send;
    ShapedPacketQueue m_queue;
    Timer m_timer;  // A wait is pending iff queue not empty.
};

template <class Derived>
class AsioNode
{
public:
    using endpoint_t = udp::endpoint;

    AsioNode(asio::io_context& io_context, int port):
        m_io_context(io_context),
        m_socket(io_context, udp::endpoint(udp::v4(), port))
    {
    }

    AsioNode(AsioNode const &) = delete;

    AsioReceiver make_receiver(udp::endpoint endpoint, unsigned kbps)
    {
        return AsioReceiver(m_io_context, [this, endpoint](Bytes packet) {
            send_bytes(m_socket, endpoint, std::move(packet));
        }, kbps);
    }

    void listen()
    {
        m_buffer.resize(MAX_SIZE);
        m_socket.async_receive_from(
            asio::buffer(m_buffer), m_peer,
            [this](error_code ec, std::size_t bytes_read)
            {
                enforce_ec(ec);
                m_buffer.resize(bytes_read);

                as_derived().handle_packet(
                    Packet(std::move(m_buffer)),
                    m_peer
                );
                
                listen();
            }
        );
    }
    
private:
    asio::io_context& m_io_context;
    udp::socket m_socket;
    udp::endpoint m_peer;
    std::vector<char> m_buffer;

    Derived& as_derived()
    {
        return static_cast<Derived &>(*this);
    }
};
