#ifndef INCLUDE_SENDER_HPP_
#define INCLUDE_SENDER_HPP_

#include "async.hpp"

struct SeedConn{
    AsyncConn conn;
    TorrentState &torrent_state;
    // time_t last_keep_alive;

    bool handshook = false;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;


    static awaitable<std::shared_ptr<SeedConn>> create(asio::io_context &ioc, Peer p,
                                                        TorrentState &ts);
    awaitable<bool> do_handshake();
    awaitable<Message> recv_message();
    awaitable<void> send_keep_alive();
    awaitable<void> send_bitfield();
    awaitable<void> send_have(uint32_t index);
    awaitable<void> send_unchoke();
    awaitable<void> run();
    awaitable<void> handle_request();
};

struct SeedServer{
    tcp::acceptor acceptor;

    void accept_loop();
};

#endif // INCLUDE_SENDER_HPP_