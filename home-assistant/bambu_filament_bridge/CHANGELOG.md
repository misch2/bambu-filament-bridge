# Unreleased

# 0.1.5-experimental

- Audit filament-setting commands and replies against current Bambu Studio; add complete golden wire commands.
- Decode packed virtual-slot IDs and use legacy vt_tray only when vir_slot is absent.
- Add ACK, metadata and telemetry regression coverage and errno diagnostics.
- External-holder writes still require live X2D validation of physical metadata changes.

# 0.1.4

- Restore external slot_id=0 and include slot_id equal to tray_id for normal AMS commands.
- Accept matching ams_filament_setting ACKs without result; reject explicit result=fail while preserving post-ACK pushall and fresh telemetry verification.
- Add wire addressing and ACK regression tests and bounded reply diagnostics.

# 0.1.3

- Restore external spool writes through virtual AMS IDs 254 (left) and 255 (right), fixing `invalid_request` when assigning either X2D holder.
- Preserve the prototype's external wire encoding and require a successful printer reply plus fresh matching `vir_slot` telemetry; retain legacy `vt_tray` verification for ID 255.
- Advertise `externalFilamentWrite=true` and update API documentation and smoke checks.
- Add regression coverage for both external holders, mismatched or stale telemetry, and serialized writes across AMS and external slots.
- Log validated filament requests, HTTP outcomes and command phases with request/sequence IDs; explain reply and verification timeouts without exposing credentials or raw payloads.

# 0.1.2

- Start without manual certificate copying by default; add optional printer TLS verification.
- Import user-supplied printer certificates from the App configuration directory.
- Preserve other plugin settings and run the bridge as the existing non-root user.
- Simplify README files for users.

# 0.1.1 
- Fix GitHub actions

# 0.1.0

- Initial wrapper around the standalone bridge image for amd64 and aarch64.
