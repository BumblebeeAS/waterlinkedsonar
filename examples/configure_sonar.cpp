// Prints the sonar's configuration, then enables acoustics and multicast
// output. Usage: configure_sonar [ip]

#include <exception>
#include <iostream>
#include <string>
#include <waterlinkedsonar/http/client.hpp>

namespace sonar = waterlinked::sonar;

int main(int argc, char* argv[]) {
  const std::string ip = argc > 1 ? argv[1] : sonar::DEFAULT_IP;
  try {
    sonar::SonarClient client(ip);

    const sonar::About about = client.about();
    std::cout << about.product_name << ", chip " << about.chipid
              << ", firmware " << about.version_short << "\n"
              << "Temperature: " << client.temperature() << " C\n"
              << "Acoustics enabled: " << std::boolalpha
              << client.acoustics_enabled() << "\n";
    const sonar::Range range = client.range();
    std::cout << "Range: " << range.min << " to " << range.max << " m\n"
              << "Speed of sound: " << client.speed_of_sound() << " m/s\n"
              << "UDP mode: " << sonar::to_string(client.udp_config().mode)
              << "\n";

    client.set_acoustics_enabled(true);
    sonar::UdpConfig multicast;
    multicast.mode = sonar::UdpConfig::Mode::MULTICAST;
    client.set_udp_config(multicast);
    std::cout << "Enabled acoustics and multicast output\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
