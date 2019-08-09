#pragma once

#include <memory>
#include <string_view>
#include <vector>
#include <queue>

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

std::string_view v2sv(std::vector<char> const& v)
{
    return { &v[0], v.size() };
}