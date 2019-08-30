#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include "fec.hpp"
#include "stream.hpp"
#include "net.hpp"


namespace po = boost::program_options;

int main(int argc, char** argv)
{
    std::cout << "Hi there!" << std::endl;
    std::cout << "Max payload size: " << Packet::MAX_PAYLOAD_SIZE << std::endl;

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "help")
        ("action,a", po::value<std::string>(), "action")
        ("port,p", po::value<int>(), "port")
        ("connect,c", po::value<int>(), "server port")
        ("kbps,k", po::value<int>(), "bandwidth")
        ("size,s", po::value<int>(), "packet size")
    ;
    po::variables_map options;
    po::store(po::parse_command_line(argc, argv, desc), options);
    po::notify(options);

    if (options.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }

    try
    {
        auto action = options.at("action").as<std::string>();
        int port = options.at("port").as<int>();

        fec_init();

        boost::asio::io_context io_context;

        int channel = 123;

        Node node(io_context, port);
        if(action == "proxy")
        {
            node.listen();
            io_context.run();
        }
        else if(action == "block")
        {
            auto server = udp::endpoint(asio::ip::make_address("127.0.0.1"),
                options.at("connect").as<int>());
            
            AsioReceiver receiver = node.make_receiver(server, 2000);
            
            std::vector<char> message(options.at("size").as<int>(), 'j');
            std::cout << "New message, crc=" << show_crc32{to_sv(message)} << std::endl;
            
            Block block(to_sv(message));
            for(auto packet : block_packet_range(block, channel, 456, REDUNDANCY))
            {
                receiver.queue_packet(packet.move_data());
            }

            io_context.run();
        }
        else if(action == "stream")
        {
            auto server = udp::endpoint(asio::ip::make_address("127.0.0.1"),
                options.at("connect").as<int>());
            
            AsioReceiver receiver = node.make_receiver(server, 2000);
            
            ContinuousStreamDecoder decoder;
            
            ContinuousStreamEncoder encoder;
            for(auto n = options.at("size").as<int>(); n--; )
            {
                Bytes chunk = random_chunk();
                std::cout << "New chunk, crc=" << show_crc32{to_sv(chunk)} << std::endl;
                encoder.queue_chunk(chunk);
            }

            int lose_counter = 0;
            while(encoder.has_data())
            {
                Symbol symbol = encoder.get_symbol();
                std::cout << "New symbol, crc=" << show_crc32{to_sv(symbol.first)}
                    << " ix=" << symbol.second << " sz=" << symbol.first.size() << std::endl;
                receiver.queue_packet(make_stream_packet(
                    channel,
                    symbol.second,
                    to_sv(symbol.first)
                ).move_data());

                if(lose_counter++ % LOSE_EVERY != 0)
                {
                    decoder.process_symbol(to_sv(symbol.first), symbol.second);
                }
                while(decoder.has_data())
                {
                    Bytes chunk = decoder.get_chunk();
                    std::cout << "Decoded back: crc=" << show_crc32{to_sv(chunk)} << std::endl;
                }
            }

            io_context.run();
        }
        else if(action == "subscribe")
        {
            auto server = udp::endpoint(asio::ip::make_address("127.0.0.1"),
                options.at("connect").as<int>());
            
            AsioReceiver r = node.make_receiver(server, 100'000);
            auto p = make_subscribe_packet(channel, options.at("kbps").as<int>());
            r.queue_packet(p.move_data());

            node.listen();
            io_context.run();
        }
        else if(action == "wtf")
        {
            std::vector<Bytes> const c = {
                random_chunk(),
                random_chunk(),
                random_chunk()
            };

            SiameseEncoder se = siamese_encoder_create();

            ContinuousStreamEncoder encoder;
            encoder.queue_chunk(c[0]);
            encoder.queue_chunk(c[1]);
            encoder.queue_chunk(c[2]);

            for(Bytes const& b : c)
            {
                SiameseOriginalPacket op = {
                    0,
                    (unsigned)b.size(),
                    char_cast<unsigned char const *>(&b[0])
                };
                ENFORCE0(siamese_encoder_is_ready(se));
                ENFORCE0(siamese_encoder_add(se, &op));
                std::cout << op.PacketNum << " " << b.size() << std::endl;
            }

            Symbol const s0 = encoder.get_symbol();
            ENFORCE(s0.first == c[0]);
            ENFORCE(s0.second == 0);

            Symbol const s1 = encoder.get_symbol();
            ENFORCE(s1.first == c[1]);
            ENFORCE(s1.second == 1);

            Symbol const s2 = encoder.get_symbol();
            ENFORCE(s2.first == c[2]);
            ENFORCE(s2.second == 2);

            Symbol const s3 = encoder.get_symbol();
            ENFORCE(s3.second > 9000);
            
            SiameseRecoveryPacket rp;
            siamese_encode(se, &rp);

            std::vector<char> out(
                char_cast<char const *>(rp.Data),
                char_cast<char const *>(rp.Data) + rp.DataBytes
            );

            ENFORCE(out == s3.first);

            std::cout << "out size: " << out.size() << std::endl;

            SiameseDecoder sd = siamese_decoder_create();

            SiameseOriginalPacket op1 = {
                1,
                (unsigned)c[1].size(),
                char_cast<unsigned char const *>(&c[1][0])
            };
            SiameseOriginalPacket op2 = {
                2,
                (unsigned)c[2].size(),
                char_cast<unsigned char const *>(&c[2][0])
            };
            SiameseRecoveryPacket rp1 = {
                (unsigned)out.size(),
                char_cast<unsigned char const *>(&out[0])
            };

            ENFORCE0(siamese_decoder_add_original(sd, &op1));
            ENFORCE0(siamese_decoder_add_original(sd, &op2));

            ENFORCE0(siamese_decoder_add_recovery(sd, &rp1));

            ENFORCE0(siamese_decoder_is_ready(sd));

            SiameseOriginalPacket* ops;
            unsigned count;
            ENFORCE0(siamese_decode(sd, &ops, &count));

            std::cout << "..??..???\n";

            // StreamFecDecoder de;
            // de.process_symbol(to_sv(s1.first), s1.second);
            // de.process_symbol(to_sv(s2.first), s2.second);
            // de.process_symbol(to_sv(s3.first), s3.second);

            // ENFORCE(de.has_data());
            // de.get_new_chunks();
            // std::cout << "???\n";
            
            ContinuousStreamDecoder decoder;
            decoder.process_symbol(to_sv(s1.first), s1.second);
            decoder.process_symbol(to_sv(s2.first), s2.second);
            decoder.process_symbol(to_sv(s3.first), s3.second);

            ENFORCE(decoder.has_data());

            decoder.get_chunk();
        }
        else
        {
            std::cout << "Unrecognized command!" << std::endl;
            return 1;
        }
    }
    catch (boost::system::system_error& e)
    {
        std::cerr << "System error: " << e.code().message() << "\n";
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}