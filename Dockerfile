# ─────────────────────────────────────────────────────────────────────────────
# Stage 1 — Builder
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy everything (main.cpp, tcp-server/, etc.)
COPY . .

# Run CMake pointing to the directory where CMakeLists.txt actually lives
RUN cmake -S ./tcp-server -B build \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-flto" \
          -DCMAKE_EXE_LINKER_FLAGS="-flto"

RUN cmake --build build --parallel "$(nproc)"

# ... (Rest of Stage 2 remains the same)
# ─────────────────────────────────────────────────────────────────────────────
# Stage 2 — Runtime
# ─────────────────────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN groupadd --gid 1001 appgroup \
 && useradd  --uid 1001 --gid appgroup --shell /bin/sh --create-home appuser

WORKDIR /app

# Copy the compiled binary from the builder stage
# Double-check the binary name matches what is defined in your CMakeLists.txt
COPY --from=builder /build/build/log_committer_tcp .

RUN chmod +x log_committer_tcp
USER appuser

# Environment Defaults
ENV TCP_HOST=0.0.0.0
ENV TCP_PORT=9090
ENV THREAD_COUNT=4

EXPOSE 9090

# Health check using bash (ensure port is active)
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD bash -c 'exec 3<>/dev/tcp/127.0.0.1/9090 && exec 3<&- && exec 3>&-' || exit 1

ENTRYPOINT ["./log_committer_tcp"]