# Version bump

Use an unused stable `X.Y.Z` version unless an experimental release is explicitly
requested. Experimental releases use `X.Y.Z-experimental` and publish only their
full version tags, without updating `latest` or the stable major/minor tag.
The example below
releases **0.1.2**; replace it everywhere with the intended version. Commands
assume the default branch is `main`.

1. Start with a clean checkout of the current default branch:

   ```sh
   git switch main
   git pull --ff-only origin main
   git status --short
   ```

2. Set the same version, **without `v`**, in all four release-gated fields:

   | File | Field |
   | --- | --- |
   | `CMakeLists.txt` | `project(... VERSION 0.1.2 LANGUAGES CXX)` |
   | `home-assistant/bambu_filament_bridge/config.yaml` | `version: "0.1.2"` |
   | `Dockerfile` | `io.hass.version="0.1.2"` |
   | `src/main.cpp` | Startup log `version=0.1.2 backend=` |

   Also set `BFB_VERSION` in `CMakeLists.txt` to the full release version.
   CMake's `project(... VERSION ...)` remains numeric: for
   `0.1.5-experimental`, use project version `0.1.5` and
   `set(BFB_VERSION "0.1.5-experimental")`. The contract and release checks
   validate both values.

3. In `home-assistant/bambu_filament_bridge/CHANGELOG.md`, rename the existing
   `# Unreleased` heading to `# 0.1.2`, keep its entries as the release notes,
   and add a fresh, empty `# Unreleased` section at the top:

   ```markdown
   # Unreleased

   # 0.1.2

   - Release notes moved from the previous Unreleased section.
   ```

   Update current image/release examples in `docker-compose.example.yml`,
   `README.md`, `docs/docker.md`, and `docs/home-assistant.md`. Preserve historical
   version references. `openapi.yaml`'s contract version and the plugin's
   `OBN_VERSION`/ABI/submodule pin are independent of the service release.

4. Validate on Linux/WSL with the build dependencies and PyYAML installed:

   ```sh
   cmake -S . -B build -DBUILD_TESTING=ON
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   python3 scripts/check_contract.py
   docker build -t bfb:release-check .
   git diff --check
   ```

5. Review and stage only the version-bump changes, then commit and push (or
   merge through a PR). Wait for CI to pass on the resulting default-branch
   commit, including both Docker architectures and runtime smoke tests:

   ```sh
   git diff
   git add -p
   git commit -m "Release 0.1.2"
   git push origin main
   ```

6. From a clean checkout of **that same CI-tested commit**, create and push the
   annotated tag. Tag creation alone does not publish the image:

   ```sh
   git tag -a v0.1.2 -m "Release 0.1.2"
   git push origin refs/tags/v0.1.2
   ```

   The tag push starts [Release images](.github/workflows/release.yml). It checks
   that the commit is on the default branch and all four versions match, then
   publishes `ghcr.io/<owner>/bambu-filament-bridge` for `linux/amd64` and
   `linux/arm64` with tags `v0.1.2`, `0.1.2`, `0.1`, and `latest`. Confirm the
   workflow succeeds before announcing the release. Never move a published tag.
