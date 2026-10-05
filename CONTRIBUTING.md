# Contributing

Keep the service narrow: printer communication, authentication, and verified slot
metadata updates. Read AGENTS.md. Preserve the working ABI setup and command flow;
document evidence before changing printer behavior.

```sh
git clone --recurse-submodules https://github.com/misch2/bambu-filament-bridge.git
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror src/*.cpp include/bfb/*.hpp tests/*.cpp tests/*.hpp
docker build -t bfb:dev .
```

CMake >=3.20, Linux and a C++17 compiler are required. The six CTest suites use a
fake backend, never a real printer or networking plugin. `plugin_smoke` is an
explicit packaging check, not registered in CTest. CI builds both image platforms
and loads the pinned plugin in each build. Use Linux/Docker/WSL on Windows.

For HTTP changes update implementation, tests, OpenAPI and README together.
Never weaken send/reply/fresh-status/exact-target verification. Avoid unrelated
refactors. Use RAII, joined threads and condition variables. Preserve upstream
AGPL notices; update THIRD_PARTY_NOTICES for dependency changes. Never commit
runtime data, .env, credentials, certificates or proprietary plugins. Live tests
are opt-in and any write changes printer state; do not run them in CI.

Release tags must be stable `vMAJOR.MINOR.PATCH` on the repository default branch.
Synchronize CMake/main version, image HA label, App config/changelog and compose
example before tagging. Release workflow publishes semver and latest tags only
after verifying that tag is on the default branch. No local publish is required.
