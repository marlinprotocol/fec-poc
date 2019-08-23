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
            
            Receiver r(io_context, server, 2000);
            node.queue_random_block(channel, 456, options.at("size").as<int>(), 'a', 1.2, r);
            io_context.run();
        }
        else if(action == "subscribe")
        {
            auto server = udp::endpoint(asio::ip::make_address("127.0.0.1"),
                options.at("connect").as<int>());
            node.subscribe(channel, server, options.at("kbps").as<int>());
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