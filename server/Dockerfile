# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake make g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /project

# Copy only what the server needs (protocol header + server source)
COPY src/net/NetProtocol.h src/net/NetProtocol.h
COPY server/main.cpp       server/main.cpp
COPY server/CMakeLists.txt server/CMakeLists.txt

RUN cmake -S server -B /build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /build -j$(nproc)

# ── Stage 2: Run ──────────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/bin/GameServer /usr/local/bin/GameServer

EXPOSE 7777

CMD ["GameServer"]
