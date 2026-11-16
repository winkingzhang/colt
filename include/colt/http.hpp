#pragma once

#include <colt/json.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace colt {
    class Headers {
    public:
        using value_type = std::pair<std::string, std::string>;
        using const_iterator = std::vector<value_type>::const_iterator;

        void set(std::string name, std::string value) {
            const auto key = normalized(name);
            const auto first = std::ranges::find_if(fields_, [&](const auto &field) {
                return normalized(field.first) == key;
            });
            if (first == fields_.end()) {
                fields_.emplace_back(std::move(name), std::move(value));
                return;
            }
            first->second = std::move(value);
            fields_.erase(std::remove_if(std::next(first), fields_.end(), [&](const auto &field) {
                              return normalized(field.first) == key;
                          }),
                          fields_.end());
        }

        void add(std::string name, std::string value) {
            fields_.emplace_back(std::move(name), std::move(value));
        }

        [[nodiscard]] std::optional<std::string_view> get(const std::string_view name) const {
            const auto key = normalized(name);
            const auto found = std::ranges::find_if(fields_, [&](const auto &field) {
                return normalized(field.first) == key;
            });
            if (found == fields_.end()) return std::nullopt;
            return found->second;
        }

        [[nodiscard]] const_iterator begin() const { return fields_.begin(); }
        [[nodiscard]] const_iterator end() const { return fields_.end(); }
        [[nodiscard]] std::size_t size() const { return fields_.size(); }

    private:
        static std::string normalized(const std::string_view value) {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const char ch) {
                if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch + ('a' - 'A'));
                return ch;
            });
            return result;
        }

        std::vector<value_type> fields_;
    };

    class JsonBodyError : public JsonParseError {
    public:
        explicit JsonBodyError(const JsonParseError &error) : JsonParseError(error) {
        }
    };

    struct Request {
        std::string method;
        std::string target;
        std::string path;
        std::string body;
        Headers headers;
        std::unordered_map<std::string, std::string> path_params;
        std::unordered_map<std::string, std::string> query_params;

        [[nodiscard]] Json json(const std::size_t max_depth = Json::default_max_depth) const {
            return json<BuiltinJsonCodec>(max_depth);
        }

        template<JsonCodec Codec>
        [[nodiscard]] Codec::value_type json(
            std::size_t max_depth = Json::default_max_depth) const {
            try {
                return Codec::parse(body, max_depth);
            } catch (const JsonParseError &error) {
                throw JsonBodyError(error);
            }
        }
    };

    struct Response {
        int status{200};
        Headers headers;
        std::string body;

        static Response text(std::string body, const int status = 200) {
            Response response;
            response.status = status;
            response.body = std::move(body);
            return response;
        }

        static Response json(const Json &value, const int status = 200) {
            return json<BuiltinJsonCodec>(value, status);
        }

        template<JsonCodec Codec>
        static Response json(const Codec::value_type &value,
                             const int status = 200,
                             std::size_t max_depth = Json::default_max_depth) {
            Response response;
            response.status = status;
            response.headers.set("Content-Type", "application/json");
            response.body = Codec::dump(value, max_depth);
            return response;
        }
    };

    struct HttpLimits {
        std::size_t request_line_bytes{8 * 1024};
        std::size_t header_bytes{32 * 1024};
        std::size_t body_bytes{1024 * 1024};
    };
} // namespace colt
