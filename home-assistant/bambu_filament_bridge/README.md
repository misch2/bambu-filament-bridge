# Bambu Filament Bridge

Bambu Filament Bridge lets applications such as SpoolmanSync update the
filament information stored in the AMS slots of your Bambu printer.

For example, when you assign a spool to an AMS tray in SpoolmanSync, the bridge
can update the printer with the same filament type, color and profile so that
Bambu Studio sees the correct loaded filament as well.

## Requirements

Currently tested with:

- **Bambu X2D**
- AMS filament slots

External spool slots are not yet supported.

The App communicates directly with the printer over your local network.

When using the default networking backend, enable **LAN access / LAN-only mode**
and **Developer Mode** on the printer.

No Bambu Cloud login is required.

## Configuration

The App asks for four values:

### Printer ID

The serial number / device ID of your Bambu printer.

Example:

```text
01P...
```

### Printer IP

The local IP address of your printer.

Example:

```text
192.168.1.100
```

It is recommended to give the printer a fixed DHCP lease so its address does
not change.

### Printer access code

The LAN access code shown by the printer.

This is stored as a secret and is used only to connect to your printer.

### API token

A password used by applications such as SpoolmanSync to access the bridge.

Create your own random token of at least **32 characters**.

For example, you can generate one with:

```sh
openssl rand -hex 32
```

Keep this token private.

## Starting the App

After saving the configuration:

1. Start **Bambu Filament Bridge**
2. Open the App log
3. Wait until the printer connection reports ready

Temporary reconnect messages are normal when the printer is starting,
rebooting or unavailable.

The bridge automatically reconnects when the printer becomes available again.

## SpoolmanSync configuration

Configure SpoolmanSync to use this bridge.

Use:

```text
BAMBU_BRIDGE_URL=http://<home-assistant-ip>:8080
BAMBU_BRIDGE_TOKEN=<your API token>
```

For example:

```text
BAMBU_BRIDGE_URL=http://192.168.1.10:8080
```

`BAMBU_BRIDGE_TOKEN` must be the same token you entered in the App
configuration.

After that, assigning a spool to an AMS tray in SpoolmanSync can also update
the corresponding filament metadata in the printer.

## What gets synchronized

The bridge can update:

- filament profile
- filament type
- color
- nozzle temperature range

A change is reported as successful only after the printer confirms it and the
new AMS tray state is verified.

## Health check

The bridge exposes:

```text
http://<home-assistant-ip>:8080/health
```

HTTP `200` means the printer is ready.

HTTP `503` means the App itself is running but the printer is currently not
connected or not ready yet.

## Security

The bridge API is intended for use on your trusted home network.

Do **not** expose port 8080 directly to the internet.

Keep both the printer access code and API token private.

For remote access, prefer a VPN such as Tailscale or WireGuard.

## Troubleshooting

If the bridge does not become ready, check:

- the printer is powered on
- the configured IP address is correct
- the LAN access code is correct
- LAN access is enabled on the printer
- Developer Mode is enabled
- Home Assistant can reach the printer over the local network

If SpoolmanSync reports an authentication error, verify that its
`BAMBU_BRIDGE_TOKEN` exactly matches the API token configured here.

Bambu Filament Bridge is an independent community project and is not affiliated
with or endorsed by Bambu Lab.