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



add_executable(test_typeT_lf_heap EXCLUDE_FROM_ALL test_rc_sticky_counter.cpp)
target_include_directories( test_typeT_lf_heap  PRIVATE ../inc )
target_compile_options(test_typeT_lf_heap PUBLIC  -Wall -Wconversion -Wsign-conversion -Werror)
target_link_libraries(test_typeT_lf_heap gtest gtest_main )
add_dependencies(build-test test_typeT_lf_heap)
add_test(NAME test_typeT_lf_heap COMMAND $<TARGET_FILE:test_typeT_lf_heap>)
