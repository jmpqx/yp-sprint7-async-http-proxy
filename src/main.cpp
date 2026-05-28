#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <print>
#include <string_view>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::dynamic_buffer;
using boost::asio::io_service;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";
constexpr size_t CHUNK_SIZE = 65536;

awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    try {
        std::string request_buf;
        size_t headers_size =
            co_await async_read_until(client_socket, dynamic_buffer(request_buf), delimiter, use_awaitable);

        auto [host, port] = findHostPort(request_buf);
        if (host.empty())
            co_return;

        std::println("New request for {}:{}", host, port);
        auto client_addr = client_socket.remote_endpoint().address().to_string();
        auto client_port = client_socket.remote_endpoint().port();

        tcp::resolver resolver(io_service);
        auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);

        tcp::socket server_socket(io_service);
        co_await boost::asio::async_connect(server_socket, endpoints, use_awaitable);

        std::println("Connected to {}:{}", host, port);

        co_await boost::asio::async_write(server_socket, buffer(request_buf.data(), headers_size), use_awaitable);

        std::string response_buf;
        size_t resp_headers_size =
            co_await async_read_until(server_socket, dynamic_buffer(response_buf), delimiter, use_awaitable);

        auto content_length_opt = findContentLength(std::string_view(response_buf.data(), resp_headers_size));

        std::println("Response received from {}:{}\nHeaders:\n{}", host, port,
                     std::string_view(response_buf.data(), resp_headers_size));

        co_await boost::asio::async_write(client_socket, buffer(response_buf.data(), resp_headers_size), use_awaitable);

        if (!content_length_opt) {
            size_t already_have = response_buf.size() - resp_headers_size;
            if (already_have > 0) {
                co_await boost::asio::async_write(
                    client_socket, buffer(response_buf.data() + resp_headers_size, already_have), use_awaitable);
            }

            std::string chunk(CHUNK_SIZE, '\0');
            while (true) {
                auto [read_ec, n] =
                    co_await server_socket.async_read_some(buffer(chunk), boost::asio::as_tuple(use_awaitable));

                if (n > 0) {
                    auto [write_ec, _] = co_await boost::asio::async_write(client_socket, buffer(chunk.data(), n),
                                                                           boost::asio::as_tuple(use_awaitable));

                    if (write_ec) {
                        break;
                    }
                }

                if (read_ec) {
                    break;
                }
            }

            std::println("Response with no Content-Length successfully sent to client {}:{}", client_addr, client_port);
            co_return;
        }

        size_t content_length = content_length_opt.value();
        size_t already_buffered = response_buf.size() - resp_headers_size;

        if (already_buffered > 0) {
            co_await boost::asio::async_write(
                client_socket, buffer(response_buf.data() + resp_headers_size, already_buffered), use_awaitable);
        }

        std::string chunk(CHUNK_SIZE, '\0');
        size_t remaining = content_length - already_buffered;

        while (remaining > 0) {
            size_t to_read = std::min(remaining, CHUNK_SIZE);
            size_t n = co_await server_socket.async_read_some(buffer(chunk.data(), to_read), use_awaitable);
            co_await boost::asio::async_write(client_socket, buffer(chunk.data(), n), use_awaitable);
            remaining -= n;
        }

        std::println("Response successfully sent to client {}:{}", client_addr, client_port);
    } catch (const std::exception &e) {
        std::println("[ERROR] Session error: {}", e.what());
    }
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept([this](error_code ec, tcp::socket client_socket) {
            if (!ec) {
                co_spawn(io_service_, session(std::move(client_socket), io_service_), detached);
            }

            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        io_service io_service(1);
        Server server(io_service, std::atoi(argv[1]));
        io_service.run();
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
