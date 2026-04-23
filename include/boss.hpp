#ifndef INCLUDE_BOSS_HPP_
#define INCLUDE_BOSS_HPP_

#include <sstream>
#include <thread>
#include <list>
#include <chrono>
#include "torrent.hpp"
#include "async.hpp"
#include "defer.hpp"

#define THREAD_COUNT 2

struct Downloader {
    TorrentState &ts;
    std::string announce_host;
    std::string announce_path;
    HttpConn http_tracker_conn;
    AsyncHttpConn async_http_tracker_conn;
    std::set<Peer, PeerComparer> peer_list;
    std::set<Peer, PeerComparer> connected_peers;
    std::string client_id = "-BT7105-123456789101";
    uint16_t port = 6888;
    int interval = 30;
    asio::io_context &ctx;
    std::chrono::steady_clock::time_point last_tracker_contact;
    std::chrono::steady_clock::time_point last_progress;
    std::chrono::steady_clock::time_point last_saved_progress;
    uint64_t last_downloaded = 0;
    bool started = false;
    std::function<void()> on_complete;

    Downloader(TorrentState &_ts, asio::io_context &_ctx)
        : ts(_ts), ctx(_ctx), announce_host(get_url_hostname(ts.torrent.announce_url)),
          announce_path(get_url_path(ts.torrent.announce_url)),
          http_tracker_conn(std::wstring(announce_host.begin(), announce_host.end())),
          async_http_tracker_conn(_ctx, parse_url(ts.torrent.announce_url)),
          last_tracker_contact(std::chrono::steady_clock::now()),
          last_progress(std::chrono::steady_clock::now()),
          last_saved_progress(std::chrono::steady_clock::now()) {}

    awaitable<void> run() {
        // сохраняем стейт в конце при выходе из функции
        defer({
            try {
                ts.try_save(fs::path(ts.torrent.info_hash + ".state"));
            } catch (const std::exception &e) {
                spdlog::error("Failed to save state: {}", e.what());
            }
        });

        if (ts.is_done()) {
            spdlog::warn("Torrent already done, aborting");
            on_complete();
            co_return;
        }

        if (!started) {
            try {
                // send_start_and_recv_peers();
                co_await async_send_start_and_recv_peers();
                started = true;
            } catch (const std::exception &e) {
                spdlog::warn("Failed to notify tracker on startup: {}", e.what());
                on_complete();
                co_return;
            }
        }
        spawn_new_peers();

        asio::steady_timer timer(ctx);
        auto last_clear = std::chrono::steady_clock::now();

        while (!ts.is_done()) {
            timer.expires_after(asio::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);

            auto now = std::chrono::steady_clock::now();

            auto cur_progress = ts.downloaded();
            if (cur_progress > last_downloaded) {
                last_downloaded = cur_progress;
                last_progress = now;
            }

            auto progress_elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_progress)
                    .count();
            if (progress_elapsed > 10) {
                reconnect_peers();
                last_progress = now; // сбрасываем таймер
            }

            // проверка зависших кусков
            auto clear_elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_clear)
                    .count();
            if (clear_elapsed > 5) {
                ts.clear_downloading_pieces();
                last_clear = now;
            }

            // проверка трекера
            auto tracker_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                       now - last_tracker_contact)
                                       .count();
            if (tracker_elapsed >= interval) {
                try {
                    // send_empty_and_recv_peers();
                    co_await async_send_empty_and_recv_peers();
                    last_tracker_contact = now;
                } catch (const std::exception &e) {
                    spdlog::warn("Tracker re-announce failed: {}", e.what());
                }
            }

            spawn_new_peers();

            // сохраняем стейт
            auto save_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                    now - last_saved_progress)
                                    .count();
            if (save_elapsed > 10) {
                try {
                    ts.try_save(fs::path(ts.torrent.info_hash + ".state"));
                } catch (const std::exception &e) {
                    spdlog::error("Failed to save state: {}", e.what());
                }
                last_saved_progress = now;
            }
        }

        spdlog::info("Torrent {} done", ts.torrent.info_hash);
        try {
            if (ts.is_done()) {
                co_await async_send_request("completed");
            } else {
                co_await async_send_request("stopped");
            }
        } catch (const std::exception &e) {
            spdlog::warn("Failed to notify tracker on shutdown: {}", e.what());
        }
        on_complete();
    }

    HttpConn::response send_request(std::string event = "") {
        // clang-format off
        std::string path =
            announce_path + 
            "?info_hash=" + ts.torrent.urlencoded_info_hash +
            "&peer_id=" + client_id + 
            "&port=" + std::to_string(port) + 
            "&uploaded=0" +
            "&downloaded=" + std::to_string(ts.downloaded()) +
            "&left=" + std::to_string(ts.torrent.total_length - ts.downloaded()) +
            "&compact=1" + "&event=" + event;
        // clang-format on
        return http_tracker_conn.get(std::wstring(path.begin(), path.end()));
    }

    awaitable<AsyncHttpConn::response> async_send_request(std::string event = "") {
        spdlog::debug("CALL async_send_request");
        // clang-format off
        std::string path =
            announce_path + 
            "?info_hash=" + ts.torrent.urlencoded_info_hash +
            "&peer_id=" + client_id + 
            "&port=" + std::to_string(port) + 
            "&uploaded=0" +
            "&downloaded=" + std::to_string(ts.downloaded()) +
            "&left=" + std::to_string(ts.torrent.total_length - ts.downloaded()) +
            "&compact=1" + "&event=" + event;
        // clang-format on
        try {
            auto res = co_await async_http_tracker_conn.async_get(path);
            spdlog::debug("RET async_send_request");
            co_return res;
        } catch (std::exception &e) {
            spdlog::error("WHOA WHOA {}", e.what());
        }
        co_return AsyncHttpConn::response{false, 404, "OLEG"};
    }

  private:
    awaitable<void> async_send_start_and_recv_peers() {
        spdlog::debug("CALL async_send_start_and_recv_peers");
        auto ans = co_await async_send_request("started");
        std::istringstream ыы(ans.data);
        BencodeVal resp_decoded = read_bencode(ыы);
        try {
            interval = resp_decoded.get_dict().at("interval").get_int();
        } catch (const std::exception &e) {
            interval = 30;
        }
        update_peer_list(resp_decoded.get_dict().at("peers").get_str());
        spdlog::debug("RET async_send_start_and_recv_peers");
    }

    awaitable<void> async_send_empty_and_recv_peers() {
        spdlog::debug("CALL async_send_empty_and_recv_peers");
        auto ans = co_await async_send_request();
        std::istringstream ыы(ans.data);
        BencodeVal resp_decoded = read_bencode(ыы);
        update_peer_list(resp_decoded.get_dict().at("peers").get_str());
        spdlog::debug("RET async_send_empty_and_recv_peers");
    }

    void send_start_and_recv_peers() {
        auto ans = send_request("started");
        std::istringstream ыы(ans.data);
        BencodeVal resp_decoded = read_bencode(ыы);
        try {
            interval = resp_decoded.get_dict().at("interval").get_int();
        } catch (const std::exception &e) {
            interval = 30;
        }
        update_peer_list(resp_decoded.get_dict().at("peers").get_str());
    }

    void send_empty_and_recv_peers() {
        auto ans = send_request();
        std::istringstream ыы(ans.data);
        BencodeVal resp_decoded = read_bencode(ыы);
        update_peer_list(resp_decoded.get_dict().at("peers").get_str());
    }

    void update_peer_list(const std::string &peers_str) {
        auto peers = parse_peers(peers_str);
        for (auto &p : peers) {
            peer_list.emplace(p);
        }
    }

    void spawn_new_peers() {
        spdlog::debug("Spawning new peers");
        int n = 0;
        for (auto peer : peer_list) {
            if (connected_peers.count(peer)) {
                n++;
                continue;
            }

            connected_peers.emplace(peer);

            asio::co_spawn(
                ctx,
                [this, peer]() -> awaitable<void> {
                    try {
                        auto pc = co_await PeerConn2::create(ctx, peer, ts);
                        co_await pc->run();
                    } catch (const asio::system_error &e) {
                        spdlog::warn("[{}] asio error: {} ({})", peer.str(), e.what(),
                                     e.code().message());
                    } catch (const std::exception &e) {
                        spdlog::warn("[{}] error: {}", peer.str(), e.what());
                    } catch (...) {
                        spdlog::warn("[{}] unknown exception", peer.str());
                    }
                    spdlog::debug("[{}] coroutine exited", peer.str());
                },
                asio::detached);
        }
    }

    void reconnect_peers() {
        connected_peers.clear();
        spdlog::info("[{}] Reconnecting to peers", ts.torrent.info_hash);
    }
};

#endif
