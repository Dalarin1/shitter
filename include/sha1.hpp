#ifndef INCLUDE_SHA1_HPP_
#define INCLUDE_SHA1_HPP_

#include <windows.h>
#include <iomanip>
#include <sstream>
#include <wincrypt.h>

std::string sha1(const std::string &input){
    HCRYPTPROV hProv;
    HCRYPTHASH hHash;
    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash);
    CryptHashData(hHash, (BYTE*)input.data(), input.size(), 0);
    
    std::string result(20, '\0');
    DWORD len = 20;
    CryptGetHashParam(hHash, HP_HASHVAL, (BYTE*)result.data(), &len, 0);
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    
    std::ostringstream ss;
    for (unsigned char c : result)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return ss.str();
} 

#endif // INCLUDE_SHA1_HPP_