cmake_minimum_required(VERSION 4.3.3)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

project(im)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets REQUIRED)

add_subdirectory(third_party/c-toxcore)

set(BUILD_TOXAV ON CACHE BOOL "Build the tox AV library" FORCE)
set(MUST_BUILD_TOXAV ON CACHE BOOL "Fail the build if toxav cannot be built" FORCE)
set(ENABLE_SHARED ON CACHE BOOL "Build shared (dynamic) libraries" FORCE)
set(ENABLE_STATIC OFF CACHE BOOL "Build static libraries" FORCE)
set(FLAT_OUTPUT_STRUCTURE ON CACHE BOOL "Produce output artifacts in flat structure" FORCE)
set(UNITTEST OFF CACHE BOOL "Disable toxcore unit tests" FORCE)
set(AUTOTEST OFF CACHE BOOL "Disable toxcore autotests" FORCE)
set(DHT_BOOTSTRAP OFF CACHE BOOL "Disable DHT_bootstrap build" FORCE)
set(BOOTSTRAP_DAEMON OFF CACHE BOOL "Disable tox-bootstrapd build" FORCE)

set_target_properties(toxcore_shared PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
)

add_executable(im
${PROJECT_SOURCE_DIR}/main.cpp
${PROJECT_SOURCE_DIR}/include/widget/AddFriendDialog.h
${PROJECT_SOURCE_DIR}/src/widget/AddFriendDialog.cpp
${PROJECT_SOURCE_DIR}/src/core/ToxCoreWrapper.cpp
)

target_include_directories(im PRIVATE
${PROJECT_SOURCE_DIR}/include
${PROJECT_SOURCE_DIR}/third_party/c-toxcore
)

target_link_libraries(im PRIVATE
Qt6::Core Qt6::Widgets
toxcore_shared
)
