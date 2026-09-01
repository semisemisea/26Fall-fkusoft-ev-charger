# 电动汽车充电综合服务平台 API 设计

本文档是用户端、管理员端、Web 大屏及机器学习子系统与统一业务服务层之间的接口契约。

## 设计决定

- 所有业务请求经过统一业务服务层。客户端不直接访问 SQLite，也不直接携带地图、天气、短信或支付服务的密钥。
- 默认使用 SQLite 保存业务数据、会话、聚合指标和小型媒体文件元数据。当前作业数据量不足以要求 MySQL、InfluxDB、MinIO；接口层不依赖具体数据库，未来可替换为 PostgreSQL、时序库或对象存储。
- API 采用 JSON over HTTP。开发环境可用 HTTP，跨机器或发布时使用 HTTPS。
- 用户登录不接入短信验证码：输入手机号后，存在用户直接登录，不存在则创建用户。充值为模拟成功，不接入支付网关。
- 金额统一使用整数分（amountFen），避免浮点误差；功率、能量和距离使用小数。
- 时间使用 ISO 8601 UTC，例如 2026-09-01T08:30:00Z。服务端生成 createdAt、updatedAt 等审计时间。
- 当前不要求 WebSocket。订单和电桩状态采用短轮询，建议间隔 5 秒；后续可增加 SSE/WebSocket 而不改变资源模型。

### 调用方和权限

| 调用方 | 客户端 | 身份 | 主要能力 |
| --- | --- | --- | --- |
| 用户端 | user-app（Linux + Qt） | USER | 找站、查看电桩、预约、充电、结算、维护个人信息 |
| 管理端 | ops-app（Linux + Qt） | ADMIN | 销售看板、电站/电桩管理、远程重启、用户管理 |
| Web 大屏（预留） | 浏览器/ECharts | ADMIN_READONLY 或 ADMIN | 复用统计接口显示运营指标 |
| 机器学习（预留） | 定时任务或独立服务 | SERVICE | 读取脱敏数据、写入预测结果 |

## 基础约定

### 地址和请求头

开发环境示例：

```text
http://localhost:8080/api/v1
```

下文所有路径均相对于 /api/v1；例如 /stations/nearby 的完整开发地址为 `http://localhost:8080/api/v1/stations/nearby`。

部署时由 API_BASE_URL 配置实际地址。除登录和明确标记为公开的接口外，请求都带：

```http
Authorization: Bearer <accessToken>
Content-Type: application/json
X-Request-Id: <client-generated-uuid>
```

X-Request-Id 会原样出现在响应中；未提供时由服务端生成。创建订单、充值、远程指令建议额外带 Idempotency-Key；相同用户在 24 小时内重复提交同一 key 时返回第一次结果。

### 响应封装

成功响应统一为：

```json
{
  "data": {},
  "meta": {
    "requestId": "0c7d4f5e-7f6e-4d13-9a1c-c4b4b6725a8"
  }
}
```

列表响应在 meta 中增加 page、pageSize、total、hasNext。分页参数 page 从 1 开始，pageSize 默认 20、最大 100。除非接口另有说明，默认按 createdAt 倒序。

### 错误响应和状态码

```json
{
  "error": {
    "code": "CHARGER_UNAVAILABLE",
    "message": "电桩当前不可用",
    "details": { "chargerId": 17, "currentStatus": "charging" }
  },
  "meta": { "requestId": "..." }
}
```

客户端根据 code 展示提示，不依赖 message 的具体文字。

| HTTP | 错误码 | 说明 |
| --- | --- | --- |
| 400 | VALIDATION_ERROR | 缺字段、格式或参数超出范围 |
| 401 | UNAUTHORIZED | 未登录、令牌不存在或已过期 |
| 403 | FORBIDDEN、USER_FROZEN | 无权限或用户被冻结 |
| 404 | NOT_FOUND | 资源不存在或不属于当前用户 |
| 409 | ACTIVE_ORDER_EXISTS、CHARGER_UNAVAILABLE、INVALID_STATE_TRANSITION | 业务状态冲突 |
| 422 | INSUFFICIENT_BALANCE、GEOCODE_FAILED、PROVIDER_ERROR | 请求正确但业务无法完成 |
| 429 | RATE_LIMITED | 请求过于频繁 |
| 503 | SERVICE_UNAVAILABLE | 外部服务或业务服务暂时不可用 |
| 500 | INTERNAL_ERROR | 未预期的服务端错误 |

## 认证和会话

令牌采用服务端生成的随机不透明字符串，服务端只在 SQLite 保存令牌哈希、身份、过期时间和撤销时间。默认有效期 7 天，退出登录立即撤销。这样无需引入 JWT，同时保留后续替换实现的空间。

### 用户免密登录

POST /auth/user/login

```json
{ "phone": "13800138000" }
```

服务端校验手机号为 11 位数字（不发送验证码）。用户存在且状态为 active 时直接登录；不存在时创建用户，默认昵称为 用户 加手机号后四位，初始余额为 0 分；frozen 用户返回 403 USER_FROZEN。

响应 200：

```json
{
  "data": {
    "accessToken": "opaque-token",
    "tokenType": "Bearer",
    "expiresIn": 604800,
    "user": {
      "id": 12,
      "phone": "13800138000",
      "nickname": "用户8000",
      "avatarUrl": null,
      "walletBalanceFen": 0,
      "status": "active",
      "createdAt": "2026-09-01T08:30:00Z"
    }
  },
  "meta": { "requestId": "..." }
}
```

### 管理员登录

POST /auth/admin/login

```json
{ "username": "admin", "password": "123456" }
```

初始管理员为 admin / 123456，仅用于演示。密码仍应以哈希形式保存，禁止出现在响应或日志。账号禁用时返回 403 FORBIDDEN。登录响应中的 user 替换为：

```json
{
  "id": 1,
  "username": "admin",
  "displayName": "系统管理员",
  "role": "ADMIN",
  "status": "active"
}
```

### 当前身份和退出

| 方法 | 路径 | 权限 | 说明 |
| --- | --- | --- | --- |
| GET | /auth/me | USER/ADMIN | 返回当前令牌对应的身份 |
| POST | /auth/logout | USER/ADMIN | 撤销当前令牌，成功返回 204 |

## 资源和业务接口

### 状态枚举

| 资源 | 状态 |
| --- | --- |
| 用户 user.status | active、frozen |
| 电站 station.status | active、inactive |
| 电桩 charger.status | available（闲置）、reserved（预约）、charging（在用）、fault（故障）、offline（离线） |
| 预约 reservation.status | active、used、cancelled、expired |
| 订单 order.status | charging、awaiting_payment、settled、cancelled、failed |
| 钱包流水 walletTransaction.type | top_up、charge_debit、refund、adjustment |
| 指令 chargerCommand.status | accepted、succeeded、failed |

订单状态转移：

```text
预约 active --开始充电--> 订单 charging --停止充电--> awaiting_payment --结算--> settled
预约 active --取消/超时--> cancelled/expired
```

所有状态转移和余额变更在同一个数据库事务内完成，并通过条件更新防止重复提交造成重复扣款。

### 位置、电站和电桩（用户端）

#### 地址转坐标

```
GET /locations/geocode?address={urlEncodedAddress}&region={urlEncodedRegion}
```

无需用户令牌。服务端调用地图适配器并统一返回：

```json
{
  "data": {
    "address": "辽宁省大连市甘井子区软件园",
    "latitude": 38.889,
    "longitude": 121.537,
    "formattedAddress": "辽宁省大连市甘井子区软件园",
    "provider": "tencent"
  },
  "meta": { "requestId": "..." }
}
```

无匹配返回 `422 GEOCODE_FAILED`。

#### 附近电站

```
GET /stations/nearby
```

| 参数 | 必填 | 说明 |
| --- | --- | --- |
| latitude、longitude | 与 address 二选一 | 坐标优先，范围分别为 -90..90、-180..180 |
| address、region | 与坐标二选一 | 服务端先调用地理编码 |
| radiusKm | 否 | 半径默认 5，最大 50 |
| status | 否 | 默认只返回 active 电站 |
| sort | 否 | distance（默认）或 price |
| page、pageSize | 否 | 分页 |

返回电站摘要：

```json
{
  "data": [
    {
      "id": 3,
      "name": "软件园充电站",
      "address": "大连市甘井子区软件园",
      "latitude": 38.889,
      "longitude": 121.537,
      "pricePerKwhFen": 98,
      "chargerCount": 20,
      "availableChargerCount": 8,
      "onlineRate": 1.0,
      "distanceKm": 1.24,
      "status": "active"
    }
  ],
  "meta": { "requestId": "...", "page": 1, "pageSize": 20, "total": 1, "hasNext": false }
}
```

距离使用服务端球面距离计算；小数据集可先按经纬度粗筛，再在应用层排序，无需 SQLite GIS 扩展。

#### 电站详情和电桩列表

| 方法 | 路径 | 权限 | 说明 |
| --- | --- | --- | --- |
| GET | /stations/{stationId} | 公开 | 电站详情、价格、数量和位置 |
| GET | /stations/{stationId}/chargers | 公开 | 站内电桩，可按 status、type 筛选 |
| GET | /chargers/{chargerId} | USER/ADMIN | 单个电桩最新状态和累计统计 |

电桩对象：

```json
{
  "id": 17,
  "stationId": 3,
  "code": "S03-017",
  "type": "fast",
  "powerKw": 120.0,
  "status": "available",
  "totalChargeCount": 231,
  "totalChargeMinutes": 18420,
  "updatedAt": "2026-09-01T08:30:00Z"
}
```

type 为 fast（快充）或 slow（慢充）。客户端使用服务端的 status，不自行推断可用数量。

#### 路线规划

```
GET /locations/routes?fromLatitude=...&fromLongitude=...&toLatitude=...&toLongitude=...&mode=driving
```

mode 为 driving 或 walking。服务端调用地图适配器并返回：

```json
{
  "data": {
    "mode": "driving",
    "distanceM": 4300,
    "durationSec": 780,
    "polyline": [],
    "provider": "tencent",
    "mapUrl": "https://map.qq.com/..."
  },
  "meta": { "requestId": "..." }
}
```

用户端可用 QWebEngineView 打开 mapUrl，也可以用 polyline 自行绘制。地图密钥由服务端配置，不写入 Qt 客户端。

### 用户资料和钱包

| 方法 | 路径 | 权限 | 说明 |
| --- | --- | --- | --- |
| GET | /me | USER | 当前用户资料和余额摘要 |
| PATCH | /me | USER | 修改 nickname；手机号不可修改 |
| POST | /me/avatar | USER | multipart/form-data 上传头像，返回 avatarUrl |
| GET | /me/wallet | USER | 余额和最近流水摘要 |
| POST | /me/wallet/topups | USER | 模拟充值，立即成功 |
| GET | /me/wallet/transactions | USER | 钱包流水分页查询 |

PATCH /me 请求：

```json
{ "nickname": "小明" }
```

昵称限制建议为 1 至 30 个字符。头像限制建议为 5 MB；服务端生成文件名，SQLite 只保存相对 URL。未来接入 MinIO 时只替换媒体存储适配器。

POST /me/wallet/topups 请求和响应：

```json
// request
{ "amountFen": 5000, "note": "模拟充值" }

// response 201
{
  "data": {
    "id": 44,
    "type": "top_up",
    "amountFen": 5000,
    "balanceAfterFen": 12500,
    "status": "succeeded",
    "createdAt": "2026-09-01T08:31:00Z"
  },
  "meta": { "requestId": "..." }
}
```

金额必须为正整数；演示环境可限制为 100..10000000 分。相同 Idempotency-Key 重试不得再次增加余额。

### 预约、充电和结算

#### 查询未完成订单

```
GET /me/active-order
```

没有未完成订单时仍返回 200，data 为 null；有订单时返回 charging 或 awaiting_payment 订单。用户端进入充电页前调用此接口，非空时跳转结算页。

#### 创建预约

```
POST /reservations
```

```json
{
  "chargerId": 17,
  "startAt": "2026-09-01T09:00:00Z",
  "holdMinutes": 15
}
```

startAt 可省略，默认立即生效；holdMinutes 默认 15，最大 60。服务端检查电桩为 available、用户无其它未完成订单或有效预约后，以事务方式把电桩置为 reserved。

响应 201：

```json
{
  "id": 81,
  "chargerId": 17,
  "stationId": 3,
  "userId": 12,
  "status": "active",
  "startAt": "2026-09-01T09:00:00Z",
  "expiresAt": "2026-09-01T09:15:00Z",
  "createdAt": "2026-09-01T08:32:00Z"
}
```

其它预约接口：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | /reservations | 当前用户预约列表，可按 status 筛选 |
| GET | /reservations/{reservationId} | 预约详情 |
| POST | /reservations/{reservationId}/cancel | 取消 active 预约并释放电桩 |

#### 开始充电（创建订单）

POST /orders

```json
{ "chargerId": 17, "reservationId": 81 }
```

有预约时必须传该预约；也允许不传 `reservationId` 直接使用 available 电桩。服务端检查用户没有 charging 或 awaiting_payment 订单，且电桩状态和预约归属正确；成功后创建订单，电桩变为 charging，预约变为 used。

响应 201：

```json
{
  "id": 203,
  "orderNo": "20260901000123",
  "userId": 12,
  "stationId": 3,
  "chargerId": 17,
  "status": "charging",
  "startedAt": "2026-09-01T08:35:00Z",
  "energyKwh": 0.0,
  "durationMinutes": 0,
  "unitPriceFenPerKwh": 98,
  "amountFen": 0,
  "updatedAt": "2026-09-01T08:35:00Z"
}
```

冲突时分别返回 `409 ACTIVE_ORDER_EXISTS` 或 `409 CHARGER_UNAVAILABLE`。

#### 订单查询、停止和结算

| 方法 | 路径 | 权限 | 说明 |
| --- | --- | --- | --- |
| GET | /orders | USER | 当前用户订单分页，可按 status、时间范围筛选 |
| GET | /orders/{orderId} | USER/ADMIN | 订单详情；USER 只能查看自己的订单 |
| POST | /orders/{orderId}/stop | USER | 停止 charging 订单并生成账单 |
| POST | /orders/{orderId}/settle | USER | 钱包扣款并完成结算 |
| POST | /orders/{orderId}/cancel | USER | 仅允许取消尚未开始的订单 |

停止请求可带模拟数据；不传时由服务端按演示规则生成：

```json
{ "energyKwh": 21.5, "durationMinutes": 32 }
```

stop 将订单置为 awaiting_payment，释放电桩为 available，并计算：

```text
amountFen = round(energyKwh * unitPriceFenPerKwh)
```

响应中的 amountFen、energyKwh 和 durationMinutes 是最终账单值，客户端不能覆盖。结算请求：

```json
{ "paymentMethod": "wallet" }
```

成功后在同一事务中写入 charge_debit 流水、扣减余额并把订单置为 settled，返回 `200`。余额不足返回 `422 INSUFFICIENT_BALANCE`，订单保持 awaiting_payment，用户充值后可再次结算。重复结算返回原订单结果，不重复扣款。

### 管理端看板

以下接口均需要 ADMIN 令牌。

| 方法 | 路径 | 查询参数 | 说明 |
| --- | --- | --- | --- |
| GET | /admin/dashboard/summary | asOf 可选 | 今日、本月、累计营收及站点/电桩/用户数量 |
| GET | /admin/dashboard/revenue-series | range=7d 或 30d；也可 from、to、bucket=day | ECharts 折线图数据 |
| GET | /admin/dashboard/charger-status | stationId 可选 | 各电桩状态数量和占比 |
| GET | /admin/orders | stationId、status、from、to、分页 | 运营订单查询 |

看板摘要示例：

```json
{
  "data": {
    "asOf": "2026-09-01T08:40:00Z",
    "todayRevenueFen": 128600,
    "monthRevenueFen": 2987600,
    "totalRevenueFen": 48652000,
    "userCount": 1234,
    "stationCount": 18,
    "chargerCount": 260,
    "onlineRate": 0.96
  },
  "meta": { "requestId": "..." }
}
```

趋势接口返回：

```json
{
  "data": {
    "range": "7d",
    "unit": "day",
    "points": [
      { "bucketStart": "2026-08-26T00:00:00Z", "revenueFen": 345000, "orderCount": 83 }
    ]
  },
  "meta": { "requestId": "..." }
}
```

### 管理端电桩和电站

#### 电桩

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | /admin/chargers | 按 stationId、status、type 筛选并分页 |
| GET | /admin/chargers/{chargerId} | 详情、累计次数和时长 |
| PATCH | /admin/chargers/{chargerId} | 修改类型、功率、故障/离线等运维字段 |
| POST | /admin/chargers/{chargerId}/commands | 下发模拟指令 |
| GET | /admin/charger-commands/{commandId} | 查询指令结果 |

远程重启请求：

```json
{ "type": "restart", "reason": "处理演示环境死机" }
```

服务端创建指令记录并模拟执行，通常在同一响应中返回 succeeded；保留 accepted 状态以兼容未来真实设备通信。电桩正 charging 时返回 409 INVALID_STATE_TRANSITION，除非后续增加强制参数。

#### 电站

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | /admin/stations | 分页、按名称或状态搜索 |
| POST | /admin/stations | 新增电站及初始电桩 |
| GET | /admin/stations/{stationId} | 详情和实时摘要 |
| PATCH | /admin/stations/{stationId} | 编辑名称、地址、经纬度、价格、状态 |
| GET | /admin/stations/{stationId}/chargers | 查看站内所有电桩 |

新增请求建议显式传入电桩清单：

```json
{
  "name": "软件园充电站",
  "address": "大连市甘井子区软件园",
  "latitude": 38.889,
  "longitude": 121.537,
  "pricePerKwhFen": 98,
  "chargers": [
    { "code": "S03-001", "type": "fast", "powerKw": 120 },
    { "code": "S03-002", "type": "slow", "powerKw": 7 }
  ]
}
```

chargers 可以为空；若界面只收集数量，可由客户端生成清单，或由服务端根据站点 ID 在同一事务内生成唯一编号。站点不提供硬删除接口，使用 PATCH status=inactive，避免历史订单失去归属。

### 管理端用户

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | /admin/users | 按 phone 模糊搜索，按 status 筛选并分页 |
| GET | /admin/users/{userId} | 资料、余额摘要、最近订单 |
| PATCH | /admin/users/{userId} | 仅允许修改 status 为 active 或 frozen |
| GET | /admin/users/{userId}/wallet/transactions | 查看钱包流水 |

冻结用户后不能登录、充值、预约、开始充电或结算；已在充电订单不强制中断。解冻只需再次 PATCH 为 active。

## 外部 API 适配

外部服务全部由后端适配器调用，业务接口只依赖规范化结果。适配器负责供应商 URL、鉴权参数、字段映射、超时、重试和错误转换；更换供应商不改变 Qt/Web API。

### 腾讯地图

| 能力 | 对内接口 | 适配器行为 |
| --- | --- | --- |
| 地址转坐标 | GET /locations/geocode | 调用腾讯地图 Geocoder，映射坐标和格式化地址 |
| 驾车/步行路线 | GET /locations/routes | 根据 mode 调用对应 Direction API，映射距离、时长、折线和地图链接 |

`TENCENT_MAP_KEY` 只存在服务端环境变量。建议超时 3 秒、最多重试 1 次；超时或供应商 `5xx` 返回 `503 SERVICE_UNAVAILABLE`，无结果返回 `422 GEOCODE_FAILED`。

## 机器学习和 Web 大屏预留接口

Web 大屏优先复用 `/admin/dashboard/*`；如需只读账号，可使用 ADMIN_READONLY 角色，只允许 GET 看板和分析资源。

机器学习服务使用独立服务令牌访问：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | /internal/analytics/load-samples | 按站点、电桩、时间范围和 bucket=1h/6h/1d 导出充电量、时长、并发数 |
| GET | /internal/analytics/device-events | 导出状态变化和故障事件 |
| POST | /internal/forecasts | 写入未来 1/6/24 小时预测 |
| GET | /forecasts?stationId=...&horizon=24h | 用户端或看板读取最新预测（可选） |

预测写入示例：

```json
{
  "modelVersion": "baseline-v1",
  "generatedAt": "2026-09-01T08:00:00Z",
  "points": [
    {
      "stationId": 3,
      "forecastAt": "2026-09-01T09:00:00Z",
      "loadKw": 420.0,
      "availableChargerCount": 6,
      "confidence": 0.81
    }
  ]
}
```

没有预测数据时，用户端仍按实时 `availableChargerCount` 和距离排序，不能因为 ML 服务离线而无法找站或充电。

## 客户端典型流程

### 用户充电

```text
POST /auth/user/login
  -> GET /stations/nearby
  -> GET /stations/{id}/chargers
  -> POST /reservations
  -> GET /me/active-order（进入充电页时检查）
  -> POST /orders
  -> GET /orders/{id}（轮询状态）
  -> POST /orders/{id}/stop
  -> POST /orders/{id}/settle
```

`401` 时清除本地令牌并回到登录页；`409 ACTIVE_ORDER_EXISTS` 时打开已有订单；`422 INSUFFICIENT_BALANCE` 时打开充值页。

### 管理员新增电站并监控

```text
POST /auth/admin/login
  -> POST /admin/stations
  -> GET /admin/dashboard/summary
  -> GET /admin/dashboard/revenue-series?range=7d
  -> GET /admin/dashboard/charger-status
  -> GET /admin/chargers?status=fault
  -> POST /admin/chargers/{id}/commands {"type":"restart"}
```

## 版本和兼容性

- 当前版本为 v1；不兼容变更时整体升级为 `/api/v2`。
- 新增字段默认向后兼容；客户端应忽略未知字段。
- 枚举新增值时，客户端应显示通用“未知状态”而不是崩溃。
- 删除或重命名字段先标记弃用，并在变更日志中说明迁移方式。
- 示例 ID、金额和时间均为演示数据，不代表真实站点或支付记录。
