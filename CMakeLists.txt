cmake_minimum_required(VERSION 3.16)
project(constrained_any CXX)
set(CMAKE_VERBOSE_MAKEFILE 1)


# Switch build target
set(BUILD_TARGET common CACHE STRING "Build target?")
message("[BUILD] ${BUILD_TARGET}")
include(${BUILD_TARGET}.cmake)

add_custom_target(build-test)

enable_testing()

add_subdirectory(test)
# add_subdirectory(sample)
