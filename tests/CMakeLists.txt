cmake_minimum_required(VERSION 3.10)
project(Tests CXX)
add_executable(test_swap _STAGED_test_swap.cpp)
target_include_directories(test_swap PRIVATE ../vendor/llama.cpp/include ../include)
target_link_libraries(test_swap PRIVATE llama ggml)

add_executable(pin_vectors pin_vectors.cpp)
target_include_directories(pin_vectors PRIVATE ../vendor/llama.cpp/include ../include)
target_link_libraries(pin_vectors PRIVATE llama ggml)
