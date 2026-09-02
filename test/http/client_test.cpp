#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <waterlinkedsonar/http/client.hpp>

#include "support/fake_http_server.hpp"

namespace waterlinked::sonar {
namespace {

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

std::string about_body(const std::string& firmware) {
  return nlohmann::json{
      {"chipid", "0x12345678"},
      {"hardware_revision", 6},
      {"is_ready", true},
      {"product_id", 21045},
      {"product_name", "Sonar 3D-15"},
      {"variant", ""},
      {"version", firmware + " (build)"},
      {"version_short", firmware},
  }
      .dump();
}

std::string time_status_body(const std::string& system_time) {
  return nlohmann::json{{"system_time", system_time},
                        {"ntp_synced", false},
                        {"ntp_synced_to", ""},
                        {"ntp_seconds_since_last_sync", nullptr}}
      .dump();
}

std::int64_t micros_since_epoch(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             time.time_since_epoch())
      .count();
}

class ClientTest : public ::testing::Test {
 protected:
  SonarClient connect(const std::string& firmware = "1.8.0",
                      std::chrono::milliseconds timeout = 2s) {
    server_.respond(200, about_body(firmware));
    return SonarClient("127.0.0.1", server_.port(), timeout);
  }

  test::FakeHttpServer server_;
};

TEST_F(ClientTest, ConstructorReadsFirmwareVersion) {
  const SonarClient client = connect("1.8.0");
  EXPECT_EQ(client.firmware_version(), "1.8.0");
  ASSERT_EQ(server_.requests().size(), 1U);
  EXPECT_EQ(server_.last_request().method, "GET");
  EXPECT_EQ(server_.last_request().path, "/api/v1/integration/about");
}

TEST_F(ClientTest, ConstructorRejectsUnsupportedFirmware) {
  EXPECT_THROW(connect("1.4.0"), VersionError);
  EXPECT_THROW(connect("1.8"), ProtocolError);
}

TEST_F(ClientTest, MalformedBodyThrowsProtocolError) {
  SonarClient client = connect();
  server_.respond(200, R"({"chipid": 5})");
  EXPECT_THROW(client.about(), ProtocolError);
  server_.respond(200, "not json");
  EXPECT_THROW(client.about(), ProtocolError);
  server_.respond(200, R"("sideways")");
  EXPECT_THROW(client.mode(), ProtocolError);
  server_.respond(200, "1");
  EXPECT_THROW(client.acoustics_enabled(), ProtocolError);
}

TEST_F(ClientTest, ErrorStatusThrowsHttpError) {
  SonarClient client = connect();
  server_.respond(500, "boom");
  try {
    client.temperature();
    FAIL() << "expected HttpError";
  } catch (const HttpError& e) {
    EXPECT_EQ(e.status_code(), 500);
  }
}

TEST_F(ClientTest, GatedMethodsThrowWithoutRequests) {
  SonarClient client = connect("1.5.1");
  EXPECT_THROW(client.status(), VersionError);
  EXPECT_THROW(client.mode(), VersionError);
  EXPECT_THROW(client.set_mode(AcousticsMode::HIGH_FREQUENCY), VersionError);
  EXPECT_THROW(client.salinity(), VersionError);
  EXPECT_THROW(client.set_salinity(Salinity::FRESH), VersionError);
  EXPECT_THROW(client.time_status(), VersionError);
  EXPECT_THROW(client.set_time_manual({}), VersionError);
  EXPECT_THROW(client.ntp_address(), VersionError);
  EXPECT_THROW(client.set_ntp_address("auto"), VersionError);
  EXPECT_THROW(client.force_sync_ntp(1s), VersionError);
  EXPECT_THROW(client.imu_batch_enabled(), VersionError);
  EXPECT_THROW(client.set_imu_batch_enabled(true), VersionError);
  EXPECT_EQ(server_.requests().size(), 1U);
}

TEST_F(ClientTest, SettersSendApiBodies) {
  SonarClient client = connect();
  const auto expect_post = [&](const std::string& path,
                               const std::string& body) {
    const test::HttpRequest request = server_.last_request();
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.path, "/api/v1/integration" + path);
    EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(body));
  };

  server_.respond(204);
  client.set_acoustics_enabled(true);
  expect_post("/acoustics/enabled", "true");

  server_.respond(204);
  client.set_speed_of_sound(0.0);
  expect_post("/acoustics/speed_of_sound", "0.0");

  server_.respond(204);
  client.set_range({0.5, 10.0});
  expect_post("/acoustics/range", R"({"min":0.5,"max":10.0})");

  server_.respond(204);
  client.set_mode(AcousticsMode::HIGH_FREQUENCY);
  expect_post("/acoustics/mode", R"("high-frequency")");

  server_.respond(204);
  client.set_salinity(Salinity::FRESH);
  expect_post("/acoustics/salinity", R"("fresh")");

  server_.respond(204);
  client.set_udp_config({UdpConfig::Mode::UNICAST, "10.0.0.2", 4747});
  expect_post("/udp", R"({"mode": "unicast",
                          "unicast_destination_ip": "10.0.0.2",
                          "unicast_destination_port": 4747})");

  server_.respond(204);
  client.set_imu_batch_enabled(false);
  expect_post("/output/imu-batch/enabled", "false");

  server_.respond(204);
  client.set_ntp_address("auto");
  expect_post("/time/ntp", R"({"ntp_address":"auto"})");
}

TEST_F(ClientTest, GettersParseResponses) {
  SonarClient client = connect();

  server_.respond(200, "true");
  EXPECT_TRUE(client.acoustics_enabled());
  server_.respond(200, "23.5");
  EXPECT_DOUBLE_EQ(client.temperature(), 23.5);
  server_.respond(200, "1500");
  EXPECT_DOUBLE_EQ(client.speed_of_sound(), 1500.0);
  server_.respond(200, R"("low-frequency")");
  EXPECT_EQ(client.mode(), AcousticsMode::LOW_FREQUENCY);
  server_.respond(200, R"("salt")");
  EXPECT_EQ(client.salinity(), Salinity::SALT);
  server_.respond(200, R"({"min": 0.3, "max": 15.0})");
  const Range range = client.range();
  EXPECT_DOUBLE_EQ(range.min, 0.3);
  EXPECT_DOUBLE_EQ(range.max, 15.0);
  server_.respond(200, R"({"ntp_address": "auto"})");
  EXPECT_EQ(client.ntp_address(), "auto");

  server_.respond(200, R"({"mode": "unicast", "unicast_destination_ip":
                           "10.0.0.2", "unicast_destination_port": 4747})");
  const UdpConfig udp = client.udp_config();
  EXPECT_EQ(udp.mode, UdpConfig::Mode::UNICAST);
  EXPECT_EQ(udp.unicast_destination_ip, "10.0.0.2");
  EXPECT_EQ(udp.unicast_destination_port, 4747);
}

TEST_F(ClientTest, StatusHasTimeEntryFrom171) {
  const std::string entry =
      R"({"id": "x", "message": "m", "operational": true, "status": "ok"})";
  const std::string body = R"({"api": )" + entry + R"(, "temperature": )" +
                           entry + R"(, "systems_check": )" + entry +
                           R"(, "time": )" + entry + "}";
  {
    SonarClient client = connect("1.7.0");
    server_.respond(200, body);
    EXPECT_FALSE(client.status().time.has_value());
  }
  {
    SonarClient client = connect("1.7.1");
    server_.respond(200, body);
    EXPECT_TRUE(client.status().time.has_value());
  }
}

TEST_F(ClientTest, TimeStatusParsesRfc3339) {
  SonarClient client = connect();
  server_.respond(200, R"({"system_time": "2026-08-17T12:00:00.500000Z",
                           "ntp_synced": true,
                           "ntp_synced_to": "192.168.194.51",
                           "ntp_seconds_since_last_sync": 12})");
  const TimeStatus status = client.time_status();
  EXPECT_TRUE(status.ntp_synced);
  EXPECT_EQ(status.ntp_synced_to, "192.168.194.51");
  EXPECT_EQ(status.ntp_seconds_since_last_sync, 12);
  EXPECT_EQ(micros_since_epoch(status.system_time), 1786968000500000);

  const std::int64_t ten_utc = 1786960800000000;  // 2026-08-17T10:00:00Z
  for (const auto& [text, expected] :
       {std::pair{"2026-08-17T10:00:00Z", ten_utc},
        std::pair{"2026-08-17T12:00:00+02:00", ten_utc},
        std::pair{"2026-08-17T04:30:00-05:30", ten_utc},
        std::pair{"2026-08-17T10:00:00.25+00:00", ten_utc + 250000},
        std::pair{"2024-02-29T00:00:00Z", std::int64_t{1709164800000000}},
        std::pair{"1969-12-31T23:59:59Z", std::int64_t{-1000000}}}) {
    server_.respond(200, time_status_body(text));
    const TimeStatus parsed = client.time_status();
    EXPECT_EQ(micros_since_epoch(parsed.system_time), expected) << text;
    EXPECT_FALSE(parsed.ntp_seconds_since_last_sync.has_value());
  }

  for (const char* text :
       {"2026-08-17 10:00", "2026-13-01T00:00:00Z", "2023-02-29T00:00:00Z",
        "2026-04-31T00:00:00Z", "2026-08-17T10:00:00+02"}) {
    server_.respond(200, time_status_body(text));
    EXPECT_THROW(client.time_status(), ProtocolError) << text;
  }
}

TEST_F(ClientTest, SetTimeManualSendsRfc3339Utc) {
  SonarClient client = connect();
  const std::chrono::system_clock::time_point ten_utc{
      std::chrono::microseconds(1786960800000000)};

  server_.respond(204);
  client.set_time_manual(ten_utc + std::chrono::microseconds(250000));
  EXPECT_EQ(server_.last_request().path, "/api/v1/integration/time/manual");
  EXPECT_EQ(server_.last_request().body,
            R"({"now":"2026-08-17T10:00:00.250000Z"})");

  server_.respond(204);
  client.set_time_manual(ten_utc);
  EXPECT_EQ(server_.last_request().body,
            R"({"now":"2026-08-17T10:00:00.000000Z"})");

  server_.respond(204);
  client.set_time_manual(std::chrono::system_clock::time_point(
      std::chrono::microseconds(-1500000)));
  EXPECT_EQ(server_.last_request().body,
            R"({"now":"1969-12-31T23:59:58.500000Z"})");
}

TEST_F(ClientTest, TimeConflictsThrowSpecificErrors) {
  SonarClient client = connect();
  server_.respond(409);
  EXPECT_THROW(client.set_time_manual(std::chrono::system_clock::now()),
               TimeAlreadySynchronizedError);
  server_.respond(409);
  EXPECT_THROW(client.force_sync_ntp(3s), ForceSyncOngoingError);
}

TEST_F(ClientTest, ForceSyncWaitsForSyncTimeout) {
  SonarClient client = connect("1.8.0", 1s);
  server_.respond(200, R"({"success": true, "message": "synced",
                           "status": {"system_time": "2026-08-17T12:00:00Z",
                                      "ntp_synced": true, "ntp_synced_to": "x",
                                      "ntp_seconds_since_last_sync": 0}})",
                  1500ms);
  const ForceSyncResult result = client.force_sync_ntp(2s);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.message, "synced");
  EXPECT_TRUE(result.status.ntp_synced);
  EXPECT_EQ(nlohmann::json::parse(server_.last_request().body),
            nlohmann::json::parse(R"({"timeout_seconds": 2.0})"));
}

}  // namespace
}  // namespace waterlinked::sonar
