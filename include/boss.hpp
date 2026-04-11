#ifndef INCLUDE_BOSS_HPP_
#define INCLUDE_BOSS_HPP_

#include <sstream>
#include <thread>
#include <list>
#include <chrono>
#include "torrent.hpp"
#include "async.hpp"

/*
логика работы биг босса

при создании объекта - захватываем нужный Torrent, создаем ему TorrentState
если есть соотв. файлик стейта - подгружаем данные оттуда в наш стейт

отправляем event=started трэкеру, получаем список пиров, запоминаем их

создаем по коннекту на каждый пир, проверяем рукопожатие, если не прошло - сбрасываем и
удаляем коннект

для каждого из оставшихся коннектов - вызываем run (там в цикле скачивание следующего
куска, которого нет у нас, но есть у пира)

дальше, в цикле -

    проверяем, не скачали ли мы все. Если скачали - закрываемся, конец цикла

    смотрим, как давно общались с трэкером. Если достаточно давно - посылаем ему запрос
    снова, получаем список пиров.

    Если есть новые пиры, обновляем наш список пиров

    пробуем нерукопожатых пиров из списка, если есть рукопожатые - запускаем их коннекты
на скачивание

В деструкторе -
    закрываем все незакрытые коннекты

    если скачано всё - отправляем трэкеру запрос с event=completed, иначе - event=stopped

    сохраняем TorrentState в файлик, чтоб потом не перекачивать уже готовое

    канец
*/

/*
NOTES
пока что БигБосс будет работать с говном - один поток на один коннект
потом поменяю на один поток - несколько коннектов, через (e)poll, наверн

важно, чтобы при завершении загрузки БигБосс об этом узнавал, но наверное это можно
проверять в цикле BigBoss::run
в конце концов, даже с вагоном потоков, каждый из потоков выполняет один PeerConnect::run,
а тот завершается, когда всё скачано, т.е. TorrentState.done() == true
*/

#define THREAD_COUNT 2

struct BigBoss {
    TorrentState &ts;
    std::string announce_host;
    std::string announce_path;
    HttpConn http_tracker_conn;

    std::set<Peer, PeerComparer> peer_list;
    std::set<Peer, PeerComparer> connected_peers;
    std::list<std::unique_ptr<PeerConnection>> connects;

    std::string client_id = "-BT7105-123456789101";
    uint16_t port = 6888;

    int interval = 30;

    asio::io_context &ctx;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;

    std::vector<std::thread> io_threads;
    bool threads_joined;

    std::chrono::steady_clock::time_point last_tracker_contact;

    BigBoss(TorrentState &_ts, asio::io_context &_ctx)
        : ts(_ts), ctx(_ctx), announce_host(get_url_hostname(ts.torrent.announce_url)),
          announce_path(get_url_path(ts.torrent.announce_url)),
          http_tracker_conn(std::wstring(announce_host.begin(), announce_host.end())),
          last_tracker_contact(std::chrono::steady_clock::now()),
          work_guard(asio::make_work_guard(ctx)) {

        if (fs::exists(fs::path(ts.torrent.info_hash + ".state"))) {
            bool success = ts.try_load(fs::path(ts.torrent.info_hash + ".state"));
            if (!success) {
                spdlog::error("State file loading failed; Creating torrent files");
                ts.preallocate_files(fs::current_path());
            }
        }
    }

    void run() {
        if (ts.is_done()) {
            spdlog::warn("Torrent already done, aborting");
            return;
        }

        // запускаем потоки
        for (int i = 0; i < THREAD_COUNT; i++)
            io_threads.emplace_back([this] { ctx.run(); });

        send_start_and_recv_peers();
        spawn_new_peers();

        while (!ts.is_done()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - last_tracker_contact)
                               .count();
            if (elapsed >= interval) {
                try {
                    send_empty_and_recv_peers();
                    last_tracker_contact = std::chrono::steady_clock::now();
                } catch (const std::exception &e) {
                    spdlog::warn("Tracker re-announce failed: {}", e.what());
                }
            }
            spawn_new_peers();
        }

        // скачали всё — отпускаем ioc и ждём завершения корутин
        work_guard.reset();
        join_threads();
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

    ~BigBoss() {
        // отправляем финальный запрос трекеру
        try {
            if (ts.is_done()) {
                send_request("completed");
            } else {
                send_request("stopped");
            }
        } catch (const std::exception &e) {
            spdlog::warn("Failed to notify tracker on shutdown: {}", e.what());
        }

        // сохраняем стейт
        try {
            ts.try_save(fs::path(ts.torrent.info_hash + ".state"));
        } catch (const std::exception &e) {
            spdlog::error("Failed to save state: {}", e.what());
        }
    }

  private:
    void send_start_and_recv_peers() {
        auto ans = send_request("started");
        std::istringstream ыы(ans.data);
        BencodeVal resp_decoded = read_bencode(ыы);
        interval = resp_decoded.get_dict().at("interval").get_int();
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

    [[deprecated]] void construct_threads_for_new_peers() {
        for (auto peer : peer_list) {
            if (connected_peers.count(peer)) {
                continue;
            }
            try {
                auto pc = std::make_unique<PeerConnection>(peer, ts);
                if (!pc->do_handshake())
                    continue;

                connected_peers.emplace(peer);
                connects.push_back(std::move(pc));

                PeerConnection *raw = connects.back().get();
                io_threads.emplace_back([raw]() {
                    try {
                        raw->run();
                    } catch (const std::exception &e) {
                        spdlog::warn("[{}] run() error: {}", raw->peer.str(), e.what());
                    }
                });
            } catch (std::exception &e) {
                spdlog::warn("Cant connect to peer {}", peer.str());
            }
        }
    }
    void join_threads() {
        for (auto &th : io_threads) {
            if (th.joinable())
                th.join();
        }
    }

   void spawn_new_peers() {
    for (auto peer : peer_list) {
        if (connected_peers.count(peer))
            continue;

        connected_peers.emplace(peer);

        asio::co_spawn(
            ctx,
            [this, peer]() -> awaitable<void> {
                try {
                    auto pc = co_await PeerConn2::create(ctx, peer, ts);
                    co_await pc->run();
                } catch (const asio::system_error &e) {
                    spdlog::warn("[{}] asio error: {} ({})", 
                        peer.str(), e.what(), e.code().message());
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
};

#endif
