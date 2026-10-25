# BY04 dashboard investigation and decoder validation

## Evidence and scope

Inspected main at `d2c91c6`. The attached previous chat was only a bounded preview;
this session provided no `read_thread` or Work browser tool. Playwright MCP could
not initialize because its Chrome path was missing. Local Playwright with the
installed Microsoft Edge successfully loaded the requested page on 2026-10-25,
received HTTP 200 and captured the page, JavaScript, and same-origin XHR JSONP.

Observed public endpoints:

- `/get_data?type=all_sys&sat=BY-04uv&callbackfunc=gettable`: 54 names.
- `/get_data?type=table&sat=BY-04uv`: 3,074 populated field rows, including stale
  August entries. A response timestamp is not an onboard timestamp.
- `/get_data?type=system&sat=BY-04uv&code=OBC-SF&callbackfunc=data_callback`.
- Same system endpoint with `code=BUV-S0`.
- `/js/lines.js?v=20260826-1415` identifies `type=line`; bounded history requests
  for the selected 2026-10-24 capture returned empty arrays. They did not verify
  a time-synchronized match to the server's latest sample.

These responses establish names/bit addresses and selected raw/value pairs, not
an official numeric dispatch table or complete conversion formulas/units.
The downloadable receiver tutorial supplies setup instructions, not a wire
dictionary. The ASRTU backend's CSV definitions are for a different spacecraft.
No BY04 CSV dictionary was obtained. The new decoder deliberately reports
`by04_observed_layout_candidate`; full mission telemetry remains incomplete.

## Real capture cross-check

Read-only survey of the user's mixed recordings identified SCID `0x205` BY04
frames and SCID `0x032` ASRTU frames. Most structurally plausible BY04 KISS packets
were 198-byte `08F0/0F` on VC2 and 134-byte `08D2/00` on VC0. Their 191/127 field
bytes fit dashboard OBC-SF/BUV-S0 addresses when based at KISS byte 7. The full
CCSDS identity, declared length, subtype, VC, selected satellite and exact total
length are required. This empirical association remains explicit in exports;
APID is not assigned as a confirmed mission main type.

The 319 retained field rows comprise 220 OBC-SF and 99 BUV-S0 addresses. Twenty
server raw/value pairs equal big-endian IEEE binary32 exactly, including OBC
attitude quaternion addresses 139/143/147/151. Sixteen OBC and four BUV fields use
only this observed conversion. All other conversions and every unit remain
unknown. Signedness, ADC scaling, enum completeness, spacecraft UTC epoch,
application CRC, attitude reference frame and component convention are not
inferred. Finite outliers are retained: an unverified packet can contain bad
telemetry even when its envelope is structurally correct.

Regression fixture `by04_business_capture.b64` preserves the complete 28-frame
recording `20261024_222442_727/SSDV_received_frames223.bin`, including one
non-BY04-SCID frame and damaged business fragments. Original byte count: 6,244.
SHA-256: `afc7e91393f65b0cfb2032e318d294e9445404ac30e06b4778d9bcc35fc27c0a`.
BIN-only regression recovers 10 OBC candidates and 11 BUV candidates; the first
OBC q0 is `0.21687696874141693` at KISS offset 146. Original records were unchanged.

## Validation

- Windows MSVC 19.51 / Qt 6.8.3 Release: GUI, CLI, protocol and UI test targets built.
- Missing Qt Charts was installed only into the task's temporary workspace using
  aqt with archive hash validation; the existing Qt installation was unchanged.
- CTest: protocol, CLI help, UI startup/tab smoke tests passed (3/3).
- Added real capture field/offset/conversion checks, JSON/Chinese CSV evidence
  checks, wrong satellite/VC/length/subtype rejection and NaN preservation.
- LOG+BIN replay of the selected recording: 28 unique TM records, 35 recovered
  KISS packets, 21 observed layout candidates, 204 converted float field values,
  3,085 raw-only fields, 1 counter-gap interruption and 1 incomplete EOF packet.
  Receiver logs report FEC for 20 records; this is not independent RF validation.
- Existing real VC1 image regression still expects 10 CRC-valid and 1 damaged
  DSLWP packets; images never enter the KISS telemetry parser.

CI runs both Linux and Windows using the existing push workflow. Local Windows
results do not constitute Linux CI results. Remote run status is reported in the
delivery message after the branch has been pushed.

## Completing the protocol

`python scripts/collect_by04_dashboard.py evidence-directory` captures the six
public resources, response headers, hashes and explicit access failures without
evaluating JSONP. To complete the business decoder, obtain BY-04uv `system.csv`
and per-packet definition CSVs from the operator, plus a corresponding raw packet
and decoded record. Required columns: numeric packet IDs, field base, byte/bit
offset, signed/float encoding, formula and unit. The current dashboard does not
provide those complete definitions; no missing values were fabricated.
