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
#include <chrono>
#include <unordered_map>
#include "bencode.hpp"
#include "protocol.hpp"
#include "sha1.hpp"
#include "file.hpp"
#include "asio.hpp"

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

    Torrent(fs::path filename);
    ~Torrent() = default;
};

struct PieceStatus {
    enum class State : uint8_t { Missing, Downloading, Done } state = State::Missing;
    std::vector<uint8_t> buffer;
    uint32_t downloaded = 0;
    uint32_t total_size = 0;

    std::chrono::steady_clock::time_point last_interacted;

    bool operator==(const PieceStatus &other) const {
        return state == other.state && buffer == other.buffer &&
               downloaded == other.downloaded && total_size == other.total_size;
    }
};

struct TorFile {
    fs::path path;
    size_t size;
    size_t global_offset; // смещение начала файла в общем потоке байт торрента
#ifdef USING_SFILE
    SFile descriptor;
#else
    std::fstream descriptor;
#endif
    std::mutex mut;

    // не копируемый и не перемещаемый из-за mutex и fstream
    TorFile() = default;
    TorFile(const TorFile &) = delete;
    TorFile &operator=(const TorFile &) = delete;
    TorFile(TorFile &&) = delete;
    TorFile &operator=(TorFile &&) = delete;
};

struct TorFileOffsetComparer {
    using is_transparent = void;

    bool operator()(const std::unique_ptr<TorFile> &a,
                    const std::unique_ptr<TorFile> &b) const {
        return a->global_offset < b->global_offset;
    }
    bool operator()(const std::unique_ptr<TorFile> &a, uint64_t b) const {
        return a->global_offset < b;
    }
    bool operator()(uint64_t a, const std::unique_ptr<TorFile> &b) const {
        return a < b->global_offset;
    }
};

// TODO
// прямо сейчас - не работает вообще, переписать надобно
struct PieceOrderer {
    std::vector<int> counts;

    PieceOrderer() = default;
    PieceOrderer(int total_pieces) : counts(total_pieces) {}

    void add_bitfield(const std::vector<bool> &bitfield) {
        for (size_t i = 0; i < bitfield.size(); i++)
            counts[i]++;
    }
    void remove_bitfield(const std::vector<bool> &bitfield) {
        for (size_t i = 0; i < bitfield.size(); i++)
            counts[i]--;
    }

    int get_next(const std::vector<bool> &bitfield) {
        int best = -1;
        for (int i = 0; i < counts.size(); i++) {
            if (!bitfield[i] && counts[i] > 0)
                if (best == -1 || counts[i] < counts[best])
                    best = i;
        }
        return best;
    }
};
struct ArrayHash {
    size_t operator()(const std::array<uint8_t, 20>& a) const {
        size_t seed = 0;
        for (auto b : a)
            seed ^= std::hash<uint8_t>{}(b) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
struct TorrentState {
    bool files_built = false;

    Torrent torrent;
    std::string client_id;

    std::mutex pieces_mutex;
    std::vector<PieceStatus> pieces;

    std::set<std::unique_ptr<TorFile>, TorFileOffsetComparer> torfiles;

    PieceOrderer piece_orderer;
    inline static std::unordered_map<std::array<uint8_t, 20>, TorrentState *, ArrayHash> torrent_map;

    TorrentState(const Torrent &t);
    int next_missing_piece() const;
    int next_missing_piece(const std::vector<bool> &peer_bitfield) const;
    bool is_done() const;

    bool preallocate_files(fs::path where);
    // сохранение и загрузка из файла
    bool try_save(fs::path file);
    bool try_load(fs::path file);

    // отдача
    std::vector<uint8_t> get_piece_by_index(size_t index) const;
    void clear_downloading_pieces();
    uint64_t downloaded() const;

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

    auto ntohl_ip() const {
        uint32_t ip_net;
        std::memcpy(&ip_net, ip, 4);
        return ntohl(ip_net);
    }
    std::string ip_str() const;
    std::string port_str() const;
    std::string str() const;

    static Peer from_endpoint(const asio::ip::tcp::endpoint &ep);
};

struct PeerComparer {
    using is_transparent = void;

    bool operator()(const Peer &a, const Peer &b) const {
        uint32_t ia;
        std::memcpy(&ia, a.ip, 4);

        uint32_t ib;
        std::memcpy(&ib, b.ip, 4);
        if (ia != ib)
            return ia < ib;
        return a.port < b.port;
    }
};

std::vector<Peer> parse_peers(const std::string &peers_binary);

std::string find_info_hash(std::istream &file);

void print_torrent(const Torrent &torrent);

#endif // INCLUDE_TORRENT_HPP_