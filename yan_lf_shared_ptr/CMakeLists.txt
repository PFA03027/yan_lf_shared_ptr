cmake_minimum_required(VERSION 3.16)

file(GLOB HEADER_FILES inc/*.hpp )

add_library(yan.lf_shared_ptr INTERFACE)
# target_sources(
#     yan.lf_shared_ptr
#     INTERFACE
#         FILE_SET HEADERS
#         BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/inc
#         FILES
#             ${HEADER_FILES}
# )
set_target_properties(yan.lf_shared_ptr PROPERTIES PUBLIC_HEADER "${HEADER_FILES}")
target_include_directories(yan.lf_shared_ptr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
)
add_library(yan::lf_shared_ptr ALIAS yan.lf_shared_ptr)

# install(
#     TARGETS yan.lf_shared_ptr
#     EXPORT yan.lf_shared_ptr
#     FILE_SET HEADERS
# )
include(GNUInstallDirs)
install(TARGETS yan.lf_shared_ptr
        PUBLIC_HEADER
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/
)
