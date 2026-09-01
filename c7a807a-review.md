# docs 审查报告

审查对象：commit `c7a807a`（分支 `docs/review`）
审查范围：`docs-local/`（CONTEXT + 5 ADR + 5 spec）与 `NOTES-local.md`。整体架构清晰、ADR 推理扎实、spec 分层（对外冻结 / 内部草案）得当。但 **DRAFT-3 从 socket 架构重构为 RESTful 后，多处残留旧章节号与旧推送语义**，需在冻结 v1 前清理。

## 一、阻塞性问题（冻结前必须修）

**1. `push.order.state` 与 REST 无推送自相矛盾**
`verification-strategy.md:29` 写"用户端收 push.order.state 引导结算"，但 `protocol-v1-draft.md:43` 与 `module-design-draft.md:36` 明确 REST 侧无推送、用户端 2s 轮询感知结束。这是 DRAFT-2 socket 时代的遗留，直接破坏协议一致性，评审 Agent 会判 spec 互相打架。改为"用户端轮询感知 status=PendingSettle 后跳结算页"。

**2. protocol §5 技术性错误：QNetworkAccessManager 不能做服务端**
`protocol-v1-draft.md:123`："biz-core 内置 HTTP 服务（QtHttpServer 或 QNetworkAccessManager 对等端）"。QNAM 是 HTTP **客户端**，无法 serve 请求。module-design:11 写的"Qt6 HttpServer"才对。删掉"或 QNetworkAccessManager 对等端"。

**3. ADR-0004 仓库名打错**
`adr/0004:4` 写 `26Fall-fkusoft-nv-changer`，实际 remote 是 `26Fall-fkusoft-ev-charger`（已 git remote 核实）。nv-changer → ev-charger。

## 二、失效的章节交叉引用（批量）

协议 v1 现在只有 §0–§6，但多处引用 §7/§5.x/§8，全是 DRAFT-2 重构前的旧编号：

| 文件:行 | 失效引用 | 实际应指向 |
|---|---|---|
| trace-matrix:9,10；module-design:61；protocol §1.1 | `§7 geocoder` / `§7 routeplan`（地图细则） | §7 章节根本不存在，地图细则缺失 |
| trace-matrix:13 | `sim 链路 §5.2` | §2.2–§2.4 |
| trace-matrix:23 | `§5.5 ingest` | §1.4 |
| trace-matrix:41 | `协议 §6`（错误码）→ 实为 §4 | §4 |
| trace-matrix:45；NOTES:83 | `§8 三个未决点` | §8 不存在；未决点散落在 §1.1/§2.4 |

**地图细则是最大空洞**：protocol §1.1 的 `station.list` 用到 geocoder、`一键导航` 用到 routeplan，但整个协议没有地图 URL scheme / 离线降级 / mock 区域表的约定，3 份 spec 却都"见 §7"。要么补一节 §7 地图细则，要么把约定内联回 protocol。

## 三、未决点状态前后不一致

`trace-matrix.md:45` 把三项列为"未决"，但 protocol 已把它们写成既定规则：
- "头像 ≤200KB base64" → protocol §1.1:45 已写"头像 base64 ≤200KB"（像已决）
- "charging.status 轮询 2s+推送兜底" → protocol §1.1:43 已定"纯 2s 轮询，无推送兜底"
- "seq 服务端不去重（user-app 侧）" → 混淆了 sim.report 的 seq（§2.4，服务端去重）与 user-app 侧（REST 无 seq 概念）

建议在 protocol 末尾单设 `§7 未决点`（或评审记录区），把真未决项与已决规则分开，避免矩阵和协议互相打脸。

## 四、安全缺口（建议至少在 ADR 记一笔）

- **管理员密码明文存库**：data-model:18 "种子数据 admin/123456"、CONTEXT:13 同，全文档未提密码哈希（SHA256/BCrypt）。演示项目可接受，但 trace-matrix:40"数据安全"把"token 不入库"列了、却没提密码存储——口径不全。至少在 ADR-0001 或 data-model 加一行"明文存（演示）"的显式声明。
- **token 无过期**：protocol §3 "内存表，无状态过期"。演示可接受，但重启即全部掉线，D10 演练"杀 biz-core"时三端都得重登录——module-design:85 的演练项应注明这点。

## 五、错误码 2009 无人使用

protocol §4 定义 `2009 重复请求`，但 §1 又说 POST 非幂等操作靠"当前状态校验"防重，重复预约→2002/2004、重复结算→2007。那 2009 在什么路径返回？要么删掉、要么在协议里给一个触发示例，否则是死错误码。

## 六、小问题

- **CONTEXT.md 缺 Pile 状态机**：data-model:19 列了桩五态（空闲/预约锁/充电中/故障/离线），但 CONTEXT 只规范化了订单状态机。建议在 CONTEXT `### 机制` 前加一节 `### 桩状态`，与订单状态机对称，供 simulator/ops 共用。
- **命名不统一**：CONTEXT 用 `TimeFactor`、protocol §6 用 `time_factor`、ADR-0003 用"加速因子"。选一个 canonical（建议 `time_factor` 配置键 + "加速因子"中文称呼）。
- **ADR 体例不一致**：ADR-0002 有 `## Considered Options` 独立小节，0001/0003/0005 把候选方案塞进 `## 背景`。建议统一（5 份都不长，统一进背景也行，但口径要一致）。
- **ADR 缺 Status/Date 字段**：spec 都有 `状态：DRAFT-3`，ADR 没有。冻结日临近（9/3），加 `Status: Accepted` + 日期便于后续追溯。
- **`protocol §1.2` ops `GET /piles?sid=`** 返回分页桩数组，但没说是否含 `累计次数/累计时长` 字段；img_04 后台表格需要这两列。补进响应字段或明确"pile.list 返回完整桩字段"。
- **`NOTES-local.md:86` 错字**："腾讯地图" → "腾讯地图"。

## 七、结构层面（非问题，记录）

- docs 全部 `-local` 后缀、`.gitignore` 排除——这个"本地文档不入库"的设计在 NOTES:1 已声明，合理（含 AI 生成痕迹 / 个人笔记）。但要提醒：**ADR 和 spec 是项目真正的合同，却不在 git 里**。如果五人协作要"PR 互评"（ADR-0004），评审者看不到 docs-local 就无法评审协议变更。建议至少把 `adr/` 和 `spec/protocol-v1-*.md` 纳入版本管理（可单独放 `docs/` 而非 `docs-local/`），否则 ADR-0004"协议变更需走 PR 并同步 common/"这句话落不了地。
- user-app/ 目前只有 `.qtcreator/` 和 `build/`，无源码——与 ADR-0004"组员分支为准，不抢建"一致，符合当前 D1 阶段。

---

**优先级建议**：先清 §一（3 条阻塞）→ 再批量修 §二引用 → 补 §7 地图细则或内联回 protocol → §三未决点归类。这些在 9/3 协议冻结前必须落地；§四–§六可滚到开发期。
