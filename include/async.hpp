#ifndef INCLUDE_ASYNC_HPP_
#define INCLUDE_ASYNC_HPP_
#include <coroutine>
#include "torrent.hpp"
#include "asio.hpp"

using tcp = asio::ip::tcp;
using asio::awaitable;
#undef min

struct AsyncConn {
    tcp::socket sock;

    static awaitable<AsyncConn> connect(asio::io_context &ctx, Peer peer);
    awaitable<void> send(const std::string &data);
    awaitable<std::vector<uint8_t>> recv_exact(uint64_t n);
    awaitable<void> recv_into(void *buf, uint64_t size);
    static AsyncConn from_socket(tcp::socket sock) {
        AsyncConn c(sock.get_executor());
        c.sock = std::move(sock);
        return c;
    }

  private:
    explicit AsyncConn(asio::io_context &ioc) : sock(ioc) {}
    explicit AsyncConn(asio::any_io_executor ex) : sock(ex) {}
};

/// @brief Соединение с пиром, в основном загружает, но если  пир попросит, может и
/// отдавать
struct PeerConn2 {
    Peer peer;
    AsyncConn conn;
    TorrentState &torrent_state;
    // time_t last_keep_alive;

    bool handshook = false;

    bool am_choking = true;
    bool am_interested = false;
    bool peer_choking = true;
    bool peer_interested = false;

    std::vector<bool> bitfield{};
    std::vector<bool> downloaded{};
    bool downloaded_all_what_can = false;

    static awaitable<std::shared_ptr<PeerConn2>> create(asio::io_context &ioc, Peer p,
                                                        TorrentState &ts);
    awaitable<bool> do_handshake();
    awaitable<Message> recv_message();
    awaitable<void> send_interested();
    awaitable<void> send_not_interested();
    awaitable<void> send_keep_alive();
    awaitable<void> send_request(uint32_t index, uint32_t begin, uint32_t length);
    awaitable<void> send_bitfield();
    awaitable<void> send_have(uint32_t index);
    awaitable<void> send_unchoke();

    /// @brief Загружает байты из сообщения в буфер куска.
    /// @brief Если кусок скачан полностью, проверяет sha1 и записывает в файлы.
    /// @brief Так же помечает соотв. piece в TorrentState как Done
    /// @param msg Распаршенное сообщение от пира
    awaitable<void> handle_piece(const Message &msg);
    awaitable<void> run();
    awaitable<bool> request_next_piece();
};

#endif // INCLUDE_ASYNC_HPP_