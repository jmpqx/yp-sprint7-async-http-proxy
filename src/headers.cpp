#include "headers.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <print>
#include <ranges>
#include <string_view>

constexpr std::string_view body_delimiter = "\r\n\r\n";
constexpr std::string_view headers_delimiter = "\r\n";

constexpr std::string_view host_header = "host";
constexpr std::string_view content_length_header = "content-length";

constexpr std::string_view default_port = "80";

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

std::string strToLower(std::string_view str) {
    std::string result(str.size(), '\0');

    std::transform(str.begin(), str.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

void iterHeaders(std::string_view req, Callback &&callback) {
    auto headers_end = req.find(body_delimiter);
    if (headers_end == std::string_view::npos) {
        return;
    }

    auto headers = req.substr(0, headers_end);
    auto lines = headers | std::views::split(headers_delimiter) |
                 std::views::transform([](auto &&line) { return std::string_view(line); });

    std::ranges::for_each(lines, [&callback](auto &&line) {
        auto delim = line.find(':');

        if (delim != std::string_view::npos) {
            auto key = line.substr(0, delim);
            auto value = line.substr(delim + 2);
            callback(key, value);
        }
    });
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::optional<std::pair<std::string, std::string>> res;

    iterHeaders(req, [&res](std::string_view key, std::string_view value) {
        if (strToLower(key) == host_header) {
            std::pair<std::string, std::string> host_port;

            auto delim = value.find(':');

            if (delim == std::string_view::npos) {
                host_port.first = std::string(value);
                host_port.second = default_port;
            } else {
                host_port.first = std::string(value.substr(0, delim));

                auto port_str = value.substr(delim + 1);
                std::uint16_t port;

                auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
                if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
                    throw std::runtime_error("Invalid port in Host header: " + std::string(port_str));
                }

                host_port.second = std::to_string(port);
            }

            res = host_port;
        }
    });

    if (!res) {
        throw std::runtime_error("Host header not found: " + std::string(req));
    }

    return res.value();
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> content_length;

    iterHeaders(rsp, [&content_length](std::string_view key, std::string_view value) {
        if (strToLower(key) == content_length_header) {
            content_length = std::stoul(std::string(value));
        }
    });

    return content_length;
}
