#include "pool_order_book.hpp"
#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion FAILED at " << __FILE__ << ":" << __LINE__ << " -> " << #cond << "\n"; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

int connect_client(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(sock >= 0);

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int res = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ASSERT_TRUE(res == 0);
    return sock;
}

void test_concurrent_multiclient_traffic(uint16_t port, size_t num_clients, size_t orders_per_client) {
    std::cout << "[TEST] Starting " << num_clients << " concurrent TCP clients (" 
              << orders_per_client << " orders each, total=" << (num_clients * orders_per_client) << ")...\n";

    std::vector<std::thread> client_threads;
    std::atomic<size_t> total_acks_received{0};

    for (size_t c = 0; c < num_clients; ++c) {
        client_threads.emplace_back([=, &total_acks_received]() {
            int sock = connect_client(port);

            uint64_t base_id = (c + 1) * 1000000;
            char read_buf[4096];
            std::string incoming;

            for (size_t i = 0; i < orders_per_client; ++i) {
                uint64_t oid = base_id + i;
                // Alternate Buy and Sell around mid-price 10000
                std::string side = (i % 2 == 0) ? "BUY" : "SELL";
                uint64_t price = (i % 2 == 0) ? (9900 + (i % 50)) : (10100 + (i % 50));

                std::string cmd = "NEW " + side + " " + std::to_string(oid) + " " 
                                + std::to_string(price) + " 10 LIMIT\n";
                ssize_t sent = write(sock, cmd.data(), cmd.size());
                ASSERT_EQ(sent, static_cast<ssize_t>(cmd.size()));

                // Read responses
                while (incoming.find('\n') == std::string::npos) {
                    ssize_t bytes = read(sock, read_buf, sizeof(read_buf));
                    if (bytes > 0) {
                        incoming.append(read_buf, static_cast<size_t>(bytes));
                    }
                }

                // Count ACK or TRADE responses
                while (true) {
                    size_t pos = incoming.find('\n');
                    if (pos == std::string::npos) break;
                    std::string line = incoming.substr(0, pos);
                    incoming.erase(0, pos + 1);

                    if (line.rfind("ACK", 0) == 0 || line.rfind("TRADE", 0) == 0) {
                        total_acks_received.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            close(sock);
        });
    }

    for (auto& t : client_threads) {
        t.join();
    }

    std::cout << "[PASS] Multi-client traffic completed. Total responses verified: " 
              << total_acks_received.load() << "\n";
    ASSERT_TRUE(total_acks_received.load() >= (num_clients * orders_per_client));
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << " RUNNING CONCURRENT INGRESS + SPSC INTEGRATION TESTS     \n";
    std::cout << "=========================================================\n";

    uint16_t port = 9098;
    titan::PoolOrderBook order_book(1000000);
    titan::TcpServer server("127.0.0.1", port, order_book);

    std::thread server_thread([&]() {
        server.start();
    });

    // Wait for server to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Run 4 concurrent clients submitting 500 orders each (2,000 orders)
    test_concurrent_multiclient_traffic(port, 4, 500);

    // Stop server cleanly
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "=========================================================\n";
    std::cout << " ALL CONCURRENT INGRESS TESTS PASSED SUCCESSFULLY!       \n";
    std::cout << "=========================================================\n";
    return 0;
}
