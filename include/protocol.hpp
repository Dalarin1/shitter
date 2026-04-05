#ifndef INCLUDE_PROTOCOL_HPP_
#define INCLUDE_PROTOCOL_HPP_
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <ws2tcpip.h>
#include <winhttp.h>

#define MAX_PACKET_SIZE INT16_MAX

std::string urlencode(const std::string &url);

std::string urldecode(const std::string &url);

std::string get_url_hostname(const std::string &url);

std::string get_url_path(const std::string &url);

struct URL {
    std::string protocol, host, path;
};
URL parse_url(const std::string &url);

struct Conn {
    SOCKET sock = INVALID_SOCKET;
    addrinfo *result = nullptr;
    std::string host;
    std::string port;

    Conn(Conn&& other) noexcept;
    Conn(const std::string &_url);
    Conn(const std::string &_host, const std::string &_port);

    ~Conn();

    static void init_wsa();
    static void cleanup_wsa();

    void send_all(const std::string &_request);
    std::string recv_all();
};

struct HttpConn {

    struct response {
        bool success;
        std::string data;
    };

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    HttpConn(const std::wstring &host, INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT);

    ~HttpConn();

    response get(const std::wstring &path);
};
#endif // INCLUDE_PROTOCOL_HPP_