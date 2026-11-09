#include "torrent.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "boss.hpp"
#include "sender.hpp"
#include "session.hpp"

int main(int argc, char **argv) {
    
    // auto file_logger = spdlog::basic_logger_mt("basic_logger", "logs/basic.txt");
    // spdlog::set_default_logger(file_logger);

    App app;
    

    fs::path torrent = fs::current_path() / "test" /
                       fs::path("Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
                                "[rutracker-605222].torrent");
    fs::path dest_folder = fs::current_path();
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
        if(arg == "-f" || arg == "--folder"){
            if (i + 1 >= argc) {
                throw std::runtime_error("GDE DESTINATION FOLDER SUKA");
            }
            dest_folder = fs::path(argv[i+1]);
        }
    }

    app.add_torrent(torrent, dest_folder);
}