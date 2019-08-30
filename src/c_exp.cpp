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

Bytes random_chunk()
{
    Bytes message(MAX_BLOCK_PACKET_SIZE);
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
    std::pair<Bytes, StreamFecEncoder::packet_index_t> get_symbol()
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
            Bytes message = random_chunk();

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
        using UChunk = std::vector<unsigned char>;
        std::cout << "Hi there!" << std::endl;

        std::cout << siamese_init() << std::endl;
        std::cout << wirehair_init() << std::endl;

        //StreamFecEncoder encoder;
        SiameseEncoder enc = siamese_encoder_create();
        SiameseDecoder dec = siamese_decoder_create();
        StreamFecDecoder decoder;
        using packet_index_t = StreamFecEncoder::packet_index_t;

        UChunk const same_chunk(1000u, '?');

        std::vector<std::pair<UChunk, packet_index_t>> symbols;

        auto push_original = [&]() {
            SiameseOriginalPacket packet = {
                0, // will be filled in by Siamese
                (unsigned)same_chunk.size(),
                &same_chunk[0]
            };
            ENFORCE(siamese_encoder_add(enc, &packet) == 0);
            symbols.push_back({ same_chunk, packet.PacketNum });
        };

        auto push_fec = [&]() {
            SiameseRecoveryPacket fec_packet;
            ENFORCE(siamese_encode(enc, &fec_packet) == 0);
            symbols.push_back({
                UChunk(fec_packet.Data, fec_packet.Data + fec_packet.DataBytes),
                unsigned(-1)
            });
        };

        push_original();
        push_original();
        push_original();
        push_original();
        push_original();
        push_fec();
        push_original();
        push_original();
        push_original();
        push_original();
        push_original();
        push_fec();
        push_original();
        push_original();
        push_original();
        push_original();
        push_original();
        push_fec();
        push_original();
        push_original();
        push_original();
        push_original();
        push_original();
        push_fec();

        for(unsigned i = 0; i < symbols.size(); ++i)
        {
            auto const& [data, ix] = symbols[i];

            if((i + 1) % LOSE_EVERY == 0)
            {
                std::cout << "Oops, lost: ix=" << ix << std::endl;
            }
            else
            {
                if(ix == unsigned(-1))
                {
                    SiameseRecoveryPacket packet = {
                        (unsigned)data.size(),
                        &data[0]
                    };
                    ENFORCE(siamese_decoder_add_recovery(dec, &packet) == 0);
                }
                else
                {
                    SiameseOriginalPacket packet = {
                        ix,
                        (unsigned)data.size(),
                        &data[0]
                    };
                    ENFORCE(siamese_decoder_add_original(dec, &packet) == 0);                    
                }

                auto ready = siamese_decoder_is_ready(dec);
                if(ready == Siamese_NeedMoreData)
                {
                    continue;
                }
                ENFORCE(ready == 0);


                SiameseOriginalPacket* packet;
                unsigned np;

                siamese_decode(dec, &packet, &np);

                std::cout << "We have data! " << np << std::endl;

                for(; np--; ++packet)
                {
                    std::cout << "Packet! " << packet->PacketNum << " "
                        << packet->DataBytes << std::endl;
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
