/// \file
/// Blocking HTTP/1.1 transport built on libcurl.

#ifndef WATERLINKEDSONAR_HTTP_HTTP_TRANSPORT_HPP
#define WATERLINKEDSONAR_HTTP_HTTP_TRANSPORT_HPP

#include <curl/curl.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace waterlinked::sonar::detail {

/// One libcurl easy handle bound to a host; keeps the connection alive
/// between requests. Not thread-safe.
class HttpTransport {
 public:
  /// \param timeout Default limit for each request.
  /// \throws HttpError if libcurl cannot create a handle.
  HttpTransport(const std::string& host, std::uint16_t port,
                std::chrono::milliseconds timeout);

  /// Default limit for each request.
  [[nodiscard]] std::chrono::milliseconds timeout() const noexcept {
    return timeout_;
  }

  /// Sends a GET request.
  ///
  /// \param path Absolute path, e.g. "/api/v1/integration/about".
  /// \returns The response body.
  /// \throws HttpError if the request fails or the status is not 2xx.
  std::string get(const std::string& path);

  /// Sends a POST request with a JSON body. Parameters, return value and
  /// errors are as for get().
  std::string post(const std::string& path, const std::string& json_body);

  /// Same as the two-argument post(), with \p timeout in place of the
  /// default request limit.
  std::string post(const std::string& path, const std::string& json_body,
                   std::chrono::milliseconds timeout);

 private:
  struct CurlDeleter {
    void operator()(CURL* handle) const noexcept;
    void operator()(curl_slist* list) const noexcept;
  };

  std::string perform(const char* method, const std::string& path,
                      std::chrono::milliseconds timeout);

  std::unique_ptr<CURL, CurlDeleter> handle_;
  std::unique_ptr<curl_slist, CurlDeleter> json_headers_;
  std::string base_url_;
  std::chrono::milliseconds timeout_;
};

}  // namespace waterlinked::sonar::detail

#endif  // WATERLINKEDSONAR_HTTP_HTTP_TRANSPORT_HPP
