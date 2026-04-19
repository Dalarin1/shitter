#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "boss.hpp"
#include "sender.hpp"
#include "session.hpp"

int main() {
    SetConsoleOutputCP(CP_UTF8);
    spdlog::set_level(spdlog::level::debug);
    // Torrent torrent(L"test/Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
    //                 L"[rutracker-605222].torrent");

    // print_torrent(torrent);
    // TorrentState torrent_state = TorrentState(torrent);

    // asio::io_context ioc;
    // Downloader bb(torrent_state, ioc);

    // // bb.run();

    // SeedServer uploader(ioc, 6888);
    // asio::co_spawn(
    //     ioc, [&]() -> awaitable<void> { co_await uploader.run(); }, asio::detached);
    // ioc.run();

    App app;
    app.add_torrent(
        fs::path(L"test/Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
                 L"[rutracker-605222].torrent"));
}