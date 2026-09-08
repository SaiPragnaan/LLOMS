#include "pool_order_book.hpp"
#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion FAILED at " << __FILE__ << ":" << __LINE__ << " -> " << #cond << "\n"; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

int create_client_socket(uint16_t port) {
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

std::string read_response(int sock) {
    char buf[1024];
    ssize_t bytes = read(sock, buf, sizeof(buf) - 1);
    if (bytes <= 0) return "";
    buf[bytes] = '\0';
    return std::string(buf);
}

void send_cmd(int sock, const std::string& cmd) {
    ssize_t sent = write(sock, cmd.data(), cmd.size());
    ASSERT_EQ(sent, static_cast<ssize_t>(cmd.size()));
}

void test_gateway_flow() {
    std::cout << "[TEST] Starting Order Gateway on background thread...\n";
    titan::PoolOrderBook order_book(10000);
    uint16_t test_port = 9099;
    titan::TcpServer server("127.0.0.1", test_port, order_book);

    std::thread server_thread([&]() {
        server.start();
    });

    // Allow server to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Test 1: Single Client Order & Cancel
    {
        std::cout << "[TEST 1] Single client submit and cancel...\n";
        int client1 = create_client_socket(test_port);

        // Submit Limit Buy
        send_cmd(client1, "NEW BUY 1001 10000 50 LIMIT\n");
        std::string resp = read_response(client1);
        ASSERT_EQ(resp, "ACK 1001 50\n");

        // Cancel order
        send_cmd(client1, "CANCEL 1001\n");
        resp = read_response(client1);
        ASSERT_EQ(resp, "CANCELED 1001\n");

        // Cancel non-existent order
        send_cmd(client1, "CANCEL 1001\n");
        resp = read_response(client1);
        ASSERT_EQ(resp, "REJECT 1001 ORDER_NOT_FOUND\n");

        close(client1);
    }

    // Test 2: Two Concurrent Clients Trading
    {
        std::cout << "[TEST 2] Two concurrent clients trading over TCP...\n";
        int maker_client = create_client_socket(test_port);
        int taker_client = create_client_socket(test_port);

        // Maker posts Limit Sell: 100 @ 101.00 (10100)
        send_cmd(maker_client, "NEW SELL 2001 10100 100 LIMIT\n");
        std::string resp = read_response(maker_client);
        ASSERT_EQ(resp, "ACK 2001 100\n");

        // Taker posts crossing Limit Buy: 40 @ 101.00 (10100)
        send_cmd(taker_client, "NEW BUY 2002 10100 40 LIMIT\n");
        resp = read_response(taker_client);
        ASSERT_EQ(resp, "TRADE 2001 2002 10100 40 BUY\n");

        close(maker_client);
        close(taker_client);
    }

    // Test 3: TCP Packet Fragmentation & Framing
    {
        std::cout << "[TEST 3] TCP packet fragmentation / partial read handling...\n";
        int client = create_client_socket(test_port);

        // Send partial chunk 1
        send_cmd(client, "NEW BU");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Send partial chunk 2
        send_cmd(client, "Y 3001 9500 25 LIMIT\n");
        std::string resp = read_response(client);
        ASSERT_EQ(resp, "ACK 3001 25\n");

        close(client);
    }

    // Stop server cleanly
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "        RUNNING TITANMATCH ORDER GATEWAY TESTS           \n";
    std::cout << "=========================================================\n";

    test_gateway_flow();

    std::cout << "=========================================================\n";
    std::cout << " ALL ORDER GATEWAY TESTS PASSED SUCCESSFULLY!            \n";
    std::cout << "=========================================================\n";
    return 0;
}

