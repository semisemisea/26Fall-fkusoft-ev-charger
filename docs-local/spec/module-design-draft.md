# 模块内部设计 v1 草案

状态：DRAFT。对外契约见 protocol-v1-draft.md 与 db-schema-draft.md；本文只写模块内部的必要设计。

## biz-core（关键路径）

### 线程模型
```
主线程：Qt event loop（HTTP 快照接口 + 定时器：预约超时扫描/心跳超时扫描/加速因子时钟）
IO 线程×2：QTcpServer accept 后分发，每连接一个 socket 读写上下文
业务队列（1 消费者）：所有写操作序列化入队，保证"单写者"；读操作走缓存直接回
```
- 用 Qt 信号槽跨线程投递，业务队列处理结果按 seq 回写对应连接
- 加速因子时钟：单一 TimeService（主线程定时器 tick），业务队列统一向它取"模拟时间"，禁止各处自算

### 订单状态机（唯一定义处，用户端/后台只是触发器）
```
Reserved --start--> Charging --stop(充满/手动停)--> PendingSettle --settle(余额足)--> Settled
Reserved --5min 超时--> Expired                    PendingSettle --settle(余额不足)--> 保持，拦截新充电
Reserved --cancel--> Cancelled
Charging --cancel--> Settled（按已耗电量折算，settle 记录 kind=2）
```
- 状态迁移全部发生在业务队列线程内，桩状态与订单状态在同一事务里变更
- 充电电量：Charging 期间 simulator 按 sim.report 上报 kwh_delta 累加；simulator 离线则按功率×经过模拟时长兜底

### 计费策略（依赖注入）
- `IPricingStrategy::int32_t settle(amount kwh, int64_t price_c, int32_t slot) const`
- FlatPricingStrategy（默认装配）/ TimeOfUsePricingStrategy（读 t_pricing 费率表）
- 装配点：biz-core 启动时按 t_config 里 `pricing.strategy` 选择，不进编译期

### 模块拆分（目录级）
```
common/     协议常量、消息收发封装、JSON helper（三端+simulator 共链）
server/     net/（连接管理）biz/（状态机、计费、用户）repo/（SQL）http/（快照接口）
```

## simulator
- 无 UI 控制台进程；启动读虚拟站点表（JSON 配置：真实坐标+虚构桩位）→ sim.register → 每 10s sim.heartbeat
- 每根桩独立状态机（空闲/被锁/充电中/故障/离线），随机翻转故障（低概率），执行 push.pile.reboot（离线 N 秒）
- Charging 中的桩按 power × 加速时长 每 5s 累计 kwh_delta 上报

## user-app（Qt Widgets）
- 页面：登录 → 站列表（Tab：首页/充电/我的）→ 站详情 → 充电页（轮询 charging.status 2s）→ 结算页；我的（资料/余额/订单）
- 通信：单一 NetClient（QTcpSocket + 重连退避 + seq 映射回调）；页面只发请求收信号，不碰协议细节

## ops-app（Qt Widgets）
- 主窗口左侧菜单：数据总览/电站管理/电桩管理/订单管理/用户管理（对齐 UI 图 img_01，含正文未列的订单管理）
- 图表：QChart 近 7/30 日营收折线 + 电桩状态环形
- 全部表格走 model/view，操作按钮（重启/代结算/冻结）带二次确认

## dashboard（纯静态）
- index.html + ECharts；5s 轮询 GET /api/snapshot 渲染 7 组指标；无构建链，python -m http.server 即可起

## ml/
- 守护循环：导 CSV → pandas 按站×小时聚合 → Holt-Winters 拟合 → 写 t_ml_prediction（1/6/24h）→ sleep(120s)
- 历史数据源：gen_history.py 造的 30~60 天 + 运行期真实订单，天然滚动增长

## 错误处理横切面（说明书 2.3）
- 协议层错误码（见协议 §7）；断线重连指数退避；biz-core 文件日志（network/biz 两类，按天滚动）；未捕获异常 → t_alert + 继续运行
- 演练项（D10）：杀 biz-core 看客户端重连恢复；杀 simulator 看心跳超时告警与桩离线
