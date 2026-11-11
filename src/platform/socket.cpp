#include "socket.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace colt::detail::platform {
namespace {

constexpr auto invalid_handle = static_cast<std::uintptr_t>(-1);

#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;

const std::error_category& addrinfo_category() noexcept {
    class Category final : public std::error_category {
    public:
        const char* name() const noexcept override { return "getaddrinfo"; }
        std::string message(int error) const override { return gai_strerrorA(error); }
    };
    static const Category category;
    return category;
}

std::error_code last_socket_error() {
    return {WSAGetLastError(), std::system_category()};
}

NativeSocket native(std::uintptr_t handle) {
    return static_cast<NativeSocket>(handle);
}

[[noreturn]] void throw_socket_error(const char* operation, int error = WSAGetLastError()) {
    throw std::system_error(std::error_code(error, std::system_category()), operation);
}

void close_native(NativeSocket socket) noexcept {
    closesocket(socket);
}

class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        const int error = WSAStartup(MAKEWORD(2, 2), &data);
        if (error != 0) throw_socket_error("WSAStartup", error);
    }

    ~WinsockRuntime() { WSACleanup(); }
};

void configure_socket(NativeSocket socket) {
    (void)socket;
}
#else
using NativeSocket = int;
using SocketLength = socklen_t;

const std::error_category& addrinfo_category() noexcept {
    class Category final : public std::error_category {
    public:
        const char* name() const noexcept override { return "getaddrinfo"; }
        std::string message(int error) const override { return gai_strerror(error); }
    };
    static const Category category;
    return category;
}

std::error_code last_socket_error() {
    return {errno, std::generic_category()};
}

NativeSocket native(std::uintptr_t handle) {
    return static_cast<NativeSocket>(handle);
}

[[noreturn]] void throw_socket_error(const char* operation, int error = errno) {
    throw std::system_error(std::error_code(error, std::generic_category()), operation);
}

void close_native(NativeSocket socket) noexcept {
    (void)::close(socket);
}

void configure_socket(NativeSocket socket) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        throw_socket_error("setsockopt(SO_NOSIGPIPE)");
    }
#else
    (void)socket;
#endif
}
#endif

void ensure_valid(const Socket& socket) {
    if (!socket) throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
}

struct AddressList {
    addrinfo* value{};
    AddressList() = default;
    AddressList(const AddressList&) = delete;
    AddressList& operator=(const AddressList&) = delete;
    AddressList(AddressList&& other) noexcept : value(other.value) { other.value = nullptr; }
    AddressList& operator=(AddressList&& other) noexcept {
        if (this != &other) {
            if (value != nullptr) freeaddrinfo(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~AddressList() {
        if (value != nullptr) freeaddrinfo(value);
    }
};

AddressList resolve(std::string_view address, std::uint16_t port, int flags) {
    const std::string host(address);
    const std::string service = std::to_string(port);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = flags;

    AddressList list;
    const int result = getaddrinfo(host.c_str(), service.c_str(), &hints, &list.value);
    if (result != 0) {
        throw std::system_error(std::error_code(result, addrinfo_category()), "getaddrinfo");
    }
    return list;
}

} // namespace

Socket::Socket(std::uintptr_t handle) noexcept : handle_(handle) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = invalid_handle;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = invalid_handle;
    }
    return *this;
}

void Socket::close() noexcept {
    if (handle_ == invalid_handle) return;
    close_native(static_cast<NativeSocket>(handle_));
    handle_ = invalid_handle;
}

Socket::operator bool() const noexcept { return handle_ != invalid_handle; }

void initialize_sockets() {
#ifdef _WIN32
    static WinsockRuntime runtime;
    (void)runtime;
#endif
}

Socket listen_tcp(std::string_view address, std::uint16_t port, int backlog) {
    initialize_sockets();
    if (backlog < 0) throw std::invalid_argument("backlog must not be negative");
    auto addresses = resolve(address, port, 0);
    std::error_code last_error;

    for (auto* entry = addresses.value; entry != nullptr; entry = entry->ai_next) {
        const auto handle = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
#ifdef _WIN32
        if (handle == INVALID_SOCKET) {
            last_error = last_socket_error();
            continue;
        }
#else
        if (handle < 0) {
            last_error = last_socket_error();
            continue;
        }
#endif
        try {
            configure_socket(handle);
            const int enabled = 1;
            if (::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
                             reinterpret_cast<const char*>(&enabled), sizeof(enabled)) < 0) {
                throw_socket_error("setsockopt(SO_REUSEADDR)");
            }
            if (::bind(handle, entry->ai_addr, static_cast<SocketLength>(entry->ai_addrlen)) < 0) {
                throw_socket_error("bind");
            }
            if (::listen(handle, backlog) < 0) throw_socket_error("listen");
            return Socket(static_cast<std::uintptr_t>(handle));
        } catch (const std::system_error& error) {
            last_error = error.code();
            close_native(handle);
        } catch (...) {
            close_native(handle);
            throw;
        }
    }
    if (last_error) throw std::system_error(last_error, "listen");
    throw std::system_error(std::make_error_code(std::errc::address_not_available), "listen");
}

Socket accept(Socket& listener) {
    ensure_valid(listener);
    for (;;) {
        const auto handle = ::accept(native(listener.handle_), nullptr, nullptr);
#ifdef _WIN32
        if (handle != INVALID_SOCKET) {
            try {
                configure_socket(handle);
            } catch (...) {
                close_native(handle);
                throw;
            }
            return Socket(static_cast<std::uintptr_t>(handle));
        }
        throw_socket_error("accept");
#else
        if (handle >= 0) {
            try {
                configure_socket(handle);
            } catch (...) {
                close_native(handle);
                throw;
            }
            return Socket(static_cast<std::uintptr_t>(handle));
        }
        if (errno != EINTR) throw_socket_error("accept");
#endif
    }
}

std::size_t receive(Socket& socket, std::span<char> buffer) {
    ensure_valid(socket);
    if (buffer.empty()) return 0;
    const auto size = std::min(buffer.size(), static_cast<std::size_t>(INT_MAX));
    for (;;) {
#ifdef _WIN32
        const int result = ::recv(native(socket.handle_), buffer.data(), static_cast<int>(size), 0);
        if (result != SOCKET_ERROR) return static_cast<std::size_t>(result);
        throw_socket_error("recv");
#else
        const auto result = ::recv(native(socket.handle_), buffer.data(), size, 0);
        if (result >= 0) return static_cast<std::size_t>(result);
        if (errno != EINTR) throw_socket_error("recv");
#endif
    }
}

void send_all(Socket& socket, std::span<const char> data) {
    ensure_valid(socket);
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto size = std::min(data.size() - sent, static_cast<std::size_t>(INT_MAX));
#ifdef _WIN32
        const int result = ::send(native(socket.handle_), data.data() + sent, static_cast<int>(size), 0);
        if (result == SOCKET_ERROR) throw_socket_error("send");
#else
#ifdef MSG_NOSIGNAL
        constexpr int send_flags = MSG_NOSIGNAL;
#else
        constexpr int send_flags = 0;
#endif
        const auto result = ::send(native(socket.handle_), data.data() + sent, size, send_flags);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw_socket_error("send");
        }
#endif
        if (result == 0) {
            throw std::system_error(std::make_error_code(std::errc::io_error), "send");
        }
        sent += static_cast<std::size_t>(result);
    }
}

void set_receive_timeout(Socket& socket, std::chrono::milliseconds timeout) {
    ensure_valid(socket);
    if (timeout.count() < 0) throw std::invalid_argument("timeout must not be negative");
#ifdef _WIN32
    const auto count = std::min<std::chrono::milliseconds::rep>(
        timeout.count(), std::numeric_limits<DWORD>::max());
    const DWORD value = static_cast<DWORD>(count);
    if (::setsockopt(native(socket.handle_), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&value), sizeof(value)) == SOCKET_ERROR) {
        throw_socket_error("setsockopt(SO_RCVTIMEO)");
    }
#else
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
    value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
    if (::setsockopt(native(socket.handle_), SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) < 0) {
        throw_socket_error("setsockopt(SO_RCVTIMEO)");
    }
#endif
}

void shutdown(Socket& socket) {
    ensure_valid(socket);
#ifdef _WIN32
    if (::shutdown(native(socket.handle_), SD_BOTH) == SOCKET_ERROR) throw_socket_error("shutdown");
#else
    if (::shutdown(native(socket.handle_), SHUT_RDWR) < 0) throw_socket_error("shutdown");
#endif
}

Socket connect_tcp(std::string_view address, std::uint16_t port) {
    initialize_sockets();
    auto addresses = resolve(address, port, 0);
    std::error_code last_error;

    for (auto* entry = addresses.value; entry != nullptr; entry = entry->ai_next) {
        const auto handle = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
#ifdef _WIN32
        if (handle == INVALID_SOCKET) {
            last_error = last_socket_error();
            continue;
        }
#else
        if (handle < 0) {
            last_error = last_socket_error();
            continue;
        }
#endif
        try {
            configure_socket(handle);
            if (::connect(handle, entry->ai_addr,
                          static_cast<SocketLength>(entry->ai_addrlen)) == 0) {
                return Socket(static_cast<std::uintptr_t>(handle));
            }
            last_error = last_socket_error();
        } catch (const std::system_error& error) {
            last_error = error.code();
            close_native(handle);
            continue;
        }
        close_native(handle);
    }
    if (last_error) throw std::system_error(last_error, "connect");
    throw std::system_error(std::make_error_code(std::errc::address_not_available), "connect");
}

std::uint16_t local_port(const Socket& socket) {
    ensure_valid(socket);
    sockaddr_storage address{};
    SocketLength length = static_cast<SocketLength>(sizeof(address));
    if (::getsockname(native(socket.handle_), reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        throw_socket_error("getsockname");
    }
    if (address.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    }
    throw std::system_error(std::make_error_code(std::errc::address_family_not_supported));
}

} // namespace colt::detail::platform
