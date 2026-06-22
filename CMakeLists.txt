cmake_minimum_required(VERSION 3.10)
project(10-Bit-Village-Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pin determinism flags. Ban fast-math.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-fast-math -Wall -Wextra")

# ── Deploy target ────────────────────────────────────────────────────────────
# local (default) : auto-detect host (GGML_NATIVE). Apple Silicon → NEON + Metal.
# hf              : Hugging Face Docker Spaces (x86_64). Pin AVX2 (NOT AVX512) so
#                   the binary is byte-identical across HF's whole x86 fleet —
#                   determinism must never depend on which host builds/runs it.
#                   FP reduction order is fixed by the instruction set; NATIVE
#                   would bake in the build host's microarch and silently break
#                   same-seed byte-identity if HF reschedules onto another host.
#                   See vault: 10-Bit Village Research Dossier §F.
set(TBV_DEPLOY_TARGET "local" CACHE STRING "Build target: local | hf")

if(TBV_DEPLOY_TARGET STREQUAL "hf")
    message(STATUS "TBV: HF x86_64 deploy build — pinning AVX2/FMA/F16C/BMI2, NATIVE off, no AVX512, no Metal")
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_AVX2   ON  CACHE BOOL "" FORCE)
    set(GGML_FMA    ON  CACHE BOOL "" FORCE)
    set(GGML_F16C   ON  CACHE BOOL "" FORCE)
    set(GGML_BMI2   ON  CACHE BOOL "" FORCE)
    set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
    set(GGML_METAL  OFF CACHE BOOL "" FORCE)
else()
    message(STATUS "TBV: local dev build — GGML_NATIVE default (Metal on Apple Silicon)")
endif()

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
