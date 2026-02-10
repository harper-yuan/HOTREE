set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/benchmarks)

set(benchbin "")

macro(add_benchmark source_name)
    add_executable(${source_name} ${source_name}.cpp)
    target_link_libraries(${source_name} HOTREE cryptopp)
    list(APPEND benchbin ${source_name})
endmacro()

add_benchmark(initialization_vary_blocksize)
add_benchmark(query_vary_k)
add_benchmark(query_vary_N)
add_benchmark(query_vary_blocksize)

add_custom_target(benchmarks)
add_dependencies(benchmarks ${benchbin})
