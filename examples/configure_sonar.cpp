// Reads the sonar's configuration over HTTP, then enables acoustics and
// multicast output. Usage: configure_sonar [ip]
#include <iostream>
#include <waterlinkedsonar/client.hpp>

int main(int argc, char* argv[]) {
  const std::string ip = argc > 1 ? argv[1] : waterlinked::sonar::DEFAULT_IP;

  try {
    waterlinked::sonar::SonarClient client(ip);

    const auto about = client.about();
    std::cout << "Connected: " << about.product_name
              << " (chipid=" << about.chipid << ", fw=" << about.version_short
              << ")\n";
    std::cout << "Temperature: " << client.temperature() << " C\n";
    std::cout << "Acoustics enabled: " << client.acoustics_enabled() << "\n";
    const auto range = client.range();
    std::cout << "Range: [" << range.min << ", " << range.max << "] m\n";
    std::cout << "Speed of sound: " << client.speed_of_sound()
              << " m/s (0 = automatic)\n";
    const auto udp = client.udp_config();
    std::cout << "UDP mode: " << waterlinked::sonar::to_string(udp.mode)
              << "\n";

    client.set_acoustics_enabled(true);
    waterlinked::sonar::UdpConfig multicast;
    multicast.mode = waterlinked::sonar::UdpConfig::Mode::MULTICAST;
    client.set_udp_config(multicast);
    std::cout << "Enabled acoustics and multicast output\n";
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
