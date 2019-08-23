#pragma once

#include <deque>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

#include "fec.hpp"
#include "utility.hpp"

class Block
{
public:
    Block(std::string_view data):
        m_block_size(data.size()),
        m_decoded(data.begin(), data.end()),
        m_fec(data)
    {
    }

    Block(std::uint32_t block_size):
        m_block_size(block_size),
        m_symbols_seen(block_size / MAX_BLOCK_SIZE * 2), // some redundancy
        m_fec(block_size)
    {
    }

    bool process_symbol(std::string_view payload, std::size_t ix)
    {
        if(!m_decoded.empty())
        {
            return false;
        }

        if(ix >= m_symbols_seen.size())
        {
            m_symbols_seen.resize(std::max(ix + 1, m_symbols_seen.size() * 2));
        }
        m_symbols_seen[ix] = true;

        auto res = m_fec.process_symbol(payload, ix);
        if(!res.empty())
        {
            m_decoded = std::move(res);
            return true;
        }
        return false;
    }

    auto block_size() const
    {
        return m_block_size;
    }

    auto const& decoded_data() const
    {
        return m_decoded;
    }

    template <class Callback>
    void generate_unseen_symbols(float redundancy, Callback&& callback)
    {
        std::uint32_t n_original = (m_block_size + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE;
        std::uint32_t n = n_original * redundancy + 0.5;
        for(std::uint32_t i = 0; i < n_original; ++i)
        {
            if(i < m_symbols_seen.size() && m_symbols_seen[i])
            {
                continue;
            }

            if(!n--)
            {
                return;
            }

            callback(std::string_view(
                &m_decoded[i * MAX_BLOCK_SIZE],
                std::min((i + 1) * MAX_BLOCK_SIZE, m_block_size) - i * MAX_BLOCK_SIZE
            ), i);
        }
        for(std::uint32_t i = n_original; ; i++)
        {
            if(i < m_symbols_seen.size() && m_symbols_seen[i])
            {
                continue;
            }

            if(!n--)
            {
                return;
            }

            std::vector data = m_fec.get_symbol_data(i);
            callback(std::string_view(&data[0], data.size()), i);
        }
    }

private:
    std::uint32_t m_block_size;

    std::vector<char> m_decoded;
    std::vector<bool> m_symbols_seen;

    BlockFec m_fec;
};