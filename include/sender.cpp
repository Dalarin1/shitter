#include "sender.hpp"

awaitable<std::shared_ptr<SeedConn>> SeedConn::create(asio::io_context &ctx, Peer p) {
    auto c = std::make_shared<SeedConn>(SeedConn{p, co_await AsyncConn::connect(ctx, p)});
    co_return c;
}

awaitable<bool> SeedConn::do_handshake() {
    // получаем
    auto resp = co_await conn.recv_exact(68);

    if (resp[0] != 19) {
        conn.sock.close();
        co_return false;
    }
    if (std::string(resp.begin() + 1, resp.begin() + 20) != "BitTorrent protocol") {
        conn.sock.close();
        co_return false;
    }
    std::array<uint8_t, 20> buf = std::array<uint8_t, 20>();
    for (int i = 0; i < 20; i++) {
        // if (resp[28 + i] != torrent_state->torrent.info_hash_raw[i]) {
        //     conn.sock.close();
        //     co_return false;
        // }
        // считываем инфо-хэш. Если полученного хэша нет в torrent_map, то канец
        // иначе - переопределяем свой торрент
        buf[i] = resp[28 + i];
    }
    if (!TorrentState::torrent_map.contains(buf)) {
        co_return false;
    } else {
        torrent_state = TorrentState::torrent_map.at(buf);
    }
    // отправляем
    std::string hs;
    hs.reserve(68);
    hs += static_cast<char>(19);
    hs += "BitTorrent protocol";
    hs += std::string(8, '\0');
    for (int i = 0; i < 20; i++)
        hs += static_cast<char>(torrent_state->torrent.info_hash_raw[i]);
    hs += torrent_state->client_id;
    co_await conn.send(hs);
    co_return true;
}

awaitable<Message> SeedConn::recv_message() {
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

    if (msg.type == MessageType::Request) {
        uint32_t idx, beg, req_len;
        co_await conn.recv_into(&idx, 4);
        co_await conn.recv_into(&beg, 4);
        co_await conn.recv_into(&req_len, 4);

        msg.index = ntohl(idx);
        msg.begin = ntohl(beg);
        msg.length = ntohl(req_len);

        spdlog::debug("[{}] Recv Request index={} begin={} len={}", peer.str(), msg.index,
                      msg.begin, msg.length);

        co_return msg;
    }

    spdlog::debug("[{}] Recv unknown id={}, skipping {} bytes", peer.str(), (int)msg.type,
                  len - 1);

    co_await conn.recv_exact(msg.length - 1);
    co_return msg;
}

awaitable<void> SeedConn::send_keep_alive() {
    std::string buf(4, '\0');
    co_await conn.send(buf);
}

awaitable<void> SeedConn::send_bitfield() {
    size_t num_pieces = torrent_state->torrent.info.pieces.size();
    size_t num_bytes = (num_pieces + 7) / 8;
    std::string payload(num_bytes, '\0');
    for (size_t i = 0; i < num_pieces; i++) {
        if (torrent_state->pieces[i].state == PieceStatus::State::Done)
            payload[i / 8] |= (1 << (7 - (i % 8)));
    }
    std::string msg = u32_to_str(1 + num_bytes);
    msg += static_cast<char>(5);
    msg += payload;
    co_await conn.send(msg);
}

awaitable<void> SeedConn::send_have(uint32_t index) {
    std::string payload;
    payload += u32_to_str(5);
    payload += static_cast<char>(4);
    payload += u32_to_str(index);
    co_await conn.send(payload);
}

awaitable<void> SeedConn::send_unchoke() {
    std::string buf = u32_to_str(1);
    buf += static_cast<char>(1);
    co_await conn.send(buf);
}

awaitable<void> SeedConn::run() {
    if (!co_await do_handshake()) {
        spdlog::warn("[{}] Handshake failed", peer.str());
        conn.sock.close();
        co_return;
    }
    spdlog::debug("[{}] Handshake OK", peer.str());

    co_await send_unchoke();
    co_await send_bitfield();

    while (true) {
        Message msg = co_await recv_message();

        switch (msg.type) {
        case MessageType::KeepAlive:
            co_await send_keep_alive();
            break;
        case MessageType::Interested:
            peer_interested = true;
            break;
        case MessageType::Request:
            co_await handle_request(msg);
            break;
        }
    }
}

awaitable<void> SeedConn::handle_request(const Message &msg) {
    if (torrent_state->pieces[msg.index].state != PieceStatus::State::Done) {
        conn.sock.close();
        co_return;
    }

    std::vector<uint8_t> block = torrent_state->get_piece_by_index(msg.index);
    // begin и length из запроса — пир просит конкретный блок внутри куска
    uint32_t begin = msg.begin;
    uint32_t length = msg.length;

    std::string out;
    out += u32_to_str(9 + length); // len
    out += static_cast<char>(7);   // id = Piece
    out += u32_to_str(msg.index);  // index
    out += u32_to_str(begin);      // begin
    out += std::string(block.begin() + begin, block.begin() + begin + length);

    co_await conn.send(out);
}

awaitable<void> SeedServer::run() {
    std::cout << "Server started, waiting for connections..." << std::endl;
    
    for (;;) {
        std::cout << "Waiting for new connection..." << std::endl;
        
        auto sock = co_await acceptor.async_accept(asio::use_awaitable);
        auto peer = Peer::from_endpoint(sock.remote_endpoint());
        
        std::cout << "New connection from: " << peer.str() << std::endl;

        auto conn = std::make_shared<SeedConn>(SeedConn{
            .peer = peer,
            .conn = AsyncConn::from_socket(std::move(sock)),
        });

        co_spawn(ioc, conn->run(), asio::detached);
    }
}