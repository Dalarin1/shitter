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

TorrentState::TorrentState(const Torrent &t)
    : torrent(t), piece_orderer(t.info.pieces.size()) {
    torrent_map[torrent.info_hash_raw] = this;

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

    if (fs::exists(fs::path(torrent.info_hash + ".state"))) {
        bool success = try_load(fs::path(torrent.info_hash + ".state"));
        if (!success) {
            spdlog::error("State file loading failed; Creating torrent files");
            preallocate_files(fs::current_path());
        } else {
            spdlog::info("State successfully loaded");
        }
    } else {
        spdlog::error("State file loading failed; Creating torrent files");
        preallocate_files(fs::current_path());
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
    if (!is_done()) {
        spdlog::error("Torrent state is not done, but next_missing_piece returned -1");
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

Torrent::Torrent(fs::path filename) {
    auto file = std::ifstream(filename, std::ios::binary);
    BencodeVal data = read_bencode(file);

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
        info.length = 0;

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
    spdlog::debug("Preallocating files");
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
    if (!torfiles.empty()){
        spdlog::error("init_torfiles: torfiles empty");
        return;
    }

    // multi-file
    if (torrent.info.length == 0) {

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
            spdlog::debug("Creating file {}", file_path.string());
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
    spdlog::debug("Creating file {}", file_path.string());
    tf->descriptor = std::fstream(file_path, std::ios::in | std::ios::out |
                                                 std::ios::binary | std::ios::trunc);
#endif
    fs::resize_file(file_path, torrent.info.length);
    tf->global_offset = 0;
    tf->path = file_path;
    tf->size = torrent.info.length;
    torfiles.emplace(std::move(tf));
}

// --- try_save
bool TorrentState::try_save(fs::path filename = "") {
    std::ofstream file(filename == "" ? torrent.info_hash + ".state" : filename,
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
    // auto now = std::chrono::steady_clock::now();

    for (auto &i : pieces) {
        // auto elapsed =
        //     std::chrono::duration_cast<std::chrono::seconds>(now - i.last_interacted)
        //         .count();
        // if (i.state == PieceStatus::State::Downloading && elapsed >= 5) {
        if (i.state == PieceStatus::State::Downloading) {
            i.state = PieceStatus::State::Missing;
            i.buffer.clear();
            i.downloaded = 0;
        }
    }
}

Peer Peer::from_endpoint(const asio::ip::tcp::endpoint &ep) {
    Peer p;
    auto addr = ep.address().to_v4().to_bytes(); // std::array<uint8_t,4>
    std::memcpy(p.ip, addr.data(), 4);
    p.port = ep.port();
    return p;
}
