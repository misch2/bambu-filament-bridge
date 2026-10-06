# Standalone Docker

Initialize the submodule, copy `.env.example` to `.env`, fill the four required
values, and use the README quick start. The examples contain no plausible secrets.
Named volume `bridge-data` preserves data as UID/GID 10001; a bind mount must be
writable by that user. A brief root entrypoint sets ownership of `/data` itself
and permanently drops to UID/GID 10001 with no-new-privileges before exec;
it never recursively changes existing file ownership. The production image builds bridge and open plugin in
separate stages and includes no compiler or proprietary Bambu binary at runtime.

```sh
docker build -t bambu-filament-bridge:local .
docker buildx build --platform linux/amd64,linux/arm64 \
  --load -t bambu-filament-bridge:local .
```

The second command requires a multi-platform image store (Docker Desktop supports
this). Otherwise use `--output type=oci,dest=bridge-images.tar` or build/load each
platform separately. Both platform builds execute the fake tests and a plugin
load/version check. ARM emulation validates packaging, not real printer behavior.
The Debian base is pinned by digest. Apt packages follow the bookworm security
repositories; dependency source pins are in THIRD_PARTY_NOTICES.md. This is a
pinned source build, not a claim of bit-identical Debian package output forever.

The plugin build passes explicit OpenSSL library paths from the target compiler's
Debian multiarch directory (`cc -print-multiarch`) to CMake. Both shared libraries
must exist before configuration starts. This avoids relying on CMake's automatic
library search paths during ARM64 emulation and uses the same installed
`libssl-dev` package on both platforms.

Docker HEALTHCHECK calls `/health`. A disconnected/stale printer makes the
container unhealthy (HTTP 503), while the HTTP server stays alive and reconnects.
Docker does not automatically restart containers merely because they are unhealthy.
Avoid restart automation that prevents reconnect from completing.

For Docker secrets, remove the direct secret environment variables and use:

```yaml
services:
  bridge:
    image: ghcr.io/misch2/bambu-filament-bridge:0.1.8
    environment:
      BAMBU_DEV_ID: ${BAMBU_DEV_ID}
      BAMBU_DEV_IP: ${BAMBU_DEV_IP}
      BAMBU_ACCESS_CODE_FILE: /run/secrets/access_code
      BAMBU_HTTP_TOKEN_FILE: /run/secrets/http_token
    secrets: [access_code, http_token]
    volumes: [bridge-data:/data]
    ports: ["8080:8080"]
secrets:
  access_code:
    file: ./secrets/access_code
  http_token:
    file: ./secrets/http_token
volumes:
  bridge-data:
```

Keep those local files out of version control and readable by UID 10001. Protect
the host's port with a firewall or bind `127.0.0.1:8080:8080` behind a TLS proxy.
The image tag is available only after the release workflow publishes it. Local
`--build` remains usable before the first release.
