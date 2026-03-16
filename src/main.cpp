#include "bencode.hpp"
#include "protocol.hpp"
#include "torrent.hpp"
#include "torrent_session.hpp"

int main() {
    // info_hash для фотошопа(правильный) : 669fe3ad321a4d64a57bd7b03dbc1b2a17de86f4
    // вычисленный мной: 53f1488e234bda7e9297c1fc76658466f1211e43
    auto torrent_file = std::ifstream("test/Adobe Photoshop 2021 22.4.1 [2021,Multi Ru] "
                                      "RePack m0nkrus [rutracker-5970995].torrent");
    BencodeVal parsed_torrent_file = read_bencode(torrent_file);

    Torrent torrent("test/Adobe Photoshop 2021 22.4.1 [2021,Multi Ru] "
                    "RePack m0nkrus [rutracker-5970995].torrent");
    print_torrent(torrent);

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
    for (int i = 0; i < peers.size(); i++) {
        std::cout << peers[i].ip_str() << '\n';
    }
    std::cout << std::endl;
    return 0;
}