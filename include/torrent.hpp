#ifndef INCLUDE_TORRENT_HPP_
#define INCLUDE_TORRENT_HPP_

#include "bencode.hpp"

class Torrent {

  public:
    struct file {
        uint64_t length;
        std::vector<std::string> path;
    };
    std::string urlencoded_info_hash;
    std::string info_hash; // 20 bytes (SHA1 hash of _info_ value)
    std::string announce_url;

    struct Info {
        // Name to save the file/directory as.
        std::string name;
        // Number of bytes in each piece;
        uint64_t piece_length;
        // Each pieces[i] represents SHA1 of _i_th piece
        std::vector<std::string> pieces;
        // Represetns the length of single file in bytes; If multiple files present, set
        // to -1
        uint64_t length;
        // If only single file present, files.size = 0
        std::vector<file> files;
    } info;

    Torrent(std::string filename);
    ~Torrent() = default;
};

void print_torrent(const Torrent &torrent);

#endif // INCLUDE_TORRENT_HPP_