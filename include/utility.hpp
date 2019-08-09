#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <queue>

#include <boost/crc.hpp>

std::uint32_t const MAX_PACKET_SIZE = 1400;
std::uint32_t const CHUNK_SIZE = MAX_PACKET_SIZE - 6 * sizeof(std::uint32_t);

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
        std::pop_heap(c.begin(), c.end(), comp);
        T value = std::move(c.back());
        c.pop_back();
        return value;
    }

protected:
    using std::priority_queue<T, Container, Compare>::c;
    using std::priority_queue<T, Container, Compare>::comp;
};

inline std::string_view v2sv(std::vector<char> const& v)
{
    return { &v[0], v.size() };
}

inline int crc32(std::string_view data)
{
    boost::crc_32_type result;
    result.process_bytes(&data[0], data.size());
    return result.checksum();
}