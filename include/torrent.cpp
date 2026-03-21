#include "torrent.hpp"

// ─── Peer ────────────────────────────────────────────────────────────────────

std::string Peer::ip_str() const {
    return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
           std::to_string(ip[2]) + "." + std::to_string(ip[3]);
}
std::string Peer::port_str() const { return std::to_string(port); }
std::string Peer::str() const { return ip_str() + ":" + port_str(); }
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

// ─── TorrentState ────────────────────────────────────────────────────────────

TorrentState::TorrentState(const Torrent &t) : torrent(t) {
    pieces.resize(t.info.pieces.size());

    for (size_t i = 0; i < pieces.size(); i++) {

        bool is_last = (i == pieces.size() - 1);

        for (size_t i = 0; i < pieces.size(); i++) {
            // последний кусок может быть меньше
            bool is_last = (i == pieces.size() - 1);
            pieces[i].total_size = is_last
                                       ? t.total_length % t.info.piece_length // остаток
                                       : t.info.piece_length;
            // если длина делится нацело — последний кусок тоже полный
            if (is_last && pieces[i].total_size == 0)
                pieces[i].total_size = t.info.piece_length;
            pieces[i].state = PieceStatus::State::Missing;
            // pieces[i].buffer.resize(pieces[i].total_size);
        }
    }
}

int TorrentState::next_missing_piece() const {
    for (size_t i = 0; i < pieces.size(); i++)
        if (pieces[i].state == PieceStatus::State::Missing)
            return (int)i;
    return -1;
}

bool TorrentState::is_done() const {
    return std::all_of(pieces.begin(), pieces.end(), [](const PieceStatus &s) {
        return s.state == PieceStatus::State::Done;
    });
}

// ─── find_info_hash ──────────────────────────────────────────────────────────

std::string find_info_hash(std::istream &file) {
    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

    size_t pos = raw.find("4:info");
    if (pos == std::string::npos)
        throw std::runtime_error("No info key in torrent file");
    pos += 6; // начало info-значения

    std::istringstream ss(raw.substr(pos));
    std::streampos start = ss.tellg();
    BencodeVal info_val = read_bencode(ss);
    std::streampos end = ss.tellg();
    std::string info_raw = raw.substr(pos, (size_t)end - (size_t)start);
    return sha1(info_raw);
}

// ─── Torrent ─────────────────────────────────────────────────────────────────

Torrent::Torrent(std::string filename) {
    auto file = std::ifstream(filename, std::ios::binary);
    BencodeVal data = read_bencode(file);
    file.close();

    if (!data.is_dict())
        throw std::runtime_error(
            "Invalid torrent file: top-level element is not a dictionary");

    BEN_Dict dict = data.get_dict();
    BEN_Dict info_dict = dict.at("info").get_dict();
    announce_url = dict.at("announce").get_str();

    auto f = std::ifstream(filename, std::ios::binary);
    info_hash = find_info_hash(f);

    for (int i = 0; i < 40; i += 2)
        info_hash_raw[i / 2] =
            static_cast<uint8_t>(std::stoi(info_hash.substr(i, 2), 0, 16));

    for (size_t i = 0; i < 40; i += 2) {
        unsigned char byte = std::stoi(info_hash.substr(i, 2), 0, 16);
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", byte);
        urlencoded_info_hash += buf;
    }

    info.piece_length = info_dict.at("piece length").get_int();
    info.name = info_dict.at("name").get_str();

    if (info_dict.count("length") > 0) {
        info.length = info_dict.at("length").get_int();
        total_length = info.length;
    } else
        info.length = -1;

    if (info_dict.count("files") > 0) {
        size_t len = 0;
        for (const auto &file : info_dict.at("files").get_list()) {
            auto f = Torrent::file();
            f.length = file.get_dict().at("length").get_int();
            len += f.length;
            for (const auto &path_part : file.get_dict().at("path").get_list())
                f.path.push_back(path_part.get_str());
            info.files.push_back(f);
        }
        total_length = len;
    }

    std::string pieces_str = dict["info"].get_dict().at("pieces").get_str();
    for (size_t i = 0; i < pieces_str.size(); i += 20)
        info.pieces.push_back(pieces_str.substr(i, 20));
}

// ─── PeerConnection ──────────────────────────────────────────────────────────

PeerConnection::PeerConnection(Peer _peer, TorrentState &ts)
    : peer(_peer), torrent_state(ts), conn(_peer.ip_str(), _peer.port_str()) {}

void PeerConnection::send_handshake() {
    std::string handshake;
    handshake.reserve(68);
    handshake += static_cast<char>(19);
    handshake += "BitTorrent protocol";
    handshake += std::string(8, '\0');
    for (int i = 0; i < 20; i++)
        handshake += static_cast<char>(torrent_state.torrent.info_hash_raw[i]);
    handshake += torrent_state.client_id;
    conn.send_all(handshake);
}

Message PeerConnection::recv_handshake() {
    std::string response(68, '\0');
    int total = 0;
    while (total < 68) {
        int r = recv(conn.sock, response.data() + total, 68 - total, 0);
        if (r <= 0)
            throw std::runtime_error("Handshake failed: connection closed");
        total += r;
    }
    Message msg;
    msg.type = MessageType::Handshake;
    msg.data = std::vector<uint8_t>(response.begin(), response.end());
    return msg;
}

bool PeerConnection::do_handshake() {
    send_handshake();
    Message msg = recv_handshake();
    if (msg.data[0] != 19 || std::string(msg.data.begin() + 1, msg.data.begin() + 20) !=
                                 "BitTorrent protocol") {
        std::cerr << "Not a BitTorrent peer\n";
        return false;
    }
    std::array<uint8_t, 20> received_info_hash;
    for (int i = 28; i < 48; i++)
        received_info_hash[i - 28] = msg.data[i];
    if (received_info_hash != torrent_state.torrent.info_hash_raw) {
        std::cerr << "Peer sent incorrect info_hash\n";
        return false;
    }
    return true;
}

Message PeerConnection::recv_message() {
    auto read_exact = [&](size_t n) -> std::string {
        std::string inbuf(n, '\0');
        size_t received = 0;
        int r = 0;
        while (received < n) {
            r = recv(conn.sock, inbuf.data() + received, n - received, 0);
            if (r <= 0)
                throw std::runtime_error("Connection closed");
            received += r;
        }
        return inbuf;
    };

    auto buf4 = read_exact(4);
    uint32_t len = ntohl(*reinterpret_cast<uint32_t *>(buf4.data()));

    Message msg;
    msg.length = len;

    if (len == 0) {
        msg.type = MessageType::KeepAlive;
        return msg;
    }

    std::string id_buf = read_exact(1);
    msg.type = (MessageType)(uint8_t)id_buf[0];

    if (msg.type == MessageType::Choke || msg.type == MessageType::Unchoke ||
        msg.type == MessageType::Interested || msg.type == MessageType::Not_Interested)
        return msg;

    if (msg.type == MessageType::Have) {
        auto b = read_exact(4);
        msg.index = ntohl(*reinterpret_cast<uint32_t *>(b.data()));
        return msg;
    }

    if (msg.type == MessageType::Bitfield) {
        std::string bit_buf = read_exact(len - 1);
        msg.data = std::vector<uint8_t>(bit_buf.begin(), bit_buf.end());
        return msg;
    }

    if (msg.type == MessageType::Piece) {
        auto index_buf = read_exact(4);
        msg.index = ntohl(*reinterpret_cast<uint32_t *>(index_buf.data()));
        auto begin_buf = read_exact(4);
        msg.begin = ntohl(*reinterpret_cast<uint32_t *>(begin_buf.data()));
        std::string block_buf = read_exact(len - 9);
        msg.data = std::vector<uint8_t>(block_buf.begin(), block_buf.end());
        return msg;
    }

    // съедаем payload неизвестного сообщения
    read_exact(len - 1);
    return msg;
}

void PeerConnection::send_interested() {
    std::string buf = u32_to_str(1);
    buf.append(1, 2);
    conn.send_all(buf);
}

void PeerConnection::send_not_interested() {
    std::string buf = u32_to_str(1);
    buf.append(1, 3);
    conn.send_all(buf);
}

void PeerConnection::send_keep_alive() {
    std::string buf(4, '\0');
    conn.send_all(buf);
}

void PeerConnection::send_request(uint32_t index, uint32_t begin, uint32_t length) {
    std::string req = u32_to_str(13);
    req += static_cast<char>(6);
    req += u32_to_str(index);
    req += u32_to_str(begin);
    req += u32_to_str(length);
    conn.send_all(req);
}

void PeerConnection::run() {
    if (!do_handshake()) {
        std::cerr << "Handshake failed\n";
        return;
    }

    while (!torrent_state.is_done()) {
        Message msg = recv_message();

        switch (msg.type) {
        case MessageType::KeepAlive:
            send_keep_alive();
            break;
        case MessageType::Choke:
            peer_choking = true;
            break;
        case MessageType::Unchoke:
            peer_choking = false;
            break;
        case MessageType::Have:
            if (msg.index < bitfield.size())
                bitfield[msg.index] = true;
            break;
        case MessageType::Bitfield:
            bitfield.clear();
            bitfield.reserve(msg.data.size() * 8);
            for (size_t i = 0; i < msg.data.size(); i++)
                for (int bit = 7; bit >= 0; bit--)
                    bitfield.push_back((msg.data[i] >> bit) & 1);
            send_interested();
            am_interested = true;
            break;
        case MessageType::Piece:

            break;
        default:
            break;
        }
    }
}
std::vector<PeerConnection> connect_to_peers(const std::vector<Peer> &peers,
                                             TorrentState &ts, uint16_t conn_count = 50) {
    std::vector<PeerConnection> res;
    res.reserve(conn_count);
    for (uint16_t i = 0; i < conn_count; i++){
        res.emplace_back(peers[i], ts);
    }
    return res;
}

// ─── print_torrent ───────────────────────────────────────────────────────────

void print_torrent(const Torrent &t) {
    auto print_safe = [](const std::string &s) {
        for (unsigned char c : s) {
            if (std::isprint(c))
                std::cout << c;
            else
                std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                          << (int)c << std::dec;
        }
    };

    std::cout << "Torrent:\n";
    std::cout << "  announce_url: " << t.announce_url << "\n";
    std::cout << "  info_hash: ";
    print_safe(t.info_hash);
    std::cout << "\n";
    std::cout << "  info:\n";
    std::cout << "    name: " << t.info.name << "\n";
    std::cout << "    piece_length: " << t.info.piece_length << "\n";
    std::cout << "    pieces: " << t.info.pieces.size() << " pieces\n";

    if (t.info.files.empty()) {
        std::cout << "    length: " << t.info.length << "\n";
    } else {
        std::cout << "    files:\n";
        for (const auto &f : t.info.files) {
            std::cout << "      length: " << f.length << "\n";
            std::cout << "      path: ";
            for (const auto &part : f.path)
                std::cout << part << "/";
            std::cout << "\n";
        }
    }
}
