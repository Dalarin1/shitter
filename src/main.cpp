#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "boss.hpp"
#include "sender.hpp"
#include "session.hpp"

int main(int argc, char** argv) {
    fs::path torrent = fs::current_path() / fs::path("(JPop, JRock) HIGH and MIGHTY COLOR - Re ache (Single) - 2010, MP3 (tracks), 192 kbps [rutracker-3102615].torrent");


    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "-d") {
            spdlog::set_level(spdlog::level::debug);
        }
        if (arg == "-t" || arg == "--torrent") {
            if (i + 1 >= argc) {
                throw std::runtime_error("GDE TORRENT SUKA");
            }
            torrent = fs::path(argv[i + 1]);
        }
    }

    App app;
    app.add_torrent(torrent);
}