foreach(example IN ITEMS configure_sonar subscribe_to_images)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE waterlinkedsonar::waterlinkedsonar)
    target_compile_options(
        ${example}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
    )
endforeach()
