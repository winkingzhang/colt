#pragma once

#include <colt/http.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>

namespace colt::detail {
    enum class ParseStatus { complete, need_more, bad_request, payload_too_large };

    struct ParseResult {
        ParseStatus status{ParseStatus::need_more};
        Request request;
        std::size_t bytes_consumed{};
        bool close_connection{};
    };

    namespace http_parser_detail {
        inline bool is_token_char(unsigned char ch) {
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= 'a' && ch <= 'z'))
                return true;
            constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
            return punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
        }

        inline bool is_valid_field_value(std::string_view value) {
            for (const unsigned char ch: value) {
                if ((ch < 0x20 && ch != '\t') || ch == 0x7f) return false;
            }
            return true;
        }

        inline std::string_view trim_ows(std::string_view value) {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            return value;
        }

        inline char ascii_lower(char ch) {
            if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch + ('a' - 'A'));
            return ch;
        }

        inline int hex_value(char ch) {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        }

        inline bool is_reg_name(std::string_view host) {
            if (host.empty()) return false;
            for (std::size_t i = 0; i < host.size(); ++i) {
                const char ch = host[i];
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~' ||
                    ch == '!' || ch == '$' || ch == '&' || ch == '\'' || ch == '(' || ch == ')' ||
                    ch == '*' || ch == '+' || ch == ',' || ch == ';' || ch == '=') {
                    continue;
                }
                if (ch == '%' && i + 2 < host.size() && hex_value(host[i + 1]) >= 0 &&
                    hex_value(host[i + 2]) >= 0) {
                    i += 2;
                    continue;
                }
                return false;
            }
            return true;
        }

        inline bool is_ipv4(std::string_view address) {
            std::size_t parts = 0;
            while (!address.empty()) {
                const auto dot = address.find('.');
                const auto part = address.substr(0, dot);
                if (part.empty() || part.size() > 3 || (part.size() > 1 && part.front() == '0')) {
                    return false;
                }
                unsigned value = 0;
                for (const char ch: part) {
                    if (ch < '0' || ch > '9') return false;
                    value = value * 10 + static_cast<unsigned>(ch - '0');
                }
                if (value > 255) return false;
                ++parts;
                if (dot == std::string_view::npos) break;
                address.remove_prefix(dot + 1);
                if (address.empty()) return false;
            }
            return parts == 4;
        }

        inline bool is_ipv6(std::string_view address) {
            const auto compression = address.find("::");
            if (compression != std::string_view::npos &&
                address.find("::", compression + 2) != std::string_view::npos)
                return false;
            if (address.empty() || (compression == std::string_view::npos &&
                                    (address.front() == ':' || address.back() == ':')))
                return false;

            const auto parse_groups = [](std::string_view side, bool allow_ipv4, unsigned &count) {
                while (!side.empty()) {
                    const auto colon = side.find(':');
                    const auto group = side.substr(0, colon);
                    if (group.empty()) return false;
                    if (group.find('.') != std::string_view::npos) {
                        if (!allow_ipv4 || colon != std::string_view::npos || !is_ipv4(group)) return false;
                        count += 2;
                    } else {
                        if (group.size() > 4) return false;
                        for (const char ch: group) {
                            if (hex_value(ch) < 0) return false;
                        }
                        ++count;
                    }
                    if (colon == std::string_view::npos) break;
                    side.remove_prefix(colon + 1);
                }
                return true;
            };

            unsigned groups = 0;
            if (compression == std::string_view::npos) {
                return parse_groups(address, true, groups) && groups == 8;
            }
            const auto left = address.substr(0, compression);
            const auto right = address.substr(compression + 2);
            if (!parse_groups(left, false, groups) ||
                !parse_groups(right, true, groups))
                return false;
            return groups < 8;
        }

        inline bool is_ip_literal(std::string_view literal) {
            if (literal.empty()) return false;
            if (literal.front() == 'v' || literal.front() == 'V') {
                const auto dot = literal.find('.');
                if (dot == std::string_view::npos || dot == 1) return false;
                for (const char ch: literal.substr(1, dot - 1)) {
                    if (hex_value(ch) < 0) return false;
                }
                const auto tail = literal.substr(dot + 1);
                if (tail.empty()) return false;
                for (const unsigned char ch: tail) {
                    const bool unreserved =
                            (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' ||
                            ch == '~';
                    constexpr std::string_view sub_delimiters = "!$&'()*+,;=";
                    if (!unreserved && sub_delimiters.find(static_cast<char>(ch)) ==
                        std::string_view::npos &&
                        ch != ':')
                        return false;
                }
                return true;
            }
            return is_ipv6(literal);
        }

        inline bool is_valid_host_authority(std::string_view authority) {
            if (authority.empty()) return false;
            std::string_view host;
            std::string_view port;
            if (authority.front() == '[') {
                const auto close = authority.find(']');
                if (close == std::string_view::npos ||
                    !is_ip_literal(authority.substr(1, close - 1)))
                    return false;
                host = authority.substr(1, close - 1);
                const auto suffix = authority.substr(close + 1);
                if (!suffix.empty()) {
                    if (suffix.front() != ':') return false;
                    port = suffix.substr(1);
                }
            } else {
                const auto colon = authority.find(':');
                if (colon != std::string_view::npos) {
                    if (authority.find(':', colon + 1) != std::string_view::npos) return false;
                    host = authority.substr(0, colon);
                    port = authority.substr(colon + 1);
                } else {
                    host = authority;
                }
                if (!is_reg_name(host)) return false;
            }
            if (!port.empty()) {
                unsigned value = 0;
                for (const char ch: port) {
                    if (ch < '0' || ch > '9') return false;
                    const unsigned digit = static_cast<unsigned>(ch - '0');
                    if (value > (65535 - digit) / 10) return false;
                    value = value * 10 + digit;
                }
            } else if (authority.back() == ':') {
                return false;
            }
            return true;
        }

        inline bool decode_component(std::string_view input, std::string &output) {
            for (std::size_t i = 0; i < input.size(); ++i) {
                if (input[i] == '+') {
                    output.push_back(' ');
                } else if (input[i] == '%') {
                    if (i + 2 >= input.size()) return false;
                    const int high = hex_value(input[i + 1]);
                    const int low = hex_value(input[i + 2]);
                    if (high < 0 || low < 0) return false;
                    output.push_back(static_cast<char>((high << 4) | low));
                    i += 2;
                } else {
                    output.push_back(input[i]);
                }
            }
            return true;
        }

        inline bool is_valid_request_target(std::string_view target) {
            for (const unsigned char ch: target) {
                if (ch <= 0x20 || ch >= 0x7f) return false;
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9'))
                    continue;
                constexpr std::string_view allowed = "/?%:@!$&'()*+,;=-._~";
                if (allowed.find(static_cast<char>(ch)) == std::string_view::npos) return false;
            }
            return true;
        }

        inline bool parse_query(std::string_view query,
                                std::unordered_map<std::string, std::string> &params) {
            while (!query.empty()) {
                const auto ampersand = query.find('&');
                const auto pair = query.substr(0, ampersand);
                const auto equals = pair.find('=');
                std::string key;
                std::string value;
                if (!decode_component(pair.substr(0, equals), key) ||
                    (equals != std::string_view::npos &&
                     !decode_component(pair.substr(equals + 1), value))) {
                    return false;
                }
                params.insert_or_assign(std::move(key), std::move(value));
                if (ampersand == std::string_view::npos) break;
                query.remove_prefix(ampersand + 1);
            }
            return true;
        }

        inline ParseResult error(ParseStatus status) {
            return {status, {}, 0, true};
        }

        inline std::string_view reason_phrase(int status) {
            switch (status) {
                case 200: return "OK";
                case 201: return "Created";
                case 202: return "Accepted";
                case 100: return "Continue";
                case 204: return "No Content";
                case 205: return "Reset Content";
                case 301: return "Moved Permanently";
                case 302: return "Found";
                case 304: return "Not Modified";
                case 400: return "Bad Request";
                case 404: return "Not Found";
                case 405: return "Method Not Allowed";
                case 413: return "Payload Too Large";
                case 500: return "Internal Server Error";
                case 501: return "Not Implemented";
                case 503: return "Service Unavailable";
                default: return "Unknown";
            }
        }
    } // namespace http_parser_detail

    inline ParseResult parse_request(std::string_view input, const HttpLimits &limits = {}) {
        using namespace http_parser_detail;

        const auto line_end = input.find("\r\n");
        if (line_end == std::string_view::npos) {
            if (!input.empty() && input.back() == '\r' && input.find('\r') == input.size() - 1 &&
                input.size() - 1 <= limits.request_line_bytes) {
                return {};
            }
            if (input.size() > limits.request_line_bytes) {
                return error(ParseStatus::payload_too_large);
            }
            const auto first_cr = input.find('\r');
            const auto last_cr = input.rfind('\r');
            if (input.find('\n') != std::string_view::npos ||
                (first_cr != std::string_view::npos &&
                 (first_cr != last_cr || last_cr + 1 != input.size()))) {
                return error(ParseStatus::bad_request);
            }
            return {};
        }
        if (line_end > limits.request_line_bytes) return error(ParseStatus::payload_too_large);

        const auto request_line = input.substr(0, line_end);
        const auto first_space = request_line.find(' ');
        const auto second_space = first_space == std::string_view::npos
                                      ? std::string_view::npos
                                      : request_line.find(' ', first_space + 1);
        if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
            first_space == 0 || second_space == first_space + 1 ||
            request_line.find(' ', second_space + 1) != std::string_view::npos ||
            request_line.substr(second_space + 1) != "HTTP/1.1") {
            return error(ParseStatus::bad_request);
        }
        for (const unsigned char ch: request_line.substr(0, first_space)) {
            if (!is_token_char(ch)) return error(ParseStatus::bad_request);
        }

        Request request;
        request.method = request_line.substr(0, first_space);
        request.target = request_line.substr(first_space + 1, second_space - first_space - 1);
        if (request.target.empty() || request.target.front() != '/' ||
            request.target.find('#') != std::string::npos || !is_valid_request_target(request.target)) {
            return error(ParseStatus::bad_request);
        }
        const auto question = request.target.find('?');
        request.path = request.target.substr(0, question);
        std::string decoded_path;
        if (!decode_component(request.path, decoded_path)) return error(ParseStatus::bad_request);
        if (question != std::string::npos &&
            !parse_query(std::string_view(request.target).substr(question + 1), request.query_params)) {
            return error(ParseStatus::bad_request);
        }

        const std::size_t headers_start = line_end + 2;
        std::size_t position = headers_start;
        std::size_t content_length_count = 0;
        std::size_t host_count = 0;
        std::size_t content_length = 0;
        bool has_transfer_encoding = false;
        bool close_connection = false;
        while (true) {
            const auto header_end = input.find("\r\n", position);
            if (header_end == std::string_view::npos) {
                const auto incomplete_headers = input.substr(position);
                if (!incomplete_headers.empty() && incomplete_headers.back() == '\r' &&
                    incomplete_headers.find('\r') == incomplete_headers.size() - 1 &&
                    input.size() - headers_start <= limits.header_bytes) {
                    return {};
                }
                if (input.size() - headers_start > limits.header_bytes) {
                    return error(ParseStatus::payload_too_large);
                }
                const auto first_cr = incomplete_headers.find('\r');
                const auto last_cr = incomplete_headers.rfind('\r');
                if (incomplete_headers.find('\n') != std::string_view::npos ||
                    (first_cr != std::string_view::npos &&
                     (first_cr != last_cr || last_cr + 1 != incomplete_headers.size()))) {
                    return error(ParseStatus::bad_request);
                }
                return {};
            }
            if (header_end - headers_start + 2 > limits.header_bytes) {
                return error(ParseStatus::payload_too_large);
            }
            if (header_end == position) {
                position += 2;
                break;
            }

            const auto line = input.substr(position, header_end - position);
            const auto colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0 || line.front() == ' ' || line.front() == '\t') {
                return error(ParseStatus::bad_request);
            }
            for (const unsigned char ch: line.substr(0, colon)) {
                if (!is_token_char(ch)) return error(ParseStatus::bad_request);
            }
            const auto value = trim_ows(line.substr(colon + 1));
            for (const unsigned char ch: value) {
                if ((ch < 0x20 && ch != '\t') || ch == 0x7f) return error(ParseStatus::bad_request);
            }
            const std::string name(line.substr(0, colon));
            if (name.size() == 4 && std::equal(name.begin(), name.end(), "host",
                                               [](unsigned char a, unsigned char b) {
                                                   return ascii_lower(static_cast<char>(a)) == b;
                                               })) {
                ++host_count;
                if (host_count > 1 || !is_valid_host_authority(value)) {
                    return error(ParseStatus::bad_request);
                }
            }
            if (name.size() == 10 && std::equal(name.begin(), name.end(), "connection",
                                                [](unsigned char a, unsigned char b) {
                                                    return ascii_lower(static_cast<char>(a)) == b;
                                                })) {
                std::string lower_value(value);
                std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ascii_lower);
                std::size_t start = 0;
                while (start < lower_value.size()) {
                    const auto comma = lower_value.find(',', start);
                    const auto token = trim_ows(std::string_view(lower_value).substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start));
                    if (token == "close") close_connection = true;
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            if (name.size() == 14 && std::equal(name.begin(), name.end(), "content-length",
                                                [](unsigned char a, unsigned char b) {
                                                    return ascii_lower(static_cast<char>(a)) == b;
                                                })) {
                ++content_length_count;
                if (content_length_count > 1 || value.empty()) return error(ParseStatus::bad_request);
                const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), content_length);
                if (ec == std::errc::result_out_of_range) {
                    return error(ParseStatus::payload_too_large);
                }
                if (ec != std::errc{} || end != value.data() + value.size()) {
                    return error(ParseStatus::bad_request);
                }
            } else if (name.size() == 17 && std::equal(name.begin(), name.end(), "transfer-encoding",
                                                       [](unsigned char a, unsigned char b) {
                                                           return ascii_lower(static_cast<char>(a)) == b;
                                                       })) {
                has_transfer_encoding = true;
            }
            request.headers.add(name, std::string(value));
            position = header_end + 2;
        }

        if (host_count != 1) return error(ParseStatus::bad_request);
        if (has_transfer_encoding) return error(ParseStatus::bad_request);
        if (content_length > limits.body_bytes) return error(ParseStatus::payload_too_large);
        if (input.size() - position < content_length) return {};
        request.body.assign(input.substr(position, content_length));
        return {ParseStatus::complete, std::move(request), position + content_length, close_connection};
    }

    inline std::string serialize_response(const Response &response, bool keep_alive) {
        using namespace http_parser_detail;
        if (response.status < 100 || response.status > 599) {
            throw std::invalid_argument("invalid HTTP response status");
        }
        const bool body_forbidden = (response.status >= 100 && response.status < 200) ||
                                    response.status == 204 || response.status == 205 ||
                                    response.status == 304;
        for (const auto &[name, value]: response.headers) {
            if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
                    return is_token_char(ch);
                }) ||
                !is_valid_field_value(value)) {
                throw std::invalid_argument("invalid response header");
            }
            if (name.size() == 17 && std::equal(name.begin(), name.end(), "transfer-encoding",
                                                [](unsigned char a, unsigned char b) {
                                                    return ascii_lower(static_cast<char>(a)) == b;
                                                })) {
                throw std::invalid_argument("Transfer-Encoding is not supported");
            }
        }
        std::string result = "HTTP/1.1 " + std::to_string(response.status) + " " +
                             std::string(reason_phrase(response.status)) + "\r\n";
        for (const auto &[name, value]: response.headers) {
            if (name.size() == 14 && std::equal(name.begin(), name.end(), "content-length",
                                                [](unsigned char a, unsigned char b) {
                                                    return ascii_lower(static_cast<char>(a)) == b;
                                                }))
                continue;
            if (name.size() == 10 && std::equal(name.begin(), name.end(), "connection",
                                                [](unsigned char a, unsigned char b) {
                                                    return ascii_lower(static_cast<char>(a)) == b;
                                                }))
                continue;
            result += name + ": " + value + "\r\n";
        }
        if (!body_forbidden || response.status == 205) {
            const auto content_length = response.status == 205 ? 0 : response.body.size();
            result += "Content-Length: " + std::to_string(content_length) + "\r\n";
        }
        result += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
        result += "\r\n";
        if (!body_forbidden) result += response.body;
        return result;
    }
} // namespace colt::detail
