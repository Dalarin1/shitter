#ifndef INCLUDE_PEER_HPP_
#define INCLUDE_PEER_HPP_

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "torrent.hpp"
#include "protocol.hpp"

struct Peer {
    uint8_t ip[4];
    uint16_t port;
    std::string ip_str() const {
        return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
               std::to_string(ip[2]) + "." + std::to_string(ip[3]);
    }
    std::string port_str() const { return std::to_string(port); }
    std::string str() const { return ip_str() + ":" + port_str(); }
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

struct PeerConnection {
    Peer peer;
    Conn connection;
    Torrent *torrent;
    std::string client_id;

    PeerConnection(Torrent *_t, const Peer &_peer)
        : peer(_peer), torrent(_t), connection(_peer.ip_str(), _peer.port_str()),
          client_id("-qB4600-123456789012") {}

    void send_handshake() {
        std::string handshake;
        handshake.reserve(68);
        handshake += static_cast<char>(19);
        handshake += "BitTorrent protocol";
        handshake += std::string(8, '\0');
        for (int i = 0; i < 20; i++)
            handshake += static_cast<char>(torrent->info_hash_raw[i]);
        handshake += client_id;

        connection.send_all(handshake);
    }
    std::string recv_handshake() {
        std::string response(68, '\0');
        int total = 0;
        while (total < 68) {
            int r = recv(connection.sock, response.data() + total, 68 - total, 0);
            if (r <= 0)
                break;
            total += r;
        }
        return response;
    }
    bool verify_handshake() {
        
    }
    void get_piece(){

    }
};

// struct TorrentSession {
//     Torrent* torrent;
//     std::vector<Peer> peers;
//     std::unordered_map<Peer, Conn> connections;

//     TorrentSession();
//     ~TorrentSession();
// };

#endif // INCLUDE_PEER_HPP_