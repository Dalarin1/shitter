#include <openssl\sha.h>

template <typename DS> std::string sha1(const DS &data) {
    ;
    std::string res(20, '\0');
    SHA1(reinterpret_cast<const unsigned char *>(data.data()), data.size(),
         reinterpret_cast<unsigned char *>(res.data()));
    return res;
}
template <typename DS> std::string sha1_hex(const DS &data) {
    std::string sha_raw = sha1(data);

    std::ostringstream ss;
    for (unsigned char c : sha_raw)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return ss.str();
}
