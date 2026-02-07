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

add_custom_target(build-test)

set(EXEC_TARGET test_typeT_lf_heap)
file(GLOB SOURCES *.cpp )

add_executable(${EXEC_TARGET} EXCLUDE_FROM_ALL)
target_sources(${EXEC_TARGET} PRIVATE ${SOURCES})
target_compile_features(${EXEC_TARGET} PRIVATE cxx_std_20) #  -std=c++20
target_compile_options(${EXEC_TARGET} PUBLIC  -Wall -Wconversion -Wsign-conversion -Werror)
target_link_libraries(${EXEC_TARGET} yan::lf_shared_ptr GTest::gtest GTest::gtest_main )
add_dependencies(build-test ${EXEC_TARGET})
add_test(NAME ${EXEC_TARGET} COMMAND $<TARGET_FILE:${EXEC_TARGET}>)

# compile option defined if Build Type is Debug
# ENABLE_STICKY_COUNTER_OVERFLOW_CHECK
target_compile_definitions(${EXEC_TARGET} PRIVATE $<$<CONFIG:Debug>:ENABLE_STICKY_COUNTER_OVERFLOW_CHECK>)
#
# ENABLE_STICKY_COUNTER_LOGIC_CHECK
target_compile_definitions(${EXEC_TARGET} PRIVATE $<$<CONFIG:Debug>:ENABLE_STICKY_COUNTER_LOGIC_CHECK>)
#
# TEST_ENABLE_LOGICCHECKER
target_compile_definitions(${EXEC_TARGET} PRIVATE $<$<CONFIG:Debug>:TEST_ENABLE_LOGICCHECKER>)
