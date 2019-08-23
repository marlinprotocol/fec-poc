#include <memory>
#include <type_traits>
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <queue>

#include <siamese.h>

#include "fec.hpp"
#include "utility.hpp"

#define ENFORCE(_expr_) (void)((_expr_) || (throw std::runtime_error(#_expr_), 0))

int const MAX_BLOCK_PACKET_SIZE_MIN = 10;
int const MAX_BLOCK_PACKET_SIZE_MAX = MAX_BLOCK_PACKET_SIZE;

Chunk random_chunk()
{
    static auto engine = make_random_engine<std::mt19937>();

    std::independent_bits_engine<decltype(engine), CHAR_BIT, unsigned char> bytes;
    std::uniform_int_distribution<> sizes(MAX_BLOCK_PACKET_SIZE_MIN, MAX_BLOCK_PACKET_SIZE_MAX);

    Chunk message(sizes(engine));
    std::generate(message.begin(), message.end(), bytes);
    return message;
}


int main()
{
    try
    {
        std::cout << "Hi there!" << std::endl;

        fec_init();

        std::vector<Chunk> chunks(5);
        std::generate(chunks.begin(), chunks.end(), random_chunk);

        StreamFecEncoder enc;
        for(auto const& chunk : chunks)
        {
            enc.add_chunk(to_sv(chunk));
        }

        StreamFecDecoder dec;
        for(unsigned i = 0; i < chunks.size(); ++i)
        {
            dec.process_symbol(to_sv(chunks[i]), i);
        }

        auto ack = dec.generate_ack();
        auto next = enc.process_ack(to_sv(ack));

        std::cout << next << std::endl;

        std::cout << "All was OK!" << std::endl;

    }
    catch(std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
    }
}
