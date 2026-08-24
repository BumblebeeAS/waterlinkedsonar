#include <curl/curl.h>

#include <mutex>
#include <waterlinkedsonar/curl_transport.hpp>
#include <waterlinkedsonar/types.hpp>

namespace waterlinked::sonar {

namespace {

void global_init_once() {
  static std::once_flag flag;
  std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t write_body(char* data, std::size_t size, std::size_t nmemb,
                       void* userdata) {
  static_cast<std::string*>(userdata)->append(data, size * nmemb);
  return size * nmemb;
}

}  // namespace

struct CurlTransport::Impl {
  CURL* handle{nullptr};
  curl_slist* json_headers{nullptr};
  std::string base_url;
  std::chrono::milliseconds timeout;
};

CurlTransport::CurlTransport(const std::string& ip, std::uint16_t port,
                             std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>()) {
  global_init_once();
  impl_->handle = curl_easy_init();
  if (impl_->handle == nullptr) {
    throw HttpError("curl_easy_init failed", 0);
  }
  impl_->json_headers =
      curl_slist_append(nullptr, "Content-Type: application/json");
  impl_->base_url = "http://" + ip + ":" + std::to_string(port);
  impl_->timeout = timeout;
}

CurlTransport::~CurlTransport() {
  if (impl_->json_headers != nullptr) {
    curl_slist_free_all(impl_->json_headers);
  }
  if (impl_->handle != nullptr) {
    curl_easy_cleanup(impl_->handle);
  }
}

HttpTransport::Response CurlTransport::get(const std::string& path) {
  CURL* h = impl_->handle;
  curl_easy_reset(h);

  const std::string url = impl_->base_url + path;
  std::string body;
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(impl_->timeout.count()));

  const CURLcode rc = curl_easy_perform(h);
  if (rc != CURLE_OK) {
    throw HttpError("GET " + path + ": " + curl_easy_strerror(rc), 0);
  }
  long status = 0;
  curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
  return {status, std::move(body)};
}

HttpTransport::Response CurlTransport::post(
    const std::string& path, const std::string& json_body,
    std::optional<std::chrono::milliseconds> timeout_override) {
  CURL* h = impl_->handle;
  curl_easy_reset(h);

  const std::string url = impl_->base_url + path;
  const auto timeout = timeout_override.value_or(impl_->timeout);
  std::string body;
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_POST, 1L);
  curl_easy_setopt(h, CURLOPT_POSTFIELDS, json_body.c_str());
  curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(json_body.size()));
  curl_easy_setopt(h, CURLOPT_HTTPHEADER, impl_->json_headers);
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));

  const CURLcode rc = curl_easy_perform(h);
  if (rc != CURLE_OK) {
    throw HttpError("POST " + path + ": " + curl_easy_strerror(rc), 0);
  }
  long status = 0;
  curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
  return {status, std::move(body)};
}

}  // namespace waterlinked::sonar
