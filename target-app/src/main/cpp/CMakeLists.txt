cmake_minimum_required(VERSION 3.22.1)

project(hardened_fixture LANGUAGES CXX)

add_library(hardened_payload SHARED hardened_payload.cpp)
add_library(hardened_fixture SHARED hardened_fixture.cpp)
target_compile_features(
    hardened_payload PRIVATE cxx_std_20
)
target_compile_features(
    hardened_fixture PRIVATE cxx_std_20
)
target_compile_options(
    hardened_payload PRIVATE -O2 -Wall -Wextra -Wpedantic
)
target_compile_options(
    hardened_fixture PRIVATE -O2 -Wall -Wextra -Wpedantic
)
target_link_libraries(hardened_payload PRIVATE dl)
target_link_libraries(
    hardened_fixture PRIVATE hardened_payload log dl
)
