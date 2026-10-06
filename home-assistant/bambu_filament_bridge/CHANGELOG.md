# Unreleased

# 0.1.3

- Restore external spool writes through virtual AMS IDs 254 (left) and 255 (right), fixing `invalid_request` when assigning either X2D holder.
- Preserve the prototype's external wire encoding and require a successful printer reply plus fresh matching `vir_slot` telemetry; retain legacy `vt_tray` verification for ID 255.
- Advertise `externalFilamentWrite=true` and update API documentation and smoke checks.
- Add regression coverage for both external holders, mismatched or stale telemetry, and serialized writes across AMS and external slots.

# 0.1.2

- Start without manual certificate copying by default; add optional printer TLS verification.
- Import user-supplied printer certificates from the App configuration directory.
- Preserve other plugin settings and run the bridge as the existing non-root user.
- Simplify README files for users.

# 0.1.1 
- Fix GitHub actions

# 0.1.0

- Initial wrapper around the standalone bridge image for amd64 and aarch64.
