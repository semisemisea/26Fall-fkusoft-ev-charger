# 协议 v1 草案（biz-core 对外契约 · RESTful + Socket 混合）

状态：DRAFT-3（通信方式重构：对外统一 RESTful；biz-core↔simulator 保留裸 TCP，命中说明书 socket 考核点）。评审后冻结 v1（目标 9/3 前）。冻结后改动需走 PR 并同步 common/。

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
| GET /stations?lat=&lng= 或 ?region= | — | [{sid,name,addr,lat,lng,price_c,free,total,dist_km,pred_free?}] 距离升序 | station.list |
| GET /stations/{sid} | — | {station…, piles:[{pid,code,type,status,power_kw}]} | station.detail |
| POST /piles/{pid}/reserve | — | {oid,reserve_expire} | pile.reserve |
| POST /orders/{oid}/start | {target_kwh?} | {oid,started_at,session_id} | charging.start |
| POST /orders/{oid}/stop | — | {kwh,amount_c} | charging.stop |
| GET /orders/{oid} | — | {oid,status,kwh,amount_c,elapsed} | charging.status |
| DELETE /orders/{oid} | — | {amount_c,balance_c} | charging.cancel |
| POST /orders/{oid}/settle | — | {balance_c} | order.settle |
| POST /wallet/recharge | {amount_c} | {balance_c} | wallet.recharge |
| PUT /users/me | {nickname?}{avatar_b64?} | {uid,…} | user.profile.update |
| GET /users/me/orders?status= | — | 订单数组(分页) | order.list |

- **充电进度推送问题**：REST 无服务端推送。方案：充电页 2s 轮询 GET /orders/{oid}（与原"轮询+推送兜底"简化为纯轮询，演示可接受）；充电结束（target 达成/桩报 done）由下次轮询感知
- 未完成订单拦截（不变）：登录后/进充电页前 GET active；pile.reserve 服务端事务内原子校验活动订单（2004）
- 冻结用户登录 → 2005；头像 base64 ≤200KB

### 1.2 ops-app REST API

| Method & Path | 请求 | 响应 | 对应原消息 |
|---|---|---|---|
| POST /auth/admin-login | {username,password} | {aid,name,token} | admin.login |
| GET /stats/revenue?period=today\|7d\|30d | — | {today_c,month_c,total_c,series:[…]} | stats.revenue |
| GET /piles/summary | — | {in_use,idle,faulty,offline,total} | pile.summary |
| GET /piles?sid= | — | 桩数组(分页) | pile.list |
| POST /piles/{pid}/reboot | — | 202 Accepted（异步结果轮询 GET /piles/{pid}） | pile.reboot |
| GET /stations | — | [{sid,name,addr,lat,lng,total,online_rate,pred_load_24h?}] | station.list |
| GET /stations/{sid} | — | 同 user 端 | station.detail |
| POST /stations | {name,addr,lat,lng,price_c,piles:[{code,type,power_kw}]} | {sid,pids} | station.create |
| GET /users?search= | — | 用户数组(分页) | user.list |
| PUT /users/{uid}/status | {freeze:bool} | — | user.freeze |
| GET /orders?date=&status= | — | 订单数组(分页) | order.list |
| POST /orders/{oid}/settle | — | {} | order.settle（代结算） |
| GET /warnings | — | [{alid,kind,ref_id,msg,at,cleared_at}] | warning.list |

### 1.3 dashboard REST API

- `GET /api/v1/dashboard/snapshot` → 一个 JSON：{totals:{users,kwh_today,revenue_today,piles_online}, load_24h, prediction:{h1,h6,h24}, station_rank, revenue_mix, alerts}；前端 5s 轮询
- 同源部署（biz-core 伺服静态文件，无 CORS）；无鉴权（内网演示）

### 1.4 ML 内部通道（REST，保留原设计）

- `GET /api/internal/ml/export?days=60`（X-Ingest-Token）→ 历史 CSV（含特征列）
- `POST /api/internal/ml/ingest`（X-Ingest-Token）→ predictions.json，校验后整批替换 t_ml_prediction
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

### 2.3 充电控制与计量

| type | 方向 | body | 说明 |
|---|---|---|---|
| push.pile.reserve | 服务端→桩 | {pid,session_id} | 桩锁定自检；失败 pile.nak（服务端释放桩+订单 Cancelled） |
| push.pile.start | 服务端→桩 | {pid,session_id,target_kwh?} | 起表；桩回 pile.ack |
| push.pile.stop | 服务端→桩 | {pid,session_id} | 停表；桩回 final_kwh（pile.ack 附带） |
| push.pile.release | 服务端→桩 | {pid,session_id} | 会话终止释放（超时/取消路径） |
| push.pile.reboot | 服务端→桩 | {pid} | 离线 N 秒重连 |
| sim.report | 桩→服务端 | {pid,session_id,seq,kwh_delta,state} | 5s 周期；state ∈ charging\|done（done 附 final_kwh）；seq 幂等去重 |
| pile.ack / pile.nak | 桩→服务端 | {pid,session_id,reason?} | 指令执行结果 |

### 2.4 会话与可靠性规则（不变，沿用 DRAFT-2）

- 状态权威在服务端：桩占用状态由订单状态机维护，心跳只带 hw_ok
- session_id 贯穿 reserve/start/report/stop/release；无 session 不计量
- 指令 5s 无 ack/nak → 重发 1 次 → 再超时桩转离线+告警；nak 走订单失败路径
- sim.report 幂等（session 内 seq 单调递增）；重连后 sim.resume {pid,session_id,est_kwh} 校准；离线期服务端按 功率×加速时长 兜底续算

## 3. 身份与安全

- **HTTP 侧**：POST /auth/login 成功 → 发 token（内存表，重启失效可接受）；后续请求 Bearer token；服务端按 token 识别 uid/aid 并做归属校验（操作他人订单 → 2006）；ops 接口要求 admin token（角色校验）
- **Socket 侧**：sim.register 后连接绑定其认领的 pid 集合，只接受/只上报名下 pid
- 会话恢复：重连后重新 login 拿新 token，客户端拉 active/orders 同步状态（原设计不变）

## 4. 错误码（不变）

0 成功；1xxx 协议/请求错：1001 非法 JSON、1002 未知路径、1003 未注册/未登录、1004 版本不匹配、1005 token 无效；2xxx 业务错：2001 手机号非法、2002 桩非空闲、2003 余额不足、2004 存在活动订单、2005 用户被冻结、2006 权限/归属不符、2007 订单状态不允许该操作、2008 桩/站不存在、2009 重复请求；3xxx 服务端错：3001 数据库、3002 内部异常
- HTTP 状态码映射：2xxx → 400/409（业务冲突）、1xxx/3xxx → 400/500、token 无效 → 401

## 5. 多线程模型影响（module-design 同步修订）

- biz-core 内置 HTTP 服务（**QTcpServer 手写 HTTP/1.1 解析**——QtHttpServer 需 ≥6.4 与冻结的 6.2.4 冲突，QNAM 仅是客户端；见 env-freeze.md）：请求处理线程池 → 业务队列（单写者不变）
- 快照读取经不可变快照发布；HTTP handler 无共享可变状态
- 断线重连语义仅剩 simulator 链路（指数退避不变）；客户端 HTTP 失败重试：GET 可自动重试，POST 不自动重试（幂等性见 §1）

## 6. 配置（config.ini 不变）

[db] path / [net] tcp_port（simulator）/ http_port（REST）/ [map] key、offline / [ml] ingest_token / [sim] time_factor
