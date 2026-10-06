#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check local YAML contracts, source pins, version alignment and secret-free examples."""
import hashlib
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    spec = yaml.safe_load((ROOT / "openapi.yaml").read_text())
    require(spec["openapi"] == "3.0.3", "Unexpected OpenAPI version")
    require(set(spec["paths"]) == {
        "/health", "/api/v1/capabilities", "/api/v1/ams/{amsId}/trays/{trayId}/filament"
    }, "Unexpected public routes")
    require(spec["paths"]["/health"]["get"]["security"] == [], "Health must be public")
    require("503" in spec["paths"]["/health"]["get"]["responses"], "Health 503 missing")

    def references(node):
        if isinstance(node, dict):
            if "$ref" in node:
                ref = node["$ref"]
                require(ref.startswith("#/"), "Nonlocal contract reference")
                target = spec
                for segment in ref[2:].split("/"):
                    target = target[segment.replace("~1", "/").replace("~0", "~")]
            for value in node.values():
                references(value)
        elif isinstance(node, list):
            for value in node:
                references(value)
    references(spec)
    addon = yaml.safe_load((ROOT / "home-assistant/bambu_filament_bridge/config.yaml").read_text())
    require(addon["arch"] == ["amd64", "aarch64"], "App architectures mismatch")
    require(addon["schema"]["http_token"] == addon["schema"]["access_code"] == "password", "App secrets must be passwords")
    require(addon["options"]["verify_printer_tls"] is False and
            addon["schema"]["verify_printer_tls"] == "bool", "App TLS option mismatch")
    require({"type": "app_config", "read_only": True, "path": "/config"} in addon["map"],
            "App certificate mount missing")
    version = addon["version"]
    cmake = (ROOT / "CMakeLists.txt").read_text()
    require(f"VERSION {version.split('-')[0]} LANGUAGES" in cmake, "CMake/App numeric version mismatch")
    require(f'set(BFB_VERSION "{version}")' in cmake, "CMake/App release version mismatch")
    require(f'io.hass.version="{version}"' in (ROOT / "Dockerfile").read_text(), "Image/App version mismatch")
    require(f"version={version} backend=" in (ROOT / "src/main.cpp").read_text(), "Startup/App version mismatch")
    for path in (ROOT / ".github/workflows").glob("*.yml"):
        workflow = yaml.safe_load(path.read_text())
        # PyYAML's YAML 1.1 parser treats the 'on' key as boolean True.
        require("on" in workflow or True in workflow, "Workflow triggers missing")
        for job in workflow["jobs"].values():
            for step in job["steps"]:
                if "uses" in step:
                    sha = step["uses"].split("@")[-1]
                    require(len(sha) == 40 and all(c in "0123456789abcdef" for c in sha), "Action is not SHA-pinned")
    digest = hashlib.sha256((ROOT / "third_party/nlohmann/json.hpp").read_bytes()).hexdigest()
    require(digest == "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6", "JSON header changed")
    for line in (ROOT / ".env.example").read_text().splitlines():
        if line and not line.startswith("#"):
            require(line.endswith("="), "Example environment contains a value")
    print("Contract references, App configuration, workflow pins, versions and examples checked")


if __name__ == "__main__":
    main()
