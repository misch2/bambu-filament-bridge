# Home Assistant App

The App is a configuration wrapper around the exact released standalone image.
It does not call Home Assistant or Supervisor APIs. The format was checked against
the current [App configuration documentation](https://developers.home-assistant.io/docs/apps/configuration/)
and [repository format](https://developers.home-assistant.io/docs/apps/repository/):
root `repository.yaml`, per-App `config.yaml`, generic multi-architecture image,
password schema fields, and `/data/options.json`. No legacy `build.yaml` is needed.

Prerequisite: publish the `0.1.0` release image before installing from the store.
The repository does not publish anything during local implementation/testing.

1. Enable Developer/LAN mode and note the printer's serial, IPv4 address and code.
2. In the Home Assistant App store repository menu add
   `https://github.com/misch2/bambu-filament-bridge`.
3. Install **Bambu Filament Bridge**. Supported packaging architectures are
   `amd64` and `aarch64`.
4. Set `printer_id`, `printer_ip`, `access_code`, and `http_token`. The last two
   use password fields. Generate a random HTTP token of at least 32 characters.
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

`/health` HTTP 503 while reconnecting is expected. The API has no ingress UI; use
the host URL from a trusted network. Installation/Supervisor runtime and real X2D
operation still require a live environment validation. Plugin and image build
checks do not establish live hardware compatibility.
