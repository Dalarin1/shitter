#include "bencode.cpp"

void print_bencodeval(const BencodeVal &bv, bool _endl = true) {
    if (bv.is_int()) {
        std::cout << bv.get_int();
    }
    if (bv.is_str()) {
        std::cout << "\"" << bv.get_str() << "\"";
    }
    if (bv.is_list()) {
        std::cout << "[ ";
        std::vector list = bv.get_list();
        for (const auto &i : list) {
            print_bencodeval(i, false);
        }
        std::cout << " ]";
    }
    if (bv.is_dict()) {
        std::cout << "{ ";
        for (const auto &[k, v] : bv.get_dict()) {
            std::cout << "\"" << k << "\" : ";
            print_bencodeval(v, false);
        }
        std::cout << " }";
    }
    if (_endl) {
        std::cout << std::endl;
    } else {
        std::cout << ", ";
    }
}


int main(int argc, char **argv) {

    if (argc < 2) {
        std::cerr << "Specify filename\n";
        return 1;
    }
    auto filename = std::string(argv[1]);
    auto torrent_file = std::ifstream(filename, std::ios::binary);

    auto file = read_bencode(torrent_file);
    print_bencodeval(file);
    return 0;
}