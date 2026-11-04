# docker build -t gamecommsd -f server/Dockerfile .
# For remote access, provide a VPN-bound config; see docs/BUILD.md.
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake gcc make libc6-dev && rm -rf /var/lib/apt/lists/*
COPY protocol /src/protocol
COPY server /src/server
RUN cmake -S /src/server -B /build -DCMAKE_BUILD_TYPE=Release -DGCS_BUILD_TESTS=ON \
 && cmake --build /build -j \
 && cd /build && ctest --output-on-failure

FROM debian:bookworm-slim
COPY --from=build /build/gamecommsd /usr/local/bin/gamecommsd
COPY server/deploy/gamecommsd.conf /etc/gamecommsd.conf
EXPOSE 9888/tcp 9888/udp
USER nobody
ENTRYPOINT ["/usr/local/bin/gamecommsd", "-c", "/etc/gamecommsd.conf"]
