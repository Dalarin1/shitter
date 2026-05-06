#ifndef INCLUDE_PROTOCOL_HPP_
#define INCLUDE_PROTOCOL_HPP_
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "asio.hpp"
#include "asio/ssl.hpp"
#include "spdlog/spdlog.h"
using tcp = asio::ip::tcp;
using asio::awaitable;

#define MAX_PACKET_SIZE INT16_MAX

std::string urlencode(const std::string &url);

std::string urldecode(const std::string &url);

std::string get_url_hostname(const std::string &url);

std::string get_url_path(const std::string &url);

struct URL {
    std::string protocol, host, path;
};
URL parse_url(const std::string &url);


struct AsyncHttpConn {
    asio::io_context &ctx;
    std::string host;
    std::string port;
    bool use_ssl;

    struct response {
        bool success;
        int status_code;
        std::string data;
    };

    AsyncHttpConn(asio::io_context &ioc, URL url) : ctx(ioc) {
        use_ssl = url.protocol == "https";
        host = url.host;
        port = use_ssl ? "443" : "80";
    }
    ~AsyncHttpConn() {}

    awaitable<response> async_get(std::string path) {
        spdlog::debug("CALL async_get ");
        try {
            tcp::resolver resolver = tcp::resolver(ctx);
            spdlog::debug("resolver done");
            try {
                spdlog::debug("Host: [{}], port: [{}]", host, port);
                auto endpoints =
                    co_await resolver.async_resolve(host, port, asio::use_awaitable);

                // TODO
                if (use_ssl) {
                    // https
                    asio::ssl::context ssl_ctx(asio::ssl::context::sslv23_client);
                    asio::ssl::stream<tcp::socket> ssl_socket(ctx, ssl_ctx);

                    co_await asio::async_connect(ssl_socket.lowest_layer(), endpoints,
                                                 asio::use_awaitable);

                    co_await ssl_socket.async_handshake(
                        asio::ssl::stream<tcp::socket>::client, asio::use_awaitable);

                    std::string request = "GET " + path +
                                          " HTTP/1.1\r\n"
                                          "Connection: close\r\n"
                                          "Host: " + host +
                                          "\r\n"
                                          "User-Agent: qBittorrent/4.6.0\r\n"
                                          "Accept: text/*\r\n"
                                          "\r\n";
                    co_await asio::async_write(
                        ssl_socket, asio::buffer(request.data(), request.size()),
                        asio::use_awaitable);

                    asio::streambuf response_buf;
                    co_await asio::async_read_until(ssl_socket, response_buf, "\r\n\r\n",
                                                    asio::use_awaitable);

                    std::istream response_stream(&response_buf);
                    std::string http_version;
                    int status_code;
                    std::string status_message;

                    response_stream >> http_version >> status_code;
                    std::getline(response_stream, status_message);

                    std::string header;
                    while (std::getline(response_stream, header) && header != "\r") {
                    }

                    std::ostringstream body;
                    body << &response_buf;

                    asio::error_code ec;
                    while (asio::read(ssl_socket, response_buf,
                                      asio::transfer_at_least(1), ec)) {
                        body << &response_buf;
                    }
                    spdlog::debug("Got ans from thracker: {}", body.str());
                    co_return response{status_code == 200, status_code, body.str()};

                } else {
                    // http
                    tcp::socket socket(ctx);

                    co_await asio::async_connect(socket, endpoints, asio::use_awaitable);

                    std::string request = "GET " + path +
                                          " HTTP/1.1\r\n"
                                          "Connection: close\r\n"
                                          "Host: " +
                                          host +
                                          "\r\n"
                                          "User-Agent: qBittorrent/4.6.0\r\n"
                                          "Accept: text/*\r\n"
                                          "\r\n";
                    co_await asio::async_write(
                        socket, asio::buffer(request.data(), request.size()),
                        asio::use_awaitable);

                    asio::streambuf response_buf;
                    co_await asio::async_read_until(socket, response_buf, "\r\n\r\n",
                                                    asio::use_awaitable);

                    std::istream response_stream(&response_buf);
                    std::string http_version;
                    int status_code;
                    std::string status_message;

                    response_stream >> http_version >> status_code;
                    std::getline(response_stream, status_message);

                    std::string header;
                    while (std::getline(response_stream, header) && header != "\r") {
                    }

                    std::ostringstream body;
                    body << &response_buf;

                    asio::error_code ec;
                    while (asio::read(socket, response_buf, asio::transfer_at_least(1),
                                      ec)) {
                        body << &response_buf;
                    }
                    spdlog::debug("Got ans from thracker: {}", body.str());
                    co_return response{status_code == 200, status_code, body.str()};
                }
            } catch (std::exception &e) {
                spdlog::error("Cant GET to dest address: {}", e.what());
                co_return response{false, 0, "Cant GET to dest address"};
            }
        } catch (std::exception &e) {
            spdlog::error("ERRAH while building resolver: {}", e.what());
            co_return response{false, 0, "RRAH while building resolver"};
        }
    }
};

#endif // INCLUDE_PROTOCOL_HPP_