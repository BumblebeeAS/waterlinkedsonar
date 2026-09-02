cmake_minimum_required(VERSION 3.22)
project(
    waterlinkedsonar
    VERSION 0.0.0
    LANGUAGES CXX
    DESCRIPTION "C++ client library for the Water Linked Sonar 3D-15"
    HOMEPAGE_URL "https://github.com/BumblebeeAS/waterlinkedsonar"
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(
    WATERLINKEDSONAR_BUILD_EXAMPLES
    "Build the example programs"
    ${PROJECT_IS_TOP_LEVEL}
)
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

# Protobuf 4.22 and newer link against Abseil, which only protobuf's own
# config package declares. Older releases, such as Ubuntu's, ship no config
# package and need the FindProtobuf module.
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    find_package(Protobuf MODULE REQUIRED)
endif()
find_package(CURL REQUIRED)
find_package(Snappy REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED)

# Generated sources go to proto_out_dir, which the tests also add to their
# include path.
set(proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(proto_out_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(proto_sources
    ${proto_out_dir}/WaterLinkedSonarIntegrationProtocol.pb.cc
    ${proto_out_dir}/WaterLinkedSonarIntegrationProtocol.pb.h
)
add_custom_command(
    OUTPUT ${proto_sources}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${proto_out_dir}
    COMMAND
        protobuf::protoc --cpp_out=${proto_out_dir} -I ${proto_dir}
        ${proto_dir}/WaterLinkedSonarIntegrationProtocol.proto
    DEPENDS ${proto_dir}/WaterLinkedSonarIntegrationProtocol.proto
    COMMENT "Generating protobuf sources"
    VERBATIM
)

add_library(waterlinkedsonar SHARED)
add_library(waterlinkedsonar::waterlinkedsonar ALIAS waterlinkedsonar)
target_sources(
    waterlinkedsonar
    PRIVATE
        ${proto_sources}
        src/http/client.cpp
        src/http/http_transport.cpp
        src/http/semver.cpp
        src/http/types.cpp
        src/ntp/sntp.cpp
        src/rip/conversions.cpp
        src/rip/decoder.cpp
        src/udp/receiver.cpp
        src/udp/socket.cpp
)
target_include_directories(
    waterlinkedsonar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${proto_out_dir}
)
target_compile_features(waterlinkedsonar PUBLIC cxx_std_17)
target_compile_options(
    waterlinkedsonar
    PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)
target_link_libraries(
    waterlinkedsonar
    PRIVATE
        protobuf::libprotobuf
        CURL::libcurl
        Snappy::snappy
        ZLIB::ZLIB
        nlohmann_json::nlohmann_json
)
set_target_properties(
    waterlinkedsonar
    PROPERTIES
        CXX_EXTENSIONS OFF
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
    TARGETS waterlinkedsonar
    EXPORT waterlinkedsonarTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT waterlinkedsonarTargets
    NAMESPACE waterlinkedsonar::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/waterlinkedsonar
)
configure_package_config_file(
    cmake/waterlinkedsonarConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/waterlinkedsonar
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/waterlinkedsonar
)

if(WATERLINKEDSONAR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(test)
endif()
