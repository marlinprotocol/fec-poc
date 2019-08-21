#include <memory>
#include <type_traits>
#include <vector>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <queue>

#include <siamese.h>

#include "fec.hpp"
#include "utility.hpp"

#define ENFORCE(_expr_) (void)((_expr_) || (throw std::runtime_error(#_expr_), 0))

std::ifstream urandom("/dev/urandom");

Chunk random_chunk()
{
    Chunk message(MAX_BLOCK_SIZE);
    urandom.read(&message[0], message.size());
    return message;
}

int const N_PACKETS = 50;
int const ACK_EVERY = 3;
int const FEC_PACKET_EVERY = 5;
int const LOSE_EVERY = 7;
int const IN_FLIGHT = 10;

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

class ChunkSource
{
public:
    std::pair<Chunk, StreamFecEncoder::packet_index_t> get_symbol()
    {
        // About to send regular packet #m_packets_sent
        if(m_packets_sent && m_packets_sent % FEC_PACKET_EVERY == 0 && !m_fec_sent)
        {
            m_fec_sent = true;
            auto message = m_encoder.generate_fec_symbol();
            std::cout << ">> -- Recovery: ix=" << message.second
                << " crc=" << show_crc32{to_sv(message.first)} << std::endl;
            return message;
        }
        else
        {
            m_fec_sent = false;
            Chunk message = random_chunk();

            ++m_packets_sent;
            auto sent_as = m_encoder.add_chunk(to_sv(message));

            std::cout
                << ">> -- Data: ix=" << sent_as << " "
                << show_crc32{to_sv(message)}
                << std::endl;
                
            return { message, sent_as };
        }
    }

    void process_ack(std::string_view message)
    {
        (void)message;
        //auto n = m_encoder.process_ack(message);
        //std::cout << "<< -- Ack: " << n << std::endl;
    }

private:
    int m_packets_sent = 0;
    bool m_fec_sent = false;
    StreamFecEncoder m_encoder;
};

class ChunkSink
{
public:
    using packet_index_t = StreamFecDecoder::packet_index_t;

    std::vector<char> process_chunk(std::string_view message, packet_index_t index)
    {
        std::cout
            << "-- >> Received: ix=" << index
            << " crc=" << show_crc32{message}
            << " size=" << message.size()
            << std::endl;
        m_decoder.process_symbol(message, index);

        while(m_decoder.has_data())
        {
            std::cout << "-- ** We have data!" << std::endl;
            for(auto const& [chunk, pix] : m_decoder.get_new_chunks())
            {
                std::cout
                    << "-- ** Decoded: ix=" << pix
                    << " crc=" << show_crc32{to_sv(chunk)}
                    << " size=" << to_sv(chunk).size()
                    << std::endl;
            }
        }

        if(++n_chunks_received % ACK_EVERY == 0)
        {
            std::cout << "-- << Ack" << std::endl;
            return m_decoder.generate_ack();
        }
        return {};
    }

private:
    StreamFecDecoder m_decoder;
    int n_chunks_received = 0;
};

int main()
{
    try
    {
        std::cout << "Hi there!" << std::endl;

        siamese_init();

        ChunkSource source;
        ChunkSink sink;
        using packet_index_t = StreamFecEncoder::packet_index_t;

        std::queue<std::pair<Chunk, packet_index_t>> in_flight;
        for(int i = 0; i < IN_FLIGHT; ++i)
        {
            in_flight.push(source.get_symbol());
        }

        for(int i = 0; i < N_PACKETS; ++i)
        {
            in_flight.push(source.get_symbol());
            auto message = in_flight.front();
            in_flight.pop();

            if((i + 1) % LOSE_EVERY == 0)
            {
                std::cout << "Oops, lost: ix=" << message.second
                    << " crc=" << show_crc32{to_sv(message.first)} << std::endl;
            }
            else
            {
                auto ack = sink.process_chunk(to_sv(message.first), message.second);
                if(ack.size())
                {
                    source.process_ack(to_sv(ack));
                }
            }
        }

        std::cout << "All was OK!" << std::endl;

    }
    catch(std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
    }
}
