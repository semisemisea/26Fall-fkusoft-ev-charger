# 协议 v1 草案（biz-core 对外契约）

状态：DRAFT，评审后冻结为 v1（目标 9/3 前）。冻结后改动需走 PR 并同步 common/。

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

## 2. 连接与身份

- 连接后第一条消息必须是 `hello`，携带 `client` ∈ `user-app | ops-app | simulator` 及登录凭证（见各端消息）；biz-core 回 `hello_ack`（附 server 时间与加速因子）。未 hello 前其他消息一律 1003 拒绝。
- 心跳：客户端每 30s 发 `heartbeat`，服务端 90s 未收到判离线（simulator 超时 → 其名下桩转离线并产生告警）。

## 3. user-app 消息

| type | body 请求 | 响应 body |
|---|---|---|
| user.login | {phone} | {uid, nickname, avatar, balance, new_user} |
| station.list | {lat, lng} 或 {region} | [{sid,name,addr,lat,lng,price,free,total,dist_km}] |
| station.detail | {sid} | {station…, piles:[{pid,type,status,power}]} |
| pile.reserve | {pid} | {oid, reserve_expire} |
| charging.start | {oid} | {oid, started_at} |
| charging.status | {oid} | {oid, status, kwh, amount, elapsed} |
| charging.cancel | {oid} | {amount_deducted} |
| order.settle | {oid} | {balance} |
| wallet.recharge | {amount} | {balance} |
| user.profile.update | {nickname?}{avatar_b64?} | {uid,…} |
| order.list | {status?} | [{oid,sid,name,pid,start,end,kwh,amount,status}] |

- 登录前置检查：用户被冻结 → 2005；存在待结算订单 → 2004 且 body 带 {pending_oid}（客户端引导结算）。
- 服务端主动推送（user-app 收）：`push.order.state` {oid,status,kwh,amount}；`push.pile.status` {pid,status}。

## 4. ops-app 消息

| type | body 请求 | 响应 body |
|---|---|---|
| admin.login | {username,password} | {aid,name} |
| stats.revenue | {period: today\|7d\|30d} | {today,month,total,series:[{date,amount}]} |
| pile.summary | {} | {in_use, idle, faulty, offline, total} |
| pile.list | {sid?} | [{pid,sid,name,type,power,status,chg_count,chg_minutes}] |
| pile.reboot | {pid} | {} （异步结果走 push） |
| station.list | {} | [{sid,name,addr,lat,lng,total,online_rate}] |
| station.detail | {sid} | 同 user 端 station.detail |
| station.create | {name,addr,lat,lng,pile_count,price} | {sid} |
| user.list | {search?} | [{uid,phone,nickname,balance,reg_at,status}] |
| user.freeze | {uid,freeze:bool} | {} |
| order.list | {date?,status?} | 订单数组 |
| order.settle | {oid} | {} （代结算） |

- 推送：`push.pile.reboot` {pid,result}。

## 5. simulator 消息

| type | body | 响应 |
|---|---|---|
| sim.register | {stations:[{name,addr,lat,lng,price,piles:[{pid,type,power}]}]} | {accepted_pids} |
| sim.heartbeat | {pids:[{pid,status}]} | {} |
| sim.report | {pid, kwh_delta, session_id?} | {} （充电中电量增量） |
| push.pile.reboot 收 | {pid} | 客户端应离线 N 秒后回上线心跳 |

## 6. dashboard HTTP API

- `GET /api/snapshot` → 一个 JSON：{totals:{users,kwh_today,revenue_today,piles_online}, load_24h:[…], prediction:{h1,h6,h24}, station_rank:[…], revenue_mix:[…], alerts:[…]}
- `GET /api/predictions`（可选，历史预测对比）；前端 5s 轮询 /api/snapshot。无鉴权（内网演示）。

## 7. 错误码

- 0 成功；1xxx 协议错：1001 非法 JSON、1002 未知 type、1003 未 hello、1004 版本不匹配
- 2xxx 业务错：2001 手机号非法、2002 桩非空闲、2003 余额不足、2004 存在待结算订单、2005 用户被冻结、2006 权限不足、2007 订单状态不允许该操作、2008 桩/站不存在
- 3xxx 服务端错：3001 数据库、3002 内部异常、3003 会话失效
- 断线重连：客户端指数退避（1s/2s/4s…上限 30s），重连后重新 hello + 同步本地状态（user-app 拉 order.list 恢复充电页）。

## 8. 未决点（评审时定）

1. 头像上传：base64 进 JSON（≤200KB 限制）够用，不建议开文件通道
2. charging.status 是轮询还是纯推送：建议轮询 2s + 状态变化推送兜底
3. seq 是否需要服务端去重回放：演示项目建议不去重（写明即可）
