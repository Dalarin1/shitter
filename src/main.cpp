#include "torrent.hpp"
#include "spdlog/spdlog.h"

void download_from_peers(const std::vector<Peer> &peers, TorrentState &ts,
                         uint16_t conn_count = 10) {
    std::vector<std::thread> threads;

    for (uint16_t i = 0; i < conn_count && i < peers.size(); i++) {
        try {
            PeerConnection pc(peers[i], ts);
            if (!pc.do_handshake()) {
                spdlog::warn("[{}] Handshake failed, skipping", peers[i].str());
                continue;
            }
            spdlog::info("[{}] Connected", peers[i].str());
            threads.emplace_back([pc = std::move(pc)]() mutable {
                try {
                    pc.run();
                } catch (const std::exception &e) {
                    spdlog::warn("[{}] run() error: {}", pc.peer.str(), e.what());
                }
            });
        } catch (std::exception &e) {
            spdlog::warn("[{}] Connection failed: {}", peers[i].str(), e.what());
        }
    }

    for (auto &t : threads)
        t.join();
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    Torrent torrent(L"test/Высокая кухня - Похлебкин В. В. - ЧАЙ [2007, PDF, RUS] "
                    L"[rutracker-605222].torrent");

    print_torrent(torrent);
    TorrentState torrent_state = TorrentState(torrent);

    if (!torrent_state.preallocate_files(fs::current_path())) {
        return 1;
    }

    std::wstring tracker_url_wstring =
        std::wstring(torrent.announce_url.begin(), torrent.announce_url.end());

    URL tracker;
    tracker.host = get_url_hostname(torrent.announce_url);
    tracker.path = get_url_path(torrent.announce_url);

    std::cout << "tracker_url: " << torrent.announce_url << std::endl;
    std::cout << "URL:\n"
              << '\t' << tracker.protocol << "\n\t" << tracker.host << "\n\t"
              << tracker.path << std::endl;

    auto tracker_conn = HttpConn(std::wstring(tracker.host.begin(), tracker.host.end()));

    std::string path = tracker.path + "?info_hash=" + torrent.urlencoded_info_hash +
                       "&peer_id=-qB4600-123456789012" + "&port=6888" + "&uploaded=0" +
                       "&downloaded=0" + "&left=1000000" + "&event=started" +
                       "&compact=1";
    std::cout << "Path: " << path << std::endl;
    HttpConn::response resp = tracker_conn.get(std::wstring(path.begin(), path.end()));
    if (!resp.success) {
        std::cerr << "Error: cant GET " << path << std::endl;
        return 1;
    }
    BencodeVal resp_decoded = read_bencode(resp.data);
    std::vector<Peer> peers = parse_peers(resp_decoded.get_dict().at("peers").get_str());

    // std::vector<PeerConnection> peepers = connect_to_peers(peers, torrent_state, 10);
    // if (peepers.empty()) {
    //     spdlog::error("No peers connected");
    //     return 1;
    // }
    // try {
    //     peepers[0].run();
    // } catch (const std::exception &e) {
    //     spdlog::error("run() error: {}", e.what());
    // }
    download_from_peers(peers, torrent_state, 10);
    return 0;
}