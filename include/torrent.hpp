#ifndef INCLUDE_TORRENT_HPP_
#define INCLUDE_TORRENT_HPP_

#include "bencode.hpp"
#include <array>
#include <algorithm>

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

        std::string name;      // Name to save the file/directory as.
        uint64_t piece_length; // Number of bytes in each piece;

        std::vector<std::string> pieces; // Each pieces[i] represents SHA1 of _i_th piece

        uint64_t length; // Represetns the length of single file in bytes; If multiple
                         // files present, set
                         // to -1

        std::vector<file> files; // If only single file present, files.size = 0
    } info;

    Torrent(std::string filename);
    ~Torrent() = default;
};

enum class PieceStatus { Missing, Dowтloading, Done };

struct TorrentState {
    const Torrent &torrent;

    std::vector<PieceStatus> pieces;

    TorrentState(const Torrent &t);

    int next_missing_piece();

    bool is_done();
};

enum class MessageType : uint8_t {
    Handshake,
    KeepAlive,
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
// Распарсенное входящее сообщение
struct Message {
    MessageType type;
    // для Have, Request, Piece, Cancel:
    uint32_t index = 0;
    uint32_t begin = 0;
    uint32_t length = 0;
    std::vector<uint8_t> data; // payload для Bitfield и Piece
};

struct Peer {
    uint8_t ip[4];
    uint16_t port;

    std::string str() const;
    std::string ip_str() const;
    std::string port_str() const;
};

struct PeerConnection {
    Peer peer;
    Conn conn;
    TorrentState &torrent_state;

    // A block is downloaded by the client when
    // the client is interested in a peer,
    // and that peer is not choking the client.

    // A block is uploaded by a client when
    // the client is not choking a peer,
    // and that peer is interested in the client.

    bool am_choking = 1;      // this client is choking the peer
    bool am_interested = 0;   // this client is interested in the peer
    bool peer_choking = 1;    // peer is choking this client
    bool peer_interested = 0; // peer is interested in this client

    std::vector<bool> bitfield; // куски, которые есть у пира
    // Handshake
    void send_handshake();
    Message recv_handshake(); // возвращает распарсенный handshake
    bool do_handshake();      // send + recv + verify info_hash

    // Низкоуровневые send/recv
    void send_message(const Message &msg){
        std::string buf;
        if(msg.type == MessageType::KeepAlive){
            buf.append(4, '\0');
            conn.send_all(buf);
            return;
        }

        uint32_t len;
        if(msg.type == MessageType::Choke){
            len = 1;
        }
    }
    Message recv_message();

    // Высокоуровневые действия
    void send_interested();
    void send_not_interested();
    void send_request(uint32_t index, uint32_t begin, uint32_t length);
    void send_have(uint32_t index);

    // Скачивание одного куска (все блоки по 16KB)
    // возвращает false если пир задушил или нет куска
    bool download_piece(int piece_index);

    // Основной цикл — качает куски пока они есть
    void run();
};

void print_torrent(const Torrent &torrent);

#endif // INCLUDE_TORRENT_HPP_