#pragma once

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <fcntl.h>
#include <signal.h>

namespace fs = std::filesystem;

struct Config {
    std::string ip;
    int port = 0;
    int server_threads = 0;
    int client_threads = 0;
};
static inline uint64_t mono_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
    );
}

static inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();

    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }

    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

static inline bool valid_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }

    if (name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return false;
    }

    if (name.find("..") != std::string::npos) {
        return false;
    }

    return true;
}
static inline uint64_t file_size_bytes(const fs::path& p) {
    return static_cast<uint64_t>(fs::file_size(p));
}

static inline bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;

    while (sent < n) {
        ssize_t r = ::send(fd,data + sent,n - sent,MSG_NOSIGNAL);

        if (r > 0) {
            sent += static_cast<size_t>(r);
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static inline bool send_all(int fd, const std::string& s) {
    return send_all(fd, s.data(), s.size());
}
static inline bool recv_exact(int fd, char* data, size_t n) {
    size_t got = 0;

    while (got < n) {
        ssize_t r = ::recv(fd,data + got,n - got,0);

        if (r > 0) {
            got += static_cast<size_t>(r);
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static inline bool recv_line(
    int fd,
    std::string& out,
    int timeout_ms = 3000) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &tv,
        sizeof(tv)
    );

    out.clear();
    char c;

    while (true) {
        ssize_t r = ::recv(fd, &c, 1, 0);

        if (r == 1) {
            if (c == '\n') {
                return true;
            }

            out.push_back(c);

            if (out.size() > 4096) {
                return false;
            }
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
}

static inline void clear_recv_timeout(int fd) {
    timeval tv{};

    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
}