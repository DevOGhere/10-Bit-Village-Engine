# 10-Bit Village Engine — HF Docker Space image.
# Two-stage: builder compiles the AVX2-pinned x86_64 engine + fetches the model;
# runtime is the slim image the Space actually ships (Step 5, packet 075 spec).
#
# Determinism note (do not "fix" this without reading the CMakeLists comment):
# TBV_DEPLOY_TARGET=hf pins GGML_NATIVE=OFF + AVX2 (not AVX512, not NATIVE) so the
# binary is byte-identical across HF's x86 fleet regardless of which host builds
# or reschedules it. The x86 hash this produces is NOT expected to match the
# ARM/NEON canonical hash from local dev — see MASTER PLAN §5.

# ── Builder ───────────────────────────────────────────────────────────────────
FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates curl libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Clone llama.cpp fresh at the pinned commit (engine repo does not vendor it —
# vendor/PINNED_COMMIT.txt is the source of truth, never trust a host checkout).
RUN git clone https://github.com/ggerganov/llama.cpp vendor/llama.cpp \
    && git -C vendor/llama.cpp checkout 57fe1f07c3b6a1de3f4fff19098e2056a85275b7

COPY CMakeLists.txt ./
COPY src/ src/
COPY include/ include/
COPY tests/pin_vectors.cpp tests/pin_vectors.cpp

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTBV_DEPLOY_TARGET=hf \
    && cmake --build build -j"$(nproc)" --target tbv_engine

# Model baked at build time, not copied from the (gitignored) local checkout and
# not fetched at container start — keeps cold boot deterministic + offline.
# sha256 pinned against the local dev copy (verified identical before this
# Dockerfile was written); a mismatch means the upstream file moved — fail loud,
# don't ship a silently-different model.
RUN mkdir -p models && \
    curl -fL -o models/smollm2-360m-instruct-q8_0.gguf \
      "https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct-GGUF/resolve/main/smollm2-360m-instruct-q8_0.gguf" && \
    echo "48ab3034d0dd401fbc721eb1df3217902fee7dab9078992d66431f09b7750201  models/smollm2-360m-instruct-q8_0.gguf" | sha256sum -c -

# ── Runtime ───────────────────────────────────────────────────────────────────
# python:3.12-slim-bookworm per Step 5 spec — supervisor.py (Step 6) lands here
# later without a base-image change. ggml/llama are statically linked, but GCC's
# OpenMP support (ggml-cpu) still dynamically links libgomp — static-linking
# libgomp is a well-known footgun (thread-local-storage/threading model issues),
# so it ships as a runtime .so like glibc/libstdc++ instead.
FROM python:3.12-slim-bookworm AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/tbv_engine ./tbv_engine
COPY --from=builder /src/models/ ./models/
COPY grammars/ ./grammars/
COPY assets/ ./assets/
COPY scripts/supervisor.py ./scripts/supervisor.py

EXPOSE 7860

# supervisor.py (Step 6) owns port 7860: restores village.db from the latest
# 10-Bit-Village-Data release on boot, spawns `./tbv_engine --serve`, and
# exposes POST /backup + GET /health. GITHUB_TOKEN is supplied as an HF Space
# secret at runtime, never baked into the image.
CMD ["python3", "scripts/supervisor.py"]
