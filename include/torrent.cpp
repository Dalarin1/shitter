#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include "bencode.hpp"
#include "torrent.hpp"
#include "sha1.hpp"

std::string find_info_hash(std::istream &file) {
    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

    size_t pos = raw.find("4:info");
    pos += 6; // начало info-значения

    std::istringstream ss(raw.substr(pos));
    std::streampos start = ss.tellg();
    BencodeVal info_val = read_bencode(ss);
    std::streampos end = ss.tellg();
    std::string info_raw = raw.substr(pos, (size_t)end - (size_t)start);
    return sha1(info_raw);
}

Torrent::Torrent(std::string filename) {
    auto file = std::ifstream(filename, std::ios::binary);
    BencodeVal data = read_bencode(file);
    file.close();

    if (!data.is_dict()) {
        throw std::runtime_error(
            "Invalid torrent file: top-level element is not a dictionary");
    }

    BEN_Dict dict = data.get_dict();
    BEN_Dict info_dict = dict.at("info").get_dict();
    announce_url = dict.at("announce").get_str();
    
    auto f = std::ifstream(filename, std::ios::binary);
    info_hash = (find_info_hash(f));

    for(int i = 0; i < 40; i += 2){
        info_hash_raw[i / 2] = static_cast<uint8_t>(std::stoi(info_hash.substr(i, 2), 0, 16));
    }

    std::string temp;
    for (size_t i = 0; i < 40; i += 2) {
        unsigned char byte = std::stoi(info_hash.substr(i, 2), 0, 16);
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", byte);
        urlencoded_info_hash += buf;
    }

    info.piece_length = info_dict.at("piece length").get_int();
    info.name = info_dict.at("name").get_str();

    if (info_dict.count("length") > 0) {
        info.length = info_dict.at("length").get_int();
    } else {
        info.length = -1;
    }

    if (info_dict.count("files") > 0) {
        for (const auto &file : info_dict.at("files").get_list()) {
            auto f = Torrent::file();
            f.length = file.get_dict().at("length").get_int();
            for (const auto &path_part : file.get_dict().at("path").get_list()) {
                f.path.push_back(path_part.get_str());
            }
            info.files.push_back(f);
        }
    }

    std::string pieces_str = dict["info"].get_dict().at("pieces").get_str();
    for (size_t i = 0; i < pieces_str.size(); i += 20) {
        info.pieces.push_back(pieces_str.substr(i, 20));
    }
}

void print_torrent(const Torrent &t) {
    auto print_safe = [](const std::string &s) {
        for (unsigned char c : s) {
            if (std::isprint(c))
                std::cout << c;
            else
                std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                          << (int)c << std::dec;
        }
    };

    std::cout << "Torrent:\n";

    std::cout << "  announce_url: " << t.announce_url << "\n";

    std::cout << "  info_hash: ";
    print_safe(t.info_hash);
    std::cout << "\n";

    std::cout << "  info:\n";
    std::cout << "    name: " << t.info.name << "\n";
    std::cout << "    piece_length: " << t.info.piece_length << "\n";
    std::cout << "    pieces: " << t.info.pieces.size() << " pieces\n";

    if (t.info.files.empty()) {
        std::cout << "    length: " << t.info.length << "\n";
    } else {
        std::cout << "    files:\n";
        for (const auto &f : t.info.files) {
            std::cout << "      length: " << f.length << "\n";
            std::cout << "      path: ";
            for (const auto &part : f.path)
                std::cout << part << "/";
            std::cout << "\n";
        }
    }
}