#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include "fec.hpp"
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
        else if(action == "publish")
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