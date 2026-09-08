# 电动汽车充电综合服务平台 API 契约

本文档定义 Qt 用户端、Qt 管理端与后端之间的 HTTP 接口。除健康检查外，接口路径统一以 `/api/v1` 开头。

## 1. 通用约定

### 1.1 服务地址

默认地址为：

```text
http://127.0.0.1:8080/api/v1
```

监听地址和端口可配置。后端只提供 HTTP。

### 1.2 请求头

需要认证的接口使用：

```http
Authorization: Bearer <accessToken>
```

JSON 请求使用：

```http
Content-Type: application/json
```

`X-Request-Id` 可选。提供时必须是 UUID；省略时由后端生成。每个响应均在 `X-Request-Id` 响应头中返回该值；JSON 响应同时通过 `meta.requestId` 返回。

充值、创建预约、开始充电、停止充电和结算请求必须额外提供：

```http
Idempotency-Key: <opaque-client-key>
```

幂等作用域为认证主体、HTTP 方法、请求路径和 key。默认保留 24 小时，保留期可配置。同一作用域和相同请求体的重试返回首次 HTTP 状态及业务数据；`meta.requestId` 使用当前请求 ID。相同作用域的 key 用于不同请求体时返回 `409 IDEMPOTENCY_KEY_REUSED`。

### 1.3 时间、金额和数值

- 时间采用 RFC 3339。响应时间统一为 UTC `Z` 格式。
- 时间区间为 `[from, to)`，必须满足 `from < to`。
- 金额字段以 `Fen` 结尾，使用整数分。
- 单价字段 `priceFenPerKwh` 使用整数分/kWh。
- 功率接口单位为 kW，最多三位小数。
- 电量接口单位为 kWh，四舍五入到小数点后两位。
- 距离接口单位为 km，四舍五入到小数点后两位。

### 1.4 成功响应

JSON 成功响应统一为：

```json
{
  "data": {},
  "meta": {
    "requestId": "0c7d4f5e-7f6e-4d13-9a1c-c4b4b6725a8"
  }
}
```

列表响应的 `meta` 额外包含：

```json
{
  "page": 1,
  "pageSize": 20,
  "total": 123,
  "hasNext": true
}
```

`page` 默认 1，`pageSize` 默认 20、最大 100。超出最后一页时返回空数组。204 响应没有正文。

### 1.5 错误响应

```json
{
  "error": {
    "code": "CHARGER_UNAVAILABLE",
    "message": "电桩当前不可用",
    "details": {
      "chargerId": 17
    }
  },
  "meta": {
    "requestId": "0c7d4f5e-7f6e-4d13-9a1c-c4b4b6725a8"
  }
}
```

| HTTP 状态 | 错误码 | 条件 |
| --- | --- | --- |
| 400 | `VALIDATION_ERROR` | 缺少字段、类型错误、范围错误、非法枚举或时间区间错误 |
| 401 | `UNAUTHORIZED` | 令牌缺少、无效、过期或已撤销 |
| 401 | `INVALID_CREDENTIALS` | 管理员账号或密码错误 |
| 403 | `FORBIDDEN` | 身份有效但权限不足 |
| 403 | `USER_FROZEN` | 冻结用户执行受限操作或无未完成订单时登录 |
| 404 | `NOT_FOUND` | 资源不存在、不可见或不属于当前用户 |
| 413 | `PAYLOAD_TOO_LARGE` | 请求正文超过配置上限 |
| 409 | `ACTIVE_ORDER_EXISTS` | 用户已有正在充电或待结算订单 |
| 409 | `CHARGER_UNAVAILABLE` | 电站或电桩不能用于当前操作 |
| 409 | `INVALID_STATE_TRANSITION` | 其他非法状态转移 |
| 409 | `IDEMPOTENCY_KEY_REUSED` | 幂等键被用于不同请求体 |
| 422 | `INSUFFICIENT_BALANCE` | 钱包余额不足 |
| 422 | `BALANCE_LIMIT_EXCEEDED` | 充值后将超过钱包余额上限 |
| 422 | `GEOCODE_FAILED` | 地址解析无结果 |
| 422 | `ROUTE_NOT_FOUND` | 路线规划无结果 |
| 502 | `PROVIDER_ERROR` | 地图供应商鉴权失败或响应异常 |
| 503 | `SERVICE_UNAVAILABLE` | 地图服务暂不可用或 SQLite busy timeout |
| 500 | `INTERNAL_ERROR` | 未预期的服务端错误或计费时钟异常 |

`VALIDATION_ERROR` 的 `details` 按字段给出原因。JSON 对象中的未知字段被忽略。未知路径返回 404，路径存在但方法不支持返回 405，JSON 接口媒体类型错误返回 415。

## 2. 状态和资源对象

### 2.1 状态枚举

| 字段 | 枚举值 |
| --- | --- |
| `user.status` | `active`、`frozen` |
| `admin.status` | `active`、`disabled` |
| `station.status` | `active`、`inactive` |
| `charger.occupancyStatus` | `available`、`reserved`、`charging` |
| `charger.operationalStatus` | `online`、`fault`、`offline` |
| `reservation.status` | `active`、`used`、`cancelled`、`expired` |
| `order.status` | `charging`、`awaiting_payment`、`settled` |
| `walletTransaction.type` | `top_up`、`charge_debit` |

### 2.2 电站对象

```json
{
  "id": 3,
  "name": "软件园充电站",
  "latitude": 38.889,
  "longitude": 121.537,
  "priceFenPerKwh": 98,
  "status": "active",
  "chargerCount": 20,
  "availableChargerCount": 8,
  "createdAt": "2026-09-01T08:00:00Z",
  "updatedAt": "2026-09-01T08:30:00Z",
  "deletedAt": null
}
```

`availableChargerCount` 只统计未软删除且状态为 `available + online` 的电桩。

### 2.3 电桩对象

```json
{
  "id": 17,
  "stationId": 3,
  "type": "fast",
  "powerKw": 120.0,
  "occupancyStatus": "available",
  "operationalStatus": "online",
  "totalChargeCount": 231,
  "totalChargeSeconds": 1105200,
  "totalChargeMinutes": 18420,
  "createdAt": "2026-09-01T08:00:00Z",
  "updatedAt": "2026-09-01T08:30:00Z",
  "deletedAt": null
}
```

`totalChargeMinutes = floor(totalChargeSeconds / 60)`。

### 2.4 预约对象

```json
{
  "id": 81,
  "userId": 12,
  "stationId": 3,
  "chargerId": 17,
  "status": "active",
  "createdAt": "2026-09-01T08:32:00Z",
  "expiresAt": "2026-09-01T09:02:00Z"
}
```

### 2.5 订单对象

```json
{
  "id": 203,
  "userId": 12,
  "stationId": 3,
  "chargerId": 17,
  "reservationId": 81,
  "status": "charging",
  "powerKw": 120.0,
  "unitPriceFenPerKwh": 98,
  "startedAt": "2026-09-01T08:35:00Z",
  "stoppedAt": null,
  "settledAt": null,
  "durationSeconds": 32,
  "durationMinutes": 0,
  "energyKwh": 1.07,
  "amountFen": 105,
  "createdAt": "2026-09-01T08:35:00Z",
  "updatedAt": "2026-09-01T08:35:00Z"
}
```

`charging` 订单的时长、电量和 `amountFen` 按查询时刻动态计算。停止后这些字段固定。`durationMinutes = floor(durationSeconds / 60)`。
动态查询不修改订单的 `updatedAt`。

## 3. 健康检查

### GET /health

无需认证。服务完成启动且 SQLite 简单查询成功时返回 200：

```json
{
  "data": {
    "status": "ok"
  },
  "meta": {
    "requestId": "..."
  }
}
```

## 4. 认证

### 4.1 用户登录

#### POST /auth/user/login

无需认证。

```json
{
  "phone": "13800138000"
}
```

手机号必须为 11 位 ASCII 数字。不存在时创建用户，默认昵称为 `用户8000`、余额为 0。并发首次登录只创建一个用户。

正常响应为 200：

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
      "hasAvatar": false,
      "walletBalanceFen": 0,
      "status": "active",
      "createdAt": "2026-09-01T08:30:00Z"
    }
  },
  "meta": {
    "requestId": "..."
  }
}
```

冻结用户只有在存在 `charging` 或 `awaiting_payment` 订单时才能登录。此时签发受限 USER 令牌。

### 4.2 管理员登录

#### POST /auth/admin/login

无需认证。

```json
{
  "username": "admin",
  "password": "123456"
}
```

正常响应为 200：

```json
{
  "data": {
    "accessToken": "opaque-token",
    "tokenType": "Bearer",
    "expiresIn": 604800,
    "admin": {
      "id": 1,
      "username": "admin",
      "displayName": "系统管理员",
      "role": "ADMIN",
      "status": "active"
    }
  },
  "meta": {
    "requestId": "..."
  }
}
```

默认管理员仅在首次初始化时创建。该接口也接受数据库中已有的 `ADMIN_READONLY` 账号。
`disabled` 管理员登录返回 `403 FORBIDDEN`。

### 4.3 当前身份

#### GET /auth/me

权限：`USER`、`ADMIN`、`ADMIN_READONLY`、`SERVICE`。

人员身份返回主体 ID、类型、角色和状态。SERVICE 返回：

```json
{
  "data": {
    "principalType": "service",
    "role": "SERVICE",
    "serviceName": "ml"
  },
  "meta": {
    "requestId": "..."
  }
}
```

### 4.4 退出登录

#### POST /auth/logout

权限：`USER`、`ADMIN`、`ADMIN_READONLY`。撤销当前令牌，返回 204。SERVICE 不支持该接口。

## 5. 位置、电站和电桩查询

本章接口无需认证，但 `GET /chargers/{chargerId}` 除外。

### 5.1 地址解析

#### GET /locations/geocode

查询参数：

| 参数 | 必填 | 规则 |
| --- | --- | --- |
| `address` | 是 | 去除首尾空白后 1 至 200 个字符 |
| `region` | 否 | 去除首尾空白后 1 至 100 个字符 |

响应：

```json
{
  "data": {
    "address": "辽宁省大连市甘井子区软件园",
    "latitude": 38.889,
    "longitude": 121.537,
    "formattedAddress": "辽宁省大连市甘井子区软件园",
    "provider": "tencent"
  },
  "meta": {
    "requestId": "..."
  }
}
```

### 5.2 附近电站

#### GET /stations/nearby

查询参数：

| 参数 | 必填 | 规则 |
| --- | --- | --- |
| `latitude`、`longitude` | 条件必填 | 两者同时提供；分别位于 `[-90,90]`、`[-180,180]` |
| `address` | 条件必填 | 与经纬度二选一；规则同地址解析 |
| `region` | 否 | 仅与地址一起使用 |
| `radiusKm` | 是 | 大于 0，不超过配置的最大半径 |
| `sort` | 否 | `distance` 或 `price`，默认 `distance` |
| `page`、`pageSize` | 否 | 通用分页规则 |

地址和经纬度同时提供时返回 `VALIDATION_ERROR`。只返回 active 且未软删除的半径内电站。每项在电站对象基础上增加 `distanceKm`。

`distance` 排序为未舍入距离升序、电站 ID 升序。`price` 排序为单价升序、未舍入距离升序、电站 ID 升序。

### 5.3 电站详情和站内电桩

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/stations/{stationId}` | active、未软删除电站详情 |
| GET | `/stations/{stationId}/chargers` | 站内未软删除电桩列表 |

站内电桩列表支持 `type`、`occupancyStatus` 和 `operationalStatus` 筛选，并使用通用分页规则。inactive 或软删除电站对公开接口表现为 `NOT_FOUND`。

### 5.4 单个电桩

#### GET /chargers/{chargerId}

权限：`USER`、`ADMIN`、`ADMIN_READONLY`。返回未软删除电桩对象。

### 5.5 路线规划

#### GET /locations/routes

查询参数：

| 参数 | 必填 | 规则 |
| --- | --- | --- |
| `fromLatitude`、`fromLongitude` | 是 | 起点合法经纬度 |
| `toLatitude`、`toLongitude` | 是 | 终点合法经纬度 |
| `mode` | 是 | `driving` 或 `walking` |

响应：

```json
{
  "data": {
    "mode": "driving",
    "distanceM": 4300,
    "durationSec": 780,
    "polyline": [
      [38.889, 121.537],
      [38.901, 121.55]
    ],
    "provider": "tencent",
    "mapUrl": "https://map.qq.com/..."
  },
  "meta": {
    "requestId": "..."
  }
}
```

腾讯地图密钥缺失时返回 `503 SERVICE_UNAVAILABLE`。连接失败、超时和 HTTP 5xx 按配置次数重试；参数错误、鉴权失败和无结果不重试。错误结果使用 1.5 节定义的地图错误码。

## 6. 用户资料与钱包

本章读接口允许 active 或 frozen USER。修改资料、上传头像和删除头像要求 active USER；充值允许 active 或 frozen USER。

### 6.1 当前用户资料

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/me` | 返回手机号、昵称、`hasAvatar`、余额、状态和注册时间 |
| PATCH | `/me` | 修改昵称 |

修改昵称请求：

```json
{
  "nickname": "小明"
}
```

昵称去除首尾空白后为 1 至 30 个 Unicode 字符，允许重名。

### 6.2 头像

#### POST /me/avatar

权限：active USER。使用 `multipart/form-data`，只接受一个名为 `file` 的文件部分。该部分声明的 MIME 必须为 `image/jpeg` 或 `image/png`，大小不得超过配置上限，默认 5 MiB。后端不解码文件内容。成功返回：

```json
{
  "data": {
    "hasAvatar": true,
    "mimeType": "image/png"
  },
  "meta": {
    "requestId": "..."
  }
}
```

#### GET /me/avatar

权限：USER。存在头像时，以保存的 MIME 作为 `Content-Type` 返回原始字节，并在 `X-Request-Id` 响应头返回请求 ID。不存在时返回 JSON 错误 `404 NOT_FOUND`。

#### DELETE /me/avatar

权限：active USER。删除头像，返回 204；没有头像时同样返回 204。

### 6.3 钱包

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/me/wallet` | 返回当前余额 |
| GET | `/me/wallet/transactions` | 返回当前用户钱包流水 |

钱包流水默认按创建时间降序、ID 降序分页。

#### POST /me/wallet/topups

权限：USER。要求 `Idempotency-Key`。

```json
{
  "amountFen": 5000
}
```

金额必须处于配置的单次充值范围内，充值后余额不得超过配置的余额上限。成功返回 201：

```json
{
  "data": {
    "id": 44,
    "type": "top_up",
    "amountFen": 5000,
    "balanceAfterFen": 12500,
    "createdAt": "2026-09-01T08:31:00Z"
  },
  "meta": {
    "requestId": "..."
  }
}
```

## 7. 预约

读取接口允许 active 或 frozen USER；写接口要求 active USER。

### 7.1 创建预约

#### POST /reservations

要求 `Idempotency-Key`。

```json
{
  "chargerId": 17,
  "holdMinutes": 30
}
```

`holdMinutes` 必填，为 1 至配置上限之间的整数。预约从服务端接收请求时开始。成功返回 201 和预约对象。

用户已有有效预约或未完成订单时返回状态冲突。电站或电桩不可用、故障、离线、停用或软删除时返回 `CHARGER_UNAVAILABLE`。

### 7.2 预约查询和取消

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/reservations` | 当前用户预约列表，可按 `status` 筛选 |
| GET | `/reservations/{reservationId}` | 当前用户预约详情 |
| POST | `/reservations/{reservationId}/cancel` | 取消当前用户的 active 预约 |

取消成功返回更新后的预约。重复取消、取消已使用或已过期预约返回 `INVALID_STATE_TRANSITION`。

所有涉及电站、电桩或预约的接口在返回或修改状态前回收已过期预约。

## 8. 订单、充电和结算

订单只包含 `charging`、`awaiting_payment` 和 `settled` 三种状态。

### 8.1 当前未完成订单

#### GET /me/active-order

权限：USER。返回当前用户唯一的 `charging` 或 `awaiting_payment` 订单；不存在时返回：

```json
{
  "data": null,
  "meta": {
    "requestId": "..."
  }
}
```

### 8.2 开始充电

#### POST /orders

权限：active USER。要求 `Idempotency-Key`。

```json
{
  "chargerId": 17,
  "reservationId": 81
}
```

`chargerId` 必填。`reservationId` 可选。用户有有效预约时必须提供该预约，且 charger ID 必须匹配。用户没有有效预约时可以直接使用 `available + online` 电桩。

成功返回 201 和 `charging` 订单。订单保存开始时的功率和单价快照。余额为 0 时也允许开始。

### 8.3 订单查询

| 方法 | 路径 | 权限 | 说明 |
| --- | --- | --- | --- |
| GET | `/orders` | USER | 当前用户订单分页列表 |
| GET | `/orders/{orderId}` | USER、ADMIN | 订单详情；USER 只能读取自己的订单 |

用户列表支持 `status`、`from` 和 `to`。`from/to` 按订单 `createdAt` 筛选，必须同时提供。

查询 `charging` 订单时，后端使用查询时刻动态计算：

```text
elapsedSeconds = floor(nowUtc - startedAt)
energyWs = powerW * elapsedSeconds
amountFen = roundHalfUp(powerW * elapsedSeconds * unitPriceFenPerKwh / 3,600,000)
```

`energyKwh` 由 `energyWs` 四舍五入到两位小数。查询不固化最终账单。

### 8.4 停止充电

#### POST /orders/{orderId}/stop

权限：订单所属 USER，包括 frozen USER。要求 `Idempotency-Key`。请求正文为空或 `{}`，不接受电量、时长或金额。

后端以接收请求时刻计算最终数据，把订单置为 `awaiting_payment`，记录 `stoppedAt`，释放电桩占用并增加电桩累计次数和累计秒数。成功返回更新后的订单。

已经停止的订单再次收到停止请求时返回现有订单，不重新计算或更新累计值。

### 8.5 结算

#### POST /orders/{orderId}/settle

权限：订单所属 USER，包括 frozen USER。要求 `Idempotency-Key`。

```json
{
  "paymentMethod": "wallet"
}
```

只接受 `wallet`。余额充足时原子扣款、写入 `charge_debit` 流水、记录 `settledAt` 并把订单置为 `settled`。成功返回订单及扣款后的余额：

```json
{
  "data": {
    "order": {},
    "balanceAfterFen": 10290
  },
  "meta": {
    "requestId": "..."
  }
}
```

`balanceAfterFen` 是该订单扣款流水记录的扣款后余额。0 分订单仍写入 0 分流水。已经结算的订单再次收到结算请求时返回现有订单和原扣款流水的 `balanceAfterFen`，不重复扣款。

## 9. 管理端统计和订单

### 9.1 营收

#### GET /admin/dashboard/revenue

权限：`ADMIN`、`ADMIN_READONLY`。

查询参数 `from` 和 `to` 必须同时提供或同时省略。省略时返回累计营收；提供时返回按 `settledAt` 计算的区间营收。可选 `stationId` 按电站筛选，包括已经软删除的电站。

```json
{
  "data": {
    "from": "2026-09-01T00:00:00Z",
    "to": "2026-09-02T00:00:00Z",
    "revenueFen": 128600,
    "orderCount": 83
  },
  "meta": {
    "requestId": "..."
  }
}
```

累计响应中的 `from` 和 `to` 为 `null`。

#### GET /admin/dashboard/revenue-series

权限：`ADMIN`、`ADMIN_READONLY`。

| 参数 | 必填 | 规则 |
| --- | --- | --- |
| `from` | 是 | 区间起点 |
| `to` | 是 | 区间终点 |
| `utcOffset` | 是 | 固定偏移，格式 `+HH:MM` 或 `-HH:MM`，范围 `-14:00` 至 `+14:00` |
| `stationId` | 否 | 按电站筛选，包括已经软删除的电站 |

按该固定偏移的当地自然日分桶。首尾桶只统计与 `[from,to)` 重叠的部分。区间长度不受限制，零营收日期也返回：

```json
{
  "data": {
    "from": "2026-08-26T00:00:00Z",
    "to": "2026-09-02T00:00:00Z",
    "utcOffset": "+08:00",
    "points": [
      {
        "localDate": "2026-08-26",
        "bucketStart": "2026-08-25T16:00:00Z",
        "bucketEnd": "2026-08-26T16:00:00Z",
        "revenueFen": 345000,
        "orderCount": 83
      }
    ]
  },
  "meta": {
    "requestId": "..."
  }
}
```

### 9.2 电桩状态计数

#### GET /admin/dashboard/charger-status

权限：`ADMIN`、`ADMIN_READONLY`。可选 `stationId`。只统计未软删除电桩。

```json
{
  "data": {
    "total": 20,
    "occupancy": {
      "available": 12,
      "reserved": 3,
      "charging": 5
    },
    "operational": {
      "online": 17,
      "fault": 2,
      "offline": 1
    }
  },
  "meta": {
    "requestId": "..."
  }
}
```

后端不返回百分比、在线率或合并展示状态。

### 9.3 管理员订单列表

#### GET /admin/orders

权限：`ADMIN`。支持 `status`、`userId`、`stationId`、`chargerId`、`from`、`to` 和通用分页参数。`from/to` 按 `createdAt` 筛选。`stationId` 和 `chargerId` 可以引用软删除资源。

## 10. 管理端电站

查询权限为 `ADMIN`、`ADMIN_READONLY`，写权限为 `ADMIN`。

### 10.1 电站列表

#### GET /admin/stations

支持以下参数：

- `name`：站名子串；
- `status`：`active` 或 `inactive`；
- `includeDeleted`：默认 `false`；
- `page`、`pageSize`。

结果默认按电站 ID 升序。

### 10.2 创建电站

#### POST /admin/stations

```json
{
  "name": "软件园充电站",
  "latitude": 38.889,
  "longitude": 121.537,
  "priceFenPerKwh": 98
}
```

名称去除首尾空白后为 1 至 100 个 Unicode 字符；经纬度必须合法；单价必须在配置上限内。成功返回 201。新电站为 active 且不包含电桩。

### 10.3 电站详情和修改

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/admin/stations/{stationId}` | 电站详情；可读取 inactive 电站 |
| PATCH | `/admin/stations/{stationId}` | 修改名称、经纬度、单价或状态 |

PATCH 至少包含一个可修改字段。预约不锁定价格，修改单价只影响修改后开始的订单。把电站设为 inactive 时，原子地使站内 active 预约过期并释放对应占用。重新设为 active 不改变电桩状态。重复设置已有状态幂等成功。

### 10.4 软删除电站

#### DELETE /admin/stations/{stationId}

权限：`ADMIN`。站内存在 active 预约或 charging 订单时返回 `INVALID_STATE_TRANSITION`。成功时原子软删除电站及其全部未删除电桩，返回 204。重复删除返回 204。待结算订单不阻止删除。

### 10.5 站内电桩

#### GET /admin/stations/{stationId}/chargers

权限：`ADMIN`、`ADMIN_READONLY`。支持电桩列表筛选、`includeDeleted` 和通用分页参数。

## 11. 管理端电桩

查询权限为 `ADMIN`、`ADMIN_READONLY`，写权限为 `ADMIN`。

### 11.1 电桩列表

#### GET /admin/chargers

支持 `stationId`、`type`、`occupancyStatus`、`operationalStatus`、`includeDeleted` 和通用分页参数。默认按电桩 ID 升序。

### 11.2 新增电桩

#### POST /admin/stations/{stationId}/chargers

```json
{
  "type": "fast",
  "powerKw": 120.0
}
```

电站必须存在且未软删除。类型为 `fast` 或 `slow`；功率必须为正数、最多三位小数且不超过配置上限。成功返回 201。新电桩为 `available + online`。

### 11.3 电桩详情和修改

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/admin/chargers/{chargerId}` | 电桩详情 |
| PATCH | `/admin/chargers/{chargerId}` | 修改类型、功率或运维状态 |

PATCH 不接受 `stationId` 或 `occupancyStatus`。修改功率只影响后续订单。把 reserved 电桩设为 fault 或 offline 时，原子地使预约过期并释放占用；charging 电桩的占用状态和订单保持不变。重复设置已有值幂等成功。

### 11.4 远程重启

#### POST /admin/chargers/{chargerId}/restart

请求正文为空或 `{}`。只允许 `available + fault` 或 `available + offline` 电桩。成功时同步把运维状态改为 online 并返回更新后的电桩，不创建命令记录。

### 11.5 软删除电桩

#### DELETE /admin/chargers/{chargerId}

存在 active 预约或 charging 订单时返回 `INVALID_STATE_TRANSITION`。成功返回 204；重复删除返回 204。待结算订单不阻止删除。

## 12. 管理端用户

本章接口只允许 `ADMIN`。

### 12.1 用户列表

#### GET /admin/users

支持：

- `phone`：ASCII 子串匹配，空值返回全部；
- `status`：`active` 或 `frozen`；
- `page`、`pageSize`。

结果默认按用户 ID 升序。

### 12.2 用户详情

#### GET /admin/users/{userId}

返回用户资料、余额、最近订单摘要和注册时间。

### 12.3 冻结与解冻

#### PATCH /admin/users/{userId}

```json
{
  "status": "frozen"
}
```

只接受 `active` 或 `frozen`。冻结时原子地把该用户的 active 预约置为 cancelled 并释放电桩。正在充电和待结算订单不受影响。重复冻结或解冻幂等成功。

### 12.4 用户钱包流水

#### GET /admin/users/{userId}/wallet/transactions

返回指定用户的 `top_up` 和 `charge_debit` 流水，默认按创建时间降序、ID 降序分页。

## 13. 配置与运行行为

### 13.1 配置来源

默认配置文件为可执行文件同目录的 UTF-8 `config.ini`。环境变量覆盖 INI，INI 覆盖内置默认值。配置只在启动时加载。非法配置导致启动失败。

腾讯地图密钥使用环境变量 `TENCENT_MAP_KEY`。SERVICE 令牌使用环境变量 `ML_SERVICE_TOKEN`。这两个值不从 INI 读取。

### 13.2 默认配置

| 配置 | INI 键 | 环境变量 | 默认值 |
| --- | --- | --- | --- |
| 监听地址 | `server/host` | `EV_CHARGER_HTTP_HOST` | `127.0.0.1` |
| 监听端口 | `server/port` | `EV_CHARGER_HTTP_PORT` | `8080` |
| SQLite 文件 | `database/path` | `EV_CHARGER_DATABASE_PATH` | `data/ev-charger.sqlite3`，相对于可执行文件目录 |
| SQLite busy timeout | `database/busyTimeoutMs` | `EV_CHARGER_DATABASE_BUSY_TIMEOUT_MS` | `5000` ms |
| 最大预约时长 | `reservation/maxHoldMinutes` | `EV_CHARGER_MAX_RESERVATION_MINUTES` | `60` 分钟 |
| 最大附近半径 | `location/maxRadiusKm` | `EV_CHARGER_MAX_RADIUS_KM` | `50` km |
| 最大功率 | `charger/maxPowerKw` | `EV_CHARGER_MAX_POWER_KW` | `1000` kW |
| 最大单价 | `station/maxPriceFenPerKwh` | `EV_CHARGER_MAX_PRICE_FEN_PER_KWH` | `1000000` 分/kWh |
| 单次充值下限 | `wallet/topUpMinFen` | `EV_CHARGER_TOP_UP_MIN_FEN` | `100` 分 |
| 单次充值上限 | `wallet/topUpMaxFen` | `EV_CHARGER_TOP_UP_MAX_FEN` | `10000000` 分 |
| 钱包余额上限 | `wallet/maxBalanceFen` | `EV_CHARGER_MAX_BALANCE_FEN` | `1000000000` 分 |
| 幂等保留期 | `idempotency/retentionHours` | `EV_CHARGER_IDEMPOTENCY_RETENTION_HOURS` | `24` 小时 |
| 腾讯地图超时 | `map/timeoutMs` | `EV_CHARGER_MAP_TIMEOUT_MS` | `3000` ms |
| 腾讯地图重试 | `map/retryCount` | `EV_CHARGER_MAP_RETRY_COUNT` | `1` 次 |
| JSON 正文上限 | `http/jsonBodyLimitBytes` | `EV_CHARGER_JSON_BODY_LIMIT_BYTES` | `1048576` 字节 |
| 头像正文上限 | `http/avatarBodyLimitBytes` | `EV_CHARGER_AVATAR_BODY_LIMIT_BYTES` | `5242880` 字节 |
| 正常关闭等待 | `server/shutdownTimeoutMs` | `EV_CHARGER_SHUTDOWN_TIMEOUT_MS` | `1000` ms |

端口必须是 1 至 65535 的整数。时长、半径、功率、单价、金额上限和正文上限必须为正数；地图重试次数必须为非负整数；充值下限不得大于充值上限，钱包余额上限不得低于单次充值下限。

### 13.3 数据库

- 启动时创建缺失的数据目录和 SQLite 文件。
- 首次启动事务性创建表和默认管理员。
- 后续启动按结构版本执行内置迁移。
- 每个工作线程使用独立连接，每个连接启用外键和 WAL。
- 预约、电桩占用、订单状态、钱包余额和流水的复合变更使用事务及条件更新。

### 13.4 启动恢复

启动时按顺序执行：

1. 加载并校验配置；
2. 打开数据库并执行迁移；
3. 校验唯一业务约束，矛盾时停止启动；
4. 在一个事务中把已到期 active 预约置为 expired、其余 active 预约置为 cancelled，并解除相应占用；
5. 恢复 charging 订单对应电桩的 charging 占用状态，保留电桩运维状态；
6. 开始接受 HTTP 请求。

后端停机时间计入 charging 订单的经过时长。

### 13.5 关闭与日志

收到正常终止信号后，服务停止接收新请求，最多等待 1 秒处理在途请求，然后关闭数据库连接。关闭过程不改变订单或电桩业务状态。

日志写入标准错误，至少记录启动、关闭、配置错误、迁移失败和未预期服务错误。日志不记录密码、令牌、头像正文或腾讯地图密钥。

服务不实现请求限流，也不返回 CORS 响应头。
