cmake_minimum_required(VERSION 3.16)

include(FetchContent)
FetchContent_Declare(
  googletest
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
)
#   URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


set(EXEC_TARGET test_profile_special)
file(GLOB SOURCES *.cpp )

add_executable(${EXEC_TARGET} EXCLUDE_FROM_ALL ${SOURCES})
target_compile_features(${EXEC_TARGET} PRIVATE cxx_std_20)
target_include_directories( ${EXEC_TARGET}  PRIVATE ../inc )
target_compile_options(${EXEC_TARGET} PUBLIC  -Wall -Wconversion -Wsign-conversion -Werror) #  -std=c++20
target_link_libraries(${EXEC_TARGET} gtest gtest_main )
add_dependencies(build-test ${EXEC_TARGET})
add_test(NAME ${EXEC_TARGET} COMMAND $<TARGET_FILE:${EXEC_TARGET}>)
