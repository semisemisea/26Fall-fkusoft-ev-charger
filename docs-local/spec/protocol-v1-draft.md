# 协议 v1 草案（biz-core 对外契约）

状态：DRAFT-2（吸收 2026-09-02 评审意见），评审后冻结为 v1（目标 9/3 前）。冻结后改动需走 PR 并同步 common/。

## 1. 传输与信封

- TCP 长连接，`4 字节大端长度 + JSON 包体`（长度 = 包体字节数，不含长度头）
- 通用信封：

```json
{
  "v": 1,            // 协议版本
  "type": "msg.type",// 消息类型，见目录
  "seq": 42,         // 客户端递增序号；响应回带同 seq
  "ts": 1725187200,  // unix 秒
  "body": { }
}
```

- 响应信封：`type` 相同 + `"ok": true|false`，失败时 `"err": {"code": 2002, "msg": "电桩非空闲"}`

### 1.1 单位与精度（全协议统一）

- 金额字段一律 `*_c`（分，整数）：`price_c` `balance_c` `amount_c` `delta_c`；协议中不出现裸 `amount/price/balance`
- 电量 `kwh`：REAL，保留 3 位小数
- 金额计算：`amount_c = round(kwh × price_c)`，四舍五入到分，**单次结算只做一次舍入**；峰谷策略按各时段电量分别乘对应费率后再求和，同样只在最终金额舍入一次
- 展示层（元）由客户端自行 `*_c / 100` 换算，服务端不出"元"

## 2. 连接与身份

- 连接后第一条消息必须是 `hello`，**只声明** `client` ∈ `user-app | ops-app | simulator`（不带凭证）；biz-core 回 `hello_ack`（附 server 时间、加速因子、协议版本）。未 hello 前其他消息一律 1003 拒绝
- **身份绑定（两段式）**：hello 后连接为"匿名"态，仅允许发登录类消息（user.login / admin.login / sim.register）；登录成功后连接绑定身份——user-app 绑 uid，ops-app 绑 aid，simulator 绑其注册的 pid 集合。匿名态发业务消息 → 2006
- **权限与归属校验（服务端强制）**：user-app 连接只能操作自己 uid 的订单/钱包（订单归属不符 → 2006）；ops-app 全部业务消息须已绑 aid；simulator 只接受其名下 pid 的指令、只上报其名下 pid
- **会话恢复**：断线重连后重新 hello + 登录（免密，手机号即可）；服务端不做会话续期，客户端登录成功后自行拉 `order.active` / `order.list` 同步本地状态（user-app 充电页据此恢复）
- 心跳：客户端每 30s 发 `heartbeat`，服务端 90s 未收到判离线（simulator 超时 → 其名下桩转离线并产生告警）

## 3. user-app 消息

| type | body 请求 | 响应 body |
|---|---|---|
| user.login | {phone} | {uid, nickname, avatar, balance_c, new_user} |
| order.active | {} | {order: null 或 {oid,pid,sid,name,status,kwh,amount_c}} （**进充电页前必查**，见下） |
| station.list | {lat, lng} 或 {region} | [{sid,name,addr,lat,lng,price_c,free,total,dist_km,pred_free?}] 按距离升序，见 §8 |
| station.detail | {sid} | {station…, piles:[{pid,code,type,status,power_kw}]} |
| pile.reserve | {pid} | {oid, reserve_expire} （服务端原子拦截活动订单，见下） |
| charging.start | {oid, target_kwh?} | {oid, started_at, session_id} （target_kwh 缺省=不限，手动停） |
| charging.stop | {oid} | {kwh, amount_c} （服务端停止计量，订单转待结算） |
| charging.status | {oid} | {oid, status, kwh, amount_c, elapsed} |
| charging.cancel | {oid} | {amount_c, balance_c} （见 §9 取消规则） |
| order.settle | {oid} | {balance_c} |
| wallet.recharge | {amount_c} | {balance_c} |
| user.profile.update | {nickname?}{avatar_b64?} | {uid,…} |
| order.list | {status?} | [{oid,sid,name,pid,start,end,kwh,amount_c,status}] |

- **未完成订单拦截（需求 1.4 用户端"充电前检查"）**：a) 登录成功后客户端应调 order.active，有活动订单 → 引导至对应页面；b) 进入充电页前必须调 order.active，存在 PendingSettle → 2004 + pending_oid，客户端强制跳转结算页；c) 服务端在 `pile.reserve` 事务内原子校验该 uid 无活动订单（status ∈ Reserved/Charging/PendingSettle 任一 → 2004），并发请求由事务串行化保证
- 登录前置检查：用户被冻结 → 2005；昵称缺省规则见 data-model-draft 演示数据约定（"用户+手机号后 4 位"）
- 服务端主动推送（user-app 收）：`push.order.state` {oid,status,kwh,amount_c}（充电结束三路径均触发）；`push.pile.status` {pid,status}

## 4. ops-app 消息

| type | body 请求 | 响应 body |
|---|---|---|
| admin.login | {username,password} | {aid,name} |
| stats.revenue | {period: today\|7d\|30d} | {today_c,month_c,total_c,series:[{date,amount_c}]} |
| pile.summary | {} | {in_use, idle, faulty, offline, total} |
| pile.list | {sid?} | [{pid,code,sid,name,type,power_kw,status,chg_count,chg_minutes}] |
| pile.reboot | {pid} | {} （异步结果走 push） |
| station.list | {} | [{sid,name,addr,lat,lng,total,online_rate,pred_load_24h?}] |
| station.detail | {sid} | 同 user 端 station.detail |
| station.create | {name,addr,lat,lng,price_c,piles:[{code,type,power_kw}]} | {sid, pids} （**主数据来源**，见 §10） |
| user.list | {search?} | [{uid,phone,nickname,balance_c,reg_at,status}] |
| user.freeze | {uid,freeze:bool} | {} |
| order.list | {date?,status?} | 订单数组 |
| order.settle | {oid} | {} （代结算） |
| warning.list | {} | [{alid,kind,ref_id,msg,at,cleared_at}] （t_alert + ML 负荷预警，见 §5.5） |

- 推送：`push.pile.reboot` {pid,result}；ML 负荷预警经 t_alert 呈现，ops-app 轮询 warning.list（无独立推送通道）

## 5. simulator 消息

### 5.1 注册与心跳

| type | body | 响应 |
|---|---|---|
| sim.register | {stations:[{name,addr,lat,lng,price_c,piles:[{code,type,power_kw}]}]} | {accepted:[{code,pid}]} （**按 code 认领**主数据桩，未匹配的 code 拒绝并报告） |
| sim.heartbeat | {pids:[{pid, hw_ok}]} | {} （**仅硬件健康位 hw_ok**：true 正常 / false 故障；不报占用状态） |

### 5.2 充电控制与计量（服务端 → simulator 为 push 指令）

| type | 方向 | body | 说明 |
|---|---|---|---|
| push.pile.reserve | 服务端→桩 | {pid, session_id} | 桩锁定自检；失败回 pile.nak（服务端释放桩+订单转 Cancelled） |
| push.pile.start | 服务端→桩 | {pid, session_id, target_kwh?} | 起表计量；桩回 pile.ack |
| push.pile.stop | 服务端→桩 | {pid, session_id} | 停表，桩回 final_kwh（经 pile.ack） |
| push.pile.release | 服务端→桩 | {pid, session_id} | 会话终止后的资源释放（预约超时/取消路径） |
| push.pile.reboot | 服务端→桩 | {pid} | 离线 N 秒后重连 |
| sim.report | 桩→服务端 | {pid, session_id, seq, kwh_delta, state} | 每 5s；state ∈ charging\|done（done 附 final_kwh）；**seq 用于服务端幂等去重**（同 session 连续上报单调递增，重复包丢弃） |
| pile.ack / pile.nak | 桩→服务端 | {pid, session_id, reason?} | push 指令执行结果 |

### 5.3 会话与状态权威规则

- **状态权威在服务端**：桩的 空闲/预约锁/充电中/离线 由 biz-core 依订单状态机维护；simulator 心跳只带硬件健康位，从机制上杜绝心跳覆盖服务端状态
- **session_id**：reserve 时生成，贯穿 reserve/start/report/stop/release 全链路；simulator 只为有效 session 的桩计量，电表不动
- **指令确认与超时**：push 指令 5s 未收 ack/nak → 重发 1 次，再超时 → 桩转离线 + t_alert；nack → 对应订单走失败路径
- **计量可靠性**：sim.report 幂等去重（session 内 seq）；simulator 重连后对充电中 session 发 sim.resume {pid, session_id, est_kwh}，服务端校准（离线期兜底值以 resume 读数替换）
- 服务端兜底：simulator 离线期间按 功率×加速时长 续算

### 5.4 dashboard HTTP API

- `GET /api/snapshot` → 一个 JSON：{totals:{users,kwh_today,revenue_today,piles_online}, load_24h:[…], prediction:{h1,h6,h24}, station_rank:[…], revenue_mix:[…], alerts:[…]}
- `GET /api/predictions`（可选，历史预测对比）；前端 5s 轮询 /api/snapshot。无鉴权（内网演示）；部署为 biz-core 同源路径（见 §11 部署）

### 5.5 ML 数据接入（内部通道，唯一写者边界）

- **ML 进程不触业务库**（遵守 ADR-0001）。输出物为 JSON 文件：ml/out/predictions.json（含 forecast_at、model_version、预测数组）
- biz-core 暴露内部导入端点 `POST http://127.0.0.1:<port>/api/internal/ml/ingest`，请求头 `X-Ingest-Token`（与 ML 共享的本地 token，进配置文件不入库），body = 该 JSON
- biz-core 校验（sid 存在、horizon ∈ {1,6,24}、数值范围）后**整批替换**当前 run 的 t_ml_prediction 行，并写一条负荷预警入 t_alert（load 超阈值时）
- ML 触发：定时重跑（现实每 2 分钟）→ 写 JSON → 调 ingest → biz-core 入库。校验失败整批拒绝并记日志，不部分入库

## 6. 错误码

- 0 成功；1xxx 协议错：1001 非法 JSON、1002 未知 type、1003 未 hello、1004 版本不匹配
- 2xxx 业务错：2001 手机号非法、2002 桩非空闲、2003 余额不足、2004 存在活动订单、2005 用户被冻结、2006 权限/归属不符、2007 订单状态不允许该操作、2008 桩/站不存在、2009 重复请求
- 3xxx 服务端错：3001 数据库、3002 内部异常、3003 会话失效
- 断线重连：客户端指数退避（1s/2s/4s…上限 30s），重连后按 §2 会话恢复流程执行

## 7. 腾讯地图与一键导航（user-app 实现细则）

- **Key 配置**：config.ini `[map] key=…`（真实 key 不入 git，仓库放 config.example.ini）；geocoding 与导航共用同一 key
- **定位（软件模拟 GPS）**：默认坐标进配置（演示城市中心）；区域下拉 = 内置区域表（name→中心坐标）；手动输入地址 → 腾讯 WebService geocoder API（HTTPS GET，QNetworkAccessManager，JSON 解析 lat/lng），失败 → 提示并保留上次定位
- **距离与排序**：haversine 公式在客户端计算 dist_km，station.list 结果按 dist_km 升序展示（服务端已按请求坐标排好序，客户端重定位后可本地重排）；卡片字段：站名、price_c（显示 x.x 元/度）、free/total、dist_km
- **一键导航**：QWebEngineView 加载腾讯地图 URL 路线规划页：
  `https://apis.map.qq.com/uri/v1/routeplan?type=drive&from=…&to=…&referer=<key>`（type ∈ drive|walk，对应 UI 驾车/步行两按钮）；from=当前定位坐标，to=目标站坐标（坐标经过 URL 编码）
- **离线降级**：geocoder 请求失败/超时（3s）→ 用本地 mock 坐标表（区域表兜底）；导航页加载失败（loadFinished false 或超时）→ 显示静态路线示意页（起终点文字 + 距离估算），不白屏
- 演示无网场景：config.ini `[map] offline=1` 直接走 mock 坐标与静态示意，不外呼

## 8. 主数据规则（站/桩）

- **ops-app（管理员）是站与桩的主数据来源**：station.create 提交完整桩清单 [{code,type,power_kw}]，biz-core 建 sid/pid 并落库
- **simulator 是桩的"活性来源"**：虚拟站点表引用 code，注册时按 code 认领 pid；未被任何 simulator 认领的桩 = 永久离线（演示时全量认领）
- 电桩业务编号 code 全局唯一（如 K01/M02），管理员可自定义；pid 仍是内部主键

## 9. 取消与结算规则（消解状态机冲突）

- **Charging.cancel**：停止计量 → 计算 amount_c → 余额充足：扣款、订单 Settled（txn kind=2）；**余额不足：订单转 PendingSettle（不产生负余额），照常拦截新充电，可充值后本人结算或管理员代结算**——取消绝不产生负钱包
- **PendingSettle 是余额不足的唯一停留态**，退出路径仅两条：order.settle（本人，余额足）/ order.settle（管理员代结算，直接置已结算并记 settle_actor）
- 结算原子性：扣款 + 订单状态 + 流水在单事务内完成

## 10. 部署形态

- dashboard 静态文件由 biz-core 直接伺服（同源，无 CORS）：`GET /` → index.html，`GET /api/snapshot` → JSON；开发期可 python -m http.server 代理但验收形态是同源
- biz-core 配置文件 config.ini：[db] path、[net] tcp_port/http_port、[map] key/offline、[ml] ingest_token、[sim] time_factor
- 默认数据初始化：schema 建表脚本附 seed.sql（admin/123456 管理员 + 演示账号），首次启动空库时自动执行（config 开关）
