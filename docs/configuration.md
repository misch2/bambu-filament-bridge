# Configuration

| Variable | Default / requirement |
| --- | --- |
| `BAMBU_DEV_ID` | Required printer serial/device ID (letters, digits, underscore, hyphen; max 128). |
| `BAMBU_DEV_IP` | Required LAN IPv4 address. |
| `BAMBU_ACCESS_CODE` | Required; use your own printer LAN access code. |
| `BAMBU_HTTP_TOKEN` | Required random Bearer token, 32..4096 characters, no whitespace. |
| `BAMBU_HTTP_BIND` | `0.0.0.0`, IPv4 bind address. |
| `BAMBU_HTTP_PORT` | `8080`, integer 1..65535. |
| `BAMBU_PLUGIN` | `/usr/local/lib/bfb/libbambu_networking.so` (open backend in the image). |
| `BAMBU_DATA_DIR` | `/data`; writable persistent state. |
| `BAMBU_CERT_DIR` | `<BAMBU_DATA_DIR>/certs`; writable directory, stock adapter uses `slicer_base64.cer`. |
| `BAMBU_ACCESS_CODE_FILE` | Optional path to a file containing the access code. |
| `BAMBU_HTTP_TOKEN_FILE` | Optional path to a file containing the HTTP token. |

An explicitly present environment secret always takes precedence over its file,
even if empty (empty is rejected). Files are read only when that environment
variable is absent. Trailing CR/LF is removed; spaces are preserved. Files are
limited to 4096 bytes. Errors name the option without displaying secret contents
or file paths. Secrets containing embedded line breaks or NUL are rejected.

For the open backend, enable Developer Mode / LAN access. The image sets
`OBN_BLOCK_CLOUD=1`, `OBN_LOG_LEVEL=info` and `OBN_LOG_TO_FILE=0`. Do not enable
verbose plugin payload logs or supply slicer/cloud credentials for normal use.
Private signing material is not needed or distributed for this mode.

The open backend verifies LAN TLS by default and looks for `printer.cer` in
`BAMBU_CERT_DIR`. This is the printer CA bundle, separate from slicer signing
credentials. Standalone operators supply their own certificate or explicitly set
`lan_tls_skip_verify = 1` in `<BAMBU_DATA_DIR>/obn.conf` to disable peer verification.
The Home Assistant App instead defaults `verify_printer_tls` to `false`, requiring
no certificate copying; its entrypoint manages that backend setting and supports
optional certificate import. See [App setup](home-assistant.md) for the security
tradeoff and how to enable verification.

To use a stock ABI-compatible plugin, mount the operator's own library read-only
and set `BAMBU_PLUGIN` to its container path. Mount the operator's required
certificate directory separately and set `BAMBU_CERT_DIR`. The project does not
provide/download proprietary binaries or signing material. Stock plugin version
`02.08.02.54` is the original hardware-tested version; other versions are untested.

The HTTP bind/token apply to the API only. Never use a printer access code as the
HTTP token. The daemon logs printer ID, connection transitions, plugin version,
command sequence and outcomes, without credentials or raw plugin messages.
Protect `/data`, secret files and backups; the plugin may store private runtime
state there. Runtime state must never be added to Git or image build context.
