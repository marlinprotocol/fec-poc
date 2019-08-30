#pragma once

#include <chrono>
#include <boost/circular_buffer.hpp>

#include "utility.hpp"

// We model the connection as a buffer of a certain size that must not be exceeded,
// which the recipient drains at a certain rate.
class BandwidthShaper
{
public:
    BandwidthShaper(unsigned kbps, int buffer_size):
        m_kbps(kbps),
        m_buffer_size(buffer_size)
    {
    }

    time_point_t when_can_send(int bytes) const
    {
        int excess_bytes = m_buffer_utilization + bytes - m_buffer_size;
        return m_buffer_utilization_tp + excess_bytes * time_per_byte();
    }

    void did_send(time_point_t tp, int bytes)
    {
        fast_forward_to(tp);
        m_buffer_utilization += bytes;
    }

private:
    unsigned m_kbps;
    int m_buffer_size;                          // Of this size
    int m_buffer_utilization = 0;               // this much was utilized
    time_point_t m_buffer_utilization_tp = {};  // at this time

    std::chrono::nanoseconds time_per_byte() const
    {
        using namespace std::literals::chrono_literals;
        return 8'000'000ns / m_kbps;
    }

    void fast_forward_to(time_point_t tp)
    {
        ENFORCE(tp >= m_buffer_utilization_tp);

        auto bytes_read = (tp - m_buffer_utilization_tp) / time_per_byte();

        m_buffer_utilization = bytes_read > m_buffer_utilization ? 0 :
            m_buffer_utilization - bytes_read;
        m_buffer_utilization_tp = tp;
    }
};

class ShapedPacketQueue
{
public:
    ShapedPacketQueue(unsigned kbps, unsigned buffer_size):
        m_shaper(kbps, buffer_size)
    {
    }

    bool empty() const {
        return m_packets.empty();
    }

    void push(Bytes packet)
    {
        m_packets.push(std::move(packet));
    }

    time_point_t when_can_pop() const
    {
        if(m_packets.empty())
        {
            return FAR_FUTURE;
        }

        return m_shaper.when_can_send(m_packets.top().size());
    }

    Bytes pop_value(time_point_t now)
    {
        Bytes packet = m_packets.pop_value();
        m_shaper.did_send(now, packet.size());
        return packet;
    }

private:
    BandwidthShaper m_shaper;

    movable_priority_queue<Bytes> m_packets;
};
