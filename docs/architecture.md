# Architecture

`main.cpp` loads validated configuration, creates the backend, service and HTTP
server, and waits for SIGINT/SIGTERM. Signals are blocked before threads start;
`sigwait` initiates orderly shutdown. No detached threads or `_Exit` shortcuts.

| Layer | Responsibilities |
| --- | --- |
| Config | Environment and secret files, portable paths, IPv4/port/token validation, runtime directories. |
| Backend | Existing upstream loader, ABI `0x020802`, callbacks, detect/connect/subscribe/send/provision/refresh, agent lifetime. |
| BridgeService | Lifecycle worker, reconnect with 5..30 second backoff, command serialization, sequences, readiness, replies and verification. |
| HTTP | Authentication, 64 KiB body limit / 16 KiB headers, validation, JSON, routes and status mapping. Four bounded workers; no direct plugin calls. |

The prototype's setup order and wire payloads are retained: bind detection precedes
connect; subscribe follows connection; initial pushall/get_version/get_access_code;
two certificate install calls separated by an interruptible five-second delay;
then a fresh pushall. Country/client headers remain as in the working prototype.
The stock plugin must acknowledge certificate installation. The open plugin's
Developer Mode path does not emit that event without private slicer material;
its adapter exposes this distinction, and readiness instead requires a fresh
valid status after the same provisioning calls. No private credentials are bundled.

The lifecycle worker holds the same command mutex as writes, including refresh.
Callback state has a separate mutex and never acquires the command mutex, so
synchronous plugin callbacks cannot deadlock a send. Disconnect increments the
session epoch and cancels the pending command. Reconnect cannot complete or reuse
verification from a preceding session. Queued writes recheck readiness after
acquiring the command lock; the queue wait is bounded to one second.

Readiness requires connected state and a valid `print.command=push_status` with
AMS state, external `vir_slot`/`vt_tray` state or `gcode_state`, received after provisioning.
Replies or malformed JSON
cannot make a printer ready. Status older than six seconds makes `/health` 503
immediately, even between lifecycle ticks. All lifecycle delays use interruptible
condition variables; HTTP is started before the first connection attempt.

Verification is intentionally strict:

1. Begin tracking the unique command sequence before send.
2. Require send success and `ams_filament_setting` reply with matching sequence
   unless it explicitly reports `result=fail`. A matching ACK without `result`
   is accepted too; acceptance alone never verifies the write.
3. Record status counter and receive-time boundary **after** that reply, then
   issue a fresh pushall with another unique sequence.
4. Require post-boundary telemetry in the same session, containing the target
   AMS/tray or external holder and exact profile, type, color, minimum and maximum temperature.
5. Compare `setting_id` too when telemetry exposes it. Stock telemetry does not
   reliably echo this field; the original prototype's observable comparison is
   retained when absent. There is no cached-slot merge used for verification.

External IDs 254 (left/deputy) and 255 (right/main) share the serialized command
path. Both use wire `slot_id=0` and `tray_id=254`; normal AMS commands include
`slot_id=tray_id`. The API response echoes
the client's tray ID. External verification reads `vir_slot` by ID, with legacy
`vt_tray` fallback only for ID 255. If the target exists in `vir_slot`, that entry
is authoritative even when it mismatches and a legacy entry is also present.

Late unrelated replies and pre-command/pre-verification telemetry are ignored.
Wrong slot values, send-only success and missing telemetry cannot produce
`synced`. Disconnect aborts waits promptly. Sequence IDs are allocated only while
the serialized command mutex is held, including lifecycle requests.

On shutdown the service cancels waits, joins the lifecycle worker, waits for the
active write, and destroys the agent while callback state remains alive. Plugin
destruction must join its callback producers. HTTP workers then join and sockets
close. A third-party plugin's synchronous ABI calls can delay shutdown until they
return; the bridge does not forcibly unload a library underneath its threads.
