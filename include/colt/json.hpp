#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace colt {
    class JsonParseError : public std::invalid_argument {
    public:
        JsonParseError(const std::size_t offset, const std::string_view reason)
            : std::invalid_argument("JSON at byte " + std::to_string(offset) + ": " +
                                    std::string(reason)), offset_(offset) {
        }

        [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

    private:
        std::size_t offset_;
    };

    class Json {
    public:
        using array = std::vector<Json>;
        using object = std::map<std::string, Json, std::less<>>;
        static constexpr std::size_t default_max_depth = 128;

        Json() = default;

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(std::nullptr_t) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(bool value) : value_(value) {
        }

        template<std::integral T>
            requires (!std::same_as<T, bool> && sizeof(T) <= sizeof(std::uint64_t))
        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(T value) {
            if constexpr (std::is_signed_v<T>) value_ = static_cast<std::int64_t>(value);
            else value_ = static_cast<std::uint64_t>(value);
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(double value) : value_(value) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(const char *value) : value_(checked_string(value)) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(std::string value) : value_(std::move(value)) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(const std::string_view value) : value_(std::string(value)) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(array value) : value_(std::move(value)) {
        }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        Json(object value) : value_(std::move(value)) {
        }

        template<class T>
        [[nodiscard]] bool is() const noexcept { return std::holds_alternative<T>(value_); }

        [[nodiscard]] bool is_null() const noexcept { return is<std::nullptr_t>(); }

        [[nodiscard]] bool is_number() const noexcept {
            return is<std::int64_t>() || is<std::uint64_t>() || is<double>();
        }

        template<class T>
        [[nodiscard]] const T &get() const { return std::get<T>(value_); }

        template<class T>
        [[nodiscard]] T &get() { return std::get<T>(value_); }

        [[nodiscard]] const Json &at(const std::string_view key) const {
            const auto &fields = get<object>();
            const auto found = fields.find(key);
            if (found == fields.end()) throw std::out_of_range("JSON object key not found");
            return found->second;
        }

        [[nodiscard]] Json &at(const std::string_view key) {
            return const_cast<Json &>(std::as_const(*this).at(key));
        }

        [[nodiscard]] const Json &at(const std::size_t index) const { return get<array>().at(index); }
        [[nodiscard]] Json &at(const std::size_t index) { return get<array>().at(index); }

        [[nodiscard]] static Json parse(std::string_view input,
                                        std::size_t max_depth = default_max_depth);

        [[nodiscard]] std::string dump(const std::size_t max_depth = default_max_depth) const {
            std::string output;
            write(output, 0, max_depth);
            return output;
        }

    private:
        using variant = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double,
            std::string, array, object>;
        variant value_{nullptr};

        static std::string checked_string(const char *value) {
            if (!value) throw std::invalid_argument("JSON string pointer must not be null");
            return value;
        }

        // Return the size of one valid UTF-8 scalar, or zero for an invalid sequence.
        static std::size_t utf8_size(const std::string_view text, const std::size_t offset) {
            const auto first = static_cast<unsigned char>(text[offset]);
            if (first < 0x80) return 1;
            std::size_t size;
            std::uint32_t scalar;
            if (first >= 0xc2 && first <= 0xdf) {
                size = 2;
                scalar = first & 0x1f;
            } else if (first >= 0xe0 && first <= 0xef) {
                size = 3;
                scalar = first & 0x0f;
            } else if (first >= 0xf0 && first <= 0xf4) {
                size = 4;
                scalar = first & 0x07;
            } else {
                return 0;
            }
            if (text.size() - offset < size) return 0;
            for (std::size_t i = 1; i < size; ++i) {
                const auto byte = static_cast<unsigned char>(text[offset + i]);
                if ((byte & 0xc0) != 0x80) return 0;
                scalar = (scalar << 6) | (byte & 0x3f);
            }
            if ((size == 3 && scalar < 0x800) || (size == 4 && scalar < 0x10000) ||
                scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
                return 0;
            return size;
        }

        static void write_string(std::string &output, const std::string_view text) {
            output += '"';
            for (std::size_t i = 0; i < text.size();) {
                const auto ch = static_cast<unsigned char>(text[i]);
                if (ch >= 0x80) {
                    const auto size = utf8_size(text, i);
                    if (size == 0) throw std::invalid_argument("JSON string contains invalid UTF-8");
                    output.append(text.substr(i, size));
                    i += size;
                    continue;
                }
                ++i;
                switch (ch) {
                    case '"': output += "\\\"";
                        break;
                    case '\\': output += "\\\\";
                        break;
                    case '\b': output += "\\b";
                        break;
                    case '\f': output += "\\f";
                        break;
                    case '\n': output += "\\n";
                        break;
                    case '\r': output += "\\r";
                        break;
                    case '\t': output += "\\t";
                        break;
                    default:
                        if (ch < 0x20) {
                            constexpr char hex[] = "0123456789abcdef";
                            output += "\\u00";
                            output += hex[ch >> 4];
                            output += hex[ch & 0xf];
                        } else {
                            output += static_cast<char>(ch);
                        }
                }
            }
            output += '"';
        }

        template<class T>
        static void write_number(std::string &output, T value) {
            char buffer[128];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{}) throw std::runtime_error("JSON number formatting failed");
            output.append(buffer, result.ptr);
            if constexpr (std::is_floating_point_v<T>) {
                const std::string_view formatted(buffer, static_cast<std::size_t>(result.ptr - buffer));
                if (formatted.find_first_of(".eE") == std::string_view::npos) output += ".0";
            }
        }

        void write(std::string &output, const std::size_t depth, const std::size_t max_depth) const {
            std::visit([&]<typename TV>(const TV &value) {
                using T = std::decay_t<TV>;
                if constexpr (std::same_as<T, std::nullptr_t>) {
                    output += "null";
                } else if constexpr (std::same_as<T, bool>) {
                    output += value ? "true" : "false";
                } else if constexpr (std::integral<T>) {
                    write_number(output, value);
                } else if constexpr (std::same_as<T, double>) {
                    if (!std::isfinite(value)) {
                        throw std::invalid_argument("JSON number must be finite");
                    }
                    write_number(output, value);
                } else if constexpr (std::same_as<T, std::string>) {
                    write_string(output, value);
                } else {
                    if (depth >= max_depth) {
                        throw std::invalid_argument("JSON maximum nesting depth exceeded");
                    }
                    constexpr bool is_object = std::same_as<T, object>;
                    output += is_object ? '{' : '[';
                    bool first = true;
                    for (const auto &entry: value) {
                        if (!first) output += ',';
                        first = false;
                        if constexpr (is_object) {
                            write_string(output, entry.first);
                            output += ':';
                            entry.second.write(output, depth + 1, max_depth);
                        } else {
                            entry.write(output, depth + 1, max_depth);
                        }
                    }
                    output += is_object ? '}' : ']';
                }
            }, value_);
        }

        class Parser;
    };

    class Json::Parser {
    public:
        Parser(const std::string_view input, const std::size_t max_depth)
            : input_(input), max_depth_(max_depth) {
        }

        Json parse() {
            auto result = value(0);
            whitespace();
            if (offset_ != input_.size()) fail("unexpected trailing data");
            return result;
        }

    private:
        std::string_view input_;
        std::size_t max_depth_;
        std::size_t offset_{0};

        [[noreturn]] void fail(const std::string_view reason) const {
            throw JsonParseError(offset_, reason);
        }

        void whitespace() {
            while (offset_ < input_.size()) {
                const auto ch = input_[offset_];
                if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
                ++offset_;
            }
        }

        bool consume(const char ch) {
            if (offset_ == input_.size() || input_[offset_] != ch) return false;
            ++offset_;
            return true;
        }

        static bool digit(const char ch) { return ch >= '0' && ch <= '9'; }

        void digits() {
            const auto start = offset_;
            while (offset_ < input_.size() && digit(input_[offset_])) ++offset_;
            if (offset_ == start) fail("expected digit");
        }

        Json number() {
            const auto start = offset_;
            const bool negative = consume('-');
            if (!consume('0')) digits();
            bool floating = false;
            if (consume('.')) {
                floating = true;
                digits();
            }
            if (consume('e') || consume('E')) {
                floating = true;
                if (!consume('+')) consume('-');
                digits();
            }
            const auto token = input_.substr(start, offset_ - start);
            const auto *end = token.data() + token.size();
            if (!floating) {
                if (negative) {
                    std::int64_t integer{};
                    const auto [ptr, ec] = std::from_chars(token.data(), end, integer);
                    if (ec == std::errc{} && ptr == end) return integer;
                } else {
                    std::uint64_t integer{};
                    const auto [ptr, ec] = std::from_chars(token.data(), end, integer);
                    if (ec == std::errc{} && ptr == end) {
                        if (integer <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                            return static_cast<std::int64_t>(integer);
                        }
                        return integer;
                    }
                }
                fail("integer outside 64-bit range");
            }
            double real{};
            const auto [ptr, ec] = std::from_chars(token.data(), end, real);
            if (ec != std::errc{} || ptr != end || !std::isfinite(real)) {
                fail("number outside double range");
            }
            return real;
        }

        std::uint32_t hex_quad() {
            std::uint32_t result = 0;
            for (int i = 0; i < 4; ++i) {
                if (offset_ == input_.size()) fail("incomplete Unicode escape");
                const auto ch = input_[offset_++];
                result <<= 4;
                if (ch >= '0' && ch <= '9') result |= static_cast<std::uint32_t>(ch - '0');
                else if (ch >= 'a' && ch <= 'f') result |= static_cast<std::uint32_t>(ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') result |= static_cast<std::uint32_t>(ch - 'A' + 10);
                else fail("invalid Unicode escape");
            }
            return result;
        }

        void unicode(std::string &output) {
            auto scalar = hex_quad();
            if (scalar >= 0xd800 && scalar <= 0xdbff) {
                if (!consume('\\') || !consume('u')) fail("expected low surrogate");
                const auto low = hex_quad();
                if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
            } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
                fail("unpaired low surrogate");
            }
            if (scalar < 0x80) {
                output += static_cast<char>(scalar);
            } else if (scalar < 0x800) {
                output += static_cast<char>(0xc0 | (scalar >> 6));
                output += static_cast<char>(0x80 | (scalar & 0x3f));
            } else if (scalar < 0x10000) {
                output += static_cast<char>(0xe0 | (scalar >> 12));
                output += static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
                output += static_cast<char>(0x80 | (scalar & 0x3f));
            } else {
                output += static_cast<char>(0xf0 | (scalar >> 18));
                output += static_cast<char>(0x80 | ((scalar >> 12) & 0x3f));
                output += static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
                output += static_cast<char>(0x80 | (scalar & 0x3f));
            }
        }

        std::string string() {
            if (!consume('"')) fail("expected string");
            std::string output;
            while (offset_ < input_.size()) {
                const auto ch = static_cast<unsigned char>(input_[offset_++]);
                if (ch == '"') return output;
                if (ch < 0x20) fail("unescaped control character");
                if (ch == '\\') {
                    if (offset_ == input_.size()) fail("incomplete escape");
                    switch (input_[offset_++]) {
                        case '"': output += '"';
                            break;
                        case '\\': output += '\\';
                            break;
                        case '/': output += '/';
                            break;
                        case 'b': output += '\b';
                            break;
                        case 'f': output += '\f';
                            break;
                        case 'n': output += '\n';
                            break;
                        case 'r': output += '\r';
                            break;
                        case 't': output += '\t';
                            break;
                        case 'u': unicode(output);
                            break;
                        default: fail("invalid escape");
                    }
                } else if (ch >= 0x80) {
                    const auto size = utf8_size(input_, offset_ - 1);
                    if (size == 0) fail("invalid UTF-8");
                    output.append(input_.substr(offset_ - 1, size));
                    offset_ += size - 1;
                } else {
                    output += static_cast<char>(ch);
                }
            }
            fail("unterminated string");
        }

        Json value(const std::size_t depth) {
            whitespace();
            if (offset_ == input_.size()) fail("expected value");
            const auto ch = input_[offset_];
            if (ch == '"') return string();
            if (ch == '-' || digit(ch)) return number();
            if (ch == '[' || ch == '{') {
                if (depth >= max_depth_) fail("maximum nesting depth exceeded");
                ++offset_;
                whitespace();
                if (ch == '[') {
                    array items;
                    if (consume(']')) return items;
                    do {
                        items.push_back(value(depth + 1));
                        whitespace();
                        if (consume(']')) return items;
                        if (!consume(',')) fail("expected comma or closing bracket");
                    } while (true);
                }
                object fields;
                if (consume('}')) return fields;
                do {
                    whitespace();
                    auto key = string();
                    whitespace();
                    if (!consume(':')) fail("expected colon");
                    auto item = value(depth + 1);
                    if (!fields.emplace(std::move(key), std::move(item)).second) {
                        fail("duplicate object key");
                    }
                    whitespace();
                    if (consume('}')) return fields;
                    if (!consume(',')) fail("expected comma or closing brace");
                } while (true);
            }
            for (const auto literal: {
                     std::string_view("null"), std::string_view("true"),
                     std::string_view("false")
                 }) {
                if (input_.substr(offset_, literal.size()) == literal) {
                    offset_ += literal.size();
                    if (literal == "null") return nullptr;
                    return literal == "true";
                }
            }
            fail("expected value");
        }
    };

    inline Json Json::parse(const std::string_view input, const std::size_t max_depth) {
        return Parser(input, max_depth).parse();
    }

    template<class Codec>
    concept JsonCodec = requires(std::string_view input,
                                 const std::size_t max_depth,
                                 const typename Codec::value_type &value)
    {
        { Codec::parse(input, max_depth) } -> std::same_as<typename Codec::value_type>;
        { Codec::dump(value, max_depth) } -> std::same_as<std::string>;
    };

    struct BuiltinJsonCodec {
        using value_type = Json;

        [[nodiscard]] static Json parse(const std::string_view input, const std::size_t max_depth) {
            return Json::parse(input, max_depth);
        }

        [[nodiscard]] static std::string dump(const Json &value, const std::size_t max_depth) {
            return value.dump(max_depth);
        }
    };
} // namespace colt
