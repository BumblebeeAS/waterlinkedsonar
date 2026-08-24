#ifndef WATERLINKEDSONAR_HTTP_TRANSPORT_HPP
#define WATERLINKEDSONAR_HTTP_TRANSPORT_HPP

#include <chrono>
#include <optional>
#include <string>

namespace waterlinked::sonar {

/**
 * @brief Blocking HTTP transport.
 *
 * Implementations throw HttpError with status_code() == 0 when a request
 * cannot complete; non-2xx statuses are returned, not thrown.
 */
class HttpTransport {
 public:
  struct Response {
    long status;
    std::string body;
  };

  virtual ~HttpTransport() = default;

  /**
   * @param[in] path Relative path, e.g. "/api/v1/integration/about".
   * @return Status and body.
   * @throws HttpError When the request cannot complete.
   */
  virtual Response get(const std::string& path) = 0;

  /**
   * @param[in] path Relative path.
   * @param[in] json_body Request body, sent as application/json.
   * @param[in] timeout_override Replaces the transport's default timeout for
   *            this one request; needed for endpoints whose server-side work
   *            is caller-bounded (NTP force-sync).
   * @return Status and body.
   * @throws HttpError When the request cannot complete.
   */
  virtual Response post(const std::string& path, const std::string& json_body,
                        std::optional<std::chrono::milliseconds>
                            timeout_override = std::nullopt) = 0;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_HTTP_TRANSPORT_HPP
