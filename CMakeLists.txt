cmake_minimum_required(VERSION 3.10)
project(HOIR_tree VERSION 1.0.0 LANGUAGES C CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.72.0 COMPONENTS unit_test_framework program_options system REQUIRED)
find_package(OpenMP REQUIRED)
find_library(CRYPTOPP_LIB NAMES cryptopp)
add_subdirectory(src)
# add_subdirectory(benchmark)

enable_testing()
# add_subdirectory(test test_HoRGod)
add_subdirectory(test_code)
