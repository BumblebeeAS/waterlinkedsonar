#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <ratio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <waterlinkedsonar/http/client.hpp>

#include "http/http_transport.hpp"
#include "http/semver.hpp"

namespace waterlinked::sonar {

namespace {

using Json = nlohmann::json;

constexpr const char* API_PREFIX = "/api/v1/integration";
constexpr const char* MIN_FIRMWARE = "1.5.1";

std::string endpoint(const char* name) {
  return std::string(API_PREFIX) + name;
}

// Response body whose accessors throw ProtocolError on a type mismatch.
class Reply {
 public:
  Reply(Json body, std::string path)
      : body_(std::move(body)), path_(std::move(path)) {}

  template <typename T>
  [[nodiscard]] T as() const {
    if (!holds<T>(body_)) {
      throw ProtocolError(path_ + ": unexpected response " + body_.dump());
    }
    return body_.get<T>();
  }

  [[nodiscard]] Reply operator[](const char* key) const {
    if (!body_.is_object() || !body_.contains(key)) {
      throw ProtocolError(path_ + ": response lacks \"" + key + "\"");
    }
    return {body_[key], path_ + "." + key};
  }

  [[nodiscard]] bool has(const char* key) const {
    return body_.is_object() && body_.contains(key) && !body_[key].is_null();
  }

 private:
  template <typename T>
  static bool holds(const Json& value) {
    if constexpr (std::is_same_v<T, bool>) {
      return value.is_boolean();
    } else if constexpr (std::is_same_v<T, std::string>) {
      return value.is_string();
    } else if constexpr (std::is_integral_v<T>) {
      return value.is_number_integer();
    } else {
      return value.is_number();
    }
  }

  Json body_;
  std::string path_;
};

Reply parse_reply(const std::string& path, const std::string& body) {
  if (body.empty()) {
    return {Json(), path};
  }
  Json parsed = Json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    throw ProtocolError(path + ": response is not JSON");
  }
  return {std::move(parsed), path};
}

using Days = std::chrono::duration<std::int64_t, std::ratio<86400>>;

struct CivilDate {
  int year;
  unsigned month;
  unsigned day;
};

// days_from_civil() and civil_from_days() are Howard Hinnant's algorithms
// for the proleptic Gregorian calendar
// (https://howardhinnant.github.io/date_algorithms.html).
Days days_from_civil(CivilDate date) {
  const int year = date.year - (date.month <= 2 ? 1 : 0);
  const int era = (year >= 0 ? year : year - 399) / 400;
  const auto year_of_era = static_cast<unsigned>(year - (era * 400));
  const unsigned day_of_year =
      (((153 * (date.month > 2 ? date.month - 3 : date.month + 9)) + 2) / 5) +
      date.day - 1;
  const unsigned day_of_era = (year_of_era * 365) + (year_of_era / 4) -
                              (year_of_era / 100) + day_of_year;
  return Days((era * 146097LL) + day_of_era - 719468);
}

CivilDate civil_from_days(Days days) {
  const std::int64_t shifted = days.count() + 719468;
  const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
  const auto day_of_era = static_cast<unsigned>(shifted - (era * 146097));
  const unsigned year_of_era = (day_of_era - (day_of_era / 1460) +
                                (day_of_era / 36524) - (day_of_era / 146096)) /
                               365;
  const unsigned day_of_year =
      day_of_era -
      ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
  const unsigned month_index = ((5 * day_of_year) + 2) / 153;
  const unsigned month = month_index < 10 ? month_index + 3 : month_index - 9;
  return {static_cast<int>(year_of_era + (era * 400) + (month <= 2 ? 1 : 0)),
          month, day_of_year - (((153 * month_index) + 2) / 5) + 1};
}

std::chrono::system_clock::time_point parse_rfc3339(const std::string& text) {
  const auto invalid = [&] {
    return ProtocolError("invalid RFC 3339 timestamp \"" + text + "\"");
  };
  CivilDate date{};
  int hour = 0;
  int minute = 0;
  int second = 0;
  int consumed = 0;
  // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
  if (std::sscanf(text.c_str(), "%4d-%2u-%2uT%2d:%2d:%2d%n", &date.year,
                  &date.month, &date.day, &hour, &minute, &second,
                  &consumed) != 6) {
    throw invalid();
  }
  const Days days = days_from_civil(date);
  const CivilDate normalized = civil_from_days(days);
  if (normalized.year != date.year || normalized.month != date.month ||
      normalized.day != date.day) {
    throw invalid();
  }

  auto pos = static_cast<std::size_t>(consumed);
  std::chrono::microseconds fraction{0};
  if (pos < text.size() && text[pos] == '.') {
    std::int64_t scale = 100000;
    for (++pos; pos < text.size() && text[pos] >= '0' && text[pos] <= '9';
         ++pos) {
      fraction += std::chrono::microseconds((text[pos] - '0') * scale);
      scale /= 10;
    }
  }

  std::chrono::minutes offset{0};
  if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
    ++pos;
  } else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    unsigned offset_hours = 0;
    unsigned offset_minutes = 0;
    int offset_length = 0;
    // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
    if (std::sscanf(text.c_str() + pos + 1, "%2u:%2u%n", &offset_hours,
                    &offset_minutes, &offset_length) != 2) {
      throw invalid();
    }
    offset =
        std::chrono::hours(offset_hours) + std::chrono::minutes(offset_minutes);
    if (text[pos] == '-') {
      offset = -offset;
    }
    pos += 1 + static_cast<std::size_t>(offset_length);
  }
  if (pos != text.size()) {
    throw invalid();
  }

  return std::chrono::system_clock::time_point(
      days + std::chrono::hours(hour) + std::chrono::minutes(minute) +
      std::chrono::seconds(second) + fraction - offset);
}

std::string format_rfc3339_utc(std::chrono::system_clock::time_point time) {
  const auto micros =
      std::chrono::floor<std::chrono::microseconds>(time.time_since_epoch());
  const auto days = std::chrono::floor<Days>(micros);
  const CivilDate date = civil_from_days(days);
  const auto time_of_day = micros - days;
  const auto hours =
      std::chrono::duration_cast<std::chrono::hours>(time_of_day);
  const auto minutes =
      std::chrono::duration_cast<std::chrono::minutes>(time_of_day - hours);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
      time_of_day - hours - minutes);
  const auto subseconds = time_of_day - hours - minutes - seconds;

  std::array<char, 40> text{};
  const int length = std::snprintf(
      text.data(), text.size(), "%04d-%02u-%02uT%02d:%02d:%02d.%06dZ",
      date.year, date.month, date.day, static_cast<int>(hours.count()),
      static_cast<int>(minutes.count()), static_cast<int>(seconds.count()),
      static_cast<int>(subseconds.count()));
  return {text.data(), static_cast<std::size_t>(length)};
}

Reply get(detail::HttpTransport& transport, const char* name) {
  const std::string path = endpoint(name);
  return parse_reply(path, transport.get(path));
}

void post(detail::HttpTransport& transport, const char* name,
          const Json& payload) {
  transport.post(endpoint(name), payload.dump());
}

StatusEntry parse_status_entry(const Reply& reply) {
  return {reply["id"].as<std::string>(), reply["message"].as<std::string>(),
          reply["status"].as<std::string>(), reply["operational"].as<bool>()};
}

TimeStatus parse_time_status(const Reply& reply) {
  TimeStatus status{
      parse_rfc3339(reply["system_time"].as<std::string>()),
      reply["ntp_synced"].as<bool>(),
      reply["ntp_synced_to"].as<std::string>(),
      std::nullopt,
  };
  if (reply.has("ntp_seconds_since_last_sync")) {
    status.ntp_seconds_since_last_sync =
        reply["ntp_seconds_since_last_sync"].as<std::int64_t>();
  }
  return status;
}

}  // namespace

SonarClient::SonarClient(const std::string& host, std::uint16_t port,
                         std::chrono::milliseconds timeout)
    : transport_(std::make_unique<detail::HttpTransport>(host, port, timeout)),
      firmware_version_(about().version_short) {
  try {
    require_firmware("SonarClient", MIN_FIRMWARE);
  } catch (const std::invalid_argument&) {
    throw ProtocolError(endpoint("/about") + ": invalid version_short \"" +
                        firmware_version_ + "\"");
  }
}

SonarClient::~SonarClient() = default;
SonarClient::SonarClient(SonarClient&&) noexcept = default;
SonarClient& SonarClient::operator=(SonarClient&&) noexcept = default;

void SonarClient::require_firmware(const char* operation,
                                   const char* min_version) const {
  if (detail::semver_less(firmware_version_, min_version)) {
    throw VersionError(operation, min_version, firmware_version_);
  }
}

About SonarClient::about() {
  const Reply reply = get(*transport_, "/about");
  return {
      reply["chipid"].as<std::string>(),
      reply["hardware_revision"].as<int>(),
      reply["is_ready"].as<bool>(),
      reply["product_id"].as<int>(),
      reply["product_name"].as<std::string>(),
      reply["variant"].as<std::string>(),
      reply["version"].as<std::string>(),
      reply["version_short"].as<std::string>(),
  };
}

Status SonarClient::status() {
  require_firmware("status", "1.7.0");
  const Reply reply = get(*transport_, "/status");
  Status status{
      parse_status_entry(reply["api"]),
      parse_status_entry(reply["temperature"]),
      parse_status_entry(reply["systems_check"]),
      std::nullopt,
  };
  if (!detail::semver_less(firmware_version_, "1.7.1")) {
    status.time = parse_status_entry(reply["time"]);
  }
  return status;
}

double SonarClient::temperature() {
  return get(*transport_, "/temperature").as<double>();
}

bool SonarClient::acoustics_enabled() {
  return get(*transport_, "/acoustics/enabled").as<bool>();
}

void SonarClient::set_acoustics_enabled(bool enabled) {
  post(*transport_, "/acoustics/enabled", enabled);
}

Range SonarClient::range() {
  const Reply reply = get(*transport_, "/acoustics/range");
  return {reply["min"].as<double>(), reply["max"].as<double>()};
}

void SonarClient::set_range(Range range) {
  post(*transport_, "/acoustics/range",
       {{"min", range.min}, {"max", range.max}});
}

double SonarClient::speed_of_sound() {
  return get(*transport_, "/acoustics/speed_of_sound").as<double>();
}

void SonarClient::set_speed_of_sound(double speed) {
  post(*transport_, "/acoustics/speed_of_sound", speed);
}

AcousticsMode SonarClient::mode() {
  require_firmware("mode", "1.7.0");
  const auto mode = get(*transport_, "/acoustics/mode").as<std::string>();
  for (const AcousticsMode candidate :
       {AcousticsMode::LOW_FREQUENCY, AcousticsMode::HIGH_FREQUENCY}) {
    if (mode == to_string(candidate)) {
      return candidate;
    }
  }
  throw ProtocolError(endpoint("/acoustics/mode") + ": unknown mode \"" + mode +
                      "\"");
}

void SonarClient::set_mode(AcousticsMode mode) {
  require_firmware("set_mode", "1.7.0");
  post(*transport_, "/acoustics/mode", to_string(mode));
}

Salinity SonarClient::salinity() {
  require_firmware("salinity", "1.7.0");
  const auto salinity =
      get(*transport_, "/acoustics/salinity").as<std::string>();
  for (const Salinity candidate : {Salinity::SALT, Salinity::FRESH}) {
    if (salinity == to_string(candidate)) {
      return candidate;
    }
  }
  throw ProtocolError(endpoint("/acoustics/salinity") +
                      ": unknown salinity \"" + salinity + "\"");
}

void SonarClient::set_salinity(Salinity salinity) {
  require_firmware("set_salinity", "1.7.0");
  post(*transport_, "/acoustics/salinity", to_string(salinity));
}

UdpConfig SonarClient::udp_config() {
  const Reply reply = get(*transport_, "/udp");
  const auto mode = reply["mode"].as<std::string>();
  for (const UdpConfig::Mode candidate :
       {UdpConfig::Mode::MULTICAST, UdpConfig::Mode::UNICAST,
        UdpConfig::Mode::DISABLED}) {
    if (mode == to_string(candidate)) {
      return {candidate, reply["unicast_destination_ip"].as<std::string>(),
              reply["unicast_destination_port"].as<std::uint16_t>()};
    }
  }
  throw ProtocolError(endpoint("/udp") + ": unknown mode \"" + mode + "\"");
}

void SonarClient::set_udp_config(const UdpConfig& config) {
  post(*transport_, "/udp",
       {{"mode", to_string(config.mode)},
        {"unicast_destination_ip", config.unicast_destination_ip},
        {"unicast_destination_port", config.unicast_destination_port}});
}

bool SonarClient::imu_batch_enabled() {
  require_firmware("imu_batch_enabled", "1.8.0");
  return get(*transport_, "/output/imu-batch/enabled").as<bool>();
}

void SonarClient::set_imu_batch_enabled(bool enabled) {
  require_firmware("set_imu_batch_enabled", "1.8.0");
  post(*transport_, "/output/imu-batch/enabled", enabled);
}

TimeStatus SonarClient::time_status() {
  require_firmware("time_status", "1.7.1");
  return parse_time_status(get(*transport_, "/time/status"));
}

void SonarClient::set_time_manual(std::chrono::system_clock::time_point time) {
  require_firmware("set_time_manual", "1.7.1");
  try {
    post(*transport_, "/time/manual", {{"now", format_rfc3339_utc(time)}});
  } catch (const HttpError& e) {
    if (e.status_code() == 409) {
      throw TimeAlreadySynchronizedError();
    }
    throw;
  }
}

std::string SonarClient::ntp_address() {
  require_firmware("ntp_address", "1.7.1");
  return get(*transport_, "/time/ntp")["ntp_address"].as<std::string>();
}

void SonarClient::set_ntp_address(const std::string& address) {
  require_firmware("set_ntp_address", "1.7.1");
  post(*transport_, "/time/ntp", {{"ntp_address", address}});
}

ForceSyncResult SonarClient::force_sync_ntp(
    std::chrono::duration<double> timeout) {
  require_firmware("force_sync_ntp", "1.7.1");
  const std::string path = endpoint("/time/ntp/force-sync");
  const Json payload = {{"timeout_seconds", timeout.count()}};
  // The device answers once the sync finishes, so the request needs the
  // sync timeout on top of the usual one.
  const auto request_timeout =
      transport_->timeout() +
      std::chrono::ceil<std::chrono::milliseconds>(timeout);
  std::string body;
  try {
    body = transport_->post(path, payload.dump(), request_timeout);
  } catch (const HttpError& e) {
    if (e.status_code() == 409) {
      throw ForceSyncOngoingError();
    }
    throw;
  }
  const Reply reply = parse_reply(path, body);
  return {reply["success"].as<bool>(), reply["message"].as<std::string>(),
          parse_time_status(reply["status"])};
}

}  // namespace waterlinked::sonar
