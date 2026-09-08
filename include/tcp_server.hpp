#pragma once

#include "pool_order_book.hpp"
#include "protocol.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace titan
{

    struct ClientConnection
    {
        int fd{-1};
        std::string read_buffer;
        std::string write_buffer;
    };

    class TcpServer
    {
    public:
        TcpServer(std::string host, uint16_t port, PoolOrderBook &order_book);
        ~TcpServer();

        TcpServer(const TcpServer &) = delete;
        TcpServer &operator=(const TcpServer &) = delete;

        // Starts listening and running the epoll event loop
        // If run_in_background is false, blocks until stop() is called.
        bool start();
        void stop();

        // Runs a single iteration of epoll event polling (useful for tests/step-by-step)
        int poll_events(int timeout_ms = 100);

        [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
        [[nodiscard]] uint16_t port() const noexcept { return port_; }
        [[nodiscard]] size_t connected_clients() const noexcept { return clients_.size(); }

    private:
        std::string host_;
        uint16_t port_;
        PoolOrderBook &order_book_;

        int server_fd_{-1};
        int epoll_fd_{-1};
        std::atomic<bool> running_{false};

        std::unordered_map<int, ClientConnection> clients_;

        bool setup_server_socket();
        void handle_new_connection();
        void handle_client_read(int client_fd);
        void handle_client_write(int client_fd);
        void close_connection(int client_fd);

        void process_client_line(ClientConnection &client, std::string_view line);
        void flush_client_write_buffer(ClientConnection &client);
    };

}
