cmake_minimum_required(VERSION 4.3.3)

project(im LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Multimedia)
find_package(CURL REQUIRED)
qt_standard_project_setup(REQUIRES 6.10)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(BUILD_TOXAV ON CACHE BOOL "Build the tox AV library" FORCE)
set(MUST_BUILD_TOXAV ON CACHE BOOL "Fail the build if toxav cannot be built" FORCE)
set(ENABLE_SHARED ON CACHE BOOL "Build shared (dynamic) libraries" FORCE)
set(ENABLE_STATIC OFF CACHE BOOL "Build static libraries" FORCE)
set(FLAT_OUTPUT_STRUCTURE ON CACHE BOOL "Produce output artifacts in flat structure" FORCE)
set(UNITTEST OFF CACHE BOOL "Disable toxcore unit tests" FORCE)
set(AUTOTEST OFF CACHE BOOL "Disable toxcore autotests" FORCE)
set(DHT_BOOTSTRAP OFF CACHE BOOL "Disable DHT_bootstrap build" FORCE)
set(BOOTSTRAP_DAEMON OFF CACHE BOOL "Disable tox-bootstrapd build" FORCE)


set(SQLITECPP_INTERNAL_SQLITE OFF CACHE BOOL "Disable SQLiteCpp internal sqlite3" FORCE)
set(SQLITE_HAS_CODEC ON CACHE BOOL "Enable SQLCipher encryption support" FORCE)
set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "Disable SQLiteCpp tests" FORCE)
set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "Disable SQLiteCpp examples" FORCE)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "Disable cpplint" FORCE)
set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "Disable cppcheck" FORCE)
set(SQLITECPP_INSTALL OFF CACHE BOOL "Disable SQLiteCpp install" FORCE)


add_subdirectory(third_party/c-toxcore)
add_subdirectory(third_party/SQLiteCpp)

qt_add_executable(im
    ${PROJECT_SOURCE_DIR}/main.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ToxCoreWrapper.cpp
    ${PROJECT_SOURCE_DIR}/src/core/CallConfig.h
    ${PROJECT_SOURCE_DIR}/src/core/CallConfig.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/AudioManager.h
    ${PROJECT_SOURCE_DIR}/src/audio/AudioManager.cpp
    ${PROJECT_SOURCE_DIR}/src/video/VideoManager.h
    ${PROJECT_SOURCE_DIR}/src/video/VideoManager.cpp
    ${PROJECT_SOURCE_DIR}/include/ai/AiCurlClient.h
    ${PROJECT_SOURCE_DIR}/src/ai/AiCurlClient.cpp
    ${PROJECT_SOURCE_DIR}/src/persistence/SqliteStorage.cpp
    ${PROJECT_SOURCE_DIR}/src/app/AppController.h
    ${PROJECT_SOURCE_DIR}/src/app/AppController.cpp
    ${PROJECT_SOURCE_DIR}/src/app/BusinessServices.h
    ${PROJECT_SOURCE_DIR}/src/app/BusinessServices.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ContactListModel.h
    ${PROJECT_SOURCE_DIR}/src/app/ContactListModel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ChatMessageModel.h
    ${PROJECT_SOURCE_DIR}/src/app/ChatMessageModel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/NoticeModel.h
    ${PROJECT_SOURCE_DIR}/src/app/NoticeModel.cpp
    ${PROJECT_SOURCE_DIR}/resources/resources.qrc
    ${PROJECT_SOURCE_DIR}/src/core/AppLog.cpp

)

add_subdirectory(src/qml)

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

target_include_directories(im PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/c-toxcore
)

target_link_libraries(im PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Multimedia
    CURL::libcurl
    toxcore_shared
    SQLiteCpp
    $<TARGET_NAME_IF_EXISTS:sqlcipher>

)
target_compile_definitions(im PRIVATE
    SQLITE_HAS_CODEC
)
