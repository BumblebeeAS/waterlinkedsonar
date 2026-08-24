#ifndef WATERLINKEDSONAR_CURL_TRANSPORT_HPP
#define WATERLINKEDSONAR_CURL_TRANSPORT_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <waterlinkedsonar/http_transport.hpp>

namespace waterlinked::sonar {

/**
 * @brief libcurl easy-interface transport: one handle, blocking. Not
 * thread-safe.
 */
class CurlTransport final : public HttpTransport {
 public:
  /**
   * @param[in] ip Device IP address.
   * @param[in] port HTTP port.
   * @param[in] timeout Per-request timeout.
   * @throws HttpError When the curl handle cannot be created.
   */
  CurlTransport(const std::string& ip, std::uint16_t port,
                std::chrono::milliseconds timeout);
  ~CurlTransport() override;

  CurlTransport(const CurlTransport&) = delete;
  CurlTransport& operator=(const CurlTransport&) = delete;

  Response get(const std::string& path) override;
  Response post(const std::string& path, const std::string& json_body,
                std::optional<std::chrono::milliseconds> timeout_override =
                    std::nullopt) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_CURL_TRANSPORT_HPP
