# OrbitTLM

C++20 / Qt 6 卫星遥测回放工作台，支持 ASRTU-1 和八一04星（BY04）。

- LOG/BIN 联合导入、逐帧来源保留、SQLite 历史会话。
- 按航天器和虚拟信道重组 KISS，支持跨帧转义及计数回绕。
- BY04 使用独立 OBC-SF、BUV-S0 字头；ASRTU-1 使用 27 类公开字段表。
- 中文参数、时序曲线、相对姿态、四元数及 JSON/CSV 导出。
- BY04 VC1 原生 DSLWP SSDV 图像分片及图像 CRC 检查。

## 使用

选择卫星和 LOG/BIN 文件，点击“导入”。左侧回放，顶部切换总览、业务遥测、姿态、图像和原始数据。“字段详情”和“工具 → 调试信息”提供位地址、公式及来源。

BY04 当前覆盖录制中已核对的 OBC-SF（220 项）和 BUV-S0（99 项）。公式参考公开 ASRTU 字头，并与 BY04 网页原码逐项核对；参考换算保留来源，未确定的量显示原码。其他 52 类网页分包仍需各自的数值包型和完整字段定义。

分段业务按连续序号及首/中/末标志重组，已知 BY04 分包使用每段一个副类型字节。缺块或副类型不符时保留原块；应用 CRC、姿态参考系不作默认认证。

```sh
OrbitTLM-cli --satellite BY04 --log decoder.log --bin SSDV_received_frames223.bin --telemetry-json by04.json --telemetry-csv by04.csv --quaternion-csv by04-quaternions.csv
```

## 构建

需要 CMake 3.21+、C++20、Qt 6.4+（Core、Widgets、Network、Sql、Charts、Test）。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows 在构建和测试命令中加入 `--config Release` / `-C Release`。
GitHub Actions 提供 Linux 程序、Windows 完整运行包和界面预览。

## 公开资料

- [ASRTU 字头与接收结构](https://github.com/HyacinthSat/asrtu-sndmdm-backend)
- [BY04 遥测页面](http://119.45.229.166/pages/telemetry-BY04uv.html)
- [CCSDS 分段标志](https://ccsds.org/Pubs/133x0b2e2.pdf)
- [GitHub Primer 布局](https://primer.style/product/components/page-layout/)

`python scripts/collect_by04_dashboard.py evidence-directory` 可保存网页与接口快照。
