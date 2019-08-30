#pragma once

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <string_view>
#include <vector>
#include <queue>

#include <boost/crc.hpp>
#include <boost/io/ios_state.hpp>
#include <boost/rational.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <iostream>
#include <iomanip>

std::uint32_t const MAX_PACKET_SIZE = 1400;
std::uint32_t const MAX_BLOCK_PACKET_SIZE = MAX_PACKET_SIZE - 6 * sizeof(std::uint32_t);
std::uint32_t const MAX_STREAM_PACKET_SIZE = MAX_PACKET_SIZE - 4 * sizeof(std::uint32_t);

float const REDUNDANCY = 1.3;

using Bytes = std::vector<char>;
using Clock = std::chrono::high_resolution_clock;
using time_point_t = std::chrono::time_point<Clock>;
static auto const FAR_FUTURE = time_point_t::max();

template <
    class T,
    class Container = std::vector<T>,
    class Compare = std::less<typename Container::value_type>
>
class movable_priority_queue: public std::priority_queue<T, Container, Compare>
{
public:
    T pop_value()
    {
        if(c.empty())
        {
            throw std::runtime_error("pop_value: empty queue!");
        }
        std::pop_heap(c.begin(), c.end(), comp);
        T value = std::move(c.back());
        c.pop_back();
        return value;
    }

    friend T pop_value(movable_priority_queue& q)
    {
        return q.pop_value();
    }

protected:
    using std::priority_queue<T, Container, Compare>::c;
    using std::priority_queue<T, Container, Compare>::comp;
};

template <class X, class Cont>
auto pop_value(std::queue<X, Cont>& q)
{
    if(q.empty())
    {
        throw std::runtime_error("pop_value: empty queue!");
    }
    auto value = q.front();
    q.pop();
    return value;
}

inline unsigned crc32(std::string_view data)
{
    boost::crc_32_type result;
    result.process_bytes(&data[0], data.size());
    return result.checksum();
}

struct show_crc32
{
    std::string_view sv;

    friend std::ostream& operator <<(std::ostream& os, show_crc32 const& x)
    {
        boost::io::ios_flags_saver ifs(std::cout);
        return os
            << std::hex << std::setfill('0') << std::setw(8) << crc32(x.sv);
    };
};

template <class Ptr>
inline void assert_is_char_cast_suitable()
{
    static_assert(std::is_pointer_v<Ptr>, "char_cast: types must be pointers");
    using CharT = std::remove_const_t<std::remove_pointer_t<Ptr>>;
    static_assert(
        std::is_same_v<CharT, char> ||
        std::is_same_v<CharT, unsigned char> ||
        std::is_same_v<CharT, void>,
        "char_cast: types must be (void | [unsigned] char) [const] *"
    );
}

/// Do reinterpret_cast. Both types must be (void | [unsigned] char) [const] *.
template <class Ptr1, class Ptr2>
inline Ptr1 char_cast(Ptr2 ptr)
{
    assert_is_char_cast_suitable<Ptr1>();
    assert_is_char_cast_suitable<Ptr2>();
    return reinterpret_cast<Ptr1>(ptr);
}

inline std::string_view to_sv(std::vector<char> const& v)
{
    return { &v[0], v.size() };
}

inline std::string_view to_sv(std::vector<unsigned char> const& v)
{
    return { char_cast<char const *>(&v[0]), v.size() };
}

template <class SiamesePacket>
inline typename std::enable_if<
    std::is_same_v<SiamesePacket, SiameseOriginalPacket> ||
    std::is_same_v<SiamesePacket, SiameseRecoveryPacket>,
std::string_view>::type to_sv(SiamesePacket const& packet)
{
    return {
        char_cast<char const *>(packet.Data),
        packet.DataBytes
    };
}

inline std::ostream& operator <<(std::ostream& os, SiameseOriginalPacket const& packet)
{
    boost::io::ios_flags_saver ifs(std::cout);
    os
        << "[n=" << packet.PacketNum
        << " size=" << packet.DataBytes
        << " p=" << (void const *)packet.Data
        << " crc=" << std::hex << std::setw(8) << std::setfill('0')
            << crc32({ (char *)packet.Data, packet.DataBytes })
        << " u64=" << *reinterpret_cast<std::uint64_t const *>(packet.Data) << "]";
    return os;
}

inline std::ostream& operator <<(std::ostream& os, SiameseRecoveryPacket const& packet)
{
    boost::io::ios_flags_saver ifs(std::cout);
    os
        << "[size=" << packet.DataBytes
        << " p=" << (void const *)packet.Data
        << " crc=" << std::hex << std::setw(8) << std::setfill('0')
            << crc32({ (char *)packet.Data, packet.DataBytes })
        << "]";
    return os;
}

class RandomDeviceSeedSeq
{
public:
    using result_type = std::random_device::result_type;

    explicit RandomDeviceSeedSeq(std::random_device& dev): m_dev(&dev)
    {
    }

    template <class RandomAccessIterator>
    void generate(RandomAccessIterator first, RandomAccessIterator last) {
        std::generate(first, last, std::ref(*m_dev));
    }

private:
    std::random_device *m_dev;
};

template <class Engine>
Engine make_random_engine()
{
    static std::random_device rd;
    RandomDeviceSeedSeq ss{rd};
    return Engine(ss);
}

template <class Int>
Int ceil(boost::rational<Int> r)
{
    return (r.numerator() + r.denominator() - 1) / r.denominator();
}