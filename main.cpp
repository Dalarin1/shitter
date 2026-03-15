#include "bencode.hpp"
#include "protocol.hpp"

int main() {

    auto torrent_file = std::ifstream("test/Adobe Photoshop 2021 22.4.1 [2021,Multi Ru] "
                                      "RePack m0nkrus [rutracker-5970995].torrent");
    BencodeVal parsed_torrent_file = read_bencode(torrent_file);

    std::string tracker_url = parsed_torrent_file.get_dict().at("announce").get_str();
    std::wstring tracker_url_wstring =
        std::wstring(tracker_url.begin(), tracker_url.end());

    URL tracker;
    tracker.host = get_url_hostname(tracker_url);
    tracker.path = get_url_path(tracker_url);

    std::cout << "tracker_url: " << tracker_url << std::endl;
    std::cout << "URL:\n"
              << '\t' << tracker.protocol << "\n\t" << tracker.host << "\n\t"
              << tracker.path << std::endl;

    auto tracker_conn = HttpConn(std::wstring(tracker.host.begin(), tracker.host.end()));

    // 1. Вычисли SHA1 от bencode-сериализованного info-словаря
    std::string info_hash = "00000000000000000000"; // 20 байт сырых байт
    std::string info_hash_encoded =
        urlencode(info_hash); 

    
    std::string path = tracker.path + "?info_hash=" + info_hash_encoded +
                       "&peer_id=-qB4600-123456789012" 
                       + "&port=6888" + "&uploaded=0" + "&downloaded=0" +
                       "&left=1000000" + "&event=started" + "&compact=1";
    std::cout << "Path: " << path << std::endl;
    tracker_conn.get(std::wstring(path.begin(), path.end()));

    return 0;
}