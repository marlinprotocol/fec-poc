#include <iostream>
#include <string>

#include "fec.hpp"
#include "net.hpp"

int main(int argc, char** argv)
{
    std::cout << "Hi there!" << std::endl;
    std::cout << "Max payload size: " << Packet::MAX_PAYLOAD_SIZE << std::endl;

    if(argc != 3) {
        std::cout << "Usage: $0 <type> <port>" << std::endl;
        return 1;
    }

    try
    {
        std::string action = argv[1];
        int port = std::stoi(argv[2]);

        fec_init();

        boost::asio::io_context io_context;

        auto server = udp::endpoint(asio::ip::make_address("127.0.0.1"), 10000);
        int channel = 123;

        Node node(io_context, port);
        if(action == "proxy")
        {
            node.listen();
        }
        else if(action == "publish")
        {
            node.queue_random_block(channel, 456, 1777, 'a', 4, server);
            node.send_queued();
        }
        else if(action == "subscribe")
        {
            node.subscribe(channel, server);
            node.listen();
        }
        else
        {
            std::cout << "Unrecognized command!" << std::endl;
            return 1;
        }
        

        io_context.run();
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