#include "http/http_transport.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <waterlinkedsonar/http/errors.hpp>

#include "support/fake_http_server.hpp"

namespace waterlinked::sonar::detail {
namespace {

using std::chrono_literals::operator""ms;
using test::FakeHttpServer;

TEST(HttpTransport, GetReturnsBody) {
  FakeHttpServer server;
  server.respond(200, R"({"a": 1})");
  HttpTransport transport("127.0.0.1", server.port(), 2000ms);
  EXPECT_EQ(transport.get("/api/v1/integration/about"), R"({"a": 1})");
  EXPECT_EQ(server.last_request().method, "GET");
  EXPECT_EQ(server.last_request().path, "/api/v1/integration/about");
}

TEST(HttpTransport, PostSendsJsonBody) {
  FakeHttpServer server;
  server.respond(204);
  HttpTransport transport("127.0.0.1", server.port(), 2000ms);
  EXPECT_EQ(transport.post("/x", R"({"min":0.5,"max":10.0})"), "");
  const auto request = server.last_request();
  EXPECT_EQ(request.method, "POST");
  EXPECT_NE(request.head.find("Content-Type: application/json"),
            std::string::npos);
  EXPECT_EQ(request.body, R"({"min":0.5,"max":10.0})");
}

TEST(HttpTransport, ReusesHandleAcrossRequests) {
  FakeHttpServer server;
  server.respond(204);
  server.respond(200, "true");
  HttpTransport transport("127.0.0.1", server.port(), 2000ms);
  transport.post("/x", "false");
  EXPECT_EQ(transport.get("/y"), "true");
  EXPECT_EQ(server.last_request().method, "GET");
  EXPECT_TRUE(server.last_request().body.empty());
}

TEST(HttpTransport, ErrorStatusThrowsWithStatusAndBody) {
  FakeHttpServer server;
  server.respond(409, "conflict");
  HttpTransport transport("127.0.0.1", server.port(), 2000ms);
  try {
    transport.post("/x", "true");
    FAIL() << "expected HttpError";
  } catch (const HttpError& e) {
    EXPECT_EQ(e.status_code(), 409);
    EXPECT_NE(std::string(e.what()).find("conflict"), std::string::npos);
  }
}

TEST(HttpTransport, TimeoutThrowsWithStatusZero) {
  FakeHttpServer server;
  server.respond(200, "", 500ms);
  HttpTransport transport("127.0.0.1", server.port(), 100ms);
  try {
    transport.get("/slow");
    FAIL() << "expected HttpError";
  } catch (const HttpError& e) {
    EXPECT_EQ(e.status_code(), 0);
  }
}

TEST(HttpTransport, PostTimeoutOverridesDefault) {
  FakeHttpServer server;
  server.respond(200, "done", 300ms);
  HttpTransport transport("127.0.0.1", server.port(), 100ms);
  EXPECT_EQ(transport.post("/slow", "{}", 2000ms), "done");
}

TEST(HttpTransport, ConnectionRefusedThrows) {
  // A bound socket that does not listen refuses connections.
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&address), size), 0);
  ASSERT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size), 0);

  HttpTransport transport("127.0.0.1", ntohs(address.sin_port), 2000ms);
  EXPECT_THROW(transport.get("/about"), HttpError);
  ::close(fd);
}

}  // namespace
}  // namespace waterlinked::sonar::detail
