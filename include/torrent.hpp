#ifndef INCLUDE_TORRENT_HPP_
#define INCLUDE_TORRENT_HPP_

#include <array>
#include "protocol.hpp"

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

    int next_missing_piece() const;

    bool is_done() const;
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

std::string u32_to_str(uint32_t val) {
    uint32_t net_val = htonl(val);
    return std::string((char *)&net_val, 4);
}

struct PeerConnection {
    const TorrentState &torrent_state;
    Peer peer;
    Conn conn;
    TorrentState &torrent_state;
    std::string client_id;
    time_t last_keep_alive;
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

    void send_handshake() {
        std::string handshake;
        handshake.reserve(68);
        handshake += static_cast<char>(19);
        handshake += "BitTorrent protocol";
        handshake += std::string(8, '\0');
        for (int i = 0; i < 20; i++)
            handshake += static_cast<char>(torrent_state.torrent.info_hash_raw[i]);
        handshake += client_id;

        conn.send_all(handshake);
    }
    Message recv_handshake() {
        std::string response(68, '\0');
        int total = 0;
        while (total < 68) {
            int r = recv(conn.sock, response.data() + total, 68 - total, 0);
            if (r <= 0)
                throw std::runtime_error("Handshake failed: connection closed");
            total += r;
        }
        Message msg = Message();
        msg.type = MessageType::Handshake;
        msg.data = std::vector<uint8_t>(response.begin(), response.end());
        return msg;
    }

    // send + recv + verify info_hash
    bool do_handshake() {
        send_handshake();
        Message msg = recv_handshake();
        if (msg.data[0] != 19 ||
            std::string(msg.data.begin() + 1, msg.data.begin() + 20) !=
                "BitTorrent protocol") {
            std::cerr << "Not a BitTorrent peer" << std::endl;
            return false;
        }
        std::array<uint8_t, 20> recieved_info_hash;
        for (int i = 28; i < 48; i++) {
            recieved_info_hash[i - 28] = msg.data[i];
        }
        if (recieved_info_hash != torrent_state.torrent.info_hash_raw) {
            std::cerr << "Peer send incorrect info_hash" << std::endl;
            return false;
        }
        return true;
    }

    Message recv_message() {
        auto read_exact = [&](size_t n) -> std::string {
            std::string inbuf(n, '\0');
            size_t recieved = 0;
            int r = 0;
            while (recieved < n) {
                r = recv(conn.sock, inbuf.data() + recieved, n - recieved, 0);
                if (r <= 0) {
                    throw std::runtime_error("Connection closed");
                }
                recieved += r;
            }
            return inbuf;
        };
        std::string len_buf = read_exact(4);
        uint32_t len = ntohl(*reinterpret_cast<uint32_t *>(len_buf.data()));

        Message msg;
        msg.length = len;

        if (len == 0) {
            msg.type = MessageType::KeepAlive;
            return msg;
        }
        // читаем ID
        std::string id_buf = read_exact(1);
        msg.type = (MessageType)id_buf[0];

        if (msg.type == MessageType::Choke || msg.type == MessageType::Unchoke ||
            msg.type == MessageType::Interested ||
            msg.type == MessageType::Not_Interested) {
            return msg;
        }

        if (len == 5 && msg.type == MessageType::Have) {
            auto buf = read_exact(4);
            msg.index = ntohl(*reinterpret_cast<uint32_t *>(buf.data()));
            return msg;
        }

        if (msg.type == MessageType::Bitfield) {
            std::string bit_buf = read_exact(len - 1);
            msg.data = std::vector<uint8_t>(bit_buf.begin(), bit_buf.end());
            return msg;
        }

        if (msg.type == MessageType::Piece) {
            std::string index_buf = read_exact(4);
            msg.index = ntohl(*reinterpret_cast<uint32_t *>(index_buf.data()));
            std::string begin_buf = read_exact(4);
            msg.begin = ntohl(*reinterpret_cast<uint32_t *>(begin_buf.data()));

            std::string block_buf = read_exact(len - 9);
            msg.data = std::vector<uint8_t>(block_buf.begin(), block_buf.end());
            return msg;
        }

        // Остальные случаи пока не обрабатываем
        // так как прочие типы - для отдачи данных,
        // а мы пока только принимаем

        std::string leftover =
            read_exact(len - 1); // съедаем payload неизвестного сообщения
        return msg;
    }

    // Высокоуровневые действия
    void send_interested() {
        std::string buf = u32_to_str(1);
        buf.append(1, 2); // id = 2

        conn.send_all(buf);
    }
    
    void send_not_interested() {
        std::string buf = u32_to_str(1);
        buf.append(1, 3); // id = 3

        conn.send_all(buf);
    }
    
    void send_keep_alive() {
        std::string buf;
        buf.append(4, '\0');
        conn.send_all(buf);
    }

    void send_request(uint32_t index, uint32_t begin, uint32_t length) {
        std::string req = u32_to_str(13) + static_cast<char>(6) + u32_to_str(index) +
                          u32_to_str(begin) + u32_to_str(length);
        conn.send_all(req);
    }
    
    // нужны для отдачи, сделать потом
    void send_bitfield();
    void send_have(uint32_t index);

    // Основной цикл — качает куски пока они есть
    void run() {}
};

void print_torrent(const Torrent &torrent);

#endif // INCLUDE_TORRENT_HPP_