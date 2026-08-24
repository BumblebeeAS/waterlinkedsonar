#ifndef WATERLINKEDSONAR_RIP_INTERNAL_HPP
#define WATERLINKEDSONAR_RIP_INTERNAL_HPP

#include <cstdint>
#include <vector>

#include "WaterLinkedSonarIntegrationProtocol.pb.h"

namespace waterlinked::sonar {
namespace proto = waterlinked::sonar::protocol;
}  // namespace waterlinked::sonar

namespace waterlinked::sonar::rip {

// Wraps msg in a proto::Packet and frames it as a RIP2 packet (snappy
// compressed, with length and CRC-32). For tests and file tooling.
std::vector<std::uint8_t> encode(const google::protobuf::Message& msg);

}  // namespace waterlinked::sonar::rip

#endif  // WATERLINKEDSONAR_RIP_INTERNAL_HPP
