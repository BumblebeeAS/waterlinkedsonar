/// \file
/// HTTP server on localhost that answers with queued responses.

#ifndef WATERLINKEDSONAR_TEST_SUPPORT_FAKE_HTTP_SERVER_HPP
#define WATERLINKEDSONAR_TEST_SUPPORT_FAKE_HTTP_SERVER_HPP

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace waterlinked::sonar::test {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string head;  ///< Request line and headers.
  std::string body;
};

/// Serves one request per connection, in order, with the queued responses.
class FakeHttpServer {
 public:
  FakeHttpServer() : listen_fd_(::socket(AF_INET, SOCK_STREAM, 0)) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    EXPECT_EQ(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), size),
              0);
    EXPECT_EQ(::listen(listen_fd_, 4), 0);
    EXPECT_EQ(
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &size),
        0);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
  }

  ~FakeHttpServer() {
    ::shutdown(listen_fd_, SHUT_RDWR);
    thread_.join();
    ::close(listen_fd_);
  }

  FakeHttpServer(const FakeHttpServer&) = delete;
  FakeHttpServer& operator=(const FakeHttpServer&) = delete;

  [[nodiscard]] std::uint16_t port() const { return port_; }

  /// Queues the response to the next request.
  void respond(int status, std::string body = "",
               std::chrono::milliseconds delay = {}) {
    const std::lock_guard lock(mutex_);
    responses_.push_back({status, std::move(body), delay});
  }

  [[nodiscard]] std::vector<HttpRequest> requests() const {
    const std::lock_guard lock(mutex_);
    return requests_;
  }

  [[nodiscard]] HttpRequest last_request() const {
    const std::lock_guard lock(mutex_);
    return requests_.empty() ? HttpRequest{} : requests_.back();
  }

 private:
  struct Response {
    int status;
    std::string body;
    std::chrono::milliseconds delay;
  };

  void serve() {
    while (true) {
      const int connection = ::accept(listen_fd_, nullptr, nullptr);
      if (connection < 0) {
        return;
      }
      HttpRequest request = read_request(connection);
      Response response{500, "no response queued", {}};
      {
        const std::lock_guard lock(mutex_);
        requests_.push_back(request);
        if (!responses_.empty()) {
          response = std::move(responses_.front());
          responses_.pop_front();
        }
      }
      std::this_thread::sleep_for(response.delay);
      const std::string reply =
          "HTTP/1.1 " + std::to_string(response.status) +
          " Status\r\nContent-Length: " + std::to_string(response.body.size()) +
          "\r\nConnection: close\r\n\r\n" + response.body;
      ::send(connection, reply.data(), reply.size(), MSG_NOSIGNAL);
      ::close(connection);
    }
  }

  static HttpRequest read_request(int connection) {
    std::string data;
    std::size_t head_end = std::string::npos;
    std::size_t content_length = 0;
    std::array<char, 4096> chunk{};
    while (head_end == std::string::npos ||
           data.size() < head_end + 4 + content_length) {
      const ssize_t received =
          ::recv(connection, chunk.data(), chunk.size(), 0);
      if (received <= 0) {
        break;
      }
      data.append(chunk.data(), static_cast<std::size_t>(received));
      if (head_end == std::string::npos) {
        head_end = data.find("\r\n\r\n");
        const std::size_t length_header = data.find("Content-Length: ");
        if (head_end != std::string::npos && length_header < head_end) {
          content_length = std::stoul(data.substr(length_header + 16));
        }
      }
    }
    HttpRequest request;
    request.head = data.substr(0, head_end);
    if (head_end != std::string::npos) {
      request.body = data.substr(head_end + 4);
    }
    const std::size_t method_end = request.head.find(' ');
    const std::size_t path_end = request.head.find(' ', method_end + 1);
    request.method = request.head.substr(0, method_end);
    request.path =
        request.head.substr(method_end + 1, path_end - method_end - 1);
    return request;
  }

  int listen_fd_;
  std::uint16_t port_{0};
  mutable std::mutex mutex_;
  std::deque<Response> responses_;
  std::vector<HttpRequest> requests_;
  std::thread thread_;
};

}  // namespace waterlinked::sonar::test

#endif  // WATERLINKEDSONAR_TEST_SUPPORT_FAKE_HTTP_SERVER_HPP
