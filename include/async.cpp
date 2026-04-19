#include <chrono>
#include "async.hpp"

awaitable<AsyncConn> AsyncConn::connect(asio::io_context &ctx, Peer peer) {
    AsyncConn c(ctx);
    asio::ip::tcp::endpoint ep(asio::ip::address_v4(peer.ntohl_ip()), peer.port);
    co_await c.sock.async_connect(ep, asio::use_awaitable);
    co_return std::move(c);
}

awaitable<void> AsyncConn::send(const std::string &data) {
    co_await asio::async_write(sock, asio::buffer(data.data(), data.size()),
                               asio::use_awaitable);
}

awaitable<std::vector<uint8_t>> AsyncConn::recv_exact(uint64_t n) {
    std::vector<uint8_t> buf(n);
    co_await asio::async_read(sock, asio::buffer(buf.data(), n), asio::use_awaitable);
    co_return buf;
}

awaitable<void> AsyncConn::recv_into(void *buf, uint64_t size) {
    co_await asio::async_read(sock, asio::buffer(buf, size), asio::use_awaitable);
}

awaitable<std::shared_ptr<PeerConn2>> PeerConn2::create(asio::io_context &ioc,
                                                               Peer p, TorrentState &ts) {
    auto c = std::make_shared<PeerConn2>(
        PeerConn2{p, co_await AsyncConn::connect(ioc, p), ts});
    co_return c;
}

awaitable<bool> PeerConn2::do_handshake() {
    // отправляем
    std::string hs;
    hs.reserve(68);
    hs += static_cast<char>(19);
    hs += "BitTorrent protocol";
    hs += std::string(8, '\0');
    for (int i = 0; i < 20; i++)
        hs += static_cast<char>(torrent_state.torrent.info_hash_raw[i]);
    hs += torrent_state.client_id;
    co_await conn.send(hs);

    // получаем
    auto resp = co_await conn.recv_exact(68);

    if (resp[0] != 19)
        co_return false;
    if (std::string(resp.begin() + 1, resp.begin() + 20) != "BitTorrent protocol")
        co_return false;
    for (int i = 0; i < 20; i++)
        if (resp[28 + i] != torrent_state.torrent.info_hash_raw[i])
            co_return false;

    co_return true;
}

awaitable<Message> PeerConn2::recv_message() {
    auto buf4 = co_await conn.recv_exact(4);
    uint32_t len = ntohl(*reinterpret_cast<uint32_t *>(buf4.data()));

    Message msg;
    msg.length = len;

    if (len == 0) {
        msg.type = MessageType::KeepAlive;
        co_return msg;
    }

    uint8_t id;
    co_await conn.recv_into(&id, 1);
    msg.type = (MessageType)id;

    if (msg.type == MessageType::Choke || msg.type == MessageType::Unchoke ||
        msg.type == MessageType::Interested || msg.type == MessageType::Not_Interested) {
        spdlog::debug("[{}] Recv id={}", peer.str(), (int)msg.type);
        co_return msg;
    }

    if (msg.type == MessageType::Have) {
        uint32_t b;
        co_await conn.recv_into(&b, 4);
        msg.index = ntohl(b);

        spdlog::debug("[{}] Recv Have piece={}", peer.str(), msg.index);
        co_return msg;
    }

    if (msg.type == MessageType::Bitfield) {
        msg.data = co_await conn.recv_exact(msg.length - 1);
        spdlog::debug("[{}] Recv Bitfield ({} bytes)", peer.str(), msg.data.size());
        co_return msg;
    }

    if (msg.type == MessageType::Piece) {
        uint32_t idx, beg;
        co_await conn.recv_into(&idx, 4);
        co_await conn.recv_into(&beg, 4);

        msg.index = ntohl(idx);
        msg.begin = ntohl(beg);
        msg.data = co_await conn.recv_exact(msg.length - 9);

        spdlog::debug("[{}] Recv Piece index={} begin={} size={}", peer.str(), msg.index,
                      msg.begin, msg.data.size());

        co_return msg;
    }

    spdlog::debug("[{}] Recv unknown id={}, skipping {} bytes", peer.str(), (int)msg.type,
                  len - 1);

    co_await conn.recv_exact(msg.length - 1);
    co_return msg;
}

awaitable<void> PeerConn2::send_interested() {
    std::string buf = u32_to_str(1);
    buf += static_cast<char>(2);
    co_await conn.send(buf);
}

awaitable<void> PeerConn2::send_not_interested() {
    std::string buf = u32_to_str(1);
    buf += static_cast<char>(3);
    co_await conn.send(buf);
}

awaitable<void> PeerConn2::send_keep_alive() {
    std::string buf(4, '\0');
    co_await conn.send(buf);
}

awaitable<void> PeerConn2::send_request(uint32_t index, uint32_t begin, uint32_t length) {
    spdlog::debug("[{}] Requesting piece {} at {}, length = {}", peer.str(), index, begin, length);
    std::string req = u32_to_str(13);
    req += static_cast<char>(6);
    req += u32_to_str(index);
    req += u32_to_str(begin);
    req += u32_to_str(length);
    co_await conn.send(req);
}

awaitable<void> PeerConn2::send_bitfield() {
    size_t num_pieces = torrent_state.torrent.info.pieces.size();
    size_t num_bytes = (num_pieces + 7) / 8;
    std::string payload(num_bytes, '\0');
    std::string msg = u32_to_str(1 + num_bytes);
    msg += static_cast<char>(5);
    msg += payload;
    co_await conn.send(msg);
}

awaitable<void> PeerConn2::send_have(uint32_t index){
    std::string payload;
    payload += u32_to_str(5);
    payload += static_cast<char>(4);
    payload += u32_to_str(index);
    co_await conn.send(payload);
}

awaitable<void> PeerConn2::send_unchoke() {
    std::string buf = u32_to_str(1);
    buf += static_cast<char>(1);
    co_await conn.send(buf);
}

awaitable<void> PeerConn2::handle_piece(const Message &msg) {
    bool piece_complete = false;
    std::vector<uint8_t> completed_buffer;
    {
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        auto &piece = torrent_state.pieces[msg.index];
        if (piece.state == PieceStatus::State::Done) {
            co_return;
        }
        if (piece.buffer.empty()) {
            piece.buffer.resize(piece.total_size);
        }
        if (msg.data.size() + msg.begin > piece.buffer.size()) {
            co_return;
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
        co_return;
    }

    std::string hashsum = sha1(completed_buffer);
    if (hashsum != torrent_state.torrent.info.pieces.at(msg.index)) {
        spdlog::error("[{}] Wrong SHA1 for piece {}", peer.str(), msg.index);
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        auto &piece = torrent_state.pieces[msg.index];
        piece.state = PieceStatus::State::Missing;
        piece.downloaded = 0;
        co_return;
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

            spdlog::info("[{}] Writing {} bytes in {}, pos: {}", peer.str(), completed_buffer.size(),
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

awaitable<void> PeerConn2::run() {
    if (!co_await do_handshake()) {
        spdlog::warn("[{}] Handshake failed", peer.str());
        co_return;
    }
    spdlog::info("[{}] Handshake OK", peer.str());

    while (!torrent_state.is_done()) {
        Message msg = co_await recv_message();

        switch (msg.type) {
        case MessageType::KeepAlive:
            co_await send_keep_alive();
            break;

        case MessageType::Choke:
            peer_choking = true;
            spdlog::info("[{}] Choked", peer.str());
            break;

        case MessageType::Unchoke:
            peer_choking = false;
            spdlog::info("[{}] Unchoked", peer.str());
            if (am_interested)
                co_await request_next_piece();
            break;

        case MessageType::Have:
            if (msg.index < bitfield.size() && !bitfield[msg.index]) {
                bitfield[msg.index] = true;
                std::lock_guard lock(torrent_state.pieces_mutex);
                torrent_state.piece_orderer.counts[msg.index]++;
            }
            break;

        case MessageType::Bitfield:
            // убираем старый вклад если был
            if (!bitfield.empty()) {
                std::lock_guard lock(torrent_state.pieces_mutex);
                torrent_state.piece_orderer.remove_bitfield(bitfield);
            }
            // парсим новый
            bitfield.clear();
            bitfield.reserve(msg.data.size() * 8);
            for (size_t i = 0; i < msg.data.size(); i++)
                for (int bit = 7; bit >= 0; bit--)
                    bitfield.push_back((msg.data[i] >> bit) & 1);
            {
                std::lock_guard lock(torrent_state.pieces_mutex);
                torrent_state.piece_orderer.add_bitfield(bitfield);
            }
            spdlog::info("[{}] Got bitfield", peer.str());
            am_interested = true;
            if (!peer_choking)
                co_await send_interested();
            break;

        case MessageType::Piece: {
            bool was_done =
                torrent_state.pieces[msg.index].state == PieceStatus::State::Done;
            co_await handle_piece(msg);
            bool piece_done =
                torrent_state.pieces[msg.index].state == PieceStatus::State::Done;

            if (peer_choking)
                break;

            if (!piece_done && !was_done) {
                // кусок не завершён — следующий блок
                uint32_t next = torrent_state.pieces[msg.index].downloaded;
                uint32_t remain = torrent_state.pieces[msg.index].total_size - next;
                uint32_t block = std::min(remain, (uint32_t)16384);
                if (block > 0)
                    co_await send_request(msg.index, next, block);
            } else {
                // кусок завершён — следующий кусок
                co_await request_next_piece();
            }
            break;
        }

        default:
            spdlog::debug("[{}] Unknown msg type={}", peer.str(), (int)msg.type);
            break;
        }
    }

    // убираем вклад этого пира из ордерера
    if (!bitfield.empty()) {
        std::lock_guard lock(torrent_state.pieces_mutex);
        torrent_state.piece_orderer.remove_bitfield(bitfield);
    }

    spdlog::info("[{}] Done", peer.str());
}

awaitable<void> PeerConn2::request_next_piece() {
    // int piece;
    // {
    //     std::lock_guard lock(torrent_state.pieces_mutex);
    //     // get_next и пометка Downloading — атомарно, чтобы два пира
    //     // не схватили один и тот же кусок
    //     piece = torrent_state.piece_orderer.get_next(bitfield);
    //     spdlog::info("About to request piece {}", piece);
    //     if (piece < 0)
    //         co_return;
    //     torrent_state.pieces[piece].state = PieceStatus::State::Downloading;
    // }

    // co_await send_request(piece, 0, 16384);
    int piece = torrent_state.next_missing_piece(bitfield);
    if (piece < 0)
        co_return; // нечего качать у этого пира
    {
        std::lock_guard<std::mutex> lock(torrent_state.pieces_mutex);
        // ещё раз проверяем под мьютексом — другой поток мог занять кусок
        if (torrent_state.pieces[piece].state != PieceStatus::State::Missing)
            co_return;
        torrent_state.pieces[piece].state = PieceStatus::State::Downloading;
        torrent_state.pieces[piece].last_interacted = std::chrono::steady_clock::now();
    }
    co_await send_request(piece, 0, 16384);
}
