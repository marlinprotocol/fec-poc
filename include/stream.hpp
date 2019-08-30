#pragma once

#include "fec.hpp"
#include "utility.hpp"

auto const FEC_RATIO = boost::rational<int>(2, 5);  // recovery/original

using Symbol = std::pair<Bytes, StreamFecEncoder::packet_index_t>;

struct LeastIndexTop
{
    bool operator()(Symbol const& p1, Symbol const& p2)
    {
        return p2.second < p1.second;
    }
};

class ContinuousStreamDecoder
{
public:
    using packet_index_t = StreamFecDecoder::packet_index_t;

    bool has_data() const
    {
        return !m_symbols_ahead.empty() &&
            m_symbols_ahead.top().second == m_next_index;
    }

    Bytes get_chunk()
    {
        m_next_index = SIAMESE_PACKET_NUM_INC(m_next_index);
        return pop_value(m_symbols_ahead).first;
    }

    void process_symbol(std::string_view data, packet_index_t index)
    {
        std::cout << "p_s: crc=" << show_crc32{data} << " sz=" << data.size()
            << " index=" << index << std::endl;

        m_decoder.process_symbol(data, index);

        if(index != StreamFecDecoder::PACKET_INDEX_FEC)
        {
            process_original_chunk({data.begin(), data.end()}, index);
        }

        while(m_decoder.has_data())
        {
            std::cout << "has_data!\n";
            for(auto [data, ix] : m_decoder.get_new_chunks())
            {
                std::cout << "Decoder has something! " << index << std::endl;
                process_original_chunk(std::move(data), index);
            }
        }
    }

private:
    StreamFecDecoder m_decoder;
    packet_index_t m_next_index = 0;
    movable_priority_queue<Symbol, std::vector<Symbol>, LeastIndexTop> m_symbols_ahead;

    void process_original_chunk(Bytes data, packet_index_t index)
    {
        std::cout << "poc: i=" << index << " ni=" << m_next_index << std::endl;
        if(index < m_next_index)
        {
            // TODO: FIXME Siamese wraparound!
            std::cout << "Chunk already processed: ix=" << index
                << " next=" << m_next_index << std::endl;
        }
        else
        {
            m_symbols_ahead.push({std::move(data), index});
        }
    }
};

class ContinuousStreamEncoder
{
public:
    using packet_index_t = StreamFecEncoder::packet_index_t;

    enum class ReliabilityLevel {
        UNDER_RATIO,  // If chunks stop arriving, should send FEC symbols
        AT_RATIO,     // Could send FEC symbols if nothing better to do
        ALL_ACKED,    // No sense in sending extra symbols
    };

    void queue_chunk(Bytes chunk)
    {
        m_pending.push(chunk);
    }

    ReliabilityLevel reliability_level() const
    {
        if(m_receiver_expects == m_next_index)
        {
            return ReliabilityLevel::ALL_ACKED;
        }
        if(m_segment_chunk_index1 % FEC_RATIO.denominator() == 0 &&
            m_segment_fec_index == 0)
        {
            return ReliabilityLevel::AT_RATIO;
        }
        return ReliabilityLevel::UNDER_RATIO;
    }

    bool has_data() const
    {
        return !m_pending.empty() || is_next_symbol_fec();
    }

    Symbol generate_fec_symbol()
    {
        auto symbol = m_encoder.generate_fec_symbol();
        std::cout << ">> -- Recovery: crc=" << show_crc32{to_sv(symbol.first)}
            << " rl=" << (int)reliability_level() << std::endl;
        return symbol;
    }

    bool is_next_symbol_fec() const
    {
        int const fec_before = ceil((m_segment_fec_index + 1) / FEC_RATIO);

        return m_segment_chunk_index1 == fec_before;
    }

    Symbol get_symbol()
    {
        // Within the current segment, determine which to send:
        // - original data chunk #m_segment_chunk_index1
        // - recovery symbol #m_segment_fec_index
        if(is_next_symbol_fec())
        {
            m_segment_fec_index++;
            m_segment_fec_index %= FEC_RATIO.numerator(); // -> [0, f)

            return generate_fec_symbol();
        }
        else
        {
            ENFORCE(!m_pending.empty());
            Bytes chunk = pop_value(m_pending);
            auto index = m_encoder.add_chunk(to_sv(chunk));
            m_next_index = SIAMESE_PACKET_NUM_INC(index);

            m_segment_chunk_index1 %= FEC_RATIO.denominator();
            m_segment_chunk_index1++;  // -> [1, d]

            std::cout
                << ">> -- Data: ix=" << index << " "
                << show_crc32{to_sv(chunk)}
                << " rl=" << (int)reliability_level()
                << std::endl;
                
            return { chunk, index };
        }
    }

    void process_ack(std::string_view symbol)
    {
        // TODO: what to do with out-of-order ACKs?!
        m_receiver_expects = m_encoder.process_ack(symbol);
        std::cout << "<< -- Ack: " << m_receiver_expects << std::endl;
    }

private:
    std::queue<Bytes> m_pending;
    StreamFecEncoder m_encoder;
    packet_index_t m_receiver_expects = 0;
    packet_index_t m_next_index = 0;

    // If FEC_RATIO = f/d, given a segment of d chunks, FEC packet number k,
    // where k ∈ [0, f), goes just before chunk number ceil((k+1)d/f)
    int m_segment_chunk_index1 = 0; // Offset by 1: 0 1 2 | 3 1 2 | 3 1 2 | ...
    int m_segment_fec_index = 0;
};

struct ContinuousStream
{
    ContinuousStreamDecoder m_decoder;
    ContinuousStreamEncoder m_encoder;
};