#ifndef INCLUDE_SESSION_HPP_
#define INCLUDE_SESSION_HPP_
#include <list>
#include <filesystem>
#include "torrent.hpp"
#include "boss.hpp"
#include "sender.hpp"

namespace fs = std::filesystem;

// Одна сессия - один скачиватель, один State
struct TorrentSession {
    asio::io_context &ctx;
    TorrentState torrent_state;
    Downloader downloader;

    explicit TorrentSession(asio::io_context &ioc, Torrent t)
        : ctx(ioc), torrent_state(t), downloader(torrent_state, ctx) {}
};

struct App {
    asio::io_context ctx;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;
    std::list<TorrentSession> sessions;
    std::vector<std::thread> download_threads;
    SeedServer uploader;

    App() : work_guard(asio::make_work_guard(ctx)), uploader(ctx, 6888) {
        spdlog::debug("App built");
        for (short i = 0; i < THREAD_COUNT * 2; i++) {
            download_threads.emplace_back([this]() { ctx.run(); });
        }
        spdlog::debug("Started {} threads", THREAD_COUNT * 2);

        // asio::co_spawn(
        //     ctx, [&]() -> awaitable<void> { co_await uploader.run(); }, asio::detached);

        spdlog::debug("SeedServer started");
    }

    void add_torrent(fs::path torrent) {
        // add and start new session
        Torrent t(torrent);
        print_torrent(t);
        sessions.emplace_back(ctx, t);
        spdlog::debug("Added new session for torrent {}", t.info_hash);

        auto *dl = &sessions.back().downloader;
        dl->on_complete = [this]() {
            // проверяем все ли торренты готовы
            bool all_done = true;
            for (auto &s : sessions) {
                if (!s.torrent_state.is_done()) {
                    all_done = false;
                    break;
                }
            }
            if (all_done) {
                spdlog::debug("Work guard reset");
                work_guard.reset();
                ctx.stop();
            }
        };
        asio::co_spawn(
            ctx, [dl]() -> awaitable<void> { co_await dl->run(); }, asio::detached);
        spdlog::debug("Spawned downloader.run()");
    }

    void remove_torrent(...);

    ~App() {
        // work_guard.reset();

        for (size_t i = 0; i < download_threads.size(); i++) {
            if (download_threads[i].joinable()) {
                download_threads[i].join();
            }
        }
    }
};

#endif // INCLUDE_SESSION_HPP_