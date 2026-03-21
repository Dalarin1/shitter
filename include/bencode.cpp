#include "bencode.hpp"

BencodeVal _read_int(std::istream &file) {
    std::string buf;
    buf.reserve(21); 
    char c;
    bool neg = false;

    file.read(&c, 1); 
    while (file.read(&c, 1)) {
        if (c == 'e')
            break;
        if (c == '-') {
            neg = true;
            continue;
        }
        buf.push_back(c);
    }

    BEN_Int val = std::stoll(buf);
    if (neg)
        val = -val;
    return BencodeVal{val};
}

BencodeVal _read_str(std::istream &file) {
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

BencodeVal _read_list(std::istream &file) {
    char c;
    file.read(&c, 1); // пропустить 'l'

    BEN_List res;
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

BencodeVal _read_dict(std::istream &file) {
    char c;
    file.read(&c, 1); // пропустить 'd'

    BEN_Dict res;
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

BencodeVal read_bencode(std::istream &file) {
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

std::string encode_bencode(const BencodeVal& val){
    std::string out;

    if (val.is_int()) {
        out += 'i';
        out += std::to_string(val.get_int());
        out += 'e';
    }
    else if (val.is_str()) {
        const std::string &s = val.get_str();
        out += std::to_string(s.size());
        out += ':';
        out += s;
    }
    else if (val.is_list()) {
        out += 'l';
        for (const auto &item : val.get_list())
            out += encode_bencode(item);
        out += 'e';
    }
    else if (val.is_dict()) {
        out += 'd';
        for (const auto &[key, value] : val.get_dict()) {
            out += std::to_string(key.size());
            out += ':';
            out += key;
            out += encode_bencode(value);
        }
        out += 'e';
    }

    return out;
}

void print_bencodeval(const BencodeVal &bv, std::ostream& out=std::cout, bool _endl = true) {
    if (bv.is_int()) {
        out << bv.get_int();
    }
    if (bv.is_str()) {
        out << "\"" << bv.get_str() << "\"";
    }
    if (bv.is_list()) {
        out << "[ ";
        std::vector list = bv.get_list();
        for (const auto &i : list) {
            print_bencodeval(i, out, false);
        }
        out << " ]";
    }
    if (bv.is_dict()) {
        out << "{ ";
        for (const auto &[k, v] : bv.get_dict()) {
            out << "\"" << k << "\" : ";
            print_bencodeval(v, out, false);
        }
        out << " }";
    }
    if (_endl) {
        out << std::endl;
    } else {
       out << ", ";
    }
}

