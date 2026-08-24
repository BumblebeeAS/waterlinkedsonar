cmake_minimum_required(VERSION 3.22)
project(
    waterlinkedsonar
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "C++ client library for the Water Linked Sonar 3D-15"
)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

# Protobuf >= 4.22 links against Abseil, and only protobuf's own config
# package declares that dependency -- CMake's FindProtobuf module hands out a
# bare protobuf::libprotobuf that fails to link against it. Prefer config
# mode; fall back to the module for the pre-4.x protobuf that Ubuntu ships.
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    find_package(Protobuf MODULE REQUIRED)
endif()
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED)

find_path(Snappy_INCLUDE_DIR snappy.h)
find_library(Snappy_LIBRARY snappy)
if(NOT Snappy_INCLUDE_DIR OR NOT Snappy_LIBRARY)
    message(FATAL_ERROR "snappy not found (install libsnappy-dev)")
endif()

set(proto_file
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/WaterLinkedSonarIntegrationProtocol.proto
)
set(proto_out_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(proto_gen_cc ${proto_out_dir}/WaterLinkedSonarIntegrationProtocol.pb.cc)
set(proto_gen_h ${proto_out_dir}/WaterLinkedSonarIntegrationProtocol.pb.h)
add_custom_command(
    OUTPUT ${proto_gen_cc} ${proto_gen_h}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${proto_out_dir}
    COMMAND
        protobuf::protoc --cpp_out=${proto_out_dir} -I
        ${CMAKE_CURRENT_SOURCE_DIR}/proto ${proto_file}
    DEPENDS ${proto_file}
    COMMENT "Generating protobuf sources"
)
# Compiled into the library below; tests that construct protobuf messages
# depend on this target so the header exists before their objects compile.
add_custom_target(waterlinkedsonar_proto_gen DEPENDS ${proto_gen_cc} ${proto_gen_h})

add_library(waterlinkedsonar SHARED)
add_library(waterlinkedsonar::waterlinkedsonar ALIAS waterlinkedsonar)
target_sources(
    waterlinkedsonar
    PRIVATE
        src/client.cpp
        src/conversions.cpp
        src/curl_transport.cpp
        src/receiver.cpp
        src/rip.cpp
        src/semver.cpp
        src/sntp.cpp
        src/types.cpp
        src/udp_socket.cpp
        ${proto_gen_cc}
)
target_include_directories(
    waterlinkedsonar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${proto_out_dir} ${Snappy_INCLUDE_DIR}
)
target_compile_features(waterlinkedsonar PUBLIC cxx_std_17)
target_link_libraries(
    waterlinkedsonar
    PRIVATE
        protobuf::libprotobuf
        CURL::libcurl
        ZLIB::ZLIB
        nlohmann_json::nlohmann_json
        ${Snappy_LIBRARY}
)
set_target_properties(
    waterlinkedsonar
    PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0
)

set(EXAMPLES configure_sonar subscribe_to_images)
foreach(example IN ITEMS ${EXAMPLES})
    add_executable(${example} examples/${example}.cpp)
    target_link_libraries(${example} PRIVATE waterlinkedsonar::waterlinkedsonar)
    set_target_properties(
        ${example}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/examples
    )
endforeach()

install(
    TARGETS waterlinkedsonar
    EXPORT waterlinkedsonarTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
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
    FILES ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/waterlinkedsonarConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/waterlinkedsonar
)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)

    set(TESTS
        test_rip
        test_conversions
        test_semver
        test_client
        test_receiver
        test_sntp
    )
    foreach(test IN ITEMS ${TESTS})
        add_executable(${test} test/${test}.cpp)
        target_link_libraries(${test} PRIVATE waterlinkedsonar GTest::gtest_main)
        # src/ gives access to internal headers (rip_internal.hpp, semver.hpp);
        # proto_out_dir to the generated protobuf header.
        target_include_directories(
            ${test}
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${proto_out_dir}
        )
        add_dependencies(${test} waterlinkedsonar_proto_gen)
        # Tests read test/data/ recordings relative to this directory.
        add_test(
            NAME ${test}
            COMMAND ${test}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endforeach()
    target_link_libraries(test_client PRIVATE nlohmann_json::nlohmann_json)
    # test_rip frames a legacy RIP1 packet by hand, CRC included.
    target_link_libraries(test_rip PRIVATE ZLIB::ZLIB)
    # These construct protobuf messages directly via the internal encode().
    foreach(test IN ITEMS test_rip test_conversions test_receiver)
        target_link_libraries(${test} PRIVATE protobuf::libprotobuf)
    endforeach()
endif()
