# Locked filament protocol fixtures

These checked-in JSON documents freeze the complete single-color
`ams_filament_setting` wire envelope for regular AMS, deputy/left external,
and main/right external writes. They are independent of the production encoder
and are never regenerated from its output. The `protocol` CTest suite compares
the complete canonical JSON, including scalar types and absence of extra fields.
Only `<sequence_id>` is replaced with the current command's decimal string;
successive writes must have different sequences.

The source is Bambu Studio `command_ams_filament_settings` at commit
[`da8b44ee34dd349f2ae0df3f1cbae366df482354`](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceManager.cpp#L1691-L1722).
See [the protocol audit](../../docs/ams-filament-protocol.md) for ACK and telemetry
rules. The owner reported the `0.1.5-experimental` release working on the live
X2D on 2026-10-06; these documents are source-grounded fixtures, not raw captures.

Do not change fixtures to make a failing encoder test pass. A fixture change
requires an intentional protocol change backed by primary upstream evidence
and relevant real-printer validation, recorded in the protocol audit.
Run `ctest --test-dir build -R protocol --output-on-failure` to check them.

`reset-*.json` cover verified metadata CLEAR for normal AMS and both X2D external holders, using the existing ams_filament_setting command.
