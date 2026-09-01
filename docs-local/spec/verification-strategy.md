# 验证策略（verifier 分层）v1

用途：每张任务票的验收标准引用本文的层级；Agent 开发循环 = 写码 → verify.sh → 迭代 → 人终审 L4。

## 分层

| 层 | 检查内容 | 工具 | 成本 | 淘汰的错误 |
|---|---|---|---|---|
| L1 编译 | build + clang-format check + warnings-as-errors | CMake + clang-format | 秒级 | 语法/风格 |
| L2 结构 | 每页控件清单：objectName 存在、表格列头文本、按钮可用条件 | QTest 按 objectName 断言 | 秒级 | 页面/字段偏离 spec |
| L3 行为 | 订单状态机规则、协议交互、计费结果 | QTest（biz-core 直测）+ fake biz-core 回放 | 秒级 | 业务逻辑错 |
| L4 视觉 | 渲染结果 vs UI 参考图（img_01~11） | offscreen 截图 + Agent 视觉对比 + 人终审 | 分钟级 | 布局/交互观感 |

## 约定

1. **所有控件必须有 objectName**，命名 = 页面前缀 + 语义名（如 `tb_station_list`、`btn_reserve`）；spec 冻结后控件清单即测试依据
2. **控件清单写进各端 spec**（哪页有哪些控件/列/按钮），是从 UI 参考图转录的，评审时一并确认
3. **L2/L3 不起真服务**：客户端测试用 fake biz-core（固定 JSON 回放）；数据用 E 线 seed.py 固定 fixture，保证确定性
4. **L4 流程**：QT_QPA_PLATFORM=offscreen + QWidget::grab() 存 PNG → Agent 读图与参考图对比 → 迭代；不做像素级基线回归，人只终审
5. **dashboard 验证**：Playwright 无头截图 + /api/snapshot JSON schema 校验
6. **verify.sh 一条命令**：build + format check + test + 截图到 docs-local/screenshots/<端>/<页>.png；每轮改动必跑，绿了才算完成

## 状态机用例清单（L3 核心，biz-core）

从 CONTEXT.md 状态机逐条转录：
- Reserved +5min 无 start → Expired，桩回空闲
- 并发预约同一空闲桩：一成一败（2002）
- Charging cancel：按已耗电量折算扣款 → Settled（txn kind=2）
- Charging 正常结束三路径：charging.stop / target_kwh 达成 / sim.report state=done → 均 PendingSettle；用户端收 push.order.state 引导结算
- 预约链路：push.pile.reserve → start（session 贯穿）→ sim.report 计量；无 session 桩不计量
- simulator 心跳只带 hw_ok：hw_ok=false → 桩置故障 + t_alert；重启后 sim.resume 校准兜底读数
- PendingSettle 余额不足：停留、拦截新充电（2003/2004）
- 冻结用户登录 → 2005；待结算存在时进充电页 → 2004 + pending_oid
- 计费：Flat 与 TimeOfUse 同一 kwh 输入金额正确（峰/平/谷边界时刻）
- 远程重启：桩离线 → 心跳超时告警入 t_alert → 恢复

## 投入边界（10 天约束）

- L1/L2：每端搭半天，此后边际成本近零
- L3：只覆盖 biz-core 状态机 + 客户端消息往返，不追覆盖率数字
- L4：只做"渲染-自看-迭代"循环；dashboard 的 Playwright 截图收益最大可上
- 不做：像素 diff 基线、端到端自动化全链路（D10 用人工演练清单替代，见 module-design 错误处理演练项）
