#ifndef INCLUDE_BENCODE_HPP_
#define INCLUDE_BENCODE_HPP_

#include <cstdint>
#include <fstream>
#include <string>
#include <map>
#include <variant>
#include <vector>
#include <iostream>

struct BencodeVal;
#define BEN_Int int64_t
#define BEN_Str std::string
#define BEN_List std::vector<BencodeVal>
#define BEN_Dict std::map<std::string, BencodeVal>

struct BencodeVal {
    std::variant<BEN_Int, BEN_Str, BEN_List, BEN_Dict> data;

    BencodeVal() = default;
    explicit BencodeVal(BEN_Int i) : data(i) {};
    explicit BencodeVal(BEN_Str i) : data(std::move(i)) {};
    explicit BencodeVal(BEN_List i) : data(std::move(i)) {};
    explicit BencodeVal(BEN_Dict i) : data(std::move(i)) {};

    bool is_int() const { return std::holds_alternative<BEN_Int>(data); }
    bool is_str() const { return std::holds_alternative<BEN_Str>(data); }
    bool is_list() const { return std::holds_alternative<BEN_List>(data); }
    bool is_dict() const { return std::holds_alternative<BEN_Dict>(data); }

    const BEN_Int &get_int() const { return std::get<BEN_Int>(data); }
    const BEN_Str &get_str() const { return std::get<BEN_Str>(data); }
    const BEN_List &get_list() const { return std::get<BEN_List>(data); }
    const BEN_Dict &get_dict() const { return std::get<BEN_Dict>(data); }

    const BEN_Int *get_if_int() const { return std::get_if<BEN_Int>(&data); }
    const BEN_Str *get_if_str() const { return std::get_if<BEN_Str>(&data); }
    const BEN_List *get_if_list() const { return std::get_if<BEN_List>(&data); }
    const BEN_Dict *get_if_dict() const { return std::get_if<BEN_Dict>(&data); }
};

BencodeVal _read_int(std::istream&);
BencodeVal _read_str(std::istream&);
BencodeVal _read_list(std::istream&);
BencodeVal _read_dict(std::istream&);

BencodeVal read_bencode(std::istream &);

std::string encode_bencode(const BencodeVal &val); 

void print_bencodeval(const BencodeVal &bv, std::ostream& out=std::cout, bool _endl = true);

#endif // INCLUDE_BENCODE_HPP_