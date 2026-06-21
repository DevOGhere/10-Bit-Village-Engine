cmake_minimum_required(VERSION 3.10)
project(10-Bit-Village-Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pin determinism flags. Ban fast-math.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-fast-math -Wall -Wextra")

# Link llama.cpp
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "Disable llama.cpp tests")
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "Disable llama.cpp examples")
add_subdirectory(vendor/llama.cpp)

include_directories(include)
include_directories(vendor/llama.cpp/include)

add_executable(tbv_engine
    src/main.cpp
    src/db.cpp
    src/llama_bridge.cpp
    src/world.cpp
)

# Link sqlite3 and llama
target_link_libraries(tbv_engine sqlite3 llama)

add_executable(pin_vectors tests/pin_vectors.cpp)
target_link_libraries(pin_vectors llama)
