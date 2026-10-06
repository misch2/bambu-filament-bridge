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

## Native Linux / systemd

Run the same `bambu-bridge` binary directly on a Linux host, without Docker or
Home Assistant. The backend and HTTP API are unchanged: `BAMBU_PLUGIN` selects
the networking library. Keep your existing plugin, certificates and runtime
configuration when migrating a working installation. Running natively does not
itself change the printer's mode requirements or enable cloud communication.

The example below assumes an existing `runner` user/group and checkout at
`/home/runner/bambu-bridge`. Adjust these paths and the account in the
[example systemd unit](packaging/bambu-bridge.service) for another installation.
Run the build and configuration steps as the service account. On Debian/Ubuntu,
install the build tools first (CMake >=3.20 and C++17 are required):

```sh
sudo apt-get update
sudo apt-get install build-essential cmake git ca-certificates curl
cd /home/runner/bambu-bridge
git submodule update --init
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For a new checkout, clone with `--recurse-submodules` as shown in the Docker
quick start. These commands build the bridge and run its fake-backend tests;
they do not build or download a proprietary plugin. Use your existing
ABI-compatible stock plugin (the prototype used `02.08.02.54`, ABI `0x020802`),
with its required runtime libraries installed on the host and a matching CPU
architecture. Stock-plugin operation with the refactored bridge still needs
real-printer validation; the recorded prototype test does not specify printer
mode.

For the same open-source backend as the Docker image, build the pinned submodule
separately, without switching it to an upstream branch:

```sh
sudo apt-get install pkg-config libssl-dev libcurl4-openssl-dev zlib1g-dev uthash-dev
cmake -S third_party/open-bamboo-networking -B build-plugin \
  -DOBN_VERSION=02.08.02.99 -DOBN_RELEASE=ON \
  -DOBN_PATCH_CLIENT_CONF=OFF -DOBN_BUILD_TESTS=OFF
cmake --build build-plugin --target bambu_networking -j2
```

The initial plugin build fetches pinned dependencies and needs internet access.
Set `BAMBU_PLUGIN=/home/runner/bambu-bridge/build-plugin/libbambu_networking.so`
in that case. Without operator-supplied signing credentials, this backend
requires LAN-only mode and Developer Mode on firmware enforcing authorization.
Use `OBN_BLOCK_CLOUD=1`, `OBN_LOG_LEVEL=info` and `OBN_LOG_TO_FILE=0` in `.env`
to match the image's plugin settings.

Keep an existing `.env`; for a new installation only, copy `.env.example` to
`.env`. Fill in `BAMBU_DEV_ID`, `BAMBU_DEV_IP`, `BAMBU_ACCESS_CODE` and a random
`BAMBU_HTTP_TOKEN` of at least 32 characters. Also set absolute host paths:

```dotenv
# Replace with the path to your existing compatible plugin.
BAMBU_PLUGIN=/absolute/path/to/libbambu_networking.so
BAMBU_DATA_DIR=/home/runner/.local/share/bambu-filament-bridge
BAMBU_CERT_DIR=/home/runner/.local/share/bambu-filament-bridge/certs
BAMBU_HTTP_BIND=0.0.0.0
BAMBU_HTTP_PORT=8080
```

For an existing setup, use its actual data and certificate directories instead
of the example paths. The account must be able to read the plugin and required
certificates and write to both runtime directories. The bridge creates missing
runtime directories but does not supply stock-plugin certificates. Protect
`.env` with `chmod 600 .env`. Keep private runtime files outside the checkout.
See [configuration](docs/configuration.md) for secret-file alternatives.

The binary does not read `.env` itself; systemd's `EnvironmentFile` loads it.
Use literal `KEY=value` assignments, without `export`, `$HOME`, `~` or references
to other variables. Changes to `.env` take effect when the service restarts.

For a new service, install and start the unit below. If the service already
exists, review the example against your current unit before replacing it, then
use `sudo systemctl restart bambu-bridge.service` after `daemon-reload`.

```sh
sudo install -m 0644 packaging/bambu-bridge.service /etc/systemd/system/bambu-bridge.service
sudo systemd-analyze verify /etc/systemd/system/bambu-bridge.service
sudo systemctl daemon-reload
sudo systemctl enable --now bambu-bridge.service
sudo systemctl status bambu-bridge.service
sudo journalctl -u bambu-bridge.service -f
```

After changing only `.env`, a restart is enough. Check
`curl -i http://localhost:8080/health`: HTTP 503 while connecting/reconnecting is
expected; HTTP 200 indicates readiness. SpoolmanSync uses
`BAMBU_BRIDGE_URL=http://<Linux-host-IP>:8080` and the same bridge HTTP token.
Restrict access to the HTTP port to your trusted network, as for Docker.

## Home Assistant quick start

After the `0.1.0` multi-architecture image is published, add this GitHub repository
to the Home Assistant App store repositories, install **Bambu Filament Bridge**,
fill its four options, and start it. It runs the exact standalone image and
persists `/data`. [Full App setup](docs/home-assistant.md).

The App needs no manual certificate copying by default: `verify_printer_tls`
defaults to `false`. Printer traffic remains TLS encrypted, but the certificate
and hostname are not verified, so a LAN attacker can impersonate the printer.
Enable the option with your own printer CA bundle if you need TLS verification.

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

The plugin itself is built separately (see [native setup](#native-linux--systemd)
or Docker); core builds and tests use the vendored JSON header and need no
network after checkout. Windows developers use
Docker or WSL. See [contributing](CONTRIBUTING.md), [architecture](docs/architecture.md),
[configuration](docs/configuration.md), and [troubleshooting](docs/troubleshooting.md).
