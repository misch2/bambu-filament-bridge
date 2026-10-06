# Home Assistant App

The App is a configuration wrapper around the exact released standalone image.
It does not call Home Assistant or Supervisor APIs. The format was checked against
the current [App configuration documentation](https://developers.home-assistant.io/docs/apps/configuration/)
and [repository format](https://developers.home-assistant.io/docs/apps/repository/):
root `repository.yaml`, per-App `config.yaml`, generic multi-architecture image,
password schema fields, and `/data/options.json`. No legacy `build.yaml` is needed.

Prerequisite: publish the `0.1.2` release image before installing from the store.
The repository does not publish anything during local implementation/testing.

1. For the bundled open-source backend, enable LAN-only mode and Developer Mode, then note the printer’s serial, IPv4 address and access code. Requirements differ when using a user-supplied stock plugin.
2. In the Home Assistant App store repository menu add
   `https://github.com/misch2/bambu-filament-bridge`.
3. Install **Bambu Filament Bridge**. Supported packaging architectures are
   `amd64` and `aarch64`.
4. Set `printer_id`, `printer_ip`, `access_code`, and `http_token`. The last two
   use password fields. Generate a random HTTP token of at least 32 characters.
   Leave `verify_printer_tls` at its default `false` to start without copying certificates.
5. Start the App and wait for READY in logs. Adjust the exposed host port from
   8080 in Network if needed.
6. On SpoolmanSync set `BAMBU_BRIDGE_URL=http://<HA-host-IP>:8080` and
   `BAMBU_BRIDGE_TOKEN=<the-App-http_token>`. Use the selected host port.

`/data` is Supervisor-managed persistent storage. The image's entrypoint reads
the four options, exports the existing bridge variables, and execs the same
binary. The core bridge remains independent. The entrypoint reads Supervisor's
options and initializes ownership of the `/data` directory as root, then
permanently drops to UID/GID 10001 before executing the daemon. It does not
recursively change ownership of options or private files. This supports
Supervisor's root-owned data mount without running the bridge as root.

## Printer TLS

`verify_printer_tls` defaults to `false`, including when an existing installation
has no value for the option. No certificate upload is needed. The printer connection
still uses TLS encryption, but certificate and hostname checks are disabled: a LAN
attacker could impersonate the printer and obtain its access code or alter traffic.
HTTP Bearer authentication and command-reply-plus-fresh-telemetry verification
remain enabled; they do not authenticate the TLS peer.

To enable printer TLS verification, put your own Bambu Studio
`resources/cert/printer.cer` at:

```text
/addon_configs/<repository-id>_bambu_filament_bridge/certs/printer.cer
```

Use a file-sharing/editor App that exposes App configuration directories. The
repository ID is assigned by Supervisor; for a local installation it is `local`.
The directory is mounted read-only as `/config`. On startup, the entrypoint copies
`/config/certs/printer.cer` to `/data/certs/printer.cer`, owned by UID/GID 10001,
without changing the source. Set `verify_printer_tls: true` and restart the App.
The copied certificate persists in `/data` and is refreshed on startup whenever
the source is present. A missing/empty certificate prevents startup when verification
is enabled; an invalid or unsuitable CA bundle causes a TLS connection failure.
The image does not distribute certificates or signing material.

The App manages only `lan_tls_skip_verify` in `/data/obn.conf`, preserving other
settings. The App option takes precedence over that key on every startup because
the backend overwrites `OBN_SKIP_TLS_VERIFY` when loading its configuration.
Standalone startup leaves backend TLS configuration unchanged. The backend's
skip-verification setting also affects cloud TLS; this App uses cloud blocking.

`/health` HTTP 503 while reconnecting is expected. The API has no ingress UI; use
the host URL from a trusted network. Installation/Supervisor runtime and real X2D
operation still require a live environment validation. Plugin and image build
checks do not establish live hardware compatibility.
