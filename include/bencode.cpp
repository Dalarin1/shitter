#include "bencode.hpp"
#include <iostream>

BencodeVal _read_int(std::ifstream &file) {
    std::string buf;
    buf.reserve(21); // максимум 21 символ (включая '-')
    char c;
    bool neg = false;

    file.read(&c, 1); // пропустить 'i'
    while (file.read(&c, 1)) {
        if (c == 'e')
            break;
        if (c == '-') {
            neg = true;
            continue;
        }
        buf.push_back(c);
    }

    Int val = std::stoll(buf);
    if (neg)
        val = -val;
    return BencodeVal{val};
}

BencodeVal _read_str(std::ifstream &file) {
    std::string len_buf;
    char c;

    while (file.read(&c, 1)) {
        if (c == ':')
            break;
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            std::cerr << "Error: non-digit '" << c << "' before ':' in _read_str()\n";
            throw std::runtime_error("Invalid bencode string length");
        }
        len_buf.push_back(c);
    }

    if (len_buf.empty())
        throw std::runtime_error("Empty length in _read_str()");

    size_t len = std::stoul(len_buf);
    std::string res(len, '\0');
    file.read(res.data(), len);
    return BencodeVal{res};
}

BencodeVal _read_list(std::ifstream &file) {
    char c;
    file.read(&c, 1); // пропустить 'l'

    List res;
    while (true) {
        int peekc = file.peek();
        if (peekc == EOF)
            break;
        c = static_cast<char>(peekc);
        if (c == 'e') {
            file.read(&c, 1);
            break;
        }

        if (c == 'i')
            res.push_back(_read_int(file));
        else if ('0' <= c && c <= '9')
            res.push_back(_read_str(file));
        else if (c == 'l')
            res.push_back(_read_list(file));
        else if (c == 'd')
            res.push_back(_read_dict(file));
        else {
            file.read(&c, 1);
        }
    }
    return BencodeVal{std::move(res)};
}

BencodeVal _read_dict(std::ifstream &file) {
    char c;
    file.read(&c, 1); // пропустить 'd'

    Dict res;
    while (true) {
        int peekc = file.peek();
        if (peekc == EOF)
            break;
        c = static_cast<char>(peekc);
        if (c == 'e') {
            file.read(&c, 1);
            break;
        }

        std::string key = _read_str(file).get_str();

        peekc = file.peek();
        if (peekc == EOF)
            break;
        c = static_cast<char>(peekc);

        if (c == 'i')
            res.emplace(std::move(key), _read_int(file));
        else if ('0' <= c && c <= '9')
            res.emplace(std::move(key), _read_str(file));
        else if (c == 'l')
            res.emplace(std::move(key), _read_list(file));
        else if (c == 'd')
            res.emplace(std::move(key), _read_dict(file));
        else {
            file.read(&c, 1);
        }
    }
    return BencodeVal{std::move(res)};
}

BencodeVal read_bencode(std::ifstream &file) {
    BencodeVal res;
    int tmp;
    char c;
    tmp = file.peek();
    if (tmp == EOF)
        return res;
    c = static_cast<char>(tmp);
    if (c == 'i')
        return _read_int(file);
    if (c == 'l') {
        return _read_list(file);
    }
    if (c == 'd') {
        return _read_dict(file);
    }
    if (('0' <= c && c <= '9'))
        return _read_str(file);
    return res;
}
