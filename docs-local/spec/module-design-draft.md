# 模块内部设计 v1 草案

状态：DRAFT-2（吸收 2026-09-02 评审意见：缓存同步、身份两段式、地图细则指针）。对外契约见 protocol-v1-draft.md 与 data-model-draft.md；本文只写模块内部的必要设计。

## biz-core（关键路径）

### 线程模型与同步策略（回应评审 #11）

```
主线程：Qt event loop
  - HTTP 快照接口（只读，经缓存快照）
  - 定时器：预约超时扫描 / 心跳超时扫描 / TimeService tick（加速因子时钟）
IO 线程×2：QTcpServer accept 后分发，每连接一个 socket 读写上下文（只做收发与编解码）
业务线程×1：唯一执行写操作与事务；读请求也统一投递到业务线程处理（见下）
```

**同步策略（数据竞争消除）**：
- 不做"IO 线程读缓存、业务线程写缓存"的共享可变状态——这是竞争源
- 业务状态采用**不可变快照**：业务线程每次状态变更后原子发布新快照（`std::shared_ptr` + atomic swap）；IO 线程与主线程（HTTP）只读最近快照，不持锁、不阻塞
- 读请求路径：一致性要求高的读（订单详情）投递业务线程按 seq 回包；容忍陈旧的读（大屏快照、桩列表）直接用快照即时返回
- 连接→身份映射归 IO 层所有，业务层只见 (connection_id, identity)，避免跨界访问 socket 对象

### 订单状态机（唯一定义处，用户端/后台只是触发器）

```
Reserved --start--> Charging --stop(手动停/target_kwh 达成/桩报 done)--> PendingSettle
                    --settle(余额足)--> Settled
Reserved --5min 超时--> Expired（桩释放 push.pile.release）
Reserved --cancel--> Cancelled（桩释放）
Charging --cancel--> 余额足：Settled（kind=2 折算扣款）
                     余额不足：PendingSettle（不产生负余额，照常拦截新充电）
```

- 状态迁移全部在业务线程内，桩状态与订单状态同事务（见 schema 事务规则）
- 充电结束三路径统一汇入 PendingSettle，均触发 push.order.state；结算出路径仅两条（本人/管理员代结算）
- 拦截规则：pile.reserve 原子校验无活动订单（Reserved/Charging/PendingSettle → 2004）；进入充电页前客户端必调 order.active

### 计费策略（依赖注入）

- `IPricingStrategy::int64_t settleCents(double kwh, int64_t price_c, int slot) const` —— 返回分，舍入规则协议 §1.1（只在最终金额舍入一次）
- FlatPricingStrategy（默认装配）/ TimeOfUsePricingStrategy（读 t_pricing 费率表，按时段电量分段计算）
- 装配点：biz-core 启动时按 t_config 里 `pricing.strategy` 选择，不进编译期

### 模块拆分（目录级）

```
common/     协议常量、消息收发封装、JSON helper（三端+simulator 共链）
server/     net/（连接与身份）biz/（状态机、计费、用户、拦截规则）repo/（SQL 与事务）http/（快照 + 内部 ingest）
```

## simulator

- 无 UI 控制台进程；虚拟站点表 JSON 引用桩 code → sim.register 按 code 认领（主数据在 ops-app，见协议 §8）
- 心跳只带硬件健康位 hw_ok（状态权威在服务端）；随机低概率 hw_ok=false → 服务端置故障+告警
- 桩行为由服务端 push 指令驱动：reserve/start/stop/release/reboot（session_id 贯穿）；只为有效 session 计量；sim.report 每 5s 带 seq（服务端幂等去重）；target 达成或收 stop 回 state=done + final_kwh
- 重连后对充电中 session 发 sim.resume 校准；执行 reboot（离线 N 秒→重连）

## user-app（Qt Widgets）

- 页面：登录 →（order.active 检查分流：待结算 → 结算页；充电中 → 充电页）→ 站列表（Tab：首页/充电/我的）→ 站详情 → 充电页（轮询 charging.status 2s + push 兜底；按钮：停止充电/取消）→ 结算页；我的（资料/余额/订单）
- 地图与导航细则见协议 §7（geocoder、routeplan URL、离线降级、mock 区域表）
- 通信：单一 NetClient（QTcpSocket + 重连退避 + seq 映射回调 + 两段式身份）；页面只发请求收信号，不碰协议细节

## ops-app（Qt Widgets）

- 主窗口左侧菜单：数据总览/电站管理/电桩管理/订单管理/用户管理/预警（对齐 UI 图 img_01，订单管理+预警列表）
- 新增电站表单：站信息 + 动态桩清单（code/type/power 行编辑）——station.create 完整建桩
- 图表：QChart 近 7/30 日营收折线 + 电桩状态环形；全部表格走 model/view，操作按钮（重启/代结算/冻结）带二次确认；预警页轮询 warning.list

## dashboard（纯静态，同源部署）

- index.html + ECharts；5s 轮询 GET /api/snapshot 渲染 7 组指标；验收形态由 biz-core 同源伺服（无 CORS），开发期可代理
- 指标砍掉"用户行为分层"（评审确认的取舍）

## ml/（独立 Python，不触业务库）

- 守护循环：向 biz-core 请求导出 CSV（HTTP 只读端点，token 复用 ingest）→ pandas 聚合（站×小时 + 星期/节假日/天气特征）→ LightGBM 回归（基线 Holt-Winters 对照）→ 写 out/predictions.json → POST 内部 ingest → biz-core 校验整批入库
- 输出 JSON 含 forecast_at / model_version / 每站 1h、6h、24h 的 load（并发桩数）与 free_piles
- 历史数据源：gen_history.py 造的 30~60 天（含特征列）+ 运行期真实订单，滚动增长

## 错误处理横切面（说明书 2.3）

- 协议层错误码（协议 §6）；断线重连指数退避 + 两段式身份恢复；biz-core 文件日志（network/biz 两类，按天滚动）；未捕获异常 → t_alert + 继续运行
- 演练项（D10）：杀 biz-core 看客户端重连恢复；杀 simulator 看心跳超时告警、桩离线、兜底计量接管；断 sim.report 看幂等与 resume 校准
