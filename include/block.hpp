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
        m_symbols_seen(block_size / MAX_BLOCK_PACKET_SIZE * 2), // some redundancy
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

    std::uint32_t n_original() const
    {
        return (m_block_size + MAX_BLOCK_PACKET_SIZE - 1) / MAX_BLOCK_PACKET_SIZE;
    }

    auto const& decoded_data() const
    {
        return m_decoded;
    }

    class BlockGenerator
    {
    public:
        using result_type = std::pair<Bytes, std::uint32_t>;

        BlockGenerator(Block& block): m_block(&block)
        {
        }

        result_type operator()()
        {
            while(m_index < m_block->m_symbols_seen.size() &&
                m_block->m_symbols_seen[m_index])
            {
                ++m_index;
            }

            struct Incrementer {
                std::uint32_t& x;
                ~Incrementer() {
                    ++x;
                }
            } incrementer{m_index};

            if(m_index < m_block->n_original())
            {
                std::uint32_t ix_first = m_index * MAX_BLOCK_PACKET_SIZE;
                std::uint32_t ix_last = std::min(m_block->m_block_size,
                    (m_index + 1) * MAX_BLOCK_PACKET_SIZE);

                return {{m_block->m_decoded.begin() + ix_first,
                    m_block->m_decoded.begin() + ix_last}, m_index};
            }
            else
            {
                return {m_block->m_fec.get_symbol_data(m_index), m_index};
            }
        }

    private:
        Block* m_block;
        std::uint32_t m_index = 0;
    };

    class BlockGeneratorIterator: public boost::iterator_facade<
        BlockGeneratorIterator, 
        BlockGenerator::result_type,
        boost::single_pass_traversal_tag,
        BlockGenerator::result_type>
    {
    public:
        using iterator_category = std::input_iterator_tag;

        BlockGeneratorIterator(BlockGenerator gen, unsigned index):
            m_gen(gen), m_index(index)
        {
        }

        auto dereference() const
        {
            return m_gen();
        }

        void increment()
        {
            ++m_index;
        }

        bool equal(BlockGeneratorIterator const& other) const
        {
            return m_index == other.m_index;
        }

    private:
        mutable BlockGenerator m_gen;
        unsigned m_index;
    };

    auto unseen_generator()
    {
        return BlockGenerator(*this);
    }

    auto unseen_range(unsigned n)
    {
        BlockGenerator g = unseen_generator();
        return boost::make_iterator_range(
            BlockGeneratorIterator(g, 0),
            BlockGeneratorIterator(g, n)
        );
    }

private:
    std::uint32_t m_block_size;

    std::vector<char> m_decoded;
    std::vector<bool> m_symbols_seen;

    BlockFec m_fec;
};