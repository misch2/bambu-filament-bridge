# Implementation and local validation — 2026-10-05

## Final project tree

Generated builds, private local files and upstream submodule internals are omitted.
`AGENTS.md` and local `CODEX_TASK.md` instructions were preserved.

```text
.
├── .github/workflows/{ci.yml,release.yml}
├── .clang-format
├── .dockerignore
├── .env.example
├── .gitattributes
├── .gitignore
├── .gitmodules
├── AGENTS.md
├── CMakeLists.txt
├── CONTRIBUTING.md
├── Dockerfile
├── LICENSE
├── README.md
├── THIRD_PARTY_NOTICES.md
├── docker-compose.example.yml
├── openapi.yaml
├── repository.yaml
├── docs/
│   ├── architecture.md
│   ├── compatibility.md
│   ├── configuration.md
│   ├── docker.md
│   ├── home-assistant.md
│   ├── implementation-report.md
│   └── troubleshooting.md
├── home-assistant/bambu_filament_bridge/
│   ├── CHANGELOG.md
│   ├── DOCS.md
│   ├── README.md
│   └── config.yaml
├── include/bfb/{backend.hpp,bridge_service.hpp,config.hpp,http_server.hpp,types.hpp}
├── packaging/entrypoint.sh
├── scripts/{check_contract.py,image_smoke.sh,live_smoke.py}
├── src/{main.cpp,config.cpp,plugin_backend.cpp,bridge_service.cpp,http_server.cpp}
├── tests/{fake_backend.hpp,plugin_smoke.cpp,tests.cpp}
└── third_party/
    ├── nlohmann/{json.hpp,LICENSE.MIT}
    └── open-bamboo-networking/  [pinned git submodule]
```

Changed existing project files: `src/main.cpp`, `CMakeLists.txt`, `.env.example`
and `.gitignore`. Other deliverables above are added; the existing AGPL LICENSE
is retained. Existing user instruction changes were not reverted. Submodule
registration stages `.gitmodules` and the gitlink as part of `git submodule add`;
the remaining implementation is left for review, without a commit or push.

## Decisions

- Extracted the proven setup/wire flow into a thin backend and a serialized
  lifecycle/verification service, separate from config, HTTP and signal handling.
- Kept the prototype's bind/connect/subscribe, initial requests and certificate
  provisioning sequence. The stock plugin retains the certificate-event wait;
  the open backend's Developer Mode path proceeds to fresh telemetry without
  private slicer signing material or that event.
- Added session epochs, cancellation on disconnect, receipt-time/status-counter
  boundaries after a successful reply, and exact target-slot comparison.
- Made startup/reconnect asynchronous, all commands/refresh serialized, waits
  interruptible, HTTP workers bounded, and shutdown joined and deterministic.
- HTTP alone maps domain errors to status codes. No direct plugin calls from HTTP.
- Added secret files, portable defaults, strict validation, stable errors,
  authenticated capabilities and documented external-slot exclusion.
- Used one image/binary for standalone and Home Assistant. The entrypoint briefly
  initializes `/data` ownership and reads Supervisor options as root, then drops
  permanently to UID/GID 10001 with no-new-privileges before exec.

## Dependency pins

Open-bamboo-networking: **`8656b66125d5a6dde0a96de705896767508d8141`**,
upstream project version v2.2.0 (51 commits after the v2.2.0 tag). Compiled ABI
`0x020802`, plugin compatibility version `02.08.02.99`. The submodule and upstream
license notices are preserved. This pin has build/loader validation, not a new
hardware certification. Upstream pins embedded Mosquitto v2.1.2 and cJSON v1.7.18.

JSON: unmodified nlohmann/json v3.11.3 single header, MIT license and SHA-256
recorded in THIRD_PARTY_NOTICES.md. Debian base and GitHub Actions are SHA/digest
pinned; Debian package installation follows bookworm security repositories.

## Checks and outcomes

| Check | Result |
| --- | --- |
| `cmake -S . -B build -DBUILD_TESTING=ON` | Passed in a Linux container against this Windows workspace with networking disabled. |
| `cmake --build build -j2` | Passed; all project sources and upstream loader compile. |
| `ctest --test-dir build --output-on-failure` | All six suites passed: config, validation, service, verification, concurrency, HTTP. |
| State test stress check | 100 consecutive passes after fixing a fake-backend telemetry race exposed by ARM emulation. |
| C++ formatting | clang-format 14 dry-run/Werror passed for project sources/headers/tests. |
| OpenAPI | Full OpenAPI 3.0 schema validation passed using openapi-spec-validator 0.7.2; local reference checks passed. |
| Config/workflow contracts | YAML parsing, action SHA pins, App schema, versions, JSON header integrity and empty secret examples passed. |
| Docker production builds | Final `linux/amd64` and `linux/arm64` build/load passed; both run the six suites and load/version-check the real open-source plugin. ARM uses emulation. |
| Standalone runtime smoke | Passed on both platforms with `--network none`: public health 503, authentication, capabilities, fast write refusal, UID 10001, no synthetic secrets in logs, clean SIGTERM shutdown. |
| Supervisor-options runtime smoke | Passed on both platforms, including root-owned mode-0600 options.json, privilege drop and shutdown; no real Supervisor used. |
| Live script | Python syntax and explicit opt-in guard checked; no live run. |
| Client compatibility | Compared local SpoolmanSync BambuBridgeClient fields, routes, Bearer auth, 503 semantics and verified success contract. No client code changes. |
| Repository hygiene | `git diff --check` passed; build outputs remain ignored; no proprietary plugin or real credentials introduced. |

The final local multi-platform image is `bfb:local`, manifest digest
`sha256:f8dd092332f8dce7bdeb26a0f0f46795a7b03f9441f08ff1833922813673ff28`.
Build logs and the original prototype inspection copy are local under ignored
`build/`. CI/release workflows were authored and checked locally, not executed
on GitHub. No registry image was published.

## Remaining live validation and limitations

- The owner's original stock-plugin prototype was tested on X2D. The refactored
  daemon and default open backend still need an opt-in X2D update/reconnect test.
  No new real-printer success is claimed.
- An actual Home Assistant Supervisor installation remains untested; the options
  translation and root-owned storage case were simulated in the production image.
  App installation requires release publication first.
- External spool writes remain unsupported (`externalFilamentWrite=false`); no
  external endpoint exists. Prototype virtual AMS IDs 254/255 are rejected.
- Stock telemetry may omit `setting_id`; the original observable comparison is
  preserved for profile/type/color/temperatures, with setting checked when echoed.
- Synchronous calls inside a user-supplied plugin can delay deadlines/shutdown
  until they return. No library is forcibly unloaded under running plugin threads.
