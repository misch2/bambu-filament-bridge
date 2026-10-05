# Bambu Filament Bridge

Assign filament metadata once and let Bambu Studio see the same filament that your inventory system says is loaded.

This small standalone Linux service makes authenticated, verified updates to the
metadata of a physical Bambu AMS tray. SpoolmanSync or another client supplies the
profile, setting, material, color and temperatures. The bridge owns printer
communication only. It contains no inventory, profile mapping or Home Assistant
entity logic. **This is not Bambu Filament Manager synchronization**, a preset
manager, or a generic printer-control API.

```mermaid
flowchart LR
  Inventory[Inventory system] --> Client[SpoolmanSync / other client]
  Client -->|HTTP + Bearer token| Bridge[Bambu Filament Bridge]
  Bridge --> Plugin[Networking ABI plugin]
  Plugin -->|Persistent LAN connection| Printer[Bambu AMS tray]
```

- Persistent connection with bounded automatic reconnect; HTTP remains available.
- Public `/health`; authenticated capabilities and AMS writes.
- Success requires send, successful printer reply, and fresh matching tray telemetry.
- Fake-backend tests need no printer, plugin, credentials or internet.
- Multi-architecture Docker image and a thin Home Assistant App wrapper.
- AGPL-3.0-or-later; no proprietary plugin or private signing material shipped.

## Compatibility

| Target | Evidence |
| --- | --- |
| Bambu X2D with stock plugin `02.08.02.54`, ABI `0x020802` | Original prototype tested on real hardware, as supplied by the project owner. |
| Refactored bridge with pinned open-bamboo-networking v2.2.0 | Fake-backend tests and local plugin/image build checks; requires X2D live validation. |
| Other printers | Untested by this project. Compatible ABI alone does not establish support. |
| External spools | Unsupported in v1; `externalFilamentWrite=false`. |

Enable the printer's LAN access and **Developer Mode** for the default open-source
backend. Firmware and mode requirements can differ between models. No Bambu cloud
login is used. An advanced operator can mount their own compatible stock plugin
and its required certificates; see [configuration](docs/configuration.md).

## Docker quick start

```sh
git clone --recurse-submodules https://github.com/misch2/bambu-filament-bridge.git
cd bambu-filament-bridge
cp .env.example .env
# Fill the four values in .env; use a random token of at least 32 characters.
docker compose -f docker-compose.example.yml up -d --build
curl -i http://localhost:8080/health
```

Building works before a registry release exists. Released images use
`ghcr.io/misch2/bambu-filament-bridge:0.1.0`; publishing is a separate release
action. Never expose this plain HTTP API directly to the internet. Use a trusted
LAN or a TLS reverse proxy, restrict the host port, and protect the token and
access code. `/health` intentionally reveals printer ID/IP and version information
without authentication. HTTP 503 means the process is alive but the printer is
not ready. [Docker instructions](docs/docker.md) cover storage and secret files.

## Home Assistant quick start

After the `0.1.0` multi-architecture image is published, add this GitHub repository
to the Home Assistant App store repositories, install **Bambu Filament Bridge**,
fill its four options, and start it. It runs the exact standalone image and
persists `/data`. [Full App setup](docs/home-assistant.md).

## SpoolmanSync

Set these on SpoolmanSync, using the same token configured on the bridge:

```text
BAMBU_BRIDGE_URL=http://<bridge-host>:8080
BAMBU_BRIDGE_TOKEN=<your-generated-token>
```

No SpoolmanSync changes are required for API v1. The local `BambuBridgeClient`
contract was compared with health names/types, health 503 behavior, Bearer auth,
the POST path and body, and all success fields. See [compatibility evidence](docs/compatibility.md).

## API example

```sh
curl -H "Authorization: Bearer $BAMBU_HTTP_TOKEN" \
  http://localhost:8080/api/v1/capabilities

# This changes the metadata of physical AMS 0 / tray 3.
curl -X POST -H "Authorization: Bearer $BAMBU_HTTP_TOKEN" \
  -H 'Content-Type: application/json' \
  --data '{"profile":"GFG99","setting":"GFSG99_15","type":"PETG","color":"808080FF","tempMin":220,"tempMax":260}' \
  http://localhost:8080/api/v1/ams/0/trays/3/filament
```

```json
{"status":"synced","verified":true,"elapsedMs":1234,"sequenceId":"20001","amsId":0,"trayId":3}
```

IDs shown in examples are not sequence guarantees. See [OpenAPI](openapi.yaml)
for errors and bounds. HTTP `502`/`504` may occur after a command reached the
printer: inspect slot state before retrying.

## Build and backend

The default image builds the open backend from submodule commit
`8656b66125d5a6dde0a96de705896767508d8141`. The same thin ABI adapter supports a
user-supplied plugin through `BAMBU_PLUGIN`. The project never downloads stock
plugins. Dependency licenses and pins are in [third-party notices](THIRD_PARTY_NOTICES.md).

On Linux with CMake >=3.20 and a C++17 compiler:

```sh
git submodule update --init
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The plugin itself is built separately in Docker; core builds and tests use the
vendored JSON header and need no network after checkout. Windows developers use
Docker or WSL. See [contributing](CONTRIBUTING.md), [architecture](docs/architecture.md),
[configuration](docs/configuration.md), and [troubleshooting](docs/troubleshooting.md).
