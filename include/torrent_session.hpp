#ifndef INCLUDE_PEER_HPP_
#define INCLUDE_PEER_HPP_

#include <cstdint>
#include <string>
#include <vector>

struct Peer {
    uint8_t ip[4];
    uint16_t port;
    std::string ip_str() const {
        return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
               std::to_string(ip[2]) + "." + std::to_string(ip[3]) + ":" +
               std::to_string(port);
    }
};

std::vector<Peer> parse_peers(const std::string &peers_binary) {
    std::vector<Peer> res = std::vector<Peer>();
    res.reserve(peers_binary.size() / 6);
    Peer tmp = Peer();
    for (size_t i = 0; i < peers_binary.size(); i += 6) {
        tmp.ip[0] = static_cast<uint8_t>(peers_binary[i]);
        tmp.ip[1] = static_cast<uint8_t>(peers_binary[i + 1]);
        tmp.ip[2] = static_cast<uint8_t>(peers_binary[i + 2]);
        tmp.ip[3] = static_cast<uint8_t>(peers_binary[i + 3]);

        tmp.port = static_cast<uint8_t>(peers_binary[i + 4]) << 8 |
                   static_cast<uint8_t>(peers_binary[i + 5]);
        res.push_back(tmp);
    }
    return res;
}

struct TorrentSession {
    std::string info_hash;
    std::vector<Peer> peers;
};

#endif // INCLUDE_PEER_HPP_