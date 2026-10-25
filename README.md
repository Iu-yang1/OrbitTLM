# OrbitTLM

OrbitTLM is a C++20 / Qt 6 / CMake / SQLite desktop workbench for **offline satellite telemetry replay and engineering analysis**.

## Data integrity rules

- Imports `decoder.log` and `SSDV_received_frames223.bin` into the **same session**.
- Correlates *identical 223-byte frames* by SHA-256; preserves raw-only records, duplicates, and provenance rather than silently marking them FEC-corrected.
- Distinguishes log capture time from spacecraft UTC: absent onboard UTC is **unknown**, not interpolated.
- Each decoded RS information frame is exactly **5 bytes of short CCSDS TM header + 218 data bytes**. BY04 VC1 contains native DSLWP SSDV; its VC0/VC2 carry KISS. RS parity is not present in these 223 bytes.
- Variable-length subsystem packets have CRC added before KISS encoding; multiple packets can share a TM frame, or one packet can span frames.
- KISS escapes are processed across TM frame boundaries, independently per virtual channel, before any optional CCSDS Space Packet-shaped inner header is considered.
- Application CRC is not checked: this version restores KISS packets without using unverified CRC parameters to discard data.
- A structurally valid packet **does not prove** it passed Reed–Solomon error correction.
- Satellite-specific values without a verified conversion schema are exported as **raw**, not invented engineering units.
- Experimental ASRTU-1 quaternion candidates are explicitly labeled `candidate_unverified`: reference frame, component order and antenna alignment are not certified.

## Target features

P0 paired LOG/BIN ingestion, CCSDS frame decoding, deduplication, SQL persistence and data-quality grading.
P1 Qt desktop: frame table, inspectors, filtering, timeline controls, export.
P2 quaternion candidates, attitude visualization, engineering time-series and ordered CSV/JSON.
P3 modular satellite profiles: ASRTU-1 and BY04 / 8104.
P4 TCP receive input and optional SSDV packet reconstruction tools.

## Build

Requires a C++20 compiler, CMake 3.21+, Qt 6.4+ components Core, Widgets, Sql, Network, Charts and Test (for tests). SQLite is accessed using Qt's QSQLITE driver.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Offline batch processing (no desktop display required):

```sh
OrbitTLM-cli --satellite ASRTU-1 --log decoder.log --bin SSDV_received_frames223.bin --json frames.json --packets-json kiss-packets.json --telemetry-json engineering.json --telemetry-csv engineering.csv --quaternion-csv SAT-S3-quaternions.csv --sqlite replay.sqlite
```

For BY04 use `--satellite BY04` and optional `--profile profiles/BY04.template.json`. Data fields without a confirmed schema remain raw.

## BY04 dashboard evidence and observed layouts

The BY-04uv page was reachable on 2026-10-25. Its same-origin JSONP API publishes
54 packet names and 3,074 populated field rows, but no numeric dispatch table,
complete source formulas, data types or units. This is **not a completed BY04
engineering telemetry dictionary**.

The independent BY04 decoder now exposes two **observed layout candidates**:

| Dashboard name | Required KISS identity / subtype | VC | KISS bytes | Field base | Fields |
| --- | --- | --- | --- | --- | --- |
| OBC-SF | `08 F0` / `0F` | 2 | 198 | byte 7 | 220 |
| BUV-S0 | `08 D2` / `00` | 0 | 134 | byte 7 | 99 |

These associations are corroborated by SCID `0x205` captures and dashboard field
addresses, rather than an official numeric dispatch definition. They are always
identified as `by04_observed_layout_candidate`, with `main_type=-1`. Full inner
length, identity, subtype, VC and satellite checks are required; other payloads
remain raw or CCSDS-header-only. ASRTU field catalogs are never consulted.

Only 20 fields have exact server raw/binary32 value pairs (16 OBC, 4 BUV).
Their observed big-endian IEEE 754 conversion is labeled
`by04_observed_float32`, not an official source formula. Remaining fields keep
HEX/unsigned bit patterns and `raw_by04_conversion_unknown`; units stay empty.
OBC quaternion bit addresses are 139/143/147/151 relative to byte 7, and appear
in telemetry JSON/CSV. Packet integrity, coordinate frame, component convention,
voltage/temperature scaling and onboard UTC epoch remain unverified. The
dedicated attitude export and 3D view continue to require a confirmed schema.

The two catalogs are embedded in the binaries, so users need no site connection.
`protocols/by04/` preserves evidence pairs and a dashboard name index.
Recollect the public evidence without executing JSONP:

```sh
python scripts/collect_by04_dashboard.py collected-by04
OrbitTLM-cli --satellite BY04 --bin SSDV_received_frames223.bin --telemetry-json by04.json --telemetry-csv by04.csv
```

For full protocol confirmation, obtain the server's BY-04uv `system.csv` plus
per-packet definition CSVs (numeric IDs, base offsets, signedness/float encoding,
formulas and units), and a matching raw packet/server decoded record. A screenshot
or an `f_double` value alone cannot establish a conversion.

Open the `OrbitTLM` executable. Use **Import session** to choose a log and binary file (either may be omitted), then choose the satellite profile and import. Exports include UTC provenance and quality labels.

This project is an independently written decoder and does not include any binary executable from third parties.


## ASRTU-1 application CRC research decision

The two reference projects do not establish the satellite application's CRC width, coverage, polynomial, trailer encoding and per-subsystem handling. gr-lilacsat's AFSK CRC implementation is not proof of the spacecraft's application checksum. A separate OBC software repository is publicly available but the precise downlink CRC call path has not been independently established.

For the current task, which is recovery and offline replay of 223-byte TM information frames, the application CRC is **not required** to preserve and export recovered variable-length KISS packets. To avoid false negatives and mislabeling corrupted packets as verified, OrbitTLM has **removed experimental CRC presets, the only-CRC-passed filter, and the checker implementation**. Frames remain retained with source and FEC evidence. Application packet status is always `not_crc_verified` until a documented satellite-specific implementation is introduced. KISS escape integrity, missing-frame discontinuities and incomplete-packet detection remain active.

## ASRTU-1 full public telemetry catalog

OrbitTLM includes **27 versioned packet kinds and 2,067 published field rows** under `protocols/asrtu1/`. For ASRTU-1 the CLI and desktop automatically load the catalog with no site requests, internet connection, or manually authored JSON schema required.

The KISS application packet is mapped to a subsystem by `main_type` and `sub_type`, then all valid `byte.MSB-bit.length` descriptors are decoded MSB-first. The converter retains field code, Chinese/English labels, source formula, unmodified HEX, raw number, converted value, unit, quality annotation and receiver time evidence. Numerical types include `UX` (unsigned), `SX` (signed), `FX` (IEEE float), `HX/X` (raw HEX), `STR` (text), enumerations, and simple numerical scaling formulae.

The **Engineering telemetry** tab shows packet history, an all-field inspector and per-field graphs. The SAT-S3 calculated quaternion export uses the four published signed-int32 fields at business offsets 12/16/20/24, scaled by 1/10000. The older experimental float candidate export remains a separate command and is explicitly unverified.

### Application packet identification

Two layouts are recognized:
- A native business packet starting directly with its `main_type,sub_type` bytes.
- A legacy KISS envelope containing `main_type` at byte 1 and `sub_type` at byte 6, with subsystem fields starting at byte 7. This interpretation is flagged as a **candidate**.

Unrecognized KISS packets remain visible and exportable without being misclassified. Published field definitions alone cannot prove application CRC validity or prove that every captured channel conveys one of these 27 packet types.

Source tables may contain malformed ranges. Invalid definitions are not silently corrected; they are reported as `invalid_source_bit_position` while the remaining fields continue decoding. Unrecognized formulae also retain raw values.

## 中文桌面工作台（Qt 6）

默认页面为 **总览**：顶部展示真实的接收时间范围、去重传输帧、已识别/未识别业务包和 KISS 重组情况；右侧四个分系统状态面板只使用已识别的历史业务数据。不存在的工程量显示“暂无已识别的遥测参数”，而不是猜测值。

左侧“**数据回放列表**”按可用接收时间展示完整重组的 KISS 业务包，支持过滤、仅显示已识别包、暂停/播放、前后移动和倍率切换。状态栏、信息卡明确区分 **日志报告 FEC 成功**、**仅结构恢复** 与 **应用 CRC 未校验**；不会把未识别的 `F0/0F` 等业务包冒充 SAT-S3。

- **业务遥测**：已识别的卫星分系统包历史、按参数类别分组的中文属性、原始 HEX/位位置/公式及所选工程量时序曲线。
- **姿态与轨道**：仅使用成功匹配 SAT-S3 字段表的 q0..q3；四元数模长、相对时序曲线及简化 3D 示意。未知的姿态坐标系、应用 CRC 和星载 UTC 不会被赋予假定的准确性。
- **图像数据**：选定图像 APID 后扫描 SSDV 候选、按图像 ID 观测包序号跨度提示缺口、导出 SSDV、调用用户指定的可信外部解码器查看 JPEG。观测序号覆盖率 **不是** 整幅图像的完成百分比。
- **原始数据**：KISS HEX、源帧候选 APID 和分包列表。
- **调试信息**：通过菜单“工具 → 显示调试信息”打开，包含 MFC、VCID、帧级来源、HASH、错误与重组诊断，默认隐藏。
- **历史会话 / 导出**：保留 SQLite 重开与已有 CLI/CSV/JSON/四元数导出，BY04 不会错误使用 ASRTU-1 字段表。

UI 的 QtTest 启动及标签可见性测试纳入 Linux/Windows CI。Linux 离屏测试在支持绘制的环境会附带 `OrbitTLM-UI-preview.png` 供审阅。

## Windows 下载包及 Qt DLL

从 GitHub Actions 下载 **OrbitTLM-windows-x64**，解压 OrbitTLM-Windows-x64.zip 后运行 **OrbitTLM.exe**。命令行程序是 OrbitTLM-cli.exe。请保留应用目录中的 Qt6*.dll、platforms/、sqldrivers/、protocols/，不要单独移动 exe。

orbit_ui_tests.exe 和 orbit_tests.exe 是开发阶段的 QtTest/CTest 自动化测试程序，前者依赖 Qt6Test.dll，**不是正式应用入口**。早期的 Windows CI 压缩包误将整个 build/Release 收进去，导致测试文件出现在用户下载包。现采用独立 staging 目录，只包含正式应用、CLI、随附字段库及部署后的 Qt 运行库，并对 ZIP 解包结果执行依赖检查。

如果需要自行开发或调试测试程序，在具备 Qt 6 Test 模块的构建环境运行 cmake --build build --config Release 和 ctest --test-dir build -C Release --output-on-failure，无需在用户下载包中启动测试程序。
