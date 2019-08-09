#pragma once

#include <deque>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

#include "fec.hpp"

template<std::uint32_t CHUNK_SIZE>
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
        m_chunks_seen(block_size / CHUNK_SIZE * 2), // some redundancy
        m_fec(block_size)
    {
    }

    bool process_chunk(std::string_view payload, std::size_t ix)
    {
        if(ix >= m_chunks_seen.size())
        {
            m_chunks_seen.resize(std::max(ix + 1, m_chunks_seen.size() * 2));
        }
        m_chunks_seen[ix] = true;

        auto res = m_fec.process_chunk(payload, ix);
        if(!res.empty())
        {
            m_decoded = std::move(res);
            return true;
        }
        return false;
    }

    template <class Callback>
    void generate_unseen_chunks(int n, Callback&& callback)
    {
        std::uint32_t first_fec = (m_block_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
        for(std::uint32_t i = 0; i < first_fec; ++i)
        {
            if(i < m_chunks_seen.size() && m_chunks_seen[i])
            {
                continue;
            }

            if(!n--)
            {
                return;
            }

            callback(std::string_view(
                &m_decoded[i * CHUNK_SIZE],
                std::min((i + 1) * CHUNK_SIZE, m_block_size) - i * CHUNK_SIZE
            ), i);
        }
        for(std::uint32_t i = first_fec; ; i++)
        {
            if(i < m_chunks_seen.size() && m_chunks_seen[i])
            {
                continue;
            }

            if(!n--)
            {
                return;
            }

            std::vector chunk = m_fec.get_chunk(i);
            callback(std::string_view(&chunk[0], chunk.size()), i);
        }
    }

private:
    std::uint32_t m_block_size;

    std::vector<char> m_decoded;
    std::vector<bool> m_chunks_seen;

    Fec<CHUNK_SIZE> m_fec;
};