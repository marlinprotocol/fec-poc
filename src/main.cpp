#include <iostream>

#include "fec.hpp"
#include "net.hpp"

int main()
{
    std::cout << "Hi there!" << std::endl;
    std::cout << "Max payload size: " << Packet::MAX_PAYLOAD_SIZE << std::endl;
    try
    {
        fec_init(); 

        boost::asio::io_context io_context;

        Node s(io_context, 10001);

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