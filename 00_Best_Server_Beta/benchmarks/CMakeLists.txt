# Benchmarks CMakeLists.txt

add_executable(benchmark_example benchmark.cpp)
target_link_libraries(benchmark_example best_server)

# Simple performance test (no Google Benchmark dependency)
add_executable(simple_performance_test simple_performance_test.cpp)
target_link_libraries(simple_performance_test best_server pthread)

# Quick performance test
add_executable(quick_performance_test quick_performance_test.cpp)
target_link_libraries(quick_performance_test best_server pthread)

# Minimal test
add_executable(minimal_test minimal_test.cpp)
target_link_libraries(minimal_test best_server pthread)