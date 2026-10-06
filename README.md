# Bambu Filament Bridge

Keep the filament loaded in your Bambu printer in sync with your filament inventory.

Bambu Filament Bridge is a small service that allows applications such as
[SpoolmanSync](https://github.com/gibz104/SpoolmanSync) to update the filament
metadata stored in a Bambu AMS tray or external spool holder.

When you assign a spool in SpoolmanSync, the bridge can update the corresponding
AMS tray or external holder in the printer as well. Bambu Studio then sees the same filament type,
color and profile that your inventory system says is loaded.

```mermaid
flowchart LR
  Spoolman[Spoolman] --> SpoolmanSync[SpoolmanSync]
  SpoolmanSync -->|HTTP| Bridge[Bambu Filament Bridge]
  Bridge --> Printer[Bambu printer]
  Printer --> Studio[Bambu Studio]
```

## What it does

Bambu Filament Bridge updates filament information stored in the printer,
including:

- filament type
- filament profile
- filament color
- nozzle temperature range

Updates are verified against fresh printer telemetry before the bridge reports
success.

The bridge keeps a persistent LAN connection to the printer and reconnects
automatically if the connection is lost.

### What it does not do

This project does **not** synchronize the Bambu Studio Filament Manager database.

It also does not manage slicer presets, submit print jobs, control the camera or
replace Spoolman.

Its purpose is deliberately narrow:

> Keep the metadata of the filament physically loaded in the printer in sync
> with another application such as SpoolmanSync.

---

## Compatibility

| Printer / feature | Status |
| --- | --- |
| Bambu X2D | ✅ Tested |
| AMS filament slots | ✅ Supported |
| External spool slots | ✅ Supported via IDs 254/255 |
| Other Bambu printers | ⚠️ Not yet tested |

Other Bambu printers using the same networking interface may work, but only the
X2D has currently been verified with this project.

### Printer mode

The default Docker and Home Assistant installations use the open-source
`open-bamboo-networking` backend.

For normal LAN operation, enable **LAN access / LAN-only mode** and
**Developer Mode** on the printer.

The exact menu location depends on the printer model and firmware version.

The bridge does not require a Bambu Cloud login.

---

# Installation

There are three supported ways to run the bridge:

1. **Home Assistant App** — easiest if you already use Home Assistant
2. **Docker** — recommended for NAS, server or Linux installations
3. **Native Linux / systemd** — for advanced setups

---

## Home Assistant

If you already use Home Assistant, this is usually the easiest option.

Add this repository to the Home Assistant App store repositories:

```text
https://github.com/misch2/bambu-filament-bridge
```

Then install **Bambu Filament Bridge**.

Configure:

- **Printer ID / Serial number**
- **Printer IP address**
- **Printer access code**
- **Bridge API token**

The API token is a password you create yourself. Use a random value of at least
32 characters.

Start the App and check its log. Once the printer is connected, the bridge is
ready for SpoolmanSync.

See the App page in Home Assistant for configuration details.

---

## Docker

Clone the repository:

```sh
git clone --recurse-submodules https://github.com/misch2/bambu-filament-bridge.git
cd bambu-filament-bridge
```

Create your configuration:

```sh
cp .env.example .env
```

Edit `.env` and set at least:

```dotenv
BAMBU_DEV_ID=your-printer-serial
BAMBU_DEV_IP=192.168.1.100
BAMBU_ACCESS_CODE=your-printer-access-code
BAMBU_HTTP_TOKEN=replace-with-a-random-token-at-least-32-characters-long
```

Then start the bridge:

```sh
docker compose -f docker-compose.example.yml up -d --build
```

Check its status:

```sh
curl -i http://localhost:8080/health
```

A ready bridge returns HTTP `200`.

While the printer is connecting or reconnecting, `/health` may return HTTP
`503`. This is normal and means the bridge itself is running but the printer is
not ready yet.

### Docker image

Released versions are also available from GitHub Container Registry:

```text
ghcr.io/misch2/bambu-filament-bridge
```

For production installations, prefer a versioned tag instead of `latest`.

---

## Native Linux / systemd

The bridge can also run directly on a Linux machine without Docker.

Requirements:

- Linux
- CMake 3.20 or newer
- C++17 compiler
- compatible Bambu networking plugin

On Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake git ca-certificates curl
```

Clone and build:

```sh
git clone --recurse-submodules https://github.com/misch2/bambu-filament-bridge.git
cd bambu-filament-bridge

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Create an environment file containing your printer configuration:

```dotenv
BAMBU_DEV_ID=your-printer-serial
BAMBU_DEV_IP=192.168.1.100
BAMBU_ACCESS_CODE=your-printer-access-code
BAMBU_HTTP_TOKEN=replace-with-a-random-token-at-least-32-characters-long

BAMBU_HTTP_BIND=0.0.0.0
BAMBU_HTTP_PORT=8080
BAMBU_DATA_DIR=/var/lib/bambu-filament-bridge
```

An example systemd unit is included in:

```text
packaging/bambu-bridge.service
```

Install it after adjusting its paths and service account for your system:

```sh
sudo install -m 0644 packaging/bambu-bridge.service \
  /etc/systemd/system/bambu-bridge.service

sudo systemctl daemon-reload
sudo systemctl enable --now bambu-bridge.service
```

View the log with:

```sh
sudo journalctl -u bambu-bridge.service -f
```

Native installations are intended mainly for users who already have a suitable
networking plugin setup. Docker or the Home Assistant App is easier for a new
installation.

---

# SpoolmanSync setup

In SpoolmanSync configure:

```text
BAMBU_BRIDGE_URL=http://<bridge-address>:8080
BAMBU_BRIDGE_TOKEN=<the-token-configured-on-the-bridge>
```

For example:

```text
BAMBU_BRIDGE_URL=http://192.168.1.50:8080
```

Use the same API token that you configured in Bambu Filament Bridge.

After that, assigning a compatible spool to an AMS tray in SpoolmanSync can
also update the corresponding filament metadata in the printer.

If the bridge is unavailable, the spool assignment in SpoolmanSync remains
valid; printer synchronization is an additional operation rather than the
source of truth.

---

# Checking the bridge

The simplest status check is:

```sh
curl http://<bridge-address>:8080/health
```

Example ready response:

```json
{
  "status": "ready",
  "connected": true,
  "ready": true,
  "printerId": "01P..."
}
```

You can also check supported features:

```sh
curl \
  -H "Authorization: Bearer $BAMBU_HTTP_TOKEN" \
  http://<bridge-address>:8080/api/v1/capabilities
```

---

# Security

The bridge provides a local HTTP API intended for a trusted home network.

**Do not expose port 8080 directly to the public internet.**

Write operations require the configured Bearer token, but the connection itself
is plain HTTP.

Recommended options are:

- keep the bridge accessible only on your LAN
- restrict access with a firewall
- use a VPN such as Tailscale or WireGuard for remote access
- use a TLS reverse proxy if the API must cross an untrusted network

Keep these values private:

- printer access code
- bridge API token
- any networking-plugin credentials

The `/health` endpoint does not require authentication and may expose basic
printer information such as printer ID, IP address and software versions.

---

# Troubleshooting

### `/health` returns HTTP 503

The bridge is running, but the printer is not ready.

Check:

- printer is powered on
- printer IP address is correct
- access code is correct
- LAN access is enabled
- Developer Mode is enabled when using the default open-source backend
- the bridge host can reach the printer over the network

### SpoolmanSync reports bridge authentication failure

Make sure the token configured in SpoolmanSync is exactly the same as
`BAMBU_HTTP_TOKEN` configured on the bridge.

### The bridge is connected, but filament synchronization fails

Check the bridge log. A synchronization is only reported as successful after
the printer confirms the command and the resulting slot metadata is verified.
Request logs show the validated target and filament metadata, HTTP result and
elapsed time. Command logs distinguish reply timeouts from verification failures,
including missing telemetry, missing slots and mismatched fields. Headers and
credentials are never logged; see [log details](docs/troubleshooting.md#request-and-command-logs).

### Bambu Studio does not update immediately

Make sure Bambu Studio has refreshed the printer state. The bridge changes the
metadata stored in the physical printer slot; Bambu Studio learns about that
state from the printer.

More troubleshooting information is available in
[`docs/troubleshooting.md`](docs/troubleshooting.md).

---

# API

The public API is versioned under `/api/v1`.

Current endpoints include:

```text
GET  /health
GET  /api/v1/capabilities
POST /api/v1/ams/{amsId}/trays/{trayId}/filament
```

Applications should use the API rather than depending on the internal Bambu
networking implementation.

The full API definition is available in [`openapi.yaml`](openapi.yaml).

External holders use the same endpoint with virtual AMS ID **254 for left/deputy**
and **255 for right/main** (or the single external holder), and tray ID **0**.
For example, update the left X2D holder:

```bash
curl --fail-with-body -X POST "$BAMBU_BRIDGE_URL/api/v1/ams/254/trays/0/filament" \
  -H "Authorization: Bearer $BAMBU_BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  --data '{"profile":"GFG99","setting":"GFSG99_15","type":"PETG","color":"808080FF","tempMin":220,"tempMax":260}'
```

Use ID 255 for the right holder. Capabilities reports `externalFilamentWrite=true`.
Success still requires a printer success reply and fresh matching holder telemetry;
see [compatibility and validation scope](docs/compatibility.md).

---

# License

Bambu Filament Bridge is licensed under the
**GNU Affero General Public License v3 or later (AGPL-3.0-or-later)**.

The project uses components from
[`open-bamboo-networking`](https://github.com/ClusterM/open-bamboo-networking).

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for details.

Bambu Filament Bridge is an independent community project and is not affiliated
with or endorsed by Bambu Lab.
