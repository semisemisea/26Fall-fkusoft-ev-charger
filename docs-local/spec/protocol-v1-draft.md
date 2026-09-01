# 协议 v1 草案（biz-core 对外契约 · RESTful + Socket 混合）

状态：DRAFT-5（吸收 2026-09-02 全文档审查：stop/cancel 异步语义澄清、GET /piles/{pid} 补全、sim.resume/心跳超时 30s/重启离线 10s 正式化、ML peak_hours、recommend 开关、分页外壳 {total,items}、错误码 2010、snapshot 独立 schema 落地；明细见 §8 已决清单）。评审后冻结 v1（目标 9/3 前）。冻结后改动需走 PR 并同步 common/。

## 0. 通信拓扑总览

```
user-app  ─┐                         ┌─ ops-app
dashboard ─┼─ HTTP/JSON(RESTful) ── biz-core ── TCP Socket(自定义帧) ── simulator
           ┘       QNetworkAccessManager        QTcpServer
```

- **对外（user-app / ops-app / dashboard）**：全部 RESTful HTTP + JSON。Qt 端用 QNetworkAccessManager，无手写协议解析
- **对内（biz-core ↔ simulator）**：裸 TCP 长连接 + 自定义帧（4B 长度 + JSON），指令/上报/心跳语义，命中"Socket 通信编程"考核点（说明书 1.2/1.6）
- **鉴权**：HTTP 侧登录后发 token（随机串，服务端内存表，无状态过期——演示项目），后续请求带 `Authorization: Bearer <token>`；Socket 侧连接即身份（simulator 注册后绑定）

## 1. RESTful 通用约定（对外三端）

- Base URL：`http://<host>:<http_port>/api/v1`
- 成功：`200` + JSON body；失败：`4xx/5xx` + `{"code": 2002, "msg": "电桩非空闲"}`（错误码复用 §6，与 socket 侧共用一套）
- 幂等性：GET/PUT/DELETE 幂等；POST 非幂等操作（预约/结算）服务端以"当前状态校验"防重（重复预约 → 2002/2004，重复结算 → 2007）
- 金额一律 `*_c`（分，整数）；电量 `kwh` REAL 三位小数；金额计算 `round(kwh × price_c)` 单次舍入（不变，原 §1.1）
- 时间戳统一 unix 秒；分页（user.list/order.list）用 `?page=&size=`，响应带 `total`

### 1.1 user-app REST API

| Method & Path | 请求 body | 响应 | 对应原消息 |
|---|---|---|---|
| POST /auth/login | {phone} | {uid,nickname,avatar,balance_c,new_user,token} | user.login |
| GET /users/me/orders/active | — | {order: null 或 {...}} | order.active |
| GET /stations?lat=&lng= 或 ?region=[&recommend=1] | — | [{sid,name,addr,lat,lng,price_c,free,total,dist_km,pred_free?}] 缺省距离升序；recommend=1 时按 pred_free 降序→dist_km 升序，且 pred_free 必返（无 ML 数据时降级为距离排序并省略 pred_free）；**free=仅 Idle 桩数**（不含预约锁，"可预约"口径） | station.list |
| GET /stations/{sid} | — | {station…, piles:[{pid,code,type,status,power_kw}]} | station.detail |
| POST /piles/{pid}/reserve | — | {oid,reserve_expire} | pile.reserve |
| POST /orders/{oid}/start | {target_kwh?} | {oid,started_at,session_id} | charging.start |
| POST /orders/{oid}/stop | — | **202** {oid,status:"stopping"}（仅受理确认，**不含任何最终值**；最终 kwh/amount_c 由客户端轮询 GET /orders/{oid} 获得，异步语义见 §2.5） | charging.stop |
| GET /orders/{oid} | — | {oid,status,kwh,amount_c,elapsed} | charging.status |
| DELETE /orders/{oid} | — | **202** {oid,status:"cancelling"}（仅受理确认；折算金额与最终余额均由轮询获得，异步规则同 §2.5） | charging.cancel |
| POST /orders/{oid}/settle | — | {balance_c} | order.settle |
| POST /wallet/recharge | {amount_c} | {balance_c} | wallet.recharge |
| PUT /users/me | {nickname?}{avatar_b64?} | {uid,…} | user.profile.update |
| GET /users/me/orders?status=&page=&size= | — | {total,page,size,items:[订单对象]} | order.list |

- **充电进度推送问题**：REST 无服务端推送。方案：充电页 2s 轮询 GET /orders/{oid}（与原"轮询+推送兜底"简化为纯轮询，演示可接受）；充电结束（target 达成/桩报 done）由下次轮询感知
- 未完成订单拦截（不变）：登录后/进充电页前 GET active；pile.reserve 服务端事务内原子校验活动订单（2004）
- 冻结用户登录 → 2005；头像 base64 ≤200KB

### 1.2 ops-app REST API

| Method & Path | 请求 | 响应 | 对应原消息 |
|---|---|---|---|
| POST /auth/admin-login | {username,password} | {aid,name,token} | admin.login |
| GET /stats/revenue?period=today\|7d\|30d | — | {today_c,month_c,total_c,series:[{date,revenue_c,kwh,order_count}]}（series 元素固定四字段；date=YYYY-MM-DD 本地时区 Asia/Shanghai，7d/30d 按日连续；today 时 series 为当日单点） | stats.revenue |
| GET /piles/summary | — | {in_use,idle,faulty,offline,total,percent:{in_use,idle,faulty,offline}}（percent=各数÷total×100 保留 1 位小数，四项和=100，A4 验收用；idle=空闲+预约锁） | pile.summary |
| GET /piles?sid=&page=&size= | — | {total,page,size,items:[{pid,code,sid,station_name,type,power_kw,status,chg_count,chg_minutes,status_text}]}（img_04 表格六列全覆盖；chg_minutes=累计秒÷60 取整） | pile.list |
| POST /piles/{pid}/reboot | — | 202 Accepted（异步结果轮询 GET /piles/{pid}） | pile.reboot |
| GET /piles/{pid} | — | {pid,code,sid,station_name,type,power_kw,status,chg_count,chg_minutes,status_text}（A5 重启轮询用：status 回到非离线即恢复完成） | pile.detail |
| GET /stations | — | [{sid,name,addr,lat,lng,total,online_rate,pred_load_24h?}] | station.list |
| GET /stations/{sid} | — | 同 user 端 | station.detail |
| POST /stations | {name,addr,lat,lng,price_c,piles:[{code,type,power_kw}]} | {sid,pids} | station.create |
| GET /users?search=&page=&size= | — | {total,page,size,items:[{uid,phone,nickname,balance_c,reg_at,status,status_text}]} | user.list |
| PUT /users/{uid}/status | {freeze:bool} | — （**冻结同时删除该 uid 全部 token**，立即生效；解冻不恢复 token） | user.freeze |
| GET /orders?date=&status=&page=&size= | — | {total,page,size,items:[{oid,phone,sid,station_name,pid,code,status,start_at,end_at,kwh,amount_c,status_text}]}（img_05 列全覆盖） | order.list |
| POST /orders/{oid}/settle | — | {} | order.settle（代结算） |
| GET /warnings | — | [{alid,kind,ref_id,msg,at,cleared_at}] | warning.list |

### 1.3 dashboard REST API

- `GET /api/v1/dashboard/snapshot` → 单 JSON 快照（**机器可校验 schema 冻结于 spec/schema/snapshot-v1.schema.json，D1 验收即按该文件校验**；字段：{totals:{users,kwh_today,revenue_today_c,piles_online}, load_24h:[{hour,load}], prediction:{h1:[{sid,load,free_piles,peak_hours:[{start,end}]}],h6:[…],h24:[…],forecast_at,model_version}, station_rank:[{sid,name,revenue_c}], revenue_mix:[{slot,amount_c}], alerts:[{kind,msg,at}]}；前端 5s 轮询）
- 同源部署（biz-core 伺服静态文件，无 CORS）；无鉴权（内网演示）

### 1.4 ML 内部通道（REST，保留原设计）

- `GET /api/internal/ml/export?days=60`（X-Ingest-Token）→ 历史 CSV（含特征列）
- `POST /api/internal/ml/ingest`（X-Ingest-Token）→ predictions.json，校验后整批替换 t_ml_prediction；**每站每 horizons 必含 `peak_hours:[{start,end}]`**（"高峰充电时段"，说明书 ML 章节原文要求；start/end 为模拟时钟 HH:MM，消费方：大屏预测卡片、ops 预警文案）
- 唯一写者边界不变：ML 零 SQL

## 2. Socket 帧（仅 biz-core ↔ simulator）

### 2.1 帧格式与握手

- `4 字节大端长度 + JSON 包体`，TCP 长连接
- 每条消息：`{"type":"…","ts":…,"body":{…}}`（无 seq 信封——指令确认用 session_id+type 配对，上报幂等用 report seq）
- simulator 连接后首条 `sim.register` 之前，服务端不接受其他消息（未注册 → 1003 断开重连由 simulator 自理）

### 2.2 注册与心跳

| type | 方向 | body | 说明 |
|---|---|---|---|
| sim.register | 桩→服务端 | {stations:[{name,addr,lat,lng,price_c,piles:[{code,type,power_kw}]}]} | 按 code 认领（ops=主数据来源）；响应 {accepted:[{code,pid}]} |
| sim.heartbeat | 桩→服务端 | {pids:[{pid,hw_ok}]} | **仅硬件健康位**，不报占用状态；10s 周期 |
| sim.resume | 桩→服务端 | {pid,session_id,est_kwh}（仅重连后有活动 session 的桩逐条发送） | 重连校准正式化：服务端校验 session 仍处于 Charging 且 est_kwh ≥ 已计读数 → 以 est_kwh 校准（差值补记账）；est_kwh < 已计读数 → 拒绝校准、维持服务端读数并回 nak；无活动 session 则回 nak 并丢弃。校准在单事务内完成（订单读数+t_pile 一致） |

- **心跳超时阈值：30s**（3 个心跳周期 10s 未收到 → 桩转 Offline + t_alert kind=2 告警；恢复心跳 → 回到心跳报告的状态）。该 30s 同时是 E1/A6 验收判据（心跳超时→离线+告警）
- 离线桩指令可达性：桩 Offline 期间 push.* 指令不等待 ack，直接走失败路径（2008/兜底），不做离线指令队列

### 2.3 充电控制与计量

| type | 方向 | body | 说明 |
|---|---|---|---|
| push.pile.reserve | 服务端→桩 | {pid,session_id} | 桩锁定自检；失败 pile.nak（服务端释放桩+订单 Cancelled） |
| push.pile.start | 服务端→桩 | {pid,session_id,target_kwh?} | 起表；桩回 pile.ack |
| push.pile.stop | 服务端→桩 | {pid,session_id} | 停表；桩回 final_kwh（pile.ack 附带） |
| push.pile.release | 服务端→桩 | {pid,session_id} | 会话终止释放（超时/取消路径） |
| push.pile.reboot | 服务端→桩 | {pid,session_id?} | session_id=当前充电会话则离线前先 stop；**桩收到 reboot → 主动断开 socket 并离线 10s 后重连**（A5 用例的 N=10s；重连后按 §2.2 sim.resume 规则校准；无活动会话则纯断连 10s） |
| sim.report | 桩→服务端 | {pid,session_id,seq,kwh_delta,state} | 5s 周期；state ∈ charging\|done（done 附 final_kwh）；seq 幂等去重 |
| pile.ack / pile.nak | 桩→服务端 | {pid,session_id,reason?} | 指令执行结果 |

### 2.4 会话与可靠性规则（不变，沿用 DRAFT-2；sim.resume 已升格为 §2.2 正式消息）

- 状态权威在服务端：桩占用状态由订单状态机维护，心跳只带 hw_ok
- session_id 贯穿 reserve/start/report/stop/release；无 session 不计量
- 指令 5s 无 ack/nak → 重发 1 次 → 再超时桩转离线+告警；nak 走订单失败路径
- sim.report 幂等（session 内 seq 单调递增）；断线重连后校准一律走 §2.2 sim.resume（est_kwh 单调校验 + 事务校准）；离线期服务端按 功率×加速时长 兜底续算

### 2.5 stop/cancel 异步语义（与 REST 的衔接）

- REST stop/cancel 返回 **202 Accepted** + {oid,status:"stopping"/"cancelling"}：**仅受理确认，不含任何最终值**。最终电量/金额依赖异步 pile.ack(final_kwh)，客户端一律通过轮询 GET /orders/{oid} 获取
- biz-core 下发 push.pile.stop 后：5s 内收 pile.ack(final_kwh) → 订单转 PendingSettle，kwh/amount_c 落库；客户端下次轮询即见 status=PendingSettle + 最终金额
- 超时路径：5s 无 ack → 重发 1 次 → 再 5s 无 ack → **桩转离线**，订单按"兜底读数"（功率×加速时长）转 PendingSettle + t_alert 告警——stop 永远不会悬挂，**最终必然到达 PendingSettle 或 Failed 二者之一**
- pile.nak(stop) → 同兜底路径转 PendingSettle（桩硬件异常但订单必须可结算）
- **失败终态**：若兜底读数写库失败（数据库错误等内部原因，区别于桩侧原因），订单转 **Failed**（第七状态，复用订单状态机；Failed 仅可由管理员处理，不拦截新订单），t_alert kind=1 告警。桩侧一切失败路径都走兜底转 PendingSettle，不产生 Failed——客户端无需为 Failed 做常规 UI
- charging.cancel（DELETE /orders/{oid}）复用同一异步规则；取消后余额查询以轮询 GET /orders/{oid} 的 status 跳变为准（Cancelled→金额=0 不入结算页；Charging cancel→PendingSettle→结算页）

## 3. 身份与安全

- **HTTP 侧**：POST /auth/login 成功 → 发 token（内存表，重启失效可接受——D10 演练杀 biz-core 后三端需重新登录，已接受）；后续请求 Bearer token；服务端按 token 识别 uid/aid 并做归属校验（操作他人订单 → 2006）；ops 接口要求 admin token（角色校验）；**冻结用户时立即删除其全部 token**
- **管理员登录失败**：用户名不存在或密码错误统一返回 **2010 用户名或密码错误**（HTTP 401，不区分二者以防枚举）。2006 仅用于"已登录身份越权"（操作他人订单/非 admin 访问 ops 接口）——A1 用例错密码路径同步改为 2010
- **管理员密码**：明文存库（admin/123456 种子）、大屏无鉴权——均为演示环境的显式妥协（隔离内网，无真实资金与隐私数据），生产环境应换哈希+网关鉴权；此为 ADR 级记录，验收被问到时按此口径回答。追踪矩阵 #22"数据安全"对这两项标注演示豁免，验收以其余各项（余额 CHECK/事务原子/归属校验/token 不入库）为准
- **Socket 侧**：sim.register 后连接绑定其认领的 pid 集合，只接受/只上报名下 pid
- 会话恢复：重连后重新 login 拿新 token，客户端拉 active/orders 同步状态（原设计不变）

## 4. 错误码

0 成功；1xxx 协议/请求错：1001 非法 JSON、1002 未知路径、1003 未注册/未登录、1004 版本不匹配、1005 token 无效；2xxx 业务错：2001 手机号非法、2002 桩非空闲、2003 余额不足、2004 存在活动订单、2005 用户被冻结、2006 权限/归属不符、2007 订单状态不允许该操作、2008 桩/站不存在、2010 用户名或密码错误；3xxx 服务端错：3001 数据库、3002 内部异常
- 2009 重复请求**已删除**：POST 防重全部走状态校验（重复预约→2002/2004、重复结算→2007），无独立触发路径
- HTTP 状态码映射：2xxx → 400/409（业务冲突）、1xxx/3xxx → 400/500、token 无效 → 401

## 5. 多线程模型影响（module-design 同步修订）

- biz-core 内置 HTTP 服务（**QTcpServer 手写 HTTP/1.1 解析**——QtHttpServer 需 ≥6.4 与冻结的 6.2.4 冲突，QNAM 仅是客户端；见 env-freeze.md）：请求处理线程池 → 业务队列（单写者不变）
- 快照读取经不可变快照发布；HTTP handler 无共享可变状态
- 断线重连语义仅剩 simulator 链路（指数退避不变）；客户端 HTTP 失败重试：GET 可自动重试，POST 不自动重试（幂等性见 §1）

## 6. 配置（config.ini 不变）

[db] path / [net] tcp_port（simulator）/ http_port（REST）/ [map] key、offline / [ml] ingest_token / [sim] time_factor

## 7. 地图与导航细则（geocoder / routeplan，user-app 专用）

- **坐标系**：腾讯地图 WebService 使用 GCJ-02；虚拟站点表/区域表/站点经纬度统一 GCJ-02 存储与传输，不做 WGS-84 转换（演示口径统一即可）
- **Key**：config.ini `[map] key`；geocoder 与 routeplan 共用；`[map] offline=1` 时不外呼（见降级）
- **定位（软件模拟 GPS）**：默认坐标=区域表首个城市中心；下拉选区域=查内置区域表（name→{lat,lng}）；手输地址→geocoder：
  `GET https://apis.map.qq.com/ws/geocoder/v1/?address=<URL编码地址>&key=<key>`
  成功响应取 `result.location.lat/lng`；**失败码处理**：HTTP 非 200 / status≠0 / 超时 3s → 一律降级，且统一采用 **mock 区域表坐标**（不用"上次坐标"——首次定位尚无上次值，区域表永远可用且可复现，验收口径一致；UI 提示"定位失败，已使用默认位置"）
- **距离与排序**：haversine 公式（R=6371km）客户端计算 dist_km，保留 1 位小数；station.list 服务端按请求坐标升序返回，客户端重定位后本地重排
- **一键导航**：QWebEngineView 加载 routeplan URL：
  `https://apis.map.qq.com/uri/v1/routeplan?type=<drive|walk>&from=<lat,lng,name>&to=<lat,lng,name>&referer=<key>`
  ⚠ **已知缺陷（组件内 TODO，NOTES-local TODO#1）：上行 URL 参数格式错误**——官方参数表为 from=起点名称（文本）、fromcoord=起点坐标、to/tocoord 同理，且 type=walk 官方标注"仅适用移动端"（桌面不可依赖）。**冻结 v1 前必须修正本行**：type 仅 drive、from=站名/我的位置名、fromcoord=lat,lng、to=站名、tocoord=lat,lng、referer=key；walk 入口改走静态示意。修正前 U4 用例不得按本行执行
  全参数 URL 编码；入口两处：**站列表卡片"距离"文本可点击**（正文要求）+ 站详情页"驾车/步行"按钮（UI 图，冗余入口）
- **降级行为（统一优先级：mock 区域表坐标 → 静态示意，三种场景同一规则）**：a) geocoder 失败（含首次定位/已有位置/任意时刻）→ 用 mock 区域表坐标继续（功能不中断）；b) routeplan 页面 loadFinished(false) 或 15s 超时→切换静态路线示意页（起终点名+直线距离估算文字）；c) offline=1→直接跳过 geocoder 用 mock 坐标、导航直接显示静态示意（此三条覆盖首次失败、已有位置、离线模式全部场景，无"保留上次坐标"分支）
- **费用**：geocoder/routeplan 免费额度足够演示；不代理不缓存（key 在配置文件，config.example.ini 提交，真实 key 不入 git）

## 8. 未决点与评审记录（与已决规则分离）

**已决（正文即规则，勿再列为未决）**：
- 头像 base64 ≤200KB（§1.1）；charging.status 纯 2s 轮询、无推送（§1.1）；sim.report seq 服务端去重（§2.3，user-app 侧 REST 无 seq 概念）；stop/cancel 异步 202（§2.5）；冻结用户 token 立即删除（§1.2 user.freeze）
- 地图坐标系/降级策略（§7）；导航双入口（§7）
- 2026-09-02 审查补充已决：stop/cancel 202 仅受理、最终值只经轮询（§2.5）；GET /piles/{pid} 新增（§1.2）；sim.resume 正式化+心跳超时 30s+重启离线 10s（§2.2/§2.3）；ML peak_hours 入 predictions.json/snapshot（§1.3/§1.4）；station.list recommend=1 开关+free=仅 Idle 口径（§1.1）；pile.summary 补 percent（§1.2）；全部列表接口统一 {total,page,size,items} 分页外壳、series 元素四字段+时区（§1.1/§1.2）；错误码 2010 + A1 改 2010（§3/§4）；snapshot 独立 JSON Schema（spec/schema/snapshot-v1.schema.json）；管理端明文密码/大屏无鉴权=演示豁免（§3）

**真未决（评审中定）**：
1. ML 预测口径/阈值/回测门槛——peak_hours 字段已冻结（契约层），但阈值（何时触发 kind=3 预警）与回测门槛仍待用户指定后专门讨论（影响 M4 用例判据）
2. 多线程范围解读：说明书 1.6 原文"程序的主框架应该是一个多线程结构"未限定组件——biz-core 线程模型已定（§5/module-design），user-app/ops-app/simulator 是否需各自多线程结构待组内定（影响 trace-matrix #19/20 追踪范围）
3. spec/adr 是否迁入 git 正式路径（见 review §七：ADR 与协议是合同却不在版本管理，PR 评审落空）——待组内定
