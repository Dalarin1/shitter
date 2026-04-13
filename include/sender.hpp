#ifndef INCLUDE_SENDER_HPP_
#define INCLUDE_SENDER_HPP_

#include <coroutine>
#include "async.hpp"

struct SeedConn {
    Peer peer;
    AsyncConn conn;
    TorrentState &torrent_state;
    //std::chrono::steady_clock::time_point last_keep_alive;

    bool handshook = false;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    static awaitable<std::shared_ptr<SeedConn>> create(asio::io_context &ctx, Peer p,
                                                       TorrentState &ts) {
        auto c = std::make_shared<SeedConn>(
            SeedConn{p, co_await AsyncConn::connect(ctx, p), ts});
        co_return c;
    }

    awaitable<bool> do_handshake() {
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
        for (int i = 0; i < 20; i++)
            if (resp[28 + i] != torrent_state.torrent.info_hash_raw[i]) {
                conn.sock.close();
                co_return false;
            }

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
        co_return true;
    }

    awaitable<Message> recv_message() {
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
            msg.type == MessageType::Interested ||
            msg.type == MessageType::Not_Interested) {
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

            spdlog::debug("[{}] Recv Piece index={} begin={} size={}", peer.str(),
                          msg.index, msg.begin, msg.data.size());

            co_return msg;
        }
        
        if(msg.type == MessageType::Request){
            uint32_t idx, beg, req_len;
            co_await conn.recv_into(&idx, 4);
            co_await conn.recv_into(&beg, 4);
            co_await conn.recv_into(&req_len, 4);

            msg.index = idx;
            msg.begin = beg;
            msg.length = req_len;
            
            spdlog::debug("[{}] Recv Request index={} begin={} len={}", peer.str(), msg.index, msg.begin, msg.length);

            co_return msg;
        }

        spdlog::debug("[{}] Recv unknown id={}, skipping {} bytes", peer.str(),
                      (int)msg.type, len - 1);

        co_await conn.recv_exact(msg.length - 1);
        co_return msg;
    }

    awaitable<void> send_keep_alive() {
        std::string buf(4, '\0');
        co_await conn.send(buf);
    }

    awaitable<void> send_bitfield() {
        size_t num_pieces = torrent_state.torrent.info.pieces.size();
        size_t num_bytes = (num_pieces + 7) / 8;
        std::string payload(num_bytes, '\0');
        std::string msg = u32_to_str(1 + num_bytes);
        msg += static_cast<char>(5);
        msg += payload;
        co_await conn.send(msg);
    }

    awaitable<void> send_have(uint32_t index) {
        std::string payload;
        payload += u32_to_str(5);
        payload += static_cast<char>(4);
        payload += u32_to_str(index);
        co_await conn.send(payload);
    }

    awaitable<void> send_unchoke() {
        std::string buf = u32_to_str(1);
        buf += static_cast<char>(1);
        co_await conn.send(buf);
    }

    awaitable<void> run(){
        if(!co_await do_handshake()){
            spdlog::warn("[{}] Handshake failed", peer.str());
            conn.sock.close();
            co_return;
        }
        spdlog::debug("[{}] Handshake OK", peer.str());
    
        co_await send_unchoke();
        co_await send_bitfield();

        while(true){
            Message msg = co_await recv_message();

            switch (msg.type){
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
    awaitable<void> handle_request(const Message& msg){
        // recv smth like <len=0013><id=6><index><begin><length>
        // check if  we have that piece
        // if we dont, abort connection
        //
        // if we have piece, we: search for first file, that contains piece
        // reading requested bytes inside buffer, and sending them to peer
        
        if(torrent_state.pieces[msg.index].state != PieceStatus::State::Done){
            conn.sock.close();
            co_return;
        }
        std::vector<uint8_t> buffer = torrent_state.get_piece_by_index(msg.index);
        buffer.resize(msg.length);
        
        co_await asio::async_write(conn.sock, asio::buffer(buffer.data(), buffer.size()), asio::use_awaitable);
    }
};

struct SeedServer {
    tcp::acceptor acceptor;
    
    void accept_loop();
};

#endif // INCLUDE_SENDER_HPP_
