cmake_minimum_required(VERSION 3.16)
project(shr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)

find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)

set(SOURCES
    src/main.cxx
    src/identity.cxx
    src/database.cxx
    src/crypto.cxx
    src/network.cxx
    src/transfer.cxx
    src/messaging.cxx
    src/peers.cxx
    src/config.cxx
    src/logger.cxx
    src/utils.cxx
)

add_executable(shr ${SOURCES})

target_include_directories(shr PRIVATE
    include
    ${OPENSSL_INCLUDE_DIR}
    ${SQLITE3_INCLUDE_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(shr PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SQLITE3_LIBRARY}
    Threads::Threads
)

if(UNIX AND NOT APPLE)
    target_link_libraries(shr PRIVATE dl)
endif()

if(WIN32)
    target_link_libraries(shr PRIVATE ws2_32 mswsock)
endif()

install(TARGETS shr DESTINATION bin)
