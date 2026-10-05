# Setup

Enter the printer ID, LAN IPv4 address, access code, and a random HTTP token of at
least 32 characters in Configuration. Enable Developer/LAN mode on the printer.
Start the App and check its logs for READY. Set SpoolmanSync's `BAMBU_BRIDGE_URL`
to `http://<HA-host-IP>:8080` and `BAMBU_BRIDGE_TOKEN` to the HTTP token.

The Network tab can change the host port if 8080 is occupied. The Docker health
indicator stays unhealthy while the printer is unavailable; `/health` HTTP 503
is a valid bridge state. Data and user options persist under `/data`.

This App requires the public `0.1.0` image to have been released. See
[full documentation](https://github.com/misch2/bambu-filament-bridge/blob/main/docs/home-assistant.md).
