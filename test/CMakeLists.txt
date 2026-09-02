find_package(GTest REQUIRED)
include(GoogleTest)

function(add_unit_test name source)
    add_executable(${name} ${source})
    target_include_directories(
        ${name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${PROJECT_SOURCE_DIR}/src
            ${proto_out_dir}
    )
    target_compile_definitions(
        ${name}
        PRIVATE
            WATERLINKEDSONAR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    )
    target_link_libraries(
        ${name}
        PRIVATE waterlinkedsonar::waterlinkedsonar GTest::gtest_main ${ARGN}
    )
    target_compile_options(
        ${name}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
    )
    gtest_discover_tests(${name})
endfunction()

add_unit_test(http_client_test http/client_test.cpp nlohmann_json::nlohmann_json)
add_unit_test(http_transport_test http/http_transport_test.cpp CURL::libcurl)
add_unit_test(http_semver_test http/semver_test.cpp)
add_unit_test(ntp_sntp_test ntp/sntp_test.cpp)
add_unit_test(rip_conversions_test rip/conversions_test.cpp)
add_unit_test(
    rip_decoder_test
    rip/decoder_test.cpp
    protobuf::libprotobuf
    Snappy::snappy
    ZLIB::ZLIB
)
add_unit_test(udp_socket_test udp/socket_test.cpp)
add_unit_test(
    udp_receiver_test
    udp/receiver_test.cpp
    protobuf::libprotobuf
    Snappy::snappy
    ZLIB::ZLIB
)
