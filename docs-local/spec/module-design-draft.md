# 模块内部设计 v1 草案

状态：DRAFT-3（随协议 RESTful 化同步修订：HTTP 服务线程模型、token 鉴权、客户端网络层换 QNAM）。对外契约见 protocol-v1-draft.md（RESTful + Socket 混合）；数据参考见 data-model-draft.md（非冻结草案）。

## biz-core（关键路径）

### 线程模型与同步策略

```
主线程：Qt event loop
  - HTTP 服务（QTcpServer 手写 HTTP/1.1，见 env-freeze）：REST 请求接入（user-app/ops-app/dashboard/ML 内部端点）
  - TCP Socket 服务：simulator 接入
  - 定时器：预约超时扫描 / 心跳超时扫描 / TimeService tick（加速因子时钟）
HTTP 工作线程池：请求解析/鉴权/参数校验（只读快照可即时回）
Socket IO：simulator 连接读写与编解码
业务线程×1：唯一执行写操作与事务（HTTP 写请求、socket 指令、上报全部投递到此）
```

**同步策略（数据竞争消除）**：
- 业务状态采用**不可变快照**：业务线程每次变更后原子发布（shared_ptr + atomic swap）；HTTP handler 与 socket IO 只读最近快照，无共享可变状态
- 强一致读（订单详情）投递业务线程按请求回包；容忍陈旧的读（大屏快照、桩列表）直接用快照
- 连接/token→身份映射各归其层：HTTP token 表在 HTTP 层，socket pid 绑定在 IO 层；业务层只见 (request_context, identity)

### 订单状态机（唯一定义处）

```
Reserved --start--> Charging --stop(手动停/target_kwh 达成/桩报 done)--> PendingSettle
                    --settle(余额足)--> Settled
Reserved --5min 超时--> Expired（桩释放 push.pile.release）
Reserved --cancel--> Cancelled（桩释放）
Charging --cancel--> 余额足：Settled（kind=2 折算扣款）
                     余额不足：PendingSettle（不产生负余额，照常拦截新充电）
```

- 迁移全部在业务线程内，桩/订单状态同事务（见 data-model 不变量 6-8）
- 充电结束三路径统一 PendingSettle；REST 侧无推送，用户端轮询感知；结算出路径仅两条（本人/管理员代结算，settle_actor_type/actor_id 落库）
- 拦截：reserve 原子校验无活动订单（2004）；进充电页前 GET /users/me/orders/active

### 计费策略（依赖注入）

- `IPricingStrategy::int64_t settleCents(double kwh, int64_t price_c, int slot) const`
- FlatPricingStrategy（默认）/ TimeOfUsePricingStrategy（读费率表）；装配点=启动时按 t_config `pricing.strategy`，不进编译期

### 模块拆分

```
common/     REST 请求/响应 DTO、socket 帧编解码、JSON helper（各端共链）
server/     http/（路由+鉴权+参数校验）net/（simulator socket）biz/（状态机、计费、用户、拦截）repo/（SQL 与事务）
```

## simulator

- 无 UI 控制台进程；虚拟站点表 JSON 引用桩 code → sim.register 按 code 认领（ops=主数据来源）
- TCP 长连接 + 自定义帧（4B 长度+JSON）；心跳 10s 只带 hw_ok；指令驱动 reserve/start/stop/release/reboot；sim.report 5s 带 seq（服务端幂等）；重连退避 + sim.resume 校准
- 随机低概率 hw_ok=false → 服务端置故障+告警

## user-app（Qt Widgets）

- 网络：**QNetworkAccessManager** + Bearer token；GET 失败自动重试 1 次，POST 不自动重试；统一错误对象（HTTP 状态码 + 业务 code）映射到 UI 提示
- 页面：登录 →（active 订单分流：待结算→结算页；充电中→充电页）→ 站列表（Tab：首页/充电/我的）→ 站详情 → 充电页（**2s 轮询 GET /orders/{oid}**；按钮：停止充电/取消）→ 结算页；我的（资料/余额/订单）
- 地图细则见协议 §7 相关约定（geocoder、routeplan URL、离线降级、mock 区域表；key 在 config.ini，真实 key 不入 git）

## ops-app（Qt Widgets）

- 网络：同 user-app（QNAM + admin token；角色不符接口 403 处理）
- 主窗口左侧菜单：数据总览/电站管理/电桩管理/订单管理/用户管理/预警
- 新增电站表单：站信息 + 动态桩清单（code/type/power 行编辑）
- 图表：QChart 近 7/30 日营收折线 + 电桩状态环形；表格 model/view；重启按钮 POST 后轮询桩状态看结果；预警页轮询 GET /warnings

## dashboard（纯静态，同源部署）

- index.html + ECharts；5s 轮询 GET /api/v1/dashboard/snapshot 渲染 7 组指标；biz-core 同源伺服（无 CORS）
- 砍"用户行为分层"

## ml/（独立 Python，不触业务库）

- 守护循环：GET /api/internal/ml/export（token）→ CSV → pandas 聚合（站×小时+星期/节假日/天气）→ LightGBM（基线 Holt-Winters 对照）→ predictions.json → POST /api/internal/ml/ingest → biz-core 校验整批入库
- 定时重跑（现实每 2 分钟≈模拟一天，准实时刷新）；亦可手动触发

## 错误处理横切面（说明书 2.3）

- REST：HTTP 状态码 + 业务错误码双轨（协议 §4 映射表）；token 失效 → 401 → 客户端重登录
- Socket：指令 ack 超时重发、sim.report 幂等、断线退避重连+resume 校准
- biz-core 文件日志（http/biz/socket 三类，按天滚动）；未捕获异常 → t_alert + 继续运行
- 演练项（D10）：杀 biz-core 看三端错误提示与恢复；杀 simulator 看心跳超时告警、桩离线、兜底计量；断 sim.report 看幂等与校准；非法 token/参数走 401/400 路径
