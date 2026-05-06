#include "protocol.hpp"
#include "spdlog/spdlog.h"

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
    pos = url.find_first_of("/?:#", pos);
    if (pos == std::string::npos)
        return "/";
    return url.substr(pos);
}

URL parse_url(const std::string &url) {
    URL res;
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
        res.host = url.substr(pos, host_end - pos);
        res.path = url.substr(host_end);
    }
    return res;
}