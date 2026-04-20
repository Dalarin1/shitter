#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "boss.hpp"
#include "sender.hpp"
#include "session.hpp"

int main() {
    spdlog::set_level(spdlog::level::debug);
    
    App app;
    app.add_torrent(
        fs::path(L"test/Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
                 L"[rutracker-605222].torrent"));
}