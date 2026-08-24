#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <waterlinkedsonar/client.hpp>

namespace waterlinked::sonar {

namespace {

struct Request {
  std::string method;
  std::string path;
  std::string body;
  std::optional<std::chrono::milliseconds> timeout_override;
};

class FakeTransport : public HttpTransport {
 public:
  Response get(const std::string& path) override {
    requests.push_back({"GET", path, "", std::nullopt});
    return next();
  }
  Response post(
      const std::string& path, const std::string& json_body,
      std::optional<std::chrono::milliseconds> timeout_override) override {
    requests.push_back({"POST", path, json_body, timeout_override});
    return next();
  }

  void queue(long status, std::string body) {
    responses.push_back({status, std::move(body)});
  }

  std::vector<Request> requests;
  std::deque<Response> responses;

 private:
  Response next() {
    if (responses.empty()) {
      ADD_FAILURE() << "no queued response";
      return {500, ""};
    }
    Response r = std::move(responses.front());
    responses.pop_front();
    return r;
  }
};

std::string about_body(const std::string& version_short) {
  nlohmann::json j = {
      {"chipid", "0x12345678"},
      {"hardware_revision", 6},
      {"is_ready", true},
      {"product_id", 21045},
      {"product_name", "Sonar 3D-15"},
      {"variant", ""},
      {"version", version_short + " (build)"},
      {"version_short", version_short},
  };
  return j.dump();
}

// Constructs a client whose device reports the given firmware, leaving the
// transport available for queuing further responses.
std::pair<SonarClient, FakeTransport*> make_client(
    const std::string& firmware) {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport* raw = transport.get();
  transport->queue(200, about_body(firmware));
  return {SonarClient(std::move(transport)), raw};
}

}  // namespace

TEST(Client, ConstructorFetchesAboutAndCapturesFirmware) {
  auto [client, transport] = make_client("1.8.0");
  EXPECT_EQ(client.firmware_version(), "1.8.0");
  ASSERT_EQ(transport->requests.size(), 1U);
  EXPECT_EQ(transport->requests[0].method, "GET");
  EXPECT_EQ(transport->requests[0].path, "/api/v1/integration/about");
}

TEST(Client, ConstructorRejectsOldFirmware) {
  auto transport = std::make_unique<FakeTransport>();
  transport->queue(200, about_body("1.4.0"));
  EXPECT_THROW(SonarClient(std::move(transport)), VersionError);
}

TEST(Client, ConstructorPropagatesHttpError) {
  auto transport = std::make_unique<FakeTransport>();
  transport->queue(500, "");
  EXPECT_THROW(SonarClient(std::move(transport)), HttpError);
}

TEST(Client, AboutRejectsMalformedBody) {
  auto [client, transport] = make_client("1.8.0");
  transport->queue(200, R"({"chipid": 5})");
  EXPECT_THROW(client.about(), ProtocolError);
  transport->queue(200, "not json");
  EXPECT_THROW(client.about(), ProtocolError);
}

TEST(Client, GatedMethodsThrowBeforeAnyTraffic) {
  auto [client, transport] = make_client("1.5.1");
  const std::size_t before = transport->requests.size();
  EXPECT_THROW(client.status(), VersionError);
  EXPECT_THROW(client.mode(), VersionError);
  EXPECT_THROW(client.set_mode(AcousticsMode::HIGH_FREQUENCY), VersionError);
  EXPECT_THROW(client.salinity(), VersionError);
  EXPECT_THROW(client.set_salinity(Salinity::FRESH), VersionError);
  EXPECT_THROW(client.time_status(), VersionError);
  EXPECT_THROW(client.ntp_address(), VersionError);
  EXPECT_THROW(client.set_ntp_address("auto"), VersionError);
  EXPECT_THROW(client.force_sync_ntp(std::chrono::seconds(1)), VersionError);
  EXPECT_THROW(client.imu_batch_enabled(), VersionError);
  EXPECT_THROW(client.set_imu_batch_enabled(true), VersionError);
  EXPECT_EQ(transport->requests.size(), before);
}

TEST(Client, SettersSendJsonAndAccept204) {
  auto [client, transport] = make_client("1.8.0");

  transport->queue(204, "");
  client.set_acoustics_enabled(true);
  EXPECT_EQ(transport->requests.back().path,
            "/api/v1/integration/acoustics/enabled");
  EXPECT_EQ(transport->requests.back().body, "true");

  transport->queue(204, "");
  client.set_speed_of_sound(0.0);
  EXPECT_EQ(transport->requests.back().path,
            "/api/v1/integration/acoustics/speed_of_sound");
  EXPECT_EQ(transport->requests.back().body, "0.0");

  transport->queue(204, "");
  client.set_range({0.5, 10.0});
  EXPECT_EQ(nlohmann::json::parse(transport->requests.back().body),
            (nlohmann::json{{"min", 0.5}, {"max", 10.0}}));

  transport->queue(204, "");
  client.set_mode(AcousticsMode::HIGH_FREQUENCY);
  EXPECT_EQ(transport->requests.back().body, "\"high-frequency\"");

  transport->queue(204, "");
  client.set_salinity(Salinity::FRESH);
  EXPECT_EQ(transport->requests.back().body, "\"fresh\"");
}

TEST(Client, GettersParseBareValues) {
  auto [client, transport] = make_client("1.8.0");

  transport->queue(200, "true");
  EXPECT_TRUE(client.acoustics_enabled());

  transport->queue(200, "23.5");
  EXPECT_DOUBLE_EQ(client.temperature(), 23.5);

  transport->queue(200, "1500");
  EXPECT_DOUBLE_EQ(client.speed_of_sound(), 1500.0);

  transport->queue(200, "\"low-frequency\"");
  EXPECT_EQ(client.mode(), AcousticsMode::LOW_FREQUENCY);

  transport->queue(200, "\"salt\"");
  EXPECT_EQ(client.salinity(), Salinity::SALT);

  transport->queue(200, R"({"min": 0.3, "max": 15.0})");
  const Range r = client.range();
  EXPECT_DOUBLE_EQ(r.min, 0.3);
  EXPECT_DOUBLE_EQ(r.max, 15.0);

  transport->queue(200, "\"sideways\"");
  EXPECT_THROW(client.mode(), ProtocolError);
}

TEST(Client, UdpConfigRoundTrip) {
  auto [client, transport] = make_client("1.8.0");

  transport->queue(200,
                   R"({"mode": "unicast", "unicast_destination_ip": "10.0.0.2",
                       "unicast_destination_port": 4747})");
  const UdpConfig cfg = client.udp_config();
  EXPECT_EQ(cfg.mode, UdpConfig::Mode::UNICAST);
  EXPECT_EQ(cfg.unicast_destination_ip, "10.0.0.2");
  EXPECT_EQ(cfg.unicast_destination_port, 4747);

  transport->queue(204, "");
  client.set_udp_config(cfg);
  EXPECT_EQ(nlohmann::json::parse(transport->requests.back().body),
            (nlohmann::json{{"mode", "unicast"},
                            {"unicast_destination_ip", "10.0.0.2"},
                            {"unicast_destination_port", 4747}}));
}

TEST(Client, StatusIncludesTimeEntryFrom171) {
  const std::string entry =
      R"({"id": "x", "message": "m", "operational": true, "status": "ok"})";
  const std::string body = R"({"api": )" + entry + R"(, "temperature": )" +
                           entry + R"(, "systems_check": )" + entry +
                           R"(, "time": )" + entry + "}";

  {
    auto [client, transport] = make_client("1.7.0");
    transport->queue(200, body);
    EXPECT_FALSE(client.status().time.has_value());
  }
  {
    auto [client, transport] = make_client("1.7.1");
    transport->queue(200, body);
    EXPECT_TRUE(client.status().time.has_value());
  }
}

TEST(Client, TimeStatusParsesRfc3339) {
  auto [client, transport] = make_client("1.8.0");
  transport->queue(200,
                   R"({"system_time": "2026-08-17T12:00:00.500000Z",
                       "ntp_synced": true, "ntp_synced_to": "192.168.194.51",
                       "ntp_seconds_since_last_sync": 12})");
  const TimeStatus ts = client.time_status();
  EXPECT_TRUE(ts.ntp_synced);
  EXPECT_EQ(ts.ntp_synced_to, "192.168.194.51");
  ASSERT_TRUE(ts.ntp_seconds_since_last_sync.has_value());
  EXPECT_EQ(*ts.ntp_seconds_since_last_sync, 12);

  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          ts.system_time.time_since_epoch())
                          .count();
  // 2026-08-17T12:00:00.5Z
  EXPECT_EQ(micros, 1786968000500000LL);
}

TEST(Client, SetTimeManualMaps409) {
  auto [client, transport] = make_client("1.8.0");
  transport->queue(409, "");
  EXPECT_THROW(client.set_time_manual(std::chrono::system_clock::now()),
               TimeAlreadySynchronizedError);
}

TEST(Client, ForceSyncMaps409AndExtendsTimeout) {
  auto [client, transport] = make_client("1.8.0");

  transport->queue(409, "");
  EXPECT_THROW(client.force_sync_ntp(std::chrono::seconds(3)),
               ForceSyncOngoingError);

  transport->queue(200,
                   R"({"success": true, "message": "synced",
                       "status": {"system_time": "2026-08-17T12:00:00Z",
                                  "ntp_synced": true, "ntp_synced_to": "x",
                                  "ntp_seconds_since_last_sync": null}})");
  const ForceSyncResult r = client.force_sync_ntp(std::chrono::seconds(3));
  EXPECT_TRUE(r.success);
  EXPECT_FALSE(r.status.ntp_seconds_since_last_sync.has_value());
  ASSERT_TRUE(transport->requests.back().timeout_override.has_value());
  EXPECT_GT(*transport->requests.back().timeout_override,
            std::chrono::seconds(3));
}

TEST(Client, NonSuccessStatusBecomesHttpError) {
  auto [client, transport] = make_client("1.8.0");
  transport->queue(500, "boom");
  try {
    client.temperature();
    FAIL() << "expected HttpError";
  } catch (const HttpError& e) {
    EXPECT_EQ(e.status_code(), 500);
  }
}

}  // namespace waterlinked::sonar
