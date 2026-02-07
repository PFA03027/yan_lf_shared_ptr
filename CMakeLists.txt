cmake_minimum_required(VERSION 3.16)
project(
    yan_lf_shared_ptr
    #
    DESCRIPTION "yet another implementation of shared pointer based on lock free reference counter based on sticky counter"
    LANGUAGES CXX
    VERSION 0.5.0
    )


# Switch build target
set(BUILD_CONFIG common CACHE STRING "Build target?")
message("[BUILD] ${BUILD_CONFIG}")
include(${BUILD_CONFIG}.cmake)

# To generate the targets "test" to top of Makefile, we need to declare below in top CMakeLists.txt
enable_testing()

add_subdirectory(yan_lf_shared_ptr)
add_subdirectory(test EXCLUDE_FROM_ALL)
# add_subdirectory(sample)
