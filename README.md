# waterlinkedsonar

[![CI](https://github.com/BumblebeeAS/waterlinkedsonar/actions/workflows/ci.yml/badge.svg)](https://github.com/BumblebeeAS/waterlinkedsonar/actions/workflows/ci.yml)
[![Docs](https://github.com/BumblebeeAS/waterlinkedsonar/actions/workflows/docs.yml/badge.svg)](https://bumblebeeas.github.io/waterlinkedsonar/)
[![Release](https://img.shields.io/github/v/release/BumblebeeAS/waterlinkedsonar)](https://github.com/BumblebeeAS/waterlinkedsonar/releases)
[![License](https://img.shields.io/github/license/BumblebeeAS/waterlinkedsonar)](LICENSE)

C++17 client library for the [Water Linked Sonar 3D-15](https://docs.waterlinked.com/sonar-3d/sonar-3d-15-introduction/).

- Configuration over the sonar's HTTP API: acoustics, imaging range and mode, salinity, speed of sound, UDP and IMU batch output, time and NTP, plus device identity and status.
- UDP receiver that decodes the Range Image Protocol stream (range images, signal-strength or shaded bitmap images, IMU batches) and passes the messages to callbacks.
- Conversions from range images to distances and point clouds, and from signal-strength images to linear strength.

Requires sonar firmware 1.5.1 or newer.

## Build

```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev libsnappy-dev zlib1g-dev \
    nlohmann-json3-dev libprotobuf-dev protobuf-compiler libgtest-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
sudo cmake --install build
```

`-DBUILD_TESTING=OFF` and `-DWATERLINKEDSONAR_BUILD_EXAMPLES=OFF` skip the tests and the examples. The package also builds with colcon.

## Use in your project

```cmake
find_package(waterlinkedsonar REQUIRED)
target_link_libraries(your_target PRIVATE waterlinkedsonar::waterlinkedsonar)
```

| Header | Contents |
| --- | --- |
| `waterlinkedsonar/http/client.hpp` | `SonarClient` for the HTTP API |
| `waterlinkedsonar/http/types.hpp` | Data types of the HTTP API |
| `waterlinkedsonar/http/errors.hpp` | Exceptions thrown by `SonarClient` |
| `waterlinkedsonar/udp/receiver.hpp` | `Receiver`, which decodes the data stream on a thread |
| `waterlinkedsonar/udp/socket.hpp` | `UdpSocket` for the data stream |
| `waterlinkedsonar/rip/decoder.hpp` | `rip::Decoder` for single packets |
| `waterlinkedsonar/rip/messages.hpp` | Views of decoded range images, bitmap images and IMU batches |
| `waterlinkedsonar/rip/conversions.hpp` | Distance, point cloud and strength conversions |
| `waterlinkedsonar/span.hpp` | `Span`, the view type used by the API |
| `waterlinkedsonar/ntp/sntp.hpp` | `sntp_query()` for checking an NTP server |

Complete programs are in [`examples/`](examples/), and the API reference is at [bumblebeeas.github.io/waterlinkedsonar](https://bumblebeeas.github.io/waterlinkedsonar/).

### Configure the sonar

```cpp
#include <waterlinkedsonar/http/client.hpp>

waterlinked::sonar::SonarClient client("192.168.194.96");
client.set_range({0.5, 10.0});
client.set_acoustics_enabled(true);
```

### Receive data

```cpp
#include <waterlinkedsonar/rip/conversions.hpp>
#include <waterlinkedsonar/udp/receiver.hpp>

using namespace waterlinked::sonar;

Receiver receiver{UdpSocket(UdpSocketConfig{})};
std::vector<float> points;
receiver.on_range_image([&](const RangeImageView& image) {
  range_image_to_points(image, points, PointLayout::UNORGANIZED);
});
receiver.start();
```

Callbacks run on the receiver's thread. The views they receive are valid until the callback returns, so copy any data you keep.

## Attribution

The protocol definition ([`proto/WaterLinkedSonarIntegrationProtocol.proto`](proto/WaterLinkedSonarIntegrationProtocol.proto)) and the test recording ([`test/data/ship_short.sonar`](test/data/ship_short.sonar)) are copied from Water Linked's [wlsonar](https://github.com/waterlinked/wlsonar) under the MIT license in [`proto/LICENSE`](proto/LICENSE) and [`test/data/LICENSE`](test/data/LICENSE).

[`include/waterlinkedsonar/detail/tcb_span.hpp`](include/waterlinkedsonar/detail/tcb_span.hpp) is Tristan Brindle's [span](https://github.com/tcbrindle/span) implementation (Boost Software License 1.0).

## License

MIT. See [LICENSE](LICENSE).
