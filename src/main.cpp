#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "boss.hpp"

int main(){
    spdlog::set_level(spdlog::level::debug);
    Torrent torrent(L"test/Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
                    L"[rutracker-605222].torrent");

    print_torrent(torrent);
    TorrentState torrent_state = TorrentState(torrent);

    asio::io_context ioc;
    BigBoss bb(torrent_state, ioc);
    bb.run();
}