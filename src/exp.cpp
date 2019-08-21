#include <climits>
#include <random>
#include <memory>
#include <type_traits>
#include <vector>
#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <ratio>
#include <queue>

#include <boost/rational.hpp>

#include "fec.hpp"
#include "utility.hpp"

#define ENFORCE(_expr_) (void)((_expr_) || (throw std::runtime_error(#_expr_), 0))

int const N_PACKETS = 50;
int const MAX_BLOCK_SIZE_MIN = 10;
int const MAX_BLOCK_SIZE_MAX = MAX_BLOCK_SIZE;
int const ACK_EVERY = 3;
auto const FEC_RATIO = boost::rational<int>(2, 5); // recovery/original
int const LOSE_EVERY = 6;
int const IN_FLIGHT = 10;

using packet_index_t = StreamFecDecoder::packet_index_t;

class ChunkSource
{
public:
    Chunk get_chunk()
    {
        Chunk chunk = random_chunk();
        m_chunks.push_back(chunk);
        return chunk;
    }

    bool verify_chunk(Chunk const& chunk, packet_index_t pix) const
    {
        return chunk == m_chunks.at(pix);
    }

private:
    std::vector<Chunk> m_chunks;

    static Chunk random_chunk()
    {
        static auto engine = make_random_engine<std::mt19937>();

        std::independent_bits_engine<decltype(engine), CHAR_BIT, unsigned char> bytes;
        std::uniform_int_distribution<> sizes(MAX_BLOCK_SIZE_MIN, MAX_BLOCK_SIZE_MAX);

        Chunk symbol(sizes(engine));
        std::generate(symbol.begin(), symbol.end(), bytes);
        return symbol;
    }
};

// Breaks down the stream into segments defined by FEC_RATIO,
// emits original and recovery symbols
class StreamSender
{
public:
    using Symbol = std::pair<Chunk, packet_index_t>;

    enum class ReliabilityLevel {
        UNDER_RATIO,  // If chunks stop arriving, should send FEC symbols
        AT_RATIO,     // Could send FEC symbols if nothing better to do
        ALL_ACKED,    // No sense in sending extra symbols
    };

    void queue_chunk(Chunk chunk)
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

    Symbol generate_fec_symbol()
    {
        auto symbol = m_encoder.generate_fec_symbol();
        std::cout << ">> -- Recovery: crc=" << show_crc32{to_sv(symbol.first)}
            << " rl=" << (int)reliability_level() << std::endl;
        return symbol;
    }

    Symbol get_symbol()
    {
        // Within the current segment, determine which to send:
        // - original data chunk #m_segment_chunk_index1
        // - recovery symbol #m_segment_fec_index
        
        int const fec_before = ceil((m_segment_fec_index + 1) / FEC_RATIO);

        if(m_segment_chunk_index1 == fec_before)
        {
            m_segment_fec_index++;
            m_segment_fec_index %= FEC_RATIO.numerator(); // -> [0, f)

            return generate_fec_symbol();
        }
        else
        {
            Chunk chunk = pop_value(m_pending);
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
    std::queue<Chunk> m_pending;
    StreamFecEncoder m_encoder;
    packet_index_t m_receiver_expects = 0;
    packet_index_t m_next_index = 0;

    // If FEC_RATIO = f/d, given a segment of d chunks, FEC packet number k,
    // where k ∈ [0, f), goes just before chunk number ceil((k+1)d/f)
    int m_segment_chunk_index1 = 0; // Offset by 1: 0 1 2 | 3 1 2 | 3 1 2 | ...
    int m_segment_fec_index = 0;
};

class StreamReceiver
{
public:
    using packet_index_t = StreamFecDecoder::packet_index_t;

    StreamReceiver(std::function<bool(Chunk const &, packet_index_t)> verifier):
        m_verifier(verifier)
    {
    }

    std::vector<char> process_symbol(std::string_view symbol, packet_index_t index)
    {
        std::cout
            << "-- >>   Received: ix=" << index
            << " crc=" << show_crc32{symbol}
            << " size=" << symbol.size()
            << std::endl;
        m_decoder.process_symbol(symbol, index);

        if(index != StreamFecDecoder::PACKET_INDEX_FEC)
        {
            store({ symbol.begin(), symbol.end() }, index);
        }

        while(m_decoder.has_data())
        {
            std::cout << "-- **   We have data!" << std::endl;
            for(auto const& [chunk, pix] : m_decoder.get_new_chunks())
            {
                std::cout
                    << "-- **   Decoded: ix=" << pix
                    << " crc=" << show_crc32{to_sv(chunk)}
                    << " size=" << to_sv(chunk).size()
                    << std::endl;

                store(chunk, pix);
            }
        }

        if(++n_chunks_received % ACK_EVERY == 0)
        {
            std::cout << "-- <<   Ack" << std::endl;
            return m_decoder.generate_ack();
        }
        return {};
    }

    void store(Chunk const& chunk, packet_index_t pix)
    {
        if(pix >= m_decoded.size())
        {
            m_decoded.resize(pix + 1);
        }
        ENFORCE(m_decoded[pix].empty() || m_decoded[pix] == chunk);
        ENFORCE(m_verifier(chunk, pix));

        m_decoded[pix] = chunk;
    }

    void report() const
    {
        unsigned n = 0;
        std::cout << "Receiver:";
        for(unsigned received = 0; received < m_decoded.size(); ++received)
        {
            Chunk const& c = m_decoded[received];
            if(c.empty())
            {
                continue;
            }

            ENFORCE(m_verifier(c, received));

            std::cout << " " << received;
            ++n;
        }
        std::cout << std::endl;
        std::cout << "Receiver: n=" << n << " size=" << m_decoded.size() << std::endl;
    }

private:
    StreamFecDecoder m_decoder;
    std::vector<Chunk> m_decoded;
    std::function<bool(Chunk const &, packet_index_t)> m_verifier;
    int n_chunks_received = 0;
};

int main()
{
    try
    {
        std::cout << "Hi there!" << std::endl;

        fec_init();

        using packet_index_t = StreamFecEncoder::packet_index_t;

        ChunkSource source;
        StreamSender sender;
        StreamReceiver sink([&](Chunk const& chunk, packet_index_t pix) {
            return source.verify_chunk(chunk, pix);
        });

        for(int i = 0; i < N_PACKETS; ++i)
        {
            auto chunk = source.get_chunk();
            sender.queue_chunk(chunk);
        }

        std::queue<std::pair<Chunk, packet_index_t>> in_flight;
        int sent = 0;
        for(; sent < IN_FLIGHT; ++sent)
        {
            in_flight.push(sender.get_symbol());
        }
        
        for(int received = 0; !in_flight.empty(); ++received)
        {
            if(sent < N_PACKETS) // Includes recovery packets!
            {
                in_flight.push(sender.get_symbol());
                ++sent;
            }

            auto symbol = pop_value(in_flight);

            if((received + 1) % LOSE_EVERY == 0)
            {
                std::cout << "-- XX   Oops, lost: ix=" << symbol.second
                    << " crc=" << show_crc32{to_sv(symbol.first)} << std::endl;
            }
            else
            {
                auto ack = sink.process_symbol(to_sv(symbol.first), symbol.second);
                if(ack.size())
                {
                    sender.process_ack(to_sv(ack));
                }
            }
        }
        std::cout << "sent=" << sent
            << " rl=" << (int)sender.reliability_level() << std::endl;

        sink.report();
        std::cout << "All was OK!" << std::endl;

    }
    catch(std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
    }
}
