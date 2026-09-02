#include "http/http_transport.hpp"

#include <mutex>
#include <string>
#include <waterlinkedsonar/http/errors.hpp>

namespace waterlinked::sonar::detail {

namespace {

std::size_t append_to_string(char* data, std::size_t size, std::size_t count,
                             void* body) {
  static_cast<std::string*>(body)->append(data, size * count);
  return size * count;
}

}  // namespace

void HttpTransport::CurlDeleter::operator()(CURL* handle) const noexcept {
  curl_easy_cleanup(handle);
}

void HttpTransport::CurlDeleter::operator()(curl_slist* list) const noexcept {
  curl_slist_free_all(list);
}

HttpTransport::HttpTransport(const std::string& host, std::uint16_t port,
                             std::chrono::milliseconds timeout)
    : base_url_("http://" + host + ":" + std::to_string(port)),
      timeout_(timeout) {
  // curl_global_init is not thread-safe before libcurl 7.84.
  static std::once_flag curl_initialized;
  std::call_once(curl_initialized,
                 [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

  handle_.reset(curl_easy_init());
  json_headers_.reset(
      curl_slist_append(nullptr, "Content-Type: application/json"));
  if (!handle_ || !json_headers_) {
    throw HttpError("cannot initialize libcurl", 0);
  }
}

std::string HttpTransport::get(const std::string& path) {
  curl_easy_reset(handle_.get());
  return perform("GET", path, timeout_);
}

std::string HttpTransport::post(const std::string& path,
                                const std::string& json_body) {
  return post(path, json_body, timeout_);
}

std::string HttpTransport::post(const std::string& path,
                                const std::string& json_body,
                                std::chrono::milliseconds timeout) {
  curl_easy_reset(handle_.get());
  curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDS, json_body.c_str());
  curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(json_body.size()));
  curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, json_headers_.get());
  return perform("POST", path, timeout);
}

std::string HttpTransport::perform(const char* method, const std::string& path,
                                   std::chrono::milliseconds timeout) {
  std::string body;
  CURL* handle = handle_.get();
  curl_easy_setopt(handle, CURLOPT_URL, (base_url_ + path).c_str());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_to_string);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(timeout.count()));
  // Without this, libcurl implements timeouts with SIGALRM, which is unsafe
  // in a multithreaded process.
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

  const std::string request = std::string(method) + " " + path;
  const CURLcode result = curl_easy_perform(handle);
  if (result != CURLE_OK) {
    throw HttpError(request + ": " + curl_easy_strerror(result), 0);
  }
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  if (status < 200 || status >= 300) {
    throw HttpError(request + " returned " + std::to_string(status) +
                        (body.empty() ? "" : ": " + body),
                    static_cast<int>(status));
  }
  return body;
}

}  // namespace waterlinked::sonar::detail
