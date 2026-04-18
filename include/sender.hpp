#ifndef INCLUDE_SENDER_HPP_
#define INCLUDE_SENDER_HPP_

#include <coroutine>
#include "async.hpp"

struct SeedConn {
    Peer peer;
    AsyncConn conn;
    TorrentState *torrent_state = nullptr;
    // std::chrono::steady_clock::time_point last_keep_alive;

    bool handshook = false;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    static awaitable<std::shared_ptr<SeedConn>> create(asio::io_context &ctx, Peer p) ;

    awaitable<bool> do_handshake();
    awaitable<Message> recv_message() ;
    awaitable<void> send_keep_alive();

    awaitable<void> send_bitfield();

    awaitable<void> send_have(uint32_t index) ;

    awaitable<void> send_unchoke();
    awaitable<void> run();

    awaitable<void> handle_request(const Message &msg);
    
};

struct SeedServer {
    tcp::acceptor acceptor;
    asio::io_context &ioc;

    SeedServer(asio::io_context &ioc, uint16_t port)
        : ioc(ioc), acceptor(ioc, tcp::endpoint(tcp::v4(), port)) {}

    awaitable<void> run();
};

#endif // INCLUDE_SENDER_HPP_
