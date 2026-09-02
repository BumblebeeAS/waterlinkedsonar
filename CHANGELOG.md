# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-09-02

### Added

- `SonarClient` for the Sonar 3D-15 HTTP API, built on libcurl.
- `UdpSocket` and `Receiver` for the multicast or unicast data stream, passing range images, signal-strength or shaded bitmap images and IMU batches to callbacks on a receive thread.
- `rip::Decoder` for RIP1 and RIP2 packets.
- Conversions from range images to distances and organized or unorganized point clouds, and from signal-strength images to linear strength.
- `sntp_query()` for checking an NTP server before configuring it on the sonar.
- CMake package config, colcon `package.xml`, and example programs.

[Unreleased]: https://github.com/BumblebeeAS/waterlinkedsonar/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/BumblebeeAS/waterlinkedsonar/releases/tag/v0.1.0
