cmake_minimum_required(VERSION 3.15)
project(Best_Server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(BEST_SERVER_PLATFORM_WINDOWS ON)
elseif(APPLE)
    set(BEST_SERVER_PLATFORM_MACOS ON)
elseif(UNIX)
    set(BEST_SERVER_PLATFORM_LINUX ON)
endif()

option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization" OFF)
option(ENABLE_PGO "Enable Profile-Guided Optimization" OFF)

if(MSVC)
    add_compile_options(/W4 /WX /O2)
    if(ENABLE_LTO)
        add_compile_options(/GL)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LTCG")
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -Wno-error=unused-variable -Wno-error=unused-but-set-variable)
    add_compile_definitions(BEST_SERVER_HAS_COROUTINES=1)
    
    # Link-Time Optimization
    if(ENABLE_LTO)
        add_compile_options(-flto)
        add_link_options(-flto)
    endif()
    
    # Profile-Guided Optimization
    if(ENABLE_PGO)
        # First phase: generate profile
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pgo_data")
            add_compile_options(-fprofile-generate=${CMAKE_CURRENT_SOURCE_DIR}/pgo_data)
            add_link_options(-fprofile-generate=${CMAKE_CURRENT_SOURCE_DIR}/pgo_data)
            message(STATUS "PGO: Profile generation mode")
        else()
            # Second phase: use profile
            add_compile_options(-fprofile-use=${CMAKE_CURRENT_SOURCE_DIR}/pgo_data)
            add_link_options(-fprofile-use=${CMAKE_CURRENT_SOURCE_DIR}/pgo_data)
            message(STATUS "PGO: Profile use mode")
        endif()
    endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/best_server)

if(BEST_SERVER_PLATFORM_WINDOWS)
else()
    find_package(Threads REQUIRED)
endif()

set(BEST_SERVER_SOURCES
    src/core/scheduler.cpp
    src/core/reactor.cpp
    src/core/task_queue.cpp
    src/core/thread_pool.cpp
    src/core/per_cpu.cpp
    src/core/numa_affinity.cpp
    src/core/rcu.cpp
    src/memory/allocator.cpp
    src/memory/object_pool.cpp
    src/memory/slab_allocator.cpp
    src/memory/zero_copy_buffer.cpp
    src/memory/numa_allocator.cpp
    src/io/io_event_loop.cpp
    src/io/tcp_socket.cpp
    src/io/udp_socket.cpp
    src/io/file_reader.cpp
    src/io/file_writer.cpp
    src/io/batch_io.cpp
    src/io/io_uring.cpp
    src/io/folder_transfer.cpp
    src/future/future.cpp
    src/future/coroutine_pool.cpp
    src/future/future.cpp
    src/timer/timer_wheel.cpp
    src/timer/timer_manager.cpp
    src/network/http_request.cpp
    src/network/http_response.cpp
    src/network/http_parser.cpp
    src/network/http_server.cpp
    src/network/http2_server.cpp
    src/network/ssl/ssl_socket.cpp
    src/network/dns/dns_resolver.cpp
    src/network/websocket/websocket.cpp
    src/database/redis/redis_client.cpp
    src/database/mysql/mysql_client.cpp
    src/database/postgresql/postgres_client.cpp
    src/database/orm/orm.cpp
    src/service_discovery/service_discovery.cpp
    src/load_balancer/load_balancer.cpp
    src/config/config_manager.cpp
    src/utils/performance_monitor.cpp
    src/utils/random.cpp
    src/monitoring/system_monitor.cpp
)

set(BEST_SERVER_HEADERS
    include/best_server/core/scheduler.hpp
    include/best_server/core/reactor.hpp
    include/best_server/core/task_queue.hpp
    include/best_server/core/thread_pool.hpp
    include/best_server/core/lockfree_queue.hpp
    include/best_server/core/per_cpu.hpp
    include/best_server/core/cache_optimization.hpp
    include/best_server/core/numa_affinity.hpp
    include/best_server/core/rcu.hpp
    
    include/best_server/memory/allocator.hpp
    include/best_server/memory/object_pool.hpp
    include/best_server/memory/slab_allocator.hpp
    include/best_server/memory/zero_copy_buffer.hpp
    include/best_server/memory/intrusive_ptr.hpp
    include/best_server/memory/numa_allocator.hpp
    include/best_server/io/io_event_loop.hpp
    include/best_server/io/tcp_socket.hpp
    include/best_server/io/udp_socket.hpp
    include/best_server/io/file_reader.hpp
    include/best_server/io/file_writer.hpp
    include/best_server/io/batch_io.hpp
    include/best_server/io/io_uring.hpp
    include/best_server/io/folder_transfer.hpp
    include/best_server/future/future.hpp
    include/best_server/future/continuation.hpp
    include/best_server/future/coroutine_pool.hpp
    include/best_server/future/future.hpp
    include/best_server/timer/timer_wheel.hpp
    include/best_server/timer/timer_manager.hpp
    include/best_server/network/http_request.hpp
    include/best_server/network/http_response.hpp
    include/best_server/network/http_parser.hpp
    include/best_server/network/http_server.hpp
    include/best_server/network/http2_server.hpp
    include/best_server/network/ssl/ssl_socket.hpp
    include/best_server/network/dns/dns_resolver.hpp
    include/best_server/network/websocket/websocket.hpp
    include/best_server/database/redis/redis_client.hpp
    include/best_server/database/mysql/mysql_client.hpp
    include/best_server/database/postgresql/postgres_client.hpp
    include/best_server/database/orm/orm.hpp
    include/best_server/service_discovery/service_discovery.hpp
    include/best_server/load_balancer/load_balancer.hpp
    include/best_server/config/config_manager.hpp
    include/best_server/best_server.hpp
    include/best_server/network/simd_utils.hpp
    include/best_server/monitoring/system_monitor.hpp
)

add_library(best_server ${BEST_SERVER_SOURCES} ${BEST_SERVER_HEADERS})

target_include_directories(best_server PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(BEST_SERVER_PLATFORM_WINDOWS)
    target_link_libraries(best_server PUBLIC ws2_32 mswsock)
elseif(UNIX)
    target_link_libraries(best_server PUBLIC Threads::Threads)
    # Find OpenSSL
    find_package(OpenSSL REQUIRED)
    if(OpenSSL_FOUND)
        target_include_directories(best_server PUBLIC ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(best_server PUBLIC ${OPENSSL_LIBRARIES})
    endif()
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Test upload fix program
add_executable(test_upload_fix test_upload_fix.cpp)
target_link_libraries(test_upload_fix best_server)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

install(TARGETS best_server
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/best_server
    DESTINATION include
)
