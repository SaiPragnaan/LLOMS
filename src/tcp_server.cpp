#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace titan
{

    namespace
    {

        constexpr int MAX_EPOLL_EVENTS = 64;
        constexpr size_t READ_CHUNK_SIZE = 4096;

        bool set_non_blocking(int fd)
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1)
                return false;
            return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
        }

        bool disable_nagle(int fd)
        {
            int flag = 1;
            return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
        }

    } // anonymous namespace

    TcpServer::TcpServer(std::string host, uint16_t port, PoolOrderBook &order_book)
        : host_(std::move(host)), port_(port), order_book_(order_book) {}

    TcpServer::~TcpServer()
    {
        stop();
    }

    bool TcpServer::setup_server_socket()
    {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0)
        {
            std::cerr << "[ERROR] Failed to create socket: " << strerror(errno) << "\n";
            return false;
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
        disable_nagle(server_fd_);
        set_non_blocking(server_fd_);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0)
        {
            addr.sin_addr.s_addr = INADDR_ANY;
        }

        if (bind(server_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            std::cerr << "[ERROR] Failed to bind to " << host_ << ":" << port_ << " -> " << strerror(errno) << "\n";
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        if (listen(server_fd_, SOMAXCONN) < 0)
        {
            std::cerr << "[ERROR] Failed to listen: " << strerror(errno) << "\n";
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0)
        {
            std::cerr << "[ERROR] Failed to create epoll instance: " << strerror(errno) << "\n";
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = server_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) < 0)
        {
            std::cerr << "[ERROR] Failed to add server_fd to epoll: " << strerror(errno) << "\n";
            close(epoll_fd_);
            close(server_fd_);
            server_fd_ = -1;
            epoll_fd_ = -1;
            return false;
        }

        return true;
    }

    bool TcpServer::start()
    {
        if (running_.load())
            return true;

        if (!setup_server_socket())
        {
            return false;
        }

        running_.store(true);
        std::cout << "[INFO] TitanMatch Gateway listening on " << host_ << ":" << port_ << " (epoll)\n";

        while (running_.load())
        {
            poll_events(100);
        }

        // Cleanup resources after event loop terminates
        for (auto &[fd, client] : clients_)
        {
            if (epoll_fd_ >= 0)
            {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            }
            close(fd);
        }
        clients_.clear();

        if (epoll_fd_ >= 0)
        {
            close(epoll_fd_);
            epoll_fd_ = -1;
        }
        if (server_fd_ >= 0)
        {
            close(server_fd_);
            server_fd_ = -1;
        }

        std::cout << "[INFO] TitanMatch Gateway stopped.\n";
        return true;
    }

    void TcpServer::stop()
    {
        running_.store(false);
    }

    int TcpServer::poll_events(int timeout_ms)
    {
        if (epoll_fd_ < 0)
            return 0;

        epoll_event events[MAX_EPOLL_EVENTS];
        int n_ready = epoll_wait(epoll_fd_, events, MAX_EPOLL_EVENTS, timeout_ms);

        for (int i = 0; i < n_ready; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == server_fd_)
            {
                handle_new_connection();
            }
            else if (ev & (EPOLLHUP | EPOLLERR))
            {
                close_connection(fd);
            }
            else
            {
                if (ev & EPOLLIN)
                {
                    handle_client_read(fd);
                }
                if (ev & EPOLLOUT)
                {
                    handle_client_write(fd);
                }
            }
        }

        return n_ready;
    }

    void TcpServer::handle_new_connection()
    {
        while (true)
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

            if (client_fd < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break; // Handled all pending connections
                }
                break;
            }

            set_non_blocking(client_fd);
            disable_nagle(client_fd);

            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            ev.data.fd = client_fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev);

            clients_[client_fd] = ClientConnection{
                .fd = client_fd,
                .read_buffer = "",
                .write_buffer = ""};

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            // std::cout << "[GATEWAY] Client connected: " << ip_str << ":" << ntohs(client_addr.sin_port) << " (fd=" << client_fd << ")\n";
        }
    }

    void TcpServer::handle_client_read(int client_fd)
    {
        auto it = clients_.find(client_fd);
        if (it == clients_.end())
            return;

        ClientConnection &client = it->second;
        char buffer[READ_CHUNK_SIZE];

        while (true)
        {
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
            if (bytes_read > 0)
            {
                client.read_buffer.append(buffer, static_cast<size_t>(bytes_read));
            }
            else if (bytes_read == 0)
            {
                // EOF: Client disconnected cleanly
                close_connection(client_fd);
                return;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break; // Drained socket receive buffer
                }
                // Real socket error
                close_connection(client_fd);
                return;
            }
        }

        // Process all complete lines (\n delimited) in the buffer
        size_t newline_pos;
        while ((newline_pos = client.read_buffer.find('\n')) != std::string::npos)
        {
            std::string_view line(client.read_buffer.data(), newline_pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            process_client_line(client, line);
            client.read_buffer.erase(0, newline_pos + 1);
        }

        flush_client_write_buffer(client);
    }

    void TcpServer::process_client_line(ClientConnection &client, std::string_view line)
    {
        if (line.empty())
            return;

        ParsedCommand cmd = Protocol::parse_line(line);

        if (cmd.type == CommandType::Unknown)
        {
            Protocol::format_reject(0, cmd.error_msg, client.write_buffer);
            return;
        }

        if (cmd.type == CommandType::Flush)
        {
            order_book_.clear();
            Protocol::format_info("BOOK_FLUSHED", client.write_buffer);
            return;
        }

        if (cmd.type == CommandType::Cancel)
        {
            bool ok = order_book_.cancel_order(cmd.cancel_id);
            if (ok)
            {
                Protocol::format_canceled(cmd.cancel_id, client.write_buffer);
            }
            else
            {
                Protocol::format_reject(cmd.cancel_id, "ORDER_NOT_FOUND", client.write_buffer);
            }
            return;
        }

        if (cmd.type == CommandType::New)
        {
            std::vector<Trade> trades;
            bool ok = order_book_.submit_order(cmd.order, trades);

            if (!ok)
            {
                Protocol::format_reject(cmd.order.order_id, "DUPLICATE_OR_INVALID_ORDER", client.write_buffer);
                return;
            }

            // Format Trade execution reports
            for (const auto &tr : trades)
            {
                Protocol::format_trade(tr, client.write_buffer);
            }

            // If resting quantity remains on Limit order, acknowledge resting placement
            auto resting_ord = order_book_.get_order(cmd.order.order_id);
            if (resting_ord.has_value() && resting_ord->remaining_quantity > 0)
            {
                Protocol::format_ack(cmd.order.order_id, resting_ord->remaining_quantity, client.write_buffer);
            }
            else if (trades.empty() && cmd.order.type == OrderType::Market)
            {
                Protocol::format_reject(cmd.order.order_id, "NO_LIQUIDITY_FOR_MARKET_ORDER", client.write_buffer);
            }
        }
    }

    void TcpServer::handle_client_write(int client_fd)
    {
        auto it = clients_.find(client_fd);
        if (it != clients_.end())
        {
            flush_client_write_buffer(it->second);
        }
    }

    void TcpServer::flush_client_write_buffer(ClientConnection &client)
    {
        while (!client.write_buffer.empty())
        {
            ssize_t sent = write(client.fd, client.write_buffer.data(), client.write_buffer.size());
            if (sent > 0)
            {
                client.write_buffer.erase(0, static_cast<size_t>(sent));
            }
            else if (sent < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break; // Socket send buffer full, wait for next EPOLLOUT
                }
                close_connection(client.fd);
                return;
            }
        }
    }

    void TcpServer::close_connection(int client_fd)
    {
        auto it = clients_.find(client_fd);
        if (it != clients_.end())
        {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            clients_.erase(it);
        }
    }

}
