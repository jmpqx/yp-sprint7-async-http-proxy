#include "headers.h"
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using Headers = std::vector<std::pair<std::string, std::string>>;

static Headers collectHeaders(std::string_view input) {
    Headers result;
    iterHeaders(input, [&](std::string_view key, std::string_view value) {
        result.push_back({std::string(key), std::string(value)});
    });
    return result;
}

TEST(iterHeaders, Empty) {
    auto headers = collectHeaders("no delimiter here");
    EXPECT_TRUE(headers.empty());
}

TEST(iterHeaders, SkipRequestLine) {
    auto headers = collectHeaders("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "Host");
    EXPECT_EQ(headers[0].second, "example.com");
}

TEST(iterHeaders, SingleHeader) {
    auto headers = collectHeaders("Host: example.com\r\n\r\n");
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "Host");
    EXPECT_EQ(headers[0].second, "example.com");
}

TEST(iterHeaders, MultipleHeaders) {
    auto headers = collectHeaders("Host: example.com\r\n"
                                  "Content-Length: 42\r\n"
                                  "\r\n");
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0].first, "Host");
    EXPECT_EQ(headers[0].second, "example.com");
    EXPECT_EQ(headers[1].first, "Content-Length");
    EXPECT_EQ(headers[1].second, "42");
}

TEST(iterHeaders, MultipleSameHeaders) {
    auto headers = collectHeaders("Set-Cookie: a=1\r\n"
                                  "Set-Cookie: b=2\r\n"
                                  "\r\n");
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_EQ(headers[0].first, "Set-Cookie");
    EXPECT_EQ(headers[0].second, "a=1");
    EXPECT_EQ(headers[1].first, "Set-Cookie");
    EXPECT_EQ(headers[1].second, "b=2");
}

TEST(findHostPort, Simple) {
    auto [host, port] = findHostPort("GET / HTTP/1.1\r\n"
                                     "Host: example.com:8080\r\n"
                                     "\r\n");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");

    auto [host2, port2] = findHostPort("GET / HTTP/1.1\r\n"
                                       "Host: example.com\r\n"
                                       "\r\n");
    EXPECT_EQ(host2, "example.com");
    EXPECT_EQ(port2, "80");
}

TEST(findHostPort, NoHost) {
    EXPECT_THROW(findHostPort("GET / HTTP/1.1\r\nContent-Length: 42\r\n\r\n"), std::runtime_error);
}

TEST(findHostPort, DifferentHostHeaderRegisters) {
    auto [host, port] = findHostPort("GET / HTTP/1.1\r\n"
                                     "host: example.com:8080\r\n"
                                     "\r\n");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "8080");

    auto [host2, port2] = findHostPort("GET / HTTP/1.1\r\n"
                                       "hOsT: example.com\r\n"
                                       "\r\n");
    EXPECT_EQ(host2, "example.com");
    EXPECT_EQ(port2, "80");
}

TEST(findHostPort, InvalidPort) {
    EXPECT_THROW(findHostPort("GET / HTTP/1.1\r\n"
                              "host: example.com:8q0q\r\n"
                              "\r\n"),
                 std::runtime_error);
}

TEST(findContentLength, Simple) {
    auto result = findContentLength("HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 1234\r\n"
                                    "\r\n");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1234u);
}

TEST(findContentLength, NoContentLength) {
    auto result = findContentLength("HTTP/1.1 200 OK\r\n"
                                    "Transfer-Encoding: chunked\r\n"
                                    "\r\n");
    EXPECT_FALSE(result.has_value());
}

TEST(findContentLength, DifferentContentLengthHeaderRegisters) {
    auto result = findContentLength("HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 1234\r\n"
                                    "\r\n");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1234u);

    result = findContentLength("HTTP/1.1 200 OK\r\n"
                               "content-length: 1234\r\n"
                               "\r\n");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1234u);

    result = findContentLength("HTTP/1.1 200 OK\r\n"
                               "conTenT-lEngTH: 1234\r\n"
                               "\r\n");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1234u);
}
