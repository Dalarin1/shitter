#ifndef INCLUDE_SHA1_HPP_
#define INCLUDE_SHA1_HPP_

#include <windows.h>
#include <iomanip>
#include <sstream>
#include <vector>
#include <wincrypt.h>
#include <cstdint>

std::string sha1(const std::string &input);
std::string sha1_hex(const std::string & input);
std::string sha1(const std::vector<uint8_t>& input);

#endif // INCLUDE_SHA1_HPP_