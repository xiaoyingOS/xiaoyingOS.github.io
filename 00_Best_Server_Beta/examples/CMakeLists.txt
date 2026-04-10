# Examples CMakeLists.txt
# All examples temporarily disabled except simple_test

# add_executable(complete_example complete_example.cpp)
# target_link_libraries(complete_example best_server)

# add_executable(simple_http_server simple_http_server.cpp)
# target_link_libraries(simple_http_server best_server)

# add_executable(async_example async_example.cpp)
# target_link_libraries(async_example best_server)

# add_executable(rpc_example rpc_example.cpp)
# target_link_libraries(rpc_example best_server)

# add_executable(benchmark benchmark.cpp)
# target_link_libraries(benchmark best_server)

# Simple test without using namespace
add_executable(simple_test simple_test.cpp)
target_link_libraries(simple_test best_server)