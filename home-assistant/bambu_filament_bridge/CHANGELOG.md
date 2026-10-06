# Unreleased

# 0.1.8

- Promote verified filament metadata clearing to stable after the owner confirmed 0.1.7-experimental working on the live X2D.
- Preserve the authenticated DELETE API, matching-ACK plus fresh-target verification, existing POST behavior and SET/CLEAR serialization.
- Update packaging versions, image examples and compatibility documentation; no printer protocol changes.

# 0.1.7-experimental

- Add authenticated DELETE to clear AMS tray or external holder filament metadata using the existing ams_filament_setting command.
- Require a matching accepted ACK and fresh cleared target telemetry; preserve POST verification and serialize SET/CLEAR operations.
- Add reset golden payloads for AMS and both X2D external holders, regression tests, API documentation and explicit opt-in live clear smoke support.
- Clearing stale metadata on the real X2D LCD still requires live validation; no PA/flow calibration cleanup is performed.

# 0.1.6

- Promote the filament protocol fix after the owner reported 0.1.5-experimental working on the live X2D.
- Lock complete AMS, deputy/left and main/right wire commands in checked-in golden JSON fixtures, including scalar types and absence of extra fields.
- Retain matching-ACK plus fresh-telemetry verification and the existing SpoolmanSync API.

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
