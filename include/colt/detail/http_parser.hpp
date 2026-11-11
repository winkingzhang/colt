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
        (ch >= 'a' && ch <= 'z')) return true;
    constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
    return punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
}

inline bool is_valid_field_value(std::string_view value) {
    for (const unsigned char ch : value) {
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

inline bool decode_component(std::string_view input, std::string& output) {
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

inline bool parse_query(std::string_view query,
                        std::unordered_map<std::string, std::string>& params) {
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
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
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

inline ParseResult parse_request(std::string_view input, const HttpLimits& limits = {}) {
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
    for (const unsigned char ch : request_line.substr(0, first_space)) {
        if (!is_token_char(ch)) return error(ParseStatus::bad_request);
    }

    Request request;
    request.method = request_line.substr(0, first_space);
    request.target = request_line.substr(first_space + 1, second_space - first_space - 1);
    if (request.target.empty() || request.target.front() != '/' ||
        request.target.find('#') != std::string::npos) {
        return error(ParseStatus::bad_request);
    }
    for (const unsigned char ch : request.target) {
        if (ch <= 0x20 || ch == 0x7f) return error(ParseStatus::bad_request);
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
        for (const unsigned char ch : line.substr(0, colon)) {
            if (!is_token_char(ch)) return error(ParseStatus::bad_request);
        }
        const auto value = trim_ows(line.substr(colon + 1));
        for (const unsigned char ch : value) {
            if ((ch < 0x20 && ch != '\t') || ch == 0x7f) return error(ParseStatus::bad_request);
        }
        const std::string name(line.substr(0, colon));
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

    if (has_transfer_encoding) return error(ParseStatus::bad_request);
    if (content_length > limits.body_bytes) return error(ParseStatus::payload_too_large);
    if (input.size() - position < content_length) return {};
    request.body.assign(input.substr(position, content_length));
    return {ParseStatus::complete, std::move(request), position + content_length, close_connection};
}

inline std::string serialize_response(const Response& response, bool keep_alive) {
    using namespace http_parser_detail;
    for (const auto& [name, value] : response.headers) {
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char ch) {
                return is_token_char(ch);
            }) ||
            !is_valid_field_value(value)) {
            throw std::invalid_argument("invalid response header");
        }
    }
    std::string result = "HTTP/1.1 " + std::to_string(response.status) + " " +
                         std::string(reason_phrase(response.status)) + "\r\n";
    for (const auto& [name, value] : response.headers) {
        if (name.size() == 14 && std::equal(name.begin(), name.end(), "content-length",
                                             [](unsigned char a, unsigned char b) {
                                                 return ascii_lower(static_cast<char>(a)) == b;
                                             })) continue;
        if (name.size() == 10 && std::equal(name.begin(), name.end(), "connection",
                                             [](unsigned char a, unsigned char b) {
                                                 return ascii_lower(static_cast<char>(a)) == b;
                                             })) continue;
        result += name + ": " + value + "\r\n";
    }
    result += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    result += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    result += "\r\n";
    result += response.body;
    return result;
}

} // namespace colt::detail
