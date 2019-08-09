#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include <wirehair.h>

#define ENFORCE(_expr_) (void)((_expr_) || (throw std::runtime_error(#_expr_), 0))

struct WirehairDeleter
{
    void operator()(WirehairCodec wc) {
        wirehair_free(wc);
    }
};

using WirehairPtr = std::unique_ptr<
    std::remove_pointer_t<WirehairCodec>,
    WirehairDeleter
>;

// Workflow:
// - Either:
//   - construct with the entire block
//   - or:
//     - construct with size option only
//     - feed chunks to process_chunk() until nonempty result returned
//       (which is the recovered block)
// - Call get_chunk to obtain FEC chunks
template <std::size_t CHUNK_SIZE>
class Fec
{
public:
    Fec(std::string_view block)
    {
        char const* as_pchar = &*block.begin();
        m_wirehair.reset(
            wirehair_encoder_create(
                m_wirehair.get(),
                as_pchar,
                block.size(),
                CHUNK_SIZE
            )
        );
        ENFORCE(m_wirehair.get());
    }

    Fec(std::uint64_t block_size):
        m_block_size(block_size)
    {
        m_wirehair.reset(
            wirehair_decoder_create(
                m_wirehair.get(),
                block_size,
                CHUNK_SIZE
            )
        );
        ENFORCE(m_wirehair.get());
    }

    std::vector<char> get_chunk(unsigned chunk_index)
    {
        std::vector<char> res(CHUNK_SIZE);
        std::uint32_t bytes_written;
        ENFORCE(wirehair_encode(
            m_wirehair.get(),
            chunk_index,
            &res[0],
            res.size(),
            &bytes_written
        ) == 0);
        res.resize(bytes_written);
        return res;
    }

    std::vector<char> process_chunk(std::string_view chunk, unsigned chunk_index)
    {
        WirehairResult res = wirehair_decode(
            m_wirehair.get(),
            chunk_index,
            &*chunk.begin(),
            chunk.size()
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
    WirehairPtr m_wirehair;
};

inline void fec_init()
{
    wirehair_init();
}