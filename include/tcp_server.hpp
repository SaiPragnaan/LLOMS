#pragma once

#include "ingress_message.hpp"
#include "pool_order_book.hpp"
#include "protocol.hpp"
#include "spsc_queue.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace titan {

struct ClientConnection {
    int fd{-1};
    std::string read_buffer;
    std::string write_buffer;
};

class TcpServer {
public:
    TcpServer(std::string host, uint16_t port, PoolOrderBook& order_book, size_t queue_capacity = 65536);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start();
    void stop();

    // Runs a single iteration of epoll event polling & response draining
    int poll_events(int timeout_ms = 100);

    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] size_t connected_clients() const noexcept { return clients_.size(); }
    [[nodiscard]] uint64_t processed_orders_count() const noexcept { return processed_orders_.load(); }

    // Queue monitoring
    [[nodiscard]] size_t ingress_queue_depth() const noexcept { return ingress_queue_.size(); }
    [[nodiscard]] size_t response_queue_depth() const noexcept { return response_queue_.size(); }

private:
    std::string host_;
    uint16_t port_;
    PoolOrderBook& order_book_;

    int server_fd_{-1};
    int epoll_fd_{-1};
    std::atomic<bool> running_{false};

    // Monotonic sequence numbering on ingress
    uint64_t next_sequence_{1};
    std::atomic<uint64_t> processed_orders_{0};

    // Lock-Free Bounded SPSC Queues
    SpscQueue<IngressMessage> ingress_queue_;
    SpscQueue<ResponseMessage> response_queue_;

    // Dedicated Matching Thread
    std::thread matching_thread_;

    std::unordered_map<int, ClientConnection> clients_;

    bool setup_server_socket();
    void handle_new_connection();
    void handle_client_read(int client_fd);
    void handle_client_write(int client_fd);
    void close_connection(int client_fd);

    void process_client_line(ClientConnection& client, std::string_view line);
    void flush_client_write_buffer(ClientConnection& client);
    void drain_response_queue();

    // Loop executed exclusively by matching thread
    void matching_thread_loop();
    void execute_command(const IngressMessage& msg);
};

} // namespace titan
