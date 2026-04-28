#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "boss.hpp"
#include "sender.hpp"
#include "session.hpp"

int main() {
    // spdlog::set_level(spdlog::level::debug);
    
    App app;    
    app.add_torrent(fs::current_path() / "test" / fs::path(L"Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] [rutracker-605222].torrent"));
}
