# `ams_filament_setting` protocol audit

Audited on 2026-10-06 against Bambu Studio's current `master` commit
`da8b44ee34dd349f2ae0df3f1cbae366df482354`. Primary sources:

- [Command encoder, DeviceManager.cpp:1691-1722](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceManager.cpp#L1691-L1722).
- [Command response parser, DeviceManager.cpp:3594-3646](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceManager.cpp#L3594-L3646).
- [Virtual telemetry selection, DeviceManager.cpp:3504-3541](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceManager.cpp#L3504-L3541).
- [Virtual tray parser, DeviceManager.cpp:4146-4300](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceManager.cpp#L4146-L4300).
- [AMS metadata parser, DevFilaSystem.cpp:831-870](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceCore/DevFilaSystem.cpp#L831-L870).
- [Virtual ID constants, DevDefs.h:86-87](https://github.com/bambulab/BambuStudio/blob/da8b44ee34dd349f2ae0df3f1cbae366df482354/src/slic3r/GUI/DeviceCore/DevDefs.h#L86-L87).

## Outgoing command

Studio sends a `print` object containing `command="ams_filament_setting"`, a
decimal string `sequence_id` from an incrementing counter, integer `ams_id`,
`slot_id`, `tray_id`, and the following metadata:

| Wire field | Bridge request | Wire type |
| --- | --- | --- |
| `tray_info_idx` | `profile` | string |
| `setting_id` | `setting` | string |
| `tray_color` | `color` | string, RRGGBBAA |
| `nozzle_temp_min` | `tempMin` | integer |
| `nozzle_temp_max` | `tempMax` | integer |
| `tray_type` | `type` | string |

| Target / API address | `ams_id` | `slot_id` | `tray_id` |
| --- | --- | --- | --- |
| AMS 0, tray 2 | 0 | 2 | 2 |
| Left/deputy external, 254/0 | 254 | 0 | 254 |
| Right/main external, 255/0 | 255 | 0 | 254 |

Studio's external call sites pass slot 0. Its encoder uses deputy ID 254 for
the wire `tray_id` of **both** external holders; `ams_id` chooses the holder.
`cols` and `ctype` are emitted only when the optional color vector is nonempty.
The bridge's single-color API needs neither field.

v0.1.3 and the historical prototype at `129e4d2` omitted `slot_id`.
For the reported left-holder request the complete corrected command is:

```json
{"print":{"sequence_id":"20032","command":"ams_filament_setting","ams_id":254,"slot_id":0,"tray_id":254,"tray_info_idx":"GFL99","setting_id":"GFSL99_17","tray_color":"FFCF98FF","nozzle_temp_min":190,"nozzle_temp_max":240,"tray_type":"PLA"}}
```

The corresponding v0.1.3 JSON has exactly the same fields except `slot_id` is
absent. The actual sequence varies per command; all sequences remain unique.

## Replies

Studio dispatches this response by `command`, without testing `result`, `reason`,
`err_code`, or `errno` in this command's branch. It reads numeric `ams_id` and
`tray_id`, then temperatures, color, `tray_info_idx`, and `tray_type` to update
its UI. It does not consume `slot_id` or the wire `setting_id` in that branch.
The C++ member called `setting_id` is populated from **`tray_info_idx`** here;
it is not evidence that the firmware echoes the preset's wire `setting_id`.

There is a noteworthy upstream asymmetry: the response UI branch special-cases
`ams_id=255, tray_id=255`, although the outgoing encoder sends `tray_id=254`.
It does not special-case deputy responses. This is not a reason to change the
outgoing encoding or require an exact echoed address: ACK metadata is never
used by the bridge as physical verification.

The bridge keeps stronger transaction correlation than Studio's UI branch:
only `command="ams_filament_setting"` plus the pending `sequence_id` counts as
a reply. Missing `result` is normal for this parser and no longer causes a
timeout. `result="success"` is accepted too. Per the bridge's rejection policy,
explicit `result="fail"` produces `printer_rejected`. Diagnostic scalar
`result`, `reason`, `err_code` (logged as `errCode`), and `errno` are bounded and
logged when present. Studio supplies no command-specific numeric error-code
semantics, so the bridge does not invent a nonzero-code rejection rule.

Invalid JSON/non-object `print`, missing or malformed command/sequence, and
wrong command/sequence cannot satisfy the pending request. Echoed metadata is
not required to accept an ACK and cannot substitute for fresh status.

## Fresh telemetry and comparisons

Studio prefers `print.vir_slot[]`, selecting ID 255 for main/right and 254 for
deputy/left. Its virtual parser also decodes packed IDs as `(id >> 8) + (id & 255)`;
for example `"65024"` maps to 254 and `"65280"` to 255. The bridge accepts
decimal strings or integers, validates the 16-bit packed range, and applies
this same decoding. If `vir_slot` is an array, a missing target cannot be
substituted by `vt_tray`. Only without that array does legacy `vt_tray` identify
main/right 255. Regular trays use `print.ams.ams[].tray[]` by AMS and tray IDs.

Both upstream telemetry parsers read product ID, type, RGBA color, and minimum
and maximum temperatures. The virtual parser expects temperature strings;
the bridge also accepts integer representations. All five values must match
in the fresh target entry. Missing/malformed values cannot verify a write.
The AMS parser optionally reads wire `setting_id` into `filament_setting_id`;
the virtual parser does not read that field. The bridge therefore compares
`setting_id` when supplied, including rejecting malformed supplied values,
but does not require firmware to echo it. ACK temperatures are integers in
Studio's parser; ACK values do not participate in verification.

The receive-time and status-counter boundaries remain after the matching ACK,
before the post-command pushall. Pre-command/pre-ACK telemetry, no status,
another holder, mismatches, or a disconnected session cannot yield `verified`.
No cached partial telemetry is merged into verification.

## Validation scope and capabilities

The checkout already contained the v0.1.4 `slot_id` and ACK fixes before this
audit. This audit adds complete golden commands, wrong-command ACK coverage,
packed-ID coverage, authoritative `vir_slot` absence coverage, and `errno`
diagnostics. Existing suites cover rejected/missing/wrong-sequence ACKs,
freshness, all metadata mismatches, both holders, serialization and reconnect.

The v0.1.3 X2D report showed unchanged physical metadata and a timeout.
On 2026-10-06 the owner reported `0.1.5-experimental` working on the live X2D
and requested stable `0.1.6`. This report supplies the hardware validation;
the agent did not run a live test or collect raw ACK/status captures.
`externalFilamentWrite=true` remains advertised, backed by protocol regression
coverage and that owner report. Other printer/firmware combinations are untested.

For `0.1.6`, [checked-in golden fixtures](../tests/golden/README.md) lock the complete
AMS, deputy and main wire envelopes. Only the dynamic sequence is substituted;
field values, types, missing fields, additional fields and addressing are compared
against independent JSON documents for success and result-less ACKs. Successive
writes also check sequence uniqueness. Fixtures must not be regenerated from
encoder output to resolve a regression. No printer runtime behavior, plugin pin,
HA packaging, environment variables, or HTTP request/success fields changed.
