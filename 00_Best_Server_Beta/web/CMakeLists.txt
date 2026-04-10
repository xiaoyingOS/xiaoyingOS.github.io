cmake_minimum_required(VERSION 3.15)
project(web_server)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find parent project
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Building as part of parent project
    set(BEST_SERVER_DIR ${CMAKE_SOURCE_DIR})
else()
    # Building standalone
    set(BEST_SERVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

# Include best_server - add both parent and best_server directories
include_directories(${BEST_SERVER_DIR}/include/best_server)
include_directories(${BEST_SERVER_DIR}/include)

# Find pthread
find_package(Threads REQUIRED)

# Find OpenSSL for HTTPS support
find_package(OpenSSL REQUIRED)
if(OpenSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
endif()

# Find libarchive
find_path(LIBARCHIVE_INCLUDE_DIR archive.h HINTS /data/data/com.termux/files/usr/include)
find_library(LIBARCHIVE_LIBRARY archive HINTS /data/data/com.termux/files/usr/lib)

if(LIBARCHIVE_INCLUDE_DIR AND LIBARCHIVE_LIBRARY)
    include_directories(${LIBARCHIVE_INCLUDE_DIR})
    message(STATUS "Found libarchive: ${LIBARCHIVE_LIBRARY}")
else()
    message(WARNING "libarchive not found, using tar command instead")
endif()

# Enable LTO
option(ENABLE_LTO "Enable Link-Time Optimization" OFF)

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
endif()

# Add executable
add_executable(web_server server.cpp)
add_executable(web_server_daemon server_daemon.cpp)
add_executable(https_proxy https_proxy.cpp)

# Link to best_server library
target_link_libraries(web_server
    PRIVATE
    ${BEST_SERVER_DIR}/build/libbest_server.a
    Threads::Threads
    ${LIBARCHIVE_LIBRARY}
    ${OPENSSL_LIBRARIES}
)

target_link_libraries(web_server_daemon
    PRIVATE
    ${BEST_SERVER_DIR}/build/libbest_server.a
    Threads::Threads
    ${LIBARCHIVE_LIBRARY}
    ${OPENSSL_LIBRARIES}
)

target_link_libraries(https_proxy
    PRIVATE
    Threads::Threads
    ${OPENSSL_LIBRARIES}
)

# Enable LTO
if(ENABLE_LTO)
    set_property(TARGET web_server PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Set output directory
set_target_properties(web_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Print configuration
message(STATUS "Web Server Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  LTO: ${ENABLE_LTO}")
message(STATUS "  Best Server Dir: ${BEST_SERVER_DIR}")