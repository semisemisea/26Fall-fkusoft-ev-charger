# 环境冻结规格（frozen 2026-09-02）

> 本文件为**冻结级**约束，改动需组内评审。凡与本文冲突的 spec 内容以本文为准。

## Qt 版本与组件（frozen）

- **Qt SDK：6.2.4，精确版本锁定**（== 6.2.4，非 >=；VM Ubuntu22.04 apt 原生版本，杜绝版本漂移）
- **Qt Creator：≥ 6.2**（说明书要求；Creator 版本与 SDK 版本是两项独立约束）
- 组件清单（七个，全部 6.2.4 同版）：

| 组件 | 用途 | Ubuntu22.04 包名 |
|---|---|---|
| qt6-base (Core/Gui/Widgets) | 全部工程基础 | qt6-base-dev |
| qt6-network | REST 客户端(QNAM)/QTcpServer(socket 链路) | qt6-base-dev 内含 |
| qt6-sql (QSQLITE) | biz-core 数据层 | qt6-base-dev 内含（libqt6sql6-sqlite） |
| qt6-charts | ops-app 营收折线/状态环形图 | qt6-charts-dev / libqt6charts6-dev |
| qt6-webengine (WebEngineWidgets) | user-app 腾讯地图导航 | qt6-webengine-dev |
| qt6-test | L2/L3 验证用 QTest | qt6-base-dev 内含 |

- **明确排除 QtHttpServer**（6.4 才引入，与 6.2.4 冲突）：biz-core 的 HTTP 服务用 **QTcpServer 手写 HTTP/1.1 解析**（已定案，代码量 +1~2 天，无新增系统依赖）。QNAM 是 HTTP 客户端，不得误用作服务端

## C++/构建（frozen）

- C++17；CMake ≥ 3.16；`find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS ...)` 写死版本
- .clang-format 全仓强制（verify.sh L1 检查项）

## 开发/自测/验收环境分工

- **VM（VMware17 + Ubuntu22.04）**：验收基准环境，Qt 6.2.4 from apt；biz-core/user-app/ops-app/simulator 全量运行
- **WSL（开发机，Agent 主写码环境）**：用户本人用 Qt Online Installer 装 6.2.4（含上述组件）——**Agent 自测环境与验收环境同版**，L1/L3 验证在 WSL 完成
- **macOS**：仅代码浏览/文档，不参与构建自测
- dashboard 纯静态无 Qt 依赖；ml/ 为 Python（pandas/LightGBM），环境独立
- 注意：Arch 仓库 qt6-base 是 6.11.x 且无 6.2.4 包——**不得用 WSL Arch 原生 Qt 编译交付代码**，一律走 Online Installer 6.2.4 或 VM

## 部署端口/配置基线（frozen 初版，可随实现补）

- config.ini：[net] tcp_port=9000（simulator）/ http_port=8080（REST）；[db] path=data/charger.db；[map] key、offline=0；[ml] ingest_token；[sim] time_factor=60（1 秒=1 分钟）
- 首启空库自动执行 seed.sql（开关控制）

## 通信口径（frozen）

- 对外 RESTful HTTP+JSON（user-app/ops-app/dashboard）；错误=HTTP 状态码 + body.code 业务码**双层**（保留 2xxx 细分，映射表见 protocol §4）
- biz-core↔simulator 裸 TCP Socket（唯一 Socket 链路，命中考核点；**已与用户确认一条链路足够**）
- 用户端充电进度：2s 轮询（REST 无推送）

## 遗留待定（不阻塞 D2 协议冻结）

- ML 预测口径/阈值/回测门槛：后置专门讨论（peak_hours 字段契约已入协议 §1.3/§1.4，未决仅剩阈值与回测门槛）

## 已消化清单（2026-09-02 审查，原"下轮评审修订"各项已落入协议 DRAFT-5，本文件不再是这些主题的事实来源）

- ~~冻结用户 token 立即失效策略~~ → 已决：冻结立即删除全部 token（协议 §1.2）
- ~~REST 响应外壳/枚举编码/JSON Schema~~ → 已决：分页 {total,page,size,items}、错误双层映射（协议 §1/§4）、snapshot schema 冻结于 spec/schema/snapshot-v1.schema.json
- ~~地图章节~~ → 已决：协议 §7（geocoder/routeplan/降级/GCJ-02）
- ~~Socket stop 异步语义~~ → 已决：协议 §2.5（202 仅受理，最终值轮询获得）
- ~~追踪矩阵重写~~ → 已完成（trace-matrix 23 条四列版）
- 冲突时效声明：协议与矩阵已冻结的条目以**协议 DRAFT-5**为准，本文件仅约束环境/构建/部署基线
