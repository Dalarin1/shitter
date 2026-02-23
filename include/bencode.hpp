#ifndef _BENCODE_HPP
#define _BENCODE_HPP

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct BencodeVal;
#define Int int64_t
#define Str std::string
#define List std::vector<BencodeVal>
#define Dict std::unordered_map<std::string, BencodeVal>

struct BencodeVal {
    std::variant<Int, Str, List, Dict> data;

    BencodeVal() = default;
    explicit BencodeVal(Int i) : data(i) {};
    explicit BencodeVal(Str i) : data(std::move(i)) {};
    explicit BencodeVal(List i) : data(std::move(i)) {};
    explicit BencodeVal(Dict i) : data(std::move(i)) {};

    bool is_int() const { return std::holds_alternative<Int>(data); }
    bool is_str() const { return std::holds_alternative<Str>(data); }
    bool is_list() const { return std::holds_alternative<List>(data); }
    bool is_dict() const { return std::holds_alternative<Dict>(data); }

    const Int &get_int() const { return std::get<Int>(data); }
    const Str &get_str() const { return std::get<Str>(data); }
    const List &get_list() const { return std::get<List>(data); }
    const Dict &get_dict() const { return std::get<Dict>(data); }

    const Int *get_if_int() const { return std::get_if<Int>(&data); }
    const Str *get_if_str() const { return std::get_if<Str>(&data); }
    const List *get_if_list() const { return std::get_if<List>(&data); }
    const Dict *get_if_dict() const { return std::get_if<Dict>(&data); }
};

BencodeVal _read_int(std::ifstream&);
BencodeVal _read_str(std::ifstream&);
BencodeVal _read_list(std::ifstream&);
BencodeVal _read_dict(std::ifstream&);

BencodeVal read_bencode(std::ifstream &);
#endif // _BENCODE_HPP