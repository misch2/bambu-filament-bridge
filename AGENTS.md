# AGENTS.md

## Project overview

`bambu-filament-bridge` is a small standalone service that exposes a stable
HTTP API for changing filament metadata on Bambu Lab printers.

Its primary consumer is SpoolmanSync, but the bridge MUST remain independent
from Spoolman, SpoolmanSync and Home Assistant.

Conceptually:

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

The bridge owns printer communication only.

It must NOT contain filament inventory logic, Spoolman-specific logic,
material-to-Bambu-profile mapping, or Home Assistant entity logic.

---

## Current known-good implementation

The original working prototype is a C++17 daemon.

It uses the Bambu networking plugin ABI through the plugin loader originating
from `ClusterM/open-bamboo-networking`.

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

open-bamboo-networking is licensed under AGPL-3.0-or-later.

Unless all AGPL-derived code is removed and independently replaced, this
repository MUST be licensed AGPL-3.0-or-later.

Preserve upstream copyright/license notices.

Create:

- `LICENSE`
- `THIRD_PARTY_NOTICES.md`

Do NOT copy or redistribute Bambu Lab's proprietary
`libbambu_networking.so`, private signing material, slicer credentials,
certificates, access codes, tokens, or other user secrets.

The bridge may load a user-supplied stock plugin, but the project and its
container images must not ship that proprietary plugin.

---

## Supported networking backends

The architecture must support loading a Bambu networking ABI-compatible
plugin from a configurable path.

Preferred/default public backend:

- open-bamboo-networking

Advanced compatibility backend:

- user-provided stock `libbambu_networking.so`

Do not special-case business logic based on backend unless strictly necessary.

Both should go through the same internal plugin/backend abstraction.

The open-source backend should be the default for distributable Docker and
Home Assistant builds.

Pin upstream dependencies to a known commit/tag. Never build production
images against an unpinned `master` branch.

---

## Configuration

Preserve compatibility with the existing prototype environment variables.

Required printer configuration:

    BAMBU_DEV_ID
    BAMBU_DEV_IP
    BAMBU_ACCESS_CODE

HTTP configuration:

    BAMBU_HTTP_TOKEN
    BAMBU_HTTP_BIND
    BAMBU_HTTP_PORT

Plugin/runtime configuration:

    BAMBU_PLUGIN
    BAMBU_CERT_DIR
    BAMBU_DATA_DIR

Current defaults where appropriate:

    BAMBU_HTTP_BIND=0.0.0.0
    BAMBU_HTTP_PORT=8080
    BAMBU_DATA_DIR=/data

The old prototype-specific filesystem defaults such as `/home/runner/...`
must NOT remain as defaults in the public project.

`BAMBU_HTTP_TOKEN` must be required for write endpoints and must have a
reasonable minimum strength (currently >= 32 characters).

Supporting `*_FILE` alternatives for secrets is encouraged, for example:

    BAMBU_ACCESS_CODE_FILE
    BAMBU_HTTP_TOKEN_FILE

Environment variables must take precedence consistently and behavior must be
documented.

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
