# SPDX-License-Identifier: AGPL-3.0-or-later
FROM debian:bookworm-slim@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251 AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config libssl-dev libcurl4-openssl-dev \
    zlib1g-dev uthash-dev ca-certificates python3 clang-format \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
FROM toolchain AS plugin
COPY third_party/open-bamboo-networking /src/open-bamboo-networking
# Resolve Debian's target multiarch paths explicitly for emulated builds.
RUN set -eu; \
    libdir="/usr/lib/$(cc -print-multiarch)"; \
    test -r "$libdir/libssl.so"; \
    test -r "$libdir/libcrypto.so"; \
    cmake -S open-bamboo-networking -B plugin-build \
      -DOPENSSL_SSL_LIBRARY="$libdir/libssl.so" \
      -DOPENSSL_CRYPTO_LIBRARY="$libdir/libcrypto.so" \
      -DOBN_VERSION=02.08.02.99 -DOBN_RELEASE=ON -DOBN_PATCH_CLIENT_CONF=OFF -DOBN_BUILD_TESTS=OFF \
    && cmake --build plugin-build --target bambu_networking -j2

FROM toolchain AS build
COPY . .
RUN cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j2 \
    && ctest --test-dir build --output-on-failure
COPY --from=plugin /src/plugin-build/libbambu_networking.so /src/libbambu_networking.so
RUN build/plugin_smoke /src/libbambu_networking.so

FROM debian:bookworm-slim@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl libcurl4 libssl3 zlib1g libstdc++6 jq util-linux \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 bridge && useradd --uid 10001 --gid bridge --no-create-home bridge \
    && mkdir -p /data /usr/local/lib/bfb /usr/share/bfb/licenses \
    && chown bridge:bridge /data
COPY --from=build /src/build/bambu-bridge /usr/local/bin/bambu-bridge
COPY --from=plugin /src/plugin-build/libbambu_networking.so /usr/local/lib/bfb/libbambu_networking.so
COPY LICENSE THIRD_PARTY_NOTICES.md /usr/share/bfb/licenses/
COPY third_party/nlohmann/LICENSE.MIT /usr/share/bfb/licenses/nlohmann-json.MIT
COPY --from=plugin /src/plugin-build/_deps/eclipse_mosquitto-src/LICENSE.txt /usr/share/bfb/licenses/mosquitto.txt
COPY --from=plugin /src/plugin-build/_deps/cjson-src/LICENSE /usr/share/bfb/licenses/cjson.MIT
COPY packaging/entrypoint.sh /usr/local/bin/bfb-entrypoint
RUN chmod 755 /usr/local/bin/bfb-entrypoint
LABEL org.opencontainers.image.title="bambu-filament-bridge" \
      org.opencontainers.image.source="https://github.com/misch2/bambu-filament-bridge" \
      org.opencontainers.image.licenses="AGPL-3.0-or-later" \
      io.hass.type="app" io.hass.version="0.1.8" io.hass.arch="amd64|aarch64"
ENV BAMBU_DATA_DIR=/data BAMBU_HTTP_BIND=0.0.0.0 BAMBU_HTTP_PORT=8080 \
    OBN_BLOCK_CLOUD=1 OBN_LOG_LEVEL=info OBN_LOG_TO_FILE=0
# Initialize Supervisor's root-owned mount, then permanently drop privileges.
# The bridge daemon always runs as UID/GID 10001.
USER root
VOLUME ["/data"]
EXPOSE 8080
# 503 means the bridge is alive; Docker health also monitors printer readiness.
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl --silent --fail --max-time 3 "http://127.0.0.1:${BAMBU_HTTP_PORT}/health" >/dev/null || exit 1
ENTRYPOINT ["/usr/local/bin/bfb-entrypoint"]
CMD ["bambu-bridge"]
