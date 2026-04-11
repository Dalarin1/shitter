#include "torrent.hpp"
#include "spdlog/spdlog.h"
#undef min

// ─── Peer ────────────────────────────────────────────────────────────────────

std::string Peer::ip_str() const {
    return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." +
           std::to_string(ip[2]) + "." + std::to_string(ip[3]);
}
std::string Peer::port_str() const { return std::to_string(port); }
std::string Peer::str() const { return ip_str() + ":" + port_str(); }

std::vector<Peer> parse_peers(const std::string &peers_binary) {
    std::vector<Peer> res;
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
    spdlog::debug("Parsed {} peers from binary", res.size());
    return res;
}

// ─── TorrentState ────────────────────────────────────────────────────────────

TorrentState::TorrentState(const Torrent &t) : torrent(t), piece_orderer(t.info.pieces.size()) {
    pieces.resize(t.info.pieces.size());

    client_id = "-BT7105-123456789101";
    for (size_t i = 0; i < pieces.size(); i++) {
        bool is_last = (i == pieces.size() - 1);
        pieces[i].total_size =
            is_last ? t.total_length % t.info.piece_length : t.info.piece_length;
        if (is_last && pieces[i].total_size == 0)
            pieces[i].total_size = t.info.piece_length;
        pieces[i].state = PieceStatus::State::Missing;
        pieces[i].last_interacted = std::chrono::steady_clock::now();
    }
    spdlog::debug("TorrentState initialized: {} pieces, total {} bytes", pieces.size(),
                  t.total_length);
}

// Ищет следующий Missing кусок который есть у пира
int TorrentState::next_missing_piece(const std::vector<bool> &peer_bitfield) const {
    for (size_t i = 0; i < pieces.size(); i++) {
        if (pieces[i].state == PieceStatus::State::Missing && i < peer_bitfield.size() &&
            peer_bitfield[i]) {
            spdlog::debug("Thread {} requesting piece {}",
                          std::hash<std::thread::id>{}(std::this_thread::get_id()), i);
            return (int)i;
        }
    }
    return -1;
}

// Оставляем старую версию для совместимости
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
    pos += 6;
    std::istringstream ss(raw.substr(pos));
    std::streampos start = ss.tellg();
    BencodeVal info_val = read_bencode(ss);
    std::streampos end = ss.tellg();
    std::string info_raw = raw.substr(pos, (size_t)end - (size_t)start);
    std::string hash = sha1_hex(info_raw);
    spdlog::debug("Computed info_hash: {}", hash);
    return hash;
}

// ─── Torrent ─────────────────────────────────────────────────────────────────

Torrent::Torrent(std::wstring filename) {
    auto file = std::ifstream(fs::path(filename), std::ios::binary);
    BencodeVal data = read_bencode(file);
    file.close();

    if (!data.is_dict())
        throw std::runtime_error(
            "Invalid torrent file: top-level element is not a dictionary");

    BEN_Dict dict = data.get_dict();
    BEN_Dict info_dict = dict.at("info").get_dict();
    announce_url = dict.at("announce").get_str();
    spdlog::debug("Announce URL: {}", announce_url);

    auto f = std::ifstream(fs::path(filename), std::ios::binary);
    info_hash = find_info_hash(f);
    spdlog::info("info_hash: {}", info_hash);

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
    spdlog::info("Torrent name: {}", info.name);
    spdlog::debug("Piece length: {}", info.piece_length);

    if (info_dict.count("length") > 0) {
        info.length = info_dict.at("length").get_int();
        total_length = info.length;
        spdlog::info("Single-file mode, size: {} bytes", total_length);
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
        spdlog::info("Multi-file mode, {} files, total size: {} bytes", info.files.size(),
                     total_length);
    }

    std::string pieces_str = dict["info"].get_dict().at("pieces").get_str();
    for (size_t i = 0; i < pieces_str.size(); i += 20)
        info.pieces.push_back(pieces_str.substr(i, 20));
    spdlog::debug("Loaded {} pieces", info.pieces.size());
}

// ─── PeerConnection ──────────────────────────────────────────────────────────

PeerConnection::PeerConnection(PeerConnection &&other) noexcept
    : handshook(other.handshook), peer(other.peer), conn(std::move(other.conn)),
      torrent_state(other.torrent_state), last_keep_alive(other.last_keep_alive),
      am_choking(other.am_choking), am_interested(other.am_interested),
      peer_choking(other.peer_choking), peer_interested(other.peer_interested),
      bitfield(std::move(other.bitfield)) {}

PeerConnection::PeerConnection(Peer _peer, TorrentState &ts)
    : peer(_peer), conn(_peer.ip_str(), _peer.port_str()), torrent_state(ts) {
    spdlog::debug("Connecting to peer {}", peer.str());
}

void PeerConnection::send_handshake() {
    spdlog::debug("[{}] Sending handshake", peer.str());
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
    spdlog::debug("[{}] Received handshake", peer.str());
    return msg;
}

bool PeerConnection::do_handshake() {
    send_handshake();
    Message msg = recv_handshake();
    if (msg.data[0] != 19 || std::string(msg.data.begin() + 1, msg.data.begin() + 20) !=
                                 "BitTorrent protocol") {
        spdlog::warn("[{}] Not a BitTorrent peer", peer.str());
        return false;
    }
    std::array<uint8_t, 20> received_info_hash;
    for (int i = 28; i < 48; i++)
        received_info_hash[i - 28] = msg.data[i];
    if (received_info_hash != torrent_state.torrent.info_hash_raw) {
        spdlog::warn("[{}] Incorrect info_hash", peer.str());
        return false;
    }
    handshook = true;
    spdlog::info("[{}] Handshake OK", peer.str());
    return true;
}

Message PeerConnection::recv_message() {
    auto read_exact = [&](size_t n) -> std::string {
        std::string inbuf(n, '\0');
        size_t received = 0;
        int r = 0;
        while (received < n) {
            r = recv(conn.sock, inbuf.data() + received, n - received, 0);
            if (r == 0)
                throw std::runtime_error("Connection closed");
            if (r < 0)
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
        spdlog::debug("[{}] Recv KeepAlive", peer.str());
        return msg;
    }

    std::string id_buf = read_exact(1);
    msg.type = (MessageType)(uint8_t)id_buf[0];

    if (msg.type == MessageType::Choke || msg.type == MessageType::Unchoke ||
        msg.type == MessageType::Interested || msg.type == MessageType::Not_Interested) {
        spdlog::debug("[{}] Recv id={}", peer.str(), (int)msg.type);
        return msg;
    }

    if (msg.type == MessageType::Have) {
        auto b = read_exact(4);
        msg.index = ntohl(*reinterpret_cast<uint32_t *>(b.data()));
        spdlog::debug("[{}] Recv Have piece={}", peer.str(), msg.index);
        return msg;
    }

    if (msg.type == MessageType::Bitfield) {
        std::string bit_buf = read_exact(len - 1);
        msg.data = std::vector<uint8_t>(bit_buf.begin(), bit_buf.end());
        spdlog::debug("[{}] Recv Bitfield ({} bytes)", peer.str(), bit_buf.size());
        return msg;
    }

    if (msg.type == MessageType::Piece) {
        auto index_buf = read_exact(4);
        msg.index = ntohl(*reinterpret_cast<uint32_t *>(index_buf.data()));
        auto begin_buf = read_exact(4);
        msg.begin = ntohl(*reinterpret_cast<uint32_t *>(begin_buf.data()));
        std::string block_buf = read_exact(len - 9);
        msg.data = std::vector<uint8_t>(block_buf.begin(), block_buf.end());
        spdlog::debug("[{}] Recv Piece index={} begin={} size={}", peer.str(), msg.index,
                      msg.begin, msg.data.size());
        return msg;
    }

    spdlog::debug("[{}] Recv unknown id={}, skipping {} bytes", peer.str(), (int)msg.type,
                  len - 1);
    read_exact(len - 1);
    return msg;
}

void PeerConnection::send_interested() {
    spdlog::debug("[{}] Send Interested", peer.str());
    std::string buf = u32_to_str(1);
    buf.append(1, 2);
    try {
        conn.send_all(buf);
    } catch (const std::exception &e) {
        spdlog::error("[{}] Send interested failed: {}", peer.str(), e.what());
    }
}

void PeerConnection::send_not_interested() {
    spdlog::debug("[{}] Send NotInterested", peer.str());
    std::string buf = u32_to_str(1);
    buf.append(1, 3);
    try {
        conn.send_all(buf);
    } catch (const std::exception &e) {
        spdlog::error("[{}] Send not interested failed: {}", peer.str(), e.what());
    }
}

void PeerConnection::send_keep_alive() {
    spdlog::debug("[{}] Send KeepAlive", peer.str());
    std::string buf(4, '\0');
    try {
        conn.send_all(buf);
    } catch (const std::exception &e) {
        spdlog::error("[{}] Send keep alive failed: {}", peer.str(), e.what());
    }
}

void PeerConnection::send_request(uint32_t index, uint32_t begin, uint32_t length) {
    spdlog::debug("[{}] Send Request index={} begin={} length={}", peer.str(), index,
                  begin, length);
    std::string req = u32_to_str(13);
    req += static_cast<char>(6);
    req += u32_to_str(index);
    req += u32_to_str(begin);
    req += u32_to_str(length);
    conn.send_all(req);
}

void PeerConnection::send_bitfield() {
    size_t num_pieces = torrent_state.torrent.info.pieces.size();
    size_t num_bytes = (num_pieces + 7) / 8;
    std::string payload(num_bytes, '\0');
    std::string msg = u32_to_str(1 + num_bytes);
    msg += static_cast<char>(5);
    msg += payload;
    conn.send_all(msg);
    spdlog::debug("[{}] Sent bitfield ({} bytes)", peer.str(), num_bytes);
}

void PeerConnection::send_unchoke() {
    spdlog::debug("[{}] Send unchoke", peer.str());
    std::string buf = u32_to_str(1);
    buf.append(1, 1);
    try {
        conn.send_all(buf);
    } catch (const std::exception &e) {
        spdlog::error("[{}] Send unchoke failed: {}", peer.str(), e.what());
    }
}

void PeerConnection::request_next_piece() {
    int piece = torrent_state.next_missing_piece(bitfield);
    if (piece < 0)
        return; // нечего качать у этого пира
    {
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        // ещё раз проверяем под мьютексом — другой поток мог занять кусок
        if (torrent_state.pieces[piece].state != PieceStatus::State::Missing)
            return;
        torrent_state.pieces[piece].state = PieceStatus::State::Downloading;
    }
    send_request(piece, 0, 16384);
}

void PeerConnection::handle_piece(const Message &msg) {
    std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);

    if (msg.index >= torrent_state.pieces.size())
        return;

    auto &piece = torrent_state.pieces[msg.index];

    // если кусок уже Done (другой пир скачал) — игнорируем
    if (piece.state == PieceStatus::State::Done)
        return;

    if (piece.buffer.empty()) {
        piece.buffer.resize(piece.total_size);
        piece.state = PieceStatus::State::Downloading;
    }

    if (msg.begin + msg.data.size() > piece.buffer.size())
        return; // защита от выхода за границу

    std::copy(msg.data.begin(), msg.data.end(), piece.buffer.begin() + msg.begin);
    piece.downloaded += msg.data.size();

    if (piece.downloaded >= piece.total_size) {
        std::string hashsum = sha1(piece.buffer);

        if (hashsum == torrent_state.torrent.info.pieces.at(msg.index)) {
            std::ofstream tmp(torrent_state.torrent.info_hash + "_" +
                                  std::to_string(msg.index) + ".tmp",
                              std::ios::binary);
            tmp.write(reinterpret_cast<const char *>(piece.buffer.data()),
                      piece.buffer.size());
            piece.state = PieceStatus::State::Done;
            piece.buffer.clear();
            spdlog::info("[{}] Piece {} done", peer.str(), msg.index);
        } else {
            spdlog::error("[{}] Wrong SHA1 for piece {}, re-downloading", peer.str(),
                          msg.index);
            piece.state = PieceStatus::State::Missing;
            piece.downloaded = 0;
            piece.buffer.clear();
        }
    }
}

void PeerConnection::run() {
    spdlog::debug("[{}] Run process started; Handshook = {}", peer.str(), handshook);
    if (!handshook) {
        if (!do_handshake()) {
            spdlog::error("[{}] Handshake failed, aborting", peer.str());
            return;
        }
    }

    spdlog::info("[{}] Starting download loop", peer.str());
    send_interested();
    auto now = std::chrono::steady_clock::now();

    while (!torrent_state.is_done()) {
        auto dt = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(dt - now).count();
        if (elapsed > 5) {
            torrent_state.clear_downloading_pieces();
            now = dt;
        }
        Message msg = recv_message();
        spdlog::debug("[{}] Got message type={}", peer.str(), (int)msg.type);

        switch (msg.type) {
        case MessageType::KeepAlive:
            send_keep_alive();
            break;

        case MessageType::Choke:
            peer_choking = true;
            spdlog::info("[{}] Choked", peer.str());
            break;

        case MessageType::Unchoke:
            peer_choking = false;
            spdlog::info("[{}] Unchoked", peer.str());
            if (am_interested)
                request_next_piece();
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
            spdlog::info("[{}] Got bitfield", peer.str());
            if (!am_interested) {
                send_interested();
                am_interested = true;
            }
            break;

        case MessageType::Piece: {
            spdlog::debug("Got piece {}, len={}", msg.index, msg.length);
            bool was_done =
                (torrent_state.pieces[msg.index].state == PieceStatus::State::Done);
            handle_piece_v2(msg);
            bool piece_done =
                (torrent_state.pieces[msg.index].state == PieceStatus::State::Done);

            if (!peer_choking) {
                if (!piece_done && !was_done) {
                    // кусок не завершён — запрашиваем следующий блок
                    uint32_t next_begin = torrent_state.pieces[msg.index].downloaded;
                    uint32_t remain =
                        torrent_state.pieces[msg.index].total_size - next_begin;
                    uint32_t block_size = std::min(remain, (uint32_t)16384);
                    if (block_size > 0)
                        send_request(msg.index, next_begin, block_size);
                } else {
                    // кусок завершён — берём следующий
                    request_next_piece();
                }
            }
            break;
        }

        default:
            spdlog::debug("[{}] Got unknown msg: {}", peer.str(), (uint8_t)msg.type);
            break;
        }
    }
    torrent_state.files_built = true;
    spdlog::info("[{}] Download complete", peer.str());
}

std::vector<PeerConnection> connect_to_peers(const std::vector<Peer> &peers,
                                             TorrentState &ts, uint16_t conn_count) {
    std::vector<PeerConnection> res;
    res.reserve(conn_count);
    spdlog::info("Connecting to up to {} peers", conn_count);

    for (uint16_t i = 0; i < conn_count && i < peers.size(); i++) {
        try {
            PeerConnection pc = PeerConnection(peers[i], ts);
            if (!pc.do_handshake()) {
                spdlog::warn("[{}] Handshake failed, skipping", peers[i].str());
                continue;
            }
            spdlog::info("[{}] Connected", peers[i].str());
            res.emplace_back(std::move(pc));
        } catch (std::exception &e) {
            spdlog::warn("[{}] Connection failed: {}", peers[i].str(), e.what());
        }
    }
    spdlog::info("Connected to {}/{} peers", res.size(), conn_count);
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

// ─── preallocate_files ─────────────────────────────────────────────────────────
bool TorrentState::preallocate_files(fs::path where) {
    try {
        fs::create_directories(where);
        init_torfiles(where);
        return true;
    } catch (const std::exception &e) {
        spdlog::error("preallocate_files failed: {}", e.what());
        return false;
    }
}

// ─── init_torfiles ─────────────────────────────────────────────────────────

void TorrentState::init_torfiles(fs::path where) {
    if (!torfiles.empty())
        return;

    // multi-file
    if (torrent.info.length == -1) {

        size_t offset = 0;
        for (auto &file : torrent.info.files) {
            fs::path file_path = where;
            for (auto &entry : file.path) {
                file_path /= entry;
            }
            // make torfile
            auto tf = std::make_unique<TorFile>();
#ifdef USING_SFILE
            tf->descriptor = SFile(file_path);
#else
            tf->descriptor =
                std::fstream(file_path, std::ios::in | std::ios::out | std::ios::binary |
                                            std::ios::trunc);

#endif

            fs::resize_file(file_path, file.length);
            tf->global_offset = offset;
            tf->path = file_path;
            tf->size = file.length;
            offset += file.length;

            torfiles.emplace(std::move(tf));
        }
        return;
    }
    // single-file
    fs::path file_path = where / torrent.info.name;
    auto tf = std::make_unique<TorFile>();
#ifdef USING_SFILE
    tf->descriptor = SFile(file_path.string());
#else
    tf->descriptor = std::fstream(file_path, std::ios::in | std::ios::out |
                                                 std::ios::binary | std::ios::trunc);
#endif
    fs::resize_file(file_path, torrent.info.length);
    tf->global_offset = 0;
    tf->path = file_path;
    tf->size = torrent.info.length;
    torfiles.emplace(std::move(tf));
}

// ─── handle_piece_v2 ─────────────────────────────────────────────────────────
void PeerConnection::handle_piece_v2(const Message &msg) {
    if (msg.data.size() == 0) {
        return;
    }
    bool piece_complete = false;
    std::vector<uint8_t> completed_buffer;
    {
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        auto &piece = torrent_state.pieces[msg.index];
        if (piece.state == PieceStatus::State::Done) {
            return;
        }
        if (piece.buffer.empty()) {
            piece.buffer.resize(piece.total_size);
        }
        if (msg.data.size() + msg.begin > piece.buffer.size()) {
            return;
        }

        std::copy(msg.data.begin(), msg.data.end(), piece.buffer.data() + msg.begin);
        piece.last_interacted = std::chrono::steady_clock::now();
        piece.downloaded += msg.data.size();
        if (piece.downloaded >= piece.total_size) {
            piece_complete = true;
            // piece.state = PieceStatus::State::Done;
            completed_buffer = std::move(piece.buffer);
            piece.buffer.clear();
            // piece.state = PieceStatus::State::Verifying;
        }
    }
    if (!piece_complete) {
        return;
    }

    std::string hashsum = sha1(completed_buffer);
    if (hashsum != torrent_state.torrent.info.pieces.at(msg.index)) {
        spdlog::error("[{}] Wrong SHA1 for piece {}", peer.str(), msg.index);
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        auto &piece = torrent_state.pieces[msg.index];
        piece.state = PieceStatus::State::Missing;
        piece.downloaded = 0;
        return;
    }

    size_t global_offset = msg.index * torrent_state.torrent.info.piece_length;
    size_t remain = completed_buffer.size();
    size_t buf_offset = 0;

    auto it = torrent_state.torfiles.lower_bound(global_offset);
    if (it != torrent_state.torfiles.begin())
        --it;
    while (remain > 0 && it != torrent_state.torfiles.end()) {
        TorFile &tf = **it;
        size_t file_offset = global_offset - tf.global_offset;
        size_t to_write = std::min(remain, tf.size - file_offset);

        {
            std::lock_guard<std::mutex> flock(tf.mut); // мьютекс только на этот файл

            spdlog::info("Writing {} bytes in {}, pos: {}", completed_buffer.size(),
                         tf.path.string(), file_offset);
#ifdef USING_SFILE
            tf.descriptor.write(
                reinterpret_cast<const char *>(completed_buffer.data() + buf_offset),
                to_write, file_offset);
#else
            tf.descriptor.seekp(file_offset, std::ios::beg);
            tf.descriptor.write(
                reinterpret_cast<const char *>(completed_buffer.data() + buf_offset),
                to_write);
            tf.descriptor.flush();
#endif
        }

        remain -= to_write;
        buf_offset += to_write;
        global_offset += to_write;
        it++;
    }

    // ШАГ 4: помечаем Done под pieces_mutex
    {
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        torrent_state.pieces[msg.index].state = PieceStatus::State::Done;
    }

    spdlog::info("[{}] Piece {} written to disk", peer.str(), msg.index);
}

// --- try_save
bool TorrentState::try_save(fs::path filename = "") {
    std::ofstream file(filename == "" ? torrent.info_hash + ".shitstate" : filename,
                       std::ios::binary);
    if (!file) {
        spdlog::error("Cannot save Torrent State to file! Aborting");
        return false;
    }
    file.write(reinterpret_cast<const char *>(&files_built), sizeof(bool));
    // file.write(client_id.data(), client_id.size());
    size_t pieces_count = pieces.size();
    file.write(reinterpret_cast<const char *>(&pieces_count), sizeof(size_t));
    for (auto &i : pieces) {

        if (i.state == PieceStatus::State::Downloading) {
        }
        PieceStatus::State save_state = i.state != PieceStatus::State::Downloading
                                            ? i.state
                                            : PieceStatus::State::Missing;
        file.write(reinterpret_cast<const char *>(&save_state),
                   sizeof(PieceStatus::State));
        size_t size = i.buffer.size();
        file.write(reinterpret_cast<const char *>(&size), sizeof(size_t));
        if (size > 0 && save_state != PieceStatus::State::Missing) {
            file.write(reinterpret_cast<const char *>(i.buffer.data()), size);
        }
        file.write(reinterpret_cast<const char *>(&i.downloaded), sizeof(uint32_t));
        file.write(reinterpret_cast<const char *>(&i.total_size), sizeof(uint32_t));
    }
    return true;
}

bool TorrentState::try_load(fs::path filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        spdlog::error("Cannot open file {}", filepath.string());
        return false;
    }
    file.read(reinterpret_cast<char *>(&files_built), sizeof(bool));
    size_t pieces_count;
    file.read(reinterpret_cast<char *>(&pieces_count), sizeof(size_t));
    pieces.resize(pieces_count);
    for (size_t i = 0; i < pieces_count; i++) {
        file.read(reinterpret_cast<char *>(&pieces[i].state), sizeof(PieceStatus::State));
        size_t size;
        file.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        if (size > 0) {
            pieces[i].buffer.resize(size);
            file.read(reinterpret_cast<char *>(pieces[i].buffer.data()), size);
        }
        file.read(reinterpret_cast<char *>(&pieces[i].downloaded), sizeof(uint32_t));
        file.read(reinterpret_cast<char *>(&pieces[i].total_size), sizeof(uint32_t));
    }
    return true;
}

// --- get_piece_by_index
std::vector<uint8_t> TorrentState::get_piece_by_index(size_t index) const {
    uint64_t piece_global_offset = index * torrent.info.piece_length;
    uint64_t remain = pieces[index].total_size;
    std::vector<uint8_t> buffer(remain);
    uint64_t buff_offset = 0;
    // найти первый файл, с оффсетом <= оффсета куска
    auto it = torfiles.lower_bound(piece_global_offset);
    if (it != torfiles.end()) {
        --it;
    }
    while (remain > 0 && it != torfiles.end()) {
        TorFile &tf = **it;
        size_t file_offset = piece_global_offset - tf.global_offset;
        size_t to_read = std::min(remain, tf.size - file_offset);

#ifdef USING_SFILE
        tf.descriptor.read(reinterpret_cast<char *>(buffer.data() + buff_offset), to_read,
                           file_offset);
#else
        {
            std::lock_guard<std::mutex> lock(tf.mut);
            tf.descriptor.seekp(file_offset, std::ios::beg);
            tf.descriptor.read(reinterpret_cast<char *>(buffer.data() + buff_offset),
                               to_read);
        }
#endif

        remain -= to_read;
        buff_offset += to_read;
        piece_global_offset += to_read;
        it++;
    }
    return buffer;
}

uint64_t TorrentState::downloaded() const {
    uint64_t downloaded = 0;
    uint64_t size = pieces.size();
    for (uint64_t i = 0; i < size; i++) {
        if (pieces[i].state == PieceStatus::State::Done) {
            downloaded += pieces[i].total_size;
        }
    }
    return downloaded;
}

void TorrentState::clear_downloading_pieces() {
    std::lock_guard<std::mutex> lock(pieces_mutex);
    spdlog::warn("Clearing downloading pieces");
    auto now = std::chrono::steady_clock::now();

    for (auto &i : pieces) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - i.last_interacted)
                .count();
        if (i.state == PieceStatus::State::Downloading && elapsed >= 5) {
            i.state = PieceStatus::State::Missing;
            i.buffer.clear();
            i.downloaded = 0;
        }
    }
}

