# Setup

Enter the printer ID, LAN IPv4 address, access code, and a random HTTP token of at
least 32 characters in Configuration. Enable Developer/LAN mode on the printer.
Leave `verify_printer_tls` at its default `false`; no certificate copying is needed.
Start the App and check its logs for READY. Set SpoolmanSync's `BAMBU_BRIDGE_URL`
to `http://<HA-host-IP>:8080` and `BAMBU_BRIDGE_TOKEN` to the HTTP token.

The Network tab can change the host port if 8080 is occupied. The Docker health
indicator stays unhealthy while the printer is unavailable; `/health` HTTP 503
is a valid bridge state. Data and user options persist under `/data`.

Printer connections remain TLS encrypted, but the default disables certificate
and hostname verification. A LAN attacker could impersonate the printer, capture
its access code or alter traffic. HTTP authentication and filament-write verification
remain enabled.

For optional TLS verification, upload your own Bambu Studio
`resources/cert/printer.cer` to
`/addon_configs/<repository-id>_bambu_filament_bridge/certs/printer.cer`, enable
`verify_printer_tls`, and restart. The App imports it from its read-only `/config`
mount into persistent `/data/certs`. Missing/empty certificates prevent startup
when the option is enabled. See the [full setup](../../docs/home-assistant.md).
