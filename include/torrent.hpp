#ifndef INCLUDE_TORRENT_HPP_
#define INCLUDE_TORRENT_HPP_
#include <array>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <set>
#include <filesystem>
#include "bencode.hpp"
#include "protocol.hpp"
#include "sha1.hpp"

namespace fs = std::filesystem;

inline std::string u32_to_str(uint32_t val) {
    uint32_t net_val = htonl(val);
    return std::string((char *)&net_val, 4);
}

struct Torrent {
    struct file {
        uint64_t length;
        std::vector<std::string> path;
    };
    std::string urlencoded_info_hash; // 40 hex chars, encoded for sending via url
    std::string info_hash;            // 40 hex char string (SHA1 hash of _info_ value)
    std::array<uint8_t, 20> info_hash_raw; // 20 bytes
    std::string announce_url;
    size_t total_length; // total length of all files in bytes
    struct Info {
        std::string name;                // Name to save the file/directory as.
        uint64_t piece_length;           // Number of bytes in each piece
        std::vector<std::string> pieces; // Each pieces[i] represents SHA1 of _i_th piece
        uint64_t length;                 // Length of single file; -1 if multiple files
        std::vector<file> files;         // If only single file present, files.size = 0
    } info;

    Torrent(std::wstring filename);
    ~Torrent() = default;
};

struct PieceStatus {
    enum class State: uint8_t { Missing, Downloading, Done } state = State::Missing;
    std::vector<uint8_t> buffer;
    uint32_t downloaded = 0;
    uint32_t total_size = 0;

    bool operator==(const PieceStatus &other) const {
        return state == other.state && buffer == other.buffer &&
               downloaded == other.downloaded && total_size == other.total_size;
    }
};

struct TorFile {
    fs::path path;
    size_t size;
    size_t global_offset; // смещение начала файла в общем потоке байт торрента
    std::fstream descriptor;
    std::mutex mut;

    // не копируемый и не перемещаемый из-за mutex и fstream
    TorFile() = default;
    TorFile(const TorFile &) = delete;
    TorFile &operator=(const TorFile &) = delete;
    TorFile(TorFile &&) = delete;
    TorFile &operator=(TorFile &&) = delete;
};

struct TorrentState {
    bool files_built = false;
    std::mutex pieces_mutex;
    const Torrent &torrent;
    std::string client_id;
    std::vector<PieceStatus> pieces;

    std::set<std::unique_ptr<TorFile>> torfiles;

    TorrentState(const Torrent &t);
    int next_missing_piece() const;
    int next_missing_piece(const std::vector<bool> &peer_bitfield) const;
    bool is_done() const;

    bool preallocate_files(fs::path where);
    // сохранение и загрузка из файла
    bool try_save();
    bool try_load(fs::path file);

  private:
    void init_torfiles(fs::path where);
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

std::string find_info_hash(std::istream &file);

struct PeerConnection {
    Peer peer;
    Conn conn;
    TorrentState &torrent_state;
    time_t last_keep_alive;

    bool handshook = false;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    std::vector<bool> bitfield;

    PeerConnection() = delete;
    PeerConnection(PeerConnection &&other) noexcept;
    PeerConnection(Peer peer, TorrentState &ts);

    void send_handshake();
    Message recv_handshake();
    bool do_handshake();

    Message recv_message();

    void send_interested();
    void send_not_interested();
    void send_keep_alive();
    void send_request(uint32_t index, uint32_t begin, uint32_t length);
    void send_bitfield();
    void send_have(uint32_t index);
    void send_unchoke();
    void handle_piece(const Message &msg);
    void handle_piece_v2(const Message &msg);
    void run();
    void request_next_piece();
};

std::vector<PeerConnection> connect_to_peers(const std::vector<Peer> &peers,
                                             TorrentState &ts, uint16_t conn_count);

void print_torrent(const Torrent &torrent);

#if 0
// Управляет коннектами, хранит список известных пиров, время от времени проверят список
// на новых рабочих пиров
struct PeerBoss {
    std::set<Peer> peers;
    std::vector<std::unique_ptr<PeerConnection>> connects;  
};
#endif

#endif // INCLUDE_TORRENT_HPP_
