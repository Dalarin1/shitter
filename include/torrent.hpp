#ifndef INCLUDE_TORRENT_HPP_
#define INCLUDE_TORRENT_HPP_
#include <array>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include "bencode.hpp"
#include "protocol.hpp"
#include "sha1.hpp"

struct Torrent {
    struct file {
        uint64_t length;
        std::vector<std::string> path;
    };
    std::string urlencoded_info_hash; // 40 hex chars, encoded for sending via url
    std::string info_hash;            // 40 hex char string (SHA1 hash of _info_ value)
    std::array<uint8_t, 20> info_hash_raw; // 20 bytes
    std::string announce_url;

    struct Info {
        std::string name;                // Name to save the file/directory as.
        uint64_t piece_length;           // Number of bytes in each piece
        std::vector<std::string> pieces; // Each pieces[i] represents SHA1 of _i_th piece
        uint64_t length;                 // Length of single file; -1 if multiple files
        std::vector<file> files;         // If only single file present, files.size = 0
    } info;

    Torrent(std::string filename);
    ~Torrent() = default;
};

enum class PieceStatus { Missing, Downloading, Done };

struct TorrentState {
    const Torrent &torrent;
    std::vector<PieceStatus> pieces;

    TorrentState(const Torrent &t);
    int next_missing_piece() const;
    bool is_done() const;
};

enum class MessageType : uint8_t {
    Handshake = 255,
    KeepAlive = 254,
    Choke = 0,
    Unchoke = 1,
    Interested = 2,
    Not_Interested = 3,
    Have = 4,
    Bitfield = 5,
    Request = 6,
    Piece = 7,
    Cancel = 8,
    Port = 9
};

struct Message {
    MessageType type;
    uint32_t index = 0;
    uint32_t begin = 0;
    uint32_t length = 0;
    std::vector<uint8_t> data;
};

struct Peer {
    uint8_t ip[4];
    uint16_t port;
    std::string ip_str() const;
    std::string port_str() const;
    std::string str() const;
};
std::vector<Peer> parse_peers(const std::string &peers_binary);

inline std::string u32_to_str(uint32_t val);

std::string find_info_hash(std::istream &file);

struct PeerConnection {
    Peer peer;
    Conn conn;
    TorrentState &torrent_state;
    std::string client_id;
    time_t last_keep_alive;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    std::vector<bool> bitfield;

    void send_handshake();
    Message recv_handshake();
    bool do_handshake();

    Message recv_message();

    void send_interested();
    void send_not_interested();
    void send_keep_alive();
    void send_request(uint32_t index, uint32_t begin, uint32_t length);

    // нужны для отдачи, сделать потом
    void send_bitfield();
    void send_have(uint32_t index);

    void run();
};

void print_torrent(const Torrent &torrent);

#endif // INCLUDE_TORRENT_HPP_
