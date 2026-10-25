# OrbitTLM 验证记录 · 2026-10-26

- BY04：独立 OBC-SF 220 项、BUV-S0 99 项；main/subtype 从自身封装读取。
- 52 条参考公式来自 HyacinthSat/asrtu-sndmdm-backend 的公开 ASRTU CSV，按 BY04 字段名、位宽和网页原码核对。源码版本：6c8eeba069560483debca688ea3bf77dd5491518。
- 20 项浮点使用网页原码/数值对照；另补计数、时间及电压、电流、温度参考换算。参考模型在字段元数据中保留出处；仍未找到定义的量保持原码。
- 修复 LOG/BIN 合并及 SQLite 重开时移动无时间戳帧的问题，KISS 状态按卫星、SCID、VC 隔离。
- 分块覆盖跨 TM 帧、跨帧转义、序号回绕、重复块、缺块、独立信道，以及有界的 CCSDS 首/中/末段重组。BY04 按已知的一字节副类型处理每段头；原始块保留在 JSON。
- 保留声明长度之外的 2/4 字节尾部，不将其当作已通过 CRC。
- 真实 LOG/BIN 回放：28 帧、37 个 KISS 包（此前 35）、21 个业务包、10 条四元数；重组缺口及 EOF 残包均为 0。
- Windows Release 编译；QtTest 协议 37 项、UI 5 项通过，CTest 3/3 通过。
- UI 实包导入、导航、字段选择、失败导入保留原会话、1050 像素窗口验证；中文截图已检查。
- CCSDS 分段业务以构造包验证；现有 BY04 实录未发现首/中/末段标志。没有 RF 或在轨硬件验证。
- [分段标志标准](https://ccsds.org/Pubs/133x0b2e2.pdf) · [公开参考字头](https://github.com/HyacinthSat/asrtu-sndmdm-backend) · [布局参考](https://primer.style/product/components/page-layout/)

Linux/Windows CI 状态随交付报告记录。
