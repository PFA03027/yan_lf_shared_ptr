cmake_minimum_required(VERSION 3.16)
project(constrained_any CXX)
set(CMAKE_VERBOSE_MAKEFILE 1)


# Switch build target
set(BUILD_TARGET common CACHE STRING "Build target?")
message("[BUILD] ${BUILD_TARGET}")
include(${BUILD_TARGET}.cmake)

# To generate the targets "test" to top of Makefile, we need to declare below in top CMakeLists.txt
enable_testing()

add_subdirectory(test EXCLUDE_FROM_ALL)
# add_subdirectory(sample)
