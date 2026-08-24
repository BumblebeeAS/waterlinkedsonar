# waterlinkedsonar

[![ci](https://github.com/BumblebeeAS/waterlinkedsonar/actions/workflows/ci.yml/badge.svg)](https://github.com/BumblebeeAS/waterlinkedsonar/actions/workflows/ci.yml)

C++17 client library for the [Water Linked Sonar 3D-15](https://docs.waterlinked.com/sonar-3d/sonar-3d-15-introduction/): HTTP configuration client, UDP receiver with a Range Image Protocol (RIP2) codec, and range-image-to-point-cloud conversions. No ROS dependency.

## Dependencies

```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev libsnappy-dev zlib1g-dev \
    nlohmann-json3-dev libprotobuf-dev protobuf-compiler libgtest-dev
```

Protobuf, curl, snappy, zlib, and nlohmann-json are implementation dependencies of the shared library. Consumers link `waterlinkedsonar::waterlinkedsonar` and need none of them.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The repo also builds as a colcon package (`package.xml`, build type `cmake`).

## Usage

```cpp
#include <waterlinkedsonar/client.hpp>
#include <waterlinkedsonar/receiver.hpp>

waterlinked::sonar::SonarClient client("192.168.194.96");
client.set_acoustics_enabled(true);

waterlinked::sonar::Receiver receiver(
    std::make_unique<waterlinked::sonar::UdpSocket>(
        waterlinked::sonar::UdpSocketConfig{}));
receiver.on_range_image([](const waterlinked::sonar::RangeImageView& img) {
  // runs on the receiver's thread
});
receiver.start();
```

Callback views are valid only for the duration of the callback; copy any data you keep. Complete programs are in [`examples/`](examples/).

## Attribution

The protocol definition ([`proto/WaterLinkedSonarIntegrationProtocol.proto`](proto/WaterLinkedSonarIntegrationProtocol.proto)) and the test recording ([`test/data/ship_short.sonar`](test/data/ship_short.sonar)) are vendored from Water Linked's official [wlsonar](https://github.com/waterlinked/wlsonar) Python client (MIT, Copyright Water Linked). The implementation follows the [Sonar 3D-15 API documentation](https://docs.waterlinked.com/sonar-3d/sonar-3d-15-api/).

## License

MIT
