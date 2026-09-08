#include "pool_order_book.hpp"
#include "tcp_server.hpp"

#include <csignal>
#include <iostream>
#include <memory>

std::unique_ptr<titan::TcpServer> g_server;

void handle_signal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        std::cout << "\n[INFO] Shutdown signal received. Stopping gateway...\n";
        if (g_server)
        {
            g_server->stop();
        }
    }
}

int main(int argc, char **argv)
{
    uint16_t port = 8080;
    std::string host = "0.0.0.0";

    if (argc > 1)
    {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    if (argc > 2)
    {
        host = argv[2];
    }

    std::cout << "=========================================================\n";
    std::cout << "            TITANMATCH: LOW-LATENCY ORDER GATEWAY        \n";
    std::cout << "=========================================================\n";
    std::cout << " Engine: PoolOrderBook (Contiguous Memory + Intrusive Free-List)\n";
    std::cout << " Network: Linux Non-Blocking Epoll + TCP_NODELAY\n";
    std::cout << " Binding to: " << host << ":" << port << "\n";
    std::cout << "=========================================================\n";

    titan::PoolOrderBook order_book(1000000);
    g_server = std::make_unique<titan::TcpServer>(host, port, order_book);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!g_server->start())
    {
        std::cerr << "[FATAL] Gateway failed to start.\n";
        return 1;
    }

    std::cout << "[INFO] TitanMatch Gateway exited cleanly.\n";
    return 0;
}
