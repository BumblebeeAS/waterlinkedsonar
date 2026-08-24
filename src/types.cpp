#include <waterlinkedsonar/types.hpp>

namespace waterlinked::sonar {

const char* to_string(AcousticsMode mode) {
  switch (mode) {
    case AcousticsMode::LOW_FREQUENCY:
      return "low-frequency";
    case AcousticsMode::HIGH_FREQUENCY:
      return "high-frequency";
  }
  return "?";
}

const char* to_string(Salinity salinity) {
  switch (salinity) {
    case Salinity::SALT:
      return "salt";
    case Salinity::FRESH:
      return "fresh";
  }
  return "?";
}

const char* to_string(UdpConfig::Mode mode) {
  switch (mode) {
    case UdpConfig::Mode::MULTICAST:
      return "multicast";
    case UdpConfig::Mode::UNICAST:
      return "unicast";
    case UdpConfig::Mode::DISABLED:
      return "disabled";
  }
  return "?";
}

}  // namespace waterlinked::sonar
