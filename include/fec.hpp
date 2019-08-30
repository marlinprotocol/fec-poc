#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include <wirehair.h>
#include <siamese.h>

#include "utility.hpp"

#define ENFORCE(_expr_) (void)((_expr_) || (throw std::runtime_error(#_expr_), 0))

template <class CPtr, auto delete_function>
using PtrWithDeleteFunction = std::unique_ptr<
    std::remove_pointer_t<CPtr>,
    std::integral_constant<decltype(delete_function), delete_function>
>;

// === Terminology ===
//
// We start out with:
// - either a BLOCK (a certain number of bytes)
// - or a STREAM (an indefinite sequence of bytes)
// Split the above into CHUNKS (smaller byte sequences) sequentially numbered.
// In case of a BLOCK, there will be N CHUNKS of equal size (except last chunk).
// The goal is for the other party to receive all the CHUNKS and reassemble
// the BLOCK or the STREAM in correct order.
// FEC generates us SYMBOLS, each of which contains DATA and an INDEX.
// Some SYMBOLS just contain the original data (DATA[INDEX] == CHUNKS[INDEX]),
// others contain some bytes that FEC will use to recover the original data.
// The latter condition is signified by the index being >= N for block FEC
// or being equal to a special value for stream FEC.
// The SYMBOLS will likely be sent to the receiver in PACKETS, but this module
// doesn’t care about transport.

// === BlockFec workflow ===
//
// The same class works as both encoder and decoder.
// On the sending side:
// - Construct with the entire block.
// - Call get_symbol_data to obtain FEC symbols.
//
// On the receiving side:
// - Construct with size option only.
// - Feed symbols to process_symbol() until nonempty result returned
//   (which is the recovered block).
// - Call get_symbol_data to obtain original data and FEC symbols.
class BlockFec
{
public:
    BlockFec(std::string_view block)
    {
        char const* as_pchar = &*block.begin();
        m_wirehair.reset(
            wirehair_encoder_create(
                m_wirehair.get(),
                as_pchar,
                block.size(),
                MAX_BLOCK_PACKET_SIZE
            )
        );
        ENFORCE(m_wirehair.get());
    }

    BlockFec(std::uint64_t block_size):
        m_block_size(block_size)
    {
        m_wirehair.reset(
            wirehair_decoder_create(
                m_wirehair.get(),
                block_size,
                MAX_BLOCK_PACKET_SIZE
            )
        );
        ENFORCE(m_wirehair.get());
    }

    Bytes get_symbol_data(unsigned symbol_index)
    {
        Bytes res(MAX_BLOCK_PACKET_SIZE);
        std::uint32_t bytes_written;
        ENFORCE(wirehair_encode(
            m_wirehair.get(),
            symbol_index,
            &res[0],
            res.size(),
            &bytes_written
        ) == 0);
        res.resize(bytes_written);
        return res;
    }

    std::vector<char> process_symbol(std::string_view data, unsigned symbol_index)
    {
        WirehairResult res = wirehair_decode(
            m_wirehair.get(),
            symbol_index,
            &*data.begin(),
            data.size()
        );

        if(res == Wirehair_NeedMore)
        {
            return {};
        }

        if(res == Wirehair_Success)
        {
            std::vector<char> res(m_block_size);
            ENFORCE(wirehair_recover(
                m_wirehair.get(),
                &res[0],
                res.size()
            ) == 0);
            ENFORCE(wirehair_decoder_becomes_encoder(m_wirehair.get()) == 0);
            return res;
        }

        throw std::runtime_error(wirehair_result_string(res));
    }

private:
    std::uint32_t m_block_size;
    PtrWithDeleteFunction<WirehairCodec, wirehair_free> m_wirehair;
};

class StreamFecCommon
{
public:
    using packet_index_t = unsigned;
    static packet_index_t constexpr PACKET_INDEX_FEC = packet_index_t(-1);
};

class StreamFecEncoder: public StreamFecCommon
{
public:
    StreamFecEncoder(): m_encoder(siamese_encoder_create())
    {
    }

    packet_index_t add_chunk(std::string_view data)
    {
        SiameseOriginalPacket packet = {
            0, // will be filled in by Siamese
            (unsigned)data.size(),
            char_cast<unsigned char const *>(data.data())
        };
        //std::cout << "add_chunk: " << packet << std::endl;
        ENFORCE(siamese_encoder_add(m_encoder.get(), &packet) == 0);
        return packet.PacketNum;
    }

    std::pair<std::vector<char>, packet_index_t> generate_fec_symbol()
    {
        SiameseRecoveryPacket fec_packet;
        ENFORCE(siamese_encode(m_encoder.get(), &fec_packet) == 0);

        auto sv = to_sv(fec_packet);
        return { { begin(sv), end(sv) }, PACKET_INDEX_FEC };


        // std::string_view result;
        // packet_index_t symbol_index;

        // SiameseOriginalPacket original_packet;
        // SiameseResult retransmit_result = siamese_encoder_retransmit(
        //     m_encoder.get(), &original_packet);

        // if(retransmit_result == Siamese_NeedMoreData)
        // {
        //     SiameseRecoveryPacket fec_packet;
        //     ENFORCE(siamese_encode(m_encoder.get(), &fec_packet) == 0);

        //     result = to_sv(fec_packet);
        //     symbol_index = PACKET_INDEX_FEC;
        // }
        // else
        // {
        //     ENFORCE(retransmit_result == Siamese_Success);

        //     result = to_sv(original_packet);
        //     symbol_index = original_packet.PacketNum;
        // }

        // return { { begin(result), end(result) }, symbol_index };
    }

    unsigned process_ack(std::string_view ack_message)
    {
        unsigned next_packet_index;
        ENFORCE(siamese_encoder_ack(
            m_encoder.get(),
            char_cast<void const *>(ack_message.data()),
            ack_message.size(),
            &next_packet_index
        ) == 0);
        return next_packet_index;
    }

private:
    PtrWithDeleteFunction<SiameseEncoder, siamese_encoder_free> m_encoder;
};

class StreamFecDecoder: public StreamFecCommon
{
public:
    StreamFecDecoder(): m_decoder(siamese_decoder_create())
    {
    }

    void process_symbol(std::string_view data, packet_index_t index)
    {
        if(index == PACKET_INDEX_FEC)
        {
            SiameseRecoveryPacket packet = {
                (unsigned)data.size(),
                char_cast<unsigned char const *>(data.data())
            };
            //std::cout << "Decoder::ps[r] " << packet << std::endl;
            ENFORCE(siamese_decoder_add_recovery(
                m_decoder.get(),
                &packet
            ) == 0);
        }
        else
        {
            SiameseOriginalPacket packet = {
                index,
                (unsigned)data.size(),
                char_cast<unsigned char const *>(data.data())
            };
            //std::cout << "Decoder::ps[o] " << packet << std::endl;
            auto res = siamese_decoder_add_original(
                m_decoder.get(),
                &packet
            );
            ENFORCE(res == 0 || res == Siamese_DuplicateData);
        }
    }

    Bytes get_chunk(packet_index_t index)
    {
        SiameseOriginalPacket packet = { index, 0u, nullptr };
        ENFORCE(siamese_decoder_get(
            m_decoder.get(),
            &packet
        ) == 0);
        auto sv = to_sv(packet);
        return { begin(sv), end(sv) };
    }

    std::vector<std::pair<Bytes, packet_index_t>> get_new_chunks()
    {
        SiameseOriginalPacket* packets = nullptr;
        unsigned n_packets;
        ENFORCE(siamese_decode(
            m_decoder.get(),
            &packets,
            &n_packets
        ) == 0);
        
        std::vector<std::pair<Bytes, packet_index_t>> result;
        for(auto packet = packets; n_packets--; ++packet)
        {
            //std::cout << *packet << std::endl;
            auto sv = to_sv(*packet);
            result.push_back({
                { begin(sv), end(sv) },
                packet->PacketNum
            });
        }
        return result;
    }

    bool has_data()
    {
        switch(siamese_decoder_is_ready(m_decoder.get()))
        {
        case Siamese_Success:
            return true;

        case Siamese_NeedMoreData:
            return false;

        default:
            throw std::runtime_error("siamese_decoder_is_ready");
        }
    }

    std::vector<char> generate_ack()
    {
        std::vector<char> message(MAX_BLOCK_PACKET_SIZE); // TODO: better max
        unsigned bytes_written;
        ENFORCE(siamese_decoder_ack(
            m_decoder.get(),
            &message[0],
            message.size(),
            &bytes_written
        ) == 0);
        message.resize(bytes_written);
        return message;
    }

private:
    PtrWithDeleteFunction<SiameseDecoder, siamese_decoder_free> m_decoder;
};

inline void fec_init()
{
    wirehair_init();
    siamese_init();
}