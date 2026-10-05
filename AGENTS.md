# AGENTS.md

## Project overview

`bambu-filament-bridge` is a small standalone service that exposes a stable HTTP API for changing filament metadata on Bambu Lab printers.

Its primary consumer is SpoolmanSync, but the bridge MUST remain independent from Spoolman, SpoolmanSync and Home Assistant.

Conceptually:

```text
Spoolman / other inventory system
          |
          v
     SpoolmanSync
          |
       HTTP API
          |
          v
bambu-filament-bridge
          |
  Bambu networking ABI
          |
          v
    Bambu Lab printer
```

The bridge owns printer communication only.

It must NOT contain filament inventory logic, Spoolman-specific logic, material-to-Bambu-profile mapping, or Home Assistant entity logic.

---

## Current known-good implementation

The original working prototype is a C++17 daemon.

It uses the Bambu networking plugin ABI through plugin-loader code originating from `ClusterM/open-bamboo-networking`.

The prototype was tested with:

- plugin ABI: `0x020802`
- stock plugin version: `02.08.02.54`
- Bambu X2D
- persistent printer connection
- reconnect after printer/network disconnect
- verified AMS filament updates
- HTTP API consumed by SpoolmanSync

Do not rewrite working printer behavior without a concrete reason.

Refactor behind interfaces and preserve behavior first.

---

## Licensing

This project currently depends on code from:

https://github.com/ClusterM/open-bamboo-networking

`open-bamboo-networking` is licensed under GNU Affero General Public License v3 or later.

Unless all AGPL-derived code is removed and independently replaced, this repository MUST be licensed:

```text
AGPL-3.0-or-later
```

Use SPDX identifiers where appropriate:

```text
SPDX-License-Identifier: AGPL-3.0-or-later
```

Preserve upstream copyright and license notices.

The repository should contain:

- `LICENSE`
- `THIRD_PARTY_NOTICES.md`

Do NOT copy or redistribute Bambu Lab's proprietary `libbambu_networking.so`, private signing material, slicer credentials, certificates, access codes, tokens, or other user secrets.

The bridge may load a user-supplied stock plugin, but the project and its container images must not ship that proprietary plugin.

---

## Supported networking backends

The architecture must support loading a Bambu networking ABI-compatible plugin from a configurable path.

Preferred/default public backend:

- `open-bamboo-networking`

Advanced compatibility backend:

- user-provided stock `libbambu_networking.so`

Do not special-case business logic based on backend unless strictly necessary.

Both should go through the same internal plugin/backend abstraction.

The open-source backend should be the default for distributable Docker and Home Assistant builds.

Pin upstream dependencies to a known commit or tag. Never build production images against an unpinned `master` branch.

---

## Configuration

Preserve compatibility with the existing prototype environment variables.

Required printer configuration:

```text
BAMBU_DEV_ID
BAMBU_DEV_IP
BAMBU_ACCESS_CODE
```

HTTP configuration:

```text
BAMBU_HTTP_TOKEN
BAMBU_HTTP_BIND
BAMBU_HTTP_PORT
```

Plugin/runtime configuration:

```text
BAMBU_PLUGIN
BAMBU_CERT_DIR
BAMBU_DATA_DIR
```

Current portable defaults where appropriate:

```text
BAMBU_HTTP_BIND=0.0.0.0
BAMBU_HTTP_PORT=8080
BAMBU_DATA_DIR=/data
```

The old prototype-specific filesystem defaults such as `/home/runner/...` must NOT remain as defaults in the public project.

`BAMBU_HTTP_TOKEN` must be required for write endpoints and must have a reasonable minimum strength, currently at least 32 characters.

Supporting `*_FILE` alternatives for secrets is encouraged, for example:

```text
BAMBU_ACCESS_CODE_FILE
BAMBU_HTTP_TOKEN_FILE
```

Environment variables must take precedence consistently and behavior must be documented.

Never print secrets in logs.

---

## HTTP API compatibility

### GET /health

This endpoint is intentionally unauthenticated.

When the service and printer are ready:

HTTP 200

Example response:

```json
{
    "status": "ready",
    "connected": true,
    "ready": true,
    "reconnecting": false,
    "printerId": "01P...",
    "printerIp": "192.168.1.100",
    "pluginVersion": "02.08.02.54",
    "firmware": "...",
    "lastMessageAgeMs": 123
}
```

When the bridge process is alive but the printer is not ready:

HTTP 503

```json
{
    "status": "not_ready",
    "connected": false,
    "ready": false,
    "reconnecting": true,
    "printerId": "01P..."
}
```

A 503 response from `/health` is a valid bridge state, not an HTTP API failure.

Do not break the existing field names without introducing a new API version.

---

### GET /api/v1/capabilities

Add this endpoint.

It should be authenticated using the same Bearer token as write operations.

Example:

```json
{
    "apiVersion": 1,
    "backend": "open-bamboo-networking",
    "pluginVersion": "02.08.02",
    "printerId": "01P...",
    "features": {
        "amsFilamentWrite": true,
        "externalFilamentWrite": false
    }
}
```

Only advertise capabilities that are actually implemented and tested.

Do not claim support merely because the underlying protocol appears to contain a command.

---

### POST /api/v1/ams/{amsId}/trays/{trayId}/filament

Requires:

```text
Authorization: Bearer <token>
Content-Type: application/json
```

Request:

```json
{
    "profile": "GFG99",
    "setting": "GFSG99_15",
    "type": "PETG",
    "color": "808080FF",
    "tempMin": 220,
    "tempMax": 260
}
```

Validation requirements:

- valid JSON
- required fields present
- non-empty profile
- non-empty setting
- non-empty type
- color exactly `RRGGBBAA`
- temperatures numeric
- temperatures within `0..400`
- validate AMS/tray path parameters
- reject oversized request bodies

Successful response:

HTTP 200

```json
{
    "status": "synced",
    "verified": true,
    "elapsedMs": 1234,
    "sequenceId": "20001",
    "amsId": 0,
    "trayId": 3
}
```

A write MUST NOT return `status: synced` merely because the command was sent.

Success requires:

1. command accepted/sent
2. printer command reply indicating success
3. fresh printer telemetry / push status received after the command
4. target AMS/tray metadata matching the requested values

The verification requirement is one of the core safety properties of this project. Do not weaken it.

---

## HTTP error model

Use stable machine-readable error identifiers.

Existing behavior to preserve:

- `400` — invalid JSON / invalid request
- `401` — missing/invalid Bearer token
- `404` — unknown route/resource
- `413` — request body too large
- `502` — send failure / printer rejection / verification request failure
- `503` — printer not ready
- `504` — printer reply timeout / verification timeout

Example:

```json
{
    "status": "error",
    "error": "printer_not_ready",
    "message": "Printer is currently not ready"
}
```

Do not expose secrets, raw credentials, private plugin data, or giant raw printer payloads in public error responses.

---

## External spool support

Do NOT invent or fake external-spool write support.

It is acceptable for v1 to report:

```json
{
    "features": {
        "externalFilamentWrite": false
    }
}
```

Only add an endpoint such as:

```text
POST /api/v1/external/{slot}/filament
```

after the actual printer/plugin behavior has been established and verified on real hardware.

If implemented later, use the same command-reply-plus-fresh-status verification requirements as AMS writes.

---

## Connection lifecycle

Printer communication is persistent.

The HTTP server must remain alive while the printer reconnects.

The service must:

1. initialize the plugin/backend
2. bind/select the configured printer
3. connect using printer ID/IP/access code
4. request fresh telemetry
5. transition to ready only after valid fresh printer state exists
6. detect stale/disconnected state
7. reconnect automatically with bounded backoff
8. recover without requiring process restart

`/health` must accurately reflect this state.

Do not block the entire HTTP server while reconnecting.

Write requests while not ready must fail quickly with HTTP 503.

---

## Concurrency

The printer command path must be serialized unless the underlying API is proven safe for concurrent commands.

Avoid races between:

- reconnect
- push-status handling
- filament writes
- verification state
- HTTP request threads

Sequence IDs must be unique for the lifetime of the process.

The prototype used IDs starting around 20000; the exact start value is not part of the public API, uniqueness is.

Never verify a command against stale telemetry that predates that command.

---

## Internal architecture

Keep `main.cpp` small.

Recommended structure:

```text
include/bfb/
  config.hpp
  types.hpp
  backend.hpp
  bridge_service.hpp
  http_server.hpp

src/
  main.cpp
  config.cpp
  plugin_backend.cpp
  bridge_service.cpp
  http_server.cpp

tests/
  config_test.cpp
  request_validation_test.cpp
  bridge_service_test.cpp
  http_api_test.cpp
  verification_test.cpp
```

Exact file names may differ if the repository already has a sensible structure, but keep these responsibilities separated.

### Config

Environment/file parsing, defaults and validation.

### Backend

Thin wrapper around the Bambu networking plugin ABI.

No HTTP logic.

### Bridge service

Printer lifecycle, reconnect, command serialization, sequence IDs and verification.

No HTTP-library-specific code.

### HTTP server

Routing, authentication, JSON parsing/serialization and HTTP status mapping.

No direct plugin calls.

This separation is important because tests must be able to use a fake backend without a real printer or plugin.

---

## Tests

All normal tests must run without:

- a real printer
- Bambu cloud
- proprietary plugins
- private credentials
- internet access

Provide a fake/mock backend.

Minimum required tests:

### Configuration

- required variables
- defaults
- invalid ports
- short HTTP token rejected
- secret-file support if implemented
- secret values never rendered by config/log helpers

### Authentication

- health does not require token
- write without token => 401
- wrong token => 401
- correct token accepted

Use constant-time token comparison where practical.

### Request validation

- malformed JSON
- missing profile
- missing setting
- missing type
- invalid color
- invalid temperatures
- invalid AMS/tray indices
- oversized body

### Printer state

- disconnected => health 503
- connected but no fresh status => health 503
- ready => health 200
- reconnecting flag
- stale telemetry makes bridge non-ready

### Filament writes

- command send failure
- printer rejects command
- printer reply timeout
- successful reply but no fresh telemetry
- fresh telemetry but wrong slot contents
- successful reply + fresh matching telemetry => 200 verified
- telemetry predating command must never verify it

### Concurrency

- concurrent writes are serialized
- reconnect cannot corrupt an active verification
- unique sequence IDs

Run tests with:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Do not merge code that requires manual printer testing for basic correctness.

---

## Live tests

Real-printer tests must be opt-in and must never run in normal CI.

Use an explicit opt-in such as:

```text
BFB_LIVE_TEST=1
```

A live smoke test may check:

1. `/health`
2. capabilities
3. one filament update
4. verification of updated tray state

Never commit live credentials or printer IDs.

---

## Logging

Logs should be operationally useful.

Include:

- startup/version
- selected backend
- plugin version
- printer ID
- connection transitions
- reconnect attempts
- sequence ID for commands
- command outcome
- verification outcome and duration

Never log:

- access code
- Bearer token
- Authorization header
- private certificates/keys
- slicer credentials
- full environment dump

Log errors once at the appropriate layer; avoid duplicate stack/noise where possible.

---

## Docker

Provide a production Docker image.

Requirements:

- multi-stage build
- minimal runtime image
- non-root runtime user where practical
- persistent `/data`
- port 8080
- Docker `HEALTHCHECK` against `/health`
- `linux/amd64`
- `linux/arm64`
- no proprietary Bambu binaries inside the image
- no credentials baked into image layers

Example standalone usage must be provided in:

- `docker-compose.example.yml`
- `.env.example`

Do not put real-looking secrets into `.env.example`.

---

## open-bamboo-networking dependency

Prefer a pinned git submodule or another reproducible pinned dependency.

Never silently follow upstream `master`.

Record the pinned version/commit in documentation and `THIRD_PARTY_NOTICES.md`.

If code from its `plugin_runner` is compiled into this project, preserve its AGPL licensing requirements and notices.

The public image should prefer the open-source backend.

An optional user-mounted stock plugin may remain supported through `BAMBU_PLUGIN`, but must not be downloaded or bundled by this project.

---

## Home Assistant

Home Assistant packaging is a wrapper around the standalone bridge.

The core bridge must NEVER depend on Home Assistant APIs.

First make Docker standalone mode work.

Then provide a Home Assistant App/add-on wrapper which:

- uses the same released container image where possible
- exposes printer ID
- printer IP
- access code as secret/password
- HTTP token as secret/password
- persistent data directory
- port/configuration needed by SpoolmanSync
- supports amd64 and aarch64 when the selected backend supports them

Avoid maintaining a second implementation for HA.

The HA wrapper should only translate Supervisor options into bridge configuration and launch the exact same binary/container behavior.

---

## SpoolmanSync compatibility

SpoolmanSync is an important client but NOT a dependency of the bridge.

The following existing client behavior must continue to work:

```text
BAMBU_BRIDGE_URL=http://...
BAMBU_BRIDGE_TOKEN=...
```

SpoolmanSync currently expects:

- `GET /health`
- HTTP 503 from health to be a valid not-ready state
- `POST /api/v1/ams/{ams}/trays/{tray}/filament`
- Bearer authentication
- response `status == "synced"`
- response `verified == true`

Do not change this contract in API v1.

---

## OpenAPI

Maintain an `openapi.yaml` describing all public endpoints.

The OpenAPI document is part of the public API contract.

When HTTP behavior changes, update:

- implementation
- tests
- `openapi.yaml`
- README examples

in the same commit/PR.

---

## Documentation

Required documentation:

```text
README.md
docs/architecture.md
docs/configuration.md
docs/docker.md
docs/home-assistant.md
docs/troubleshooting.md
openapi.yaml
THIRD_PARTY_NOTICES.md
```

README should begin with the user problem, not internal implementation details.

Suggested first sentence:

> Assign filament metadata once and let Bambu Studio see the same filament that your inventory system says is loaded.

Clearly explain:

- what the bridge does
- what it does not do
- supported/tested printers
- Developer/LAN mode requirements
- backend choices
- Docker quick start
- Home Assistant quick start
- SpoolmanSync configuration
- security model

Compatibility must distinguish:

- tested
- expected to work
- untested

Do not claim printer support that has not been tested.

Initial known real-hardware test target is Bambu X2D.

---

## Non-goals

Do not turn this into:

- a generic Bambu printer control API
- a print submission service
- a camera server
- a Spoolman replacement
- a Home Assistant integration
- a filament profile database
- a Bambu Studio preset manager

Keep the project deliberately narrow.

Its job is reliable, authenticated, verified filament-slot metadata updates.

---

## CI

GitHub Actions should run:

1. CMake configure/build
2. unit/integration tests using fake backend
3. formatting/lint checks if configured
4. Docker build
5. multi-arch image build validation

Release workflow should build:

```text
ghcr.io/<owner>/bambu-filament-bridge
```

for:

```text
linux/amd64
linux/arm64
```

Suggested tags:

```text
v1.2.3
1.2
latest
```

Do not publish `latest` from arbitrary branches.

---

## Coding rules

- C++17 minimum unless the existing implementation already requires newer.
- Prefer RAII.
- Avoid raw owning pointers.
- Avoid detached threads.
- Make shutdown deterministic.
- No `sleep()`-based synchronization in core logic when condition variables or explicit state are appropriate.
- Keep plugin ABI details isolated from application logic.
- Prefer value types.
- Public API structs should be simple and testable.
- Treat printer callbacks as untrusted asynchronous input.
- Validate every external value.
- Preserve backwards compatibility within `/api/v1`.
- Do not perform unrelated large refactors while fixing a specific behavior.

---

## Definition of done

A change is complete only when:

- project builds from a clean checkout
- all non-live tests pass
- Docker image builds
- no proprietary files are included
- no secrets are included
- API contract remains compatible
- OpenAPI is updated
- relevant documentation is updated
- `git diff` contains no accidental generated/build files

For substantial tasks, finish by reporting:

1. files changed
2. architecture decisions
3. tests run and results
4. anything that still requires real-printer validation
