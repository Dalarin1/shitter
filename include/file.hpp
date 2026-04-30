#ifndef INCLUDE_FILE_HPP_
#define INCLUDE_FILE_HPP_

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <filesystem>
// clang-format off

#ifdef _WIN32
    #include <windows.h>
    #include <fileapi.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/stat.h> 
#endif

// clang-format on
#include "spdlog/spdlog.h"

// Uncomment to use SFile instead of std::fstream
#define USING_SFILE

#ifdef _WIN32
struct SFile {
    HANDLE handle = INVALID_HANDLE_VALUE;

    SFile() = default;
    SFile(const std::filesystem::path &filepath) {
        handle = CreateFileW(filepath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            spdlog::error("Cannot open file {}", filepath.string());
    }
    ~SFile() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    bool read(char *buffer, uint64_t size, uint64_t offset) {
        OVERLAPPED ov = {};
        ov.Offset = static_cast<DWORD>(offset);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD bytesRead = 0;
        if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &bytesRead, &ov))
            return false;
        return bytesRead == static_cast<DWORD>(size);
    }
    bool write(const char *buffer, uint64_t size, uint64_t offset) {
        OVERLAPPED ov = {};
        ov.Offset = static_cast<DWORD>(offset);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD bytesWritten = 0;
        return WriteFile(handle, buffer, static_cast<DWORD>(size), &bytesWritten, &ov);
    }
    SFile(const SFile &) = delete;
    SFile &operator=(const SFile &) = delete;
    SFile &operator=(SFile &&other) noexcept {
        if (this != &other) {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            handle = other.handle;
            other.handle = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    SFile(SFile &&other) noexcept {
        handle = other.handle;
        other.handle = INVALID_HANDLE_VALUE;
    }
};

#else // POSIX

struct SFile {
    int fd = -1;

    SFile() = default;
    SFile(const std::filesystem::path &path) {
        fd = open(path.string().c_str(), O_RDWR | O_CREAT, 0664);
        if (fd == -1)
            spdlog::error("Cannot open file {}", path.string());
    }
    ~SFile() {
        if (fd != -1)
            close(fd);
    }

    bool read(char *buffer, uint64_t size, uint64_t offset) {
        return pread(fd, buffer, size, static_cast<off_t>(offset)) ==
               static_cast<ssize_t>(size);
    }
    bool write(const char *buffer, uint64_t size, uint64_t offset) {
        return pwrite(fd, buffer, size, static_cast<off_t>(offset)) ==
               static_cast<ssize_t>(size);
    }
    SFile(const SFile &) = delete;
    SFile &operator=(const SFile &) = delete;
    SFile(SFile &&other) noexcept {
        fd = other.fd;
        other.fd = -1;
    }
    SFile &operator=(SFile &&other) noexcept {
        if (this != &other) {
            if (fd != -1)
                close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
};

#endif // _WIN32
#endif // INCLUDE_FILE_HPP