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

#define SFILE_READ 0x1
#define SFILE_WRITE 0x10

// Uncomment to use SFile instead of std::fstream
#define USING_SFILE

#ifdef _WIN32
struct SFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::filesystem::path path;

    SFile() = default;
    SFile(const std::filesystem::path &filepath, unsigned int flags) {
        open(filepath, flags);
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
        if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &bytesRead, &ov)) {
            spdlog::error("ReadFile returned false");
            return false;
        }
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
        // spdlog::info("SFile &operator=(SFile &&other)");
        if (this != &other) {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            handle = other.handle;
            other.handle = INVALID_HANDLE_VALUE;
            path = std::move(other.path);
        }
        return *this;
    }
    SFile(SFile &&other) noexcept {
        // spdlog::info("SFile(SFile &&other)");
        handle = other.handle;
        other.handle = INVALID_HANDLE_VALUE;
        path = std::move(other.path); 
    }

    inline void close() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    bool open(const std::filesystem::path& filepath, unsigned int flags) {
        close();
        path = filepath;

        DWORD mode = 0;
        if (flags & SFILE_READ) {
            mode |= GENERIC_READ;
        }
        if (flags & SFILE_WRITE) {
            mode |= GENERIC_WRITE;
        }

        handle = CreateFileW(filepath.wstring().c_str(), mode, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            spdlog::error("Cannot open file {}, reason: {}", filepath.string(), err);
            if(err == 3){
                spdlog::error("Tried to open file at: {}", filepath.string());
            }
            return false;
        }
        return true;
    }
    inline void change_mode(unsigned int flags) { open(path, flags); }
    inline bool is_open() { return handle != INVALID_HANDLE_VALUE; }
};

#else // POSIX

struct SFile {
    int fd = -1;
    std::filesystem::path path;

    SFile() = default;
    SFile(const std::filesystem::path &path, unsigned int flags) {
        this->open(path, flags);
    }
    ~SFile() {
        if (fd != -1)
            ::close(fd);
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
        path = std::move(other.path);
    }
    SFile &operator=(SFile &&other) noexcept {
        if (this != &other) {
            if (fd != -1)
                ::close(fd);
            fd = other.fd;
            other.fd = -1;
            path = std::move(other.path); 
        }
        return *this;
    }
    inline void close() {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }

    bool open(const std::filesystem::path& filepath, unsigned int flags) {
        this->close();
        path = filepath;
        unsigned int mode = 0;

        if (flags & SFILE_READ) {
            if (flags & SFILE_WRITE) {
                mode = O_RDWR;
            } else {
                mode = O_RDONLY;
            }
        } else {
            if (flags & SFILE_WRITE) {
                mode = O_WRONLY;
            }
        }
        mode |= O_CREAT;

        fd = ::open(path.string().c_str(), mode, 0664);
        if (fd == -1){
            spdlog::error("Cannot open file {}", path.string());
            return false;
        }
        return true;
    }

    inline void change_mode(unsigned int flags){
        open(path, flags);
    }

    bool is_open() { return fd != -1; }
};

#endif // _WIN32
#endif // INCLUDE_FILE_HPP