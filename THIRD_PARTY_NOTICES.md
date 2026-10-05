# Third-party notices

The bridge is **AGPL-3.0-or-later**. The existing LICENSE is the complete GNU
Affero General Public License version 3 text; source SPDX identifiers permit
later versions. Preserve upstream notices when redistributing source or binaries.
Provide recipients and network users access to corresponding source, including
the pinned dependencies and build files.

| Component | Exact source | License / use |
| --- | --- | --- |
| ClusterM/open-bamboo-networking | `8656b66125d5a6dde0a96de705896767508d8141` (project v2.2.0) | AGPL-3.0-or-later; upstream `LICENSE`, headers and notices preserved in the submodule. `tools/plugin_runner/plugin_loader.cpp` is compiled into the bridge; the open networking plugin is built separately. |
| nlohmann/json | v3.11.3 | MIT, Copyright (c) 2013-2022 Niels Lohmann. Vendored unmodified single header and `third_party/nlohmann/LICENSE.MIT`. Header SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`. |
| Eclipse Mosquitto | v2.1.2, pinned by the upstream submodule's CMake | EPL-2.0 / EDL-1.0 dual license; statically embedded client library. Its LICENSE.txt is included in the image. |
| cJSON | v1.7.18, pinned by upstream CMake | MIT; embedded through Mosquitto. LICENSE included in the image. |
| OpenSSL, libcurl, zlib, GNU runtime and Debian packages | Debian bookworm package repositories; base image pinned in Dockerfile | System libraries; package copyright/license files remain under `/usr/share/doc`. OpenSSL Apache-2.0, curl permissive curl license, zlib Zlib, GNU runtime GPL with runtime exception. |

Upstream: <https://github.com/ClusterM/open-bamboo-networking>. Build plugin ABI
`0x020802`, reported compatibility version `02.08.02.99`. This pin has local build
and loader validation, **not** a new real-X2D verification claim.

No proprietary Bambu plugin, signing keys, user certificates, access codes, cloud
tokens or slicer credentials are distributed. A stock plugin can only be supplied
by the operator using a read-only mount and `BAMBU_PLUGIN`. The stock plugin is
not downloaded by any project script or build.
