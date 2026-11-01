# OrbitTLM acceptance status

| Module | Current behavior | Evidence / limitation |
|---|---|---|
| Paired LOG + BIN import | implemented | hash fusion and provenance; UTC not inferred |
| CCSDS short TM 5+218 | implemented | synthetic protocol tests |
| VC-specific variable-length KISS stream | implemented | fragmented packets, multiple packets, escape tests |
| Missing VC frames / malformed escape | incomplete payload discarded from reconstructed output | tested; input raw TM bytes preserved |
| Application CRC | **intentionally not implemented** | real ASRTU-1 parameters unverified and not necessary to preserve KISS packets |
| Optional inner Space Packet-shaped header | candidate only; 0/2/4 byte suffix tolerated but unverified | never required for KISS reconstruction |
| ASRTU-1 published telemetry catalog | 27 packet kinds / 2,067 definitions loaded offline | native + candidate legacy envelope, engineering expressions, all fields exposed; real on-air samples require validation |
| Qt replay and SQLite | implemented | CI compilation and SQL tests; manual GUI validation remains |
| Quaternion series | SAT-S3 signed-int32 q0..q3 /10000 exported; experimental candidate kept separate | application CRC and physical attitude coordinate convention unverified |
| BY04/8104 | framework and optional schema supported | mission-specific field validation pending |
| SSDV | embedded JPEG reconstruction with missing-MCU fill; 218-byte DSLWP and JAMX CRC variants accepted | BY04/ASRTU-1 and JAMX regression fixtures; a partial preview is best-effort and does not make damaged packets CRC-valid |
| TCP live receive | basic 223-byte record stream | field hardware tests pending |

## Flight encoding, as described by developer BG7ZDQ

The satellite's different subsystems emit packets of various lengths and types.
Their application packets include auxiliary headers and a CRC, and are KISS-wrapped.
KISS stream data is then segmented into **218-byte slices**, prefixed with a
**5-byte short CCSDS TM header**, to form **223-byte RS information frames**.
RS encoding follows. Thus the 223-byte recorder output has no RS parity bytes
and cannot independently recheck Reed–Solomon correction.

The fifth short-TM header byte is header metadata; the application KISS stream
is recovered from the remaining 218 bytes, potentially spanning multiple frames.

KISS integrity (proper delimiters and escape sequences) and TM provenance
are *not the same as application CRC validity*. A successful RS/FEC log message
also does not prove application CRC validity.

## ASRTU-1 application CRC: research and deliberate omission

Consulted the published BG7ZDQ/ASRTU_Series_Receiver source, the
HyacinthSat/asrtu-sndmdm-backend Go source, and gr-lilacsat. The first handles
223-byte demodulated telemetry information frames and image delivery; the second
contains engineering field rules but no demonstrable end-to-end application
checksum implementation. gr-lilacsat has an AFSK CRC-CCITT routine, but that
is not a verified ASRTU-1 downlink application CRC implementation.

Additional public references include the ASRTU-1 on-board-computer software
repository, which documents a released flight-software codebase. The specific
application downlink CRC polynomial, init, xorout, reflection, header coverage
and trailing-byte placement **were not established from an independently
validated source trace**.

**Decision:** delete speculative CRC presets, checker, CRC-only packet filter,
related UI and CLI switches. Preserve all structurally reassembled KISS
packets with `integrity=not_crc_verified`; never label them `crc_pass`
or remove them because a guessed CRC variant failed. Preserve frame hashes,
FEC log provenance, timestamps and KISS loss/error statistics. The raw TM
frames remain available for future CRC reprocessing if confirmed specifications
or paired known-good packets become available.

For command-line batch export:
```sh
OrbitTLM-cli --satellite ASRTU-1 --log decoder.log --bin SSDV_received_frames223.bin --packets-json packets.json --json frames.json
```

## Explicit remaining risks

- BIN-only packets without captured timestamps cannot be reliably ordered by
  UTC; limited frame-counter ordering is an inference, not a replacement for time.
- No unverified quaternion may be used as an authoritative spacecraft attitude.
- A KISS delimiter and plausible inner payload length are insufficient to prove
  correctness of all application bytes.
- The exact satellite-specific telemetry field map and onboard UTC epoch must
  be verified before physical engineering values are labeled authoritative.

## 27-packet catalog verification

- All defined subsystem packets are loaded from the versioned files distributed with the executable.
- A regression test visits every packet kind with synthetic telemetry bytes to check bit extraction, boundary safety and formula coverage.
- Dedicated tests exercise the SAT-S3 signed quaternion field scaling, ZGK-S0 affine conversion, PDB status bits, the native two-byte subtype header and the legacy seven-byte KISS envelope.
- Unsupported/ambiguous source formulas are reported explicitly; malformed bit positions remain visible as invalid instead of being silently repaired.
- These are schema/synthetic regressions. An on-air engineering-field verification still requires actual matching KISS packets and known-good reference values. Packets from unrelated APIDs or subtypes are correctly retained as unknown rather than force-decoded.

- JAMX regression covers the public SatNOGS 14907288 sample and the supplied 2026-10-31 capture. The latter must yield 50 structurally consistent packets, 46 packets passing the capture-observed CRC profile, four flagged CRC failures, and a decodable partial 640×480 JPEG.
