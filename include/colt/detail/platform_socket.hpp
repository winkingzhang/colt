#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

namespace colt::detail::platform {
    class Socket {
    public:
        Socket() = default;

        ~Socket();

        Socket(const Socket &) = delete;

        Socket &operator=(const Socket &) = delete;

        Socket(Socket &&other) noexcept;

        Socket &operator=(Socket &&other) noexcept;

        void close() noexcept;

        explicit operator bool() const noexcept;

    private:
        explicit Socket(std::uintptr_t handle) noexcept;

        std::uintptr_t handle_{static_cast<std::uintptr_t>(-1)};

        friend Socket listen_tcp(std::string_view, std::uint16_t, int);

        friend Socket accept(Socket &);

        friend std::size_t receive(Socket &, std::span<char>);

        friend void send_all(Socket &, std::span<const char>);

        friend void set_receive_timeout(Socket &, std::chrono::milliseconds);

        friend void shutdown(Socket &);

        friend Socket connect_tcp(std::string_view, std::uint16_t);

        friend std::uint16_t local_port(const Socket &);
    };

    void initialize_sockets();

    Socket listen_tcp(std::string_view address, std::uint16_t port, int backlog);

    Socket accept(Socket &listener);

    std::size_t receive(Socket &socket, std::span<char> buffer);

    void send_all(Socket &socket, std::span<const char> data);

    void set_receive_timeout(Socket &socket, std::chrono::milliseconds timeout);

    void shutdown(Socket &socket);

    Socket connect_tcp(std::string_view address, std::uint16_t port);

    std::uint16_t local_port(const Socket &socket);
} // namespace colt::detail::platform
