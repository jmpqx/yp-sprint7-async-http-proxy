#include "headers.h"

#include <algorithm>
#include <ranges>
#include <string_view>

constexpr std::string_view body_delimiter = "\r\n\r\n";
constexpr std::string_view headers_delimiter = "\r\n";

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

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
        if (key == "Host") {
            std::pair<std::string, std::string> host_port;

            auto delim = value.find(':');

            if (delim == std::string_view::npos) {
                host_port.first = std::string(value);
                host_port.second = "80";
            } else {
                host_port.first = std::string(value.substr(0, delim));
                host_port.second = std::string(value.substr(delim + 1));
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
        if (key == "Content-Length") {
            content_length = std::stoul(std::string(value));
        }
    });

    return content_length;
}
