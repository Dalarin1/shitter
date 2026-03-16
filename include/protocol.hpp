#ifndef INCLUDE_PROTOCOL_HPP_
#define INCLUDE_PROTOCOL_HPP_
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <ws2tcpip.h>
#include <winhttp.h>

#define MAX_PACKET_SIZE INT16_MAX

bool is_valid_url(const std::string &url);
std::string urlencode(const std::string &url) {
    std::string result;
    for (char c : url) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            result.push_back(c);
        } else {
            result.push_back('%');
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(c));
            result.append(hex);
        }
    }
    return result;
}

std::string urldecode(const std::string &url) {
    std::string result;
    for (size_t i = 0; i < url.size(); i++) {
        if (url[i] == '%') {
            std::string hex = url.substr(i + 1, 2);
            char decoded = static_cast<int>(std::stoi(hex, nullptr, 16));
            i += 2;
            result.push_back(decoded);
        } else {
            result.push_back(url[i]);
        }
    }
    return result;
}

std::string get_url_hostname(const std::string &url) {
    size_t scheme_end = url.find("://");
    size_t host_start = (scheme_end == std::string::npos) ? 0 : scheme_end + 3;

    size_t host_end = url.find_first_of(":/?#", host_start);
    if (host_end == std::string::npos)
        host_end = url.size();

    return url.substr(host_start, host_end - host_start);
}

std::string get_url_path(const std::string &url) {
    size_t scheme_end = url.find("://");
    size_t pos = (scheme_end == std::string::npos) ? 0 : scheme_end + 3;
    // skip host
    pos = url.find_first_of("/?:#", pos);
    if (pos == std::string::npos)
        return "/"; // default path
    return url.substr(pos);
}

struct URL {
    std::string protocol, host, path;
};
URL parse_url(const std::string &url) {
    URL res = URL();
    size_t prot_end, host_end, pos;
    prot_end = url.find("://");
    if (prot_end != std::string::npos) {
        res.protocol = url.substr(0, prot_end);
        pos = prot_end + 3;
    } else {
        res.protocol = "";
        pos = 0;
    }
    host_end = url.find_first_of("/?:#", pos);
    if (host_end == std::string::npos) {
        res.host = url.substr(pos);
        res.path = "/";
    } else {
        res.host = url.substr(pos, host_end);
        res.path = url.substr(host_end);
    }
    return res;
}

struct Conn {
    SOCKET sock = INVALID_SOCKET;
    addrinfo *result = nullptr;
    std::string host;
    std::string port;

    Conn(const std::string &_url);
    Conn(const std::string &_host, const std::string &_port)
        : host(std::move(_host)), port(std::move(_port)) {
        init_wsa();
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
        if (rc != 0) {
            throw std::runtime_error("getaddrinfo: " + std::to_string(rc));
        }

        // пробуем каждый addrinfo до удачного connect
        for (addrinfo *p = result; p != nullptr; p = p->ai_next) {
            sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock == INVALID_SOCKET)
                continue;
            if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) {
                // connected
                return;
            }
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(result);
        result = nullptr;
        throw std::runtime_error("Could not connect to any addr");
    }

    ~Conn() {
        if (sock != INVALID_SOCKET)
            closesocket(sock);
        if (result)
            freeaddrinfo(result);
    }

    static void init_wsa() {
        static bool inited = false;
        if (!inited) {
            WSADATA w;
            if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
            inited = true;
        }
    }
    static void cleanup_wsa() { WSACleanup(); }

    void send_all(const std::string &_request) {
        const char *buf = _request.data();
        int to_send = (int)_request.size();
        while (to_send > 0) {
            int sent = send(sock, buf, to_send, 0);
            if (sent == SOCKET_ERROR) {
                throw std::runtime_error("send failed: " +
                                         std::to_string(WSAGetLastError()));
            }
            to_send -= sent;
            buf += sent;
        }
    }
    std::string recv_all() {
        std::string out;
        char buffer[4096];
        int r;
        while ((r = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            out.append(buffer, buffer + r);
        }
        if (r == SOCKET_ERROR) {
            throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
        }
        return out;
    }
};

struct HttpConn {

    struct response{
        bool success;
        std::istringstream data;
    };

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    HttpConn(const std::wstring &host, INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT) {
        hSession = WinHttpOpen(L"WinHTTP Client", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession)
            throw std::runtime_error("WinHttpOpen failed");

        hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);

        if (!hConnect)
            throw std::runtime_error("WinHttpConnect failed");
    }

    ~HttpConn() {
        if (hRequest)
            WinHttpCloseHandle(hRequest);
        if (hConnect)
            WinHttpCloseHandle(hConnect);
        if (hSession)
            WinHttpCloseHandle(hSession);
    }

    response get(const std::wstring &path) {

        if (hRequest) {
            WinHttpCloseHandle(hRequest);
            hRequest = nullptr;
        }

        hRequest =
            WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        DWORD decompression_flags =
            WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;

        // Set the option on the HINTERNET handle (hRequest, hConnection, or hSession)
        if (WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompression_flags,
                             sizeof(decompression_flags)) != TRUE) {
            std::cout << "ERR SETOPTION\n";
        }
        const wchar_t *headers = L"User-Agent: qBittorrent/4.6.0\r\n"
                                 L"Accept-Encoding: gzip, ascii\r\n"
                                 L"Connection: close\r\n";

        WinHttpAddRequestHeaders(hRequest, headers, -1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (!hRequest)
            return {false, std::istringstream()};

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            return {false, std::istringstream()};

        if (!WinHttpReceiveResponse(hRequest, NULL))
            return {false, std::istringstream()};

        DWORD size = 0;
        std::string response;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &size))
                return {false, std::istringstream()};

            if (!size)
                break;

            std::vector<char> buffer(size + 1);

            DWORD downloaded = 0;

            if (!WinHttpReadData(hRequest, buffer.data(), size, &downloaded))
                return {false, std::istringstream()};

            // buffer[downloaded] = '\0';
            response.append(buffer.data(), downloaded);

        } while (size > 0);
        std::istringstream ss = std::istringstream(response);
        return {true, std::move(ss)};
    }
};

#endif // INCLUDE_PROTOCOL_HPP_