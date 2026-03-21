#ifndef INCLUDE_SHA1_HPP_
#define INCLUDE_SHA1_HPP_

#include <windows.h>
#include <iomanip>
#include <sstream>
#include <wincrypt.h>

std::string sha1(const std::string &input);

#endif // INCLUDE_SHA1_HPP_