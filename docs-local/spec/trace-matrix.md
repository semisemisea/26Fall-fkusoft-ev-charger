# 需求追踪矩阵（说明书 → 页面/接口/表/验收用例）

用途：冻结 v1 前逐行核对，确保每条需求有落点；开发期每票引用行号。用例编号对应 verification-strategy.md 的 L2/L3 用例。

## 1.4 功能需求

| # | 说明书需求 | 页面 | 协议消息 | 表 | 验收用例 |
|---|---|---|---|---|---|
| 1 | 附近充电站查询（定位/区域/地址→坐标/距离排序/卡片字段/站内桩详情） | user-app 站列表+站详情 | station.list / station.detail；协议§7 geocoder | t_station / t_pile | U1 区域定位返回按距离升序卡片五字段齐全；U2 地址检索走 geocoder，失败回退 mock 坐标；U3 站详情展示桩 编号/类型/状态/功率 |
| 2 | 一键导航（QWebEngineView / 驾车步行 / 跳转路线页） | 站详情导航按钮 | 无（URL scheme，协议§7 routeplan） | — | U4 drive/walk URL 正确含起终点与 key；U5 无网降级静态示意页不白屏 |
| 3 | 用户信息维护（免密登录/自动注册/默认昵称/头像/昵称/充值） | 登录页+我的 | user.login / user.profile.update / wallet.recharge | t_user / t_wallet_txn | U6 新手机号自动注册昵称=用户+后4位；U7 头像上传 ≤200KB base64；U8 充值后余额实时更新（流水 kind=0） |
| 4 | 充电前未完成订单检查 + 强制跳转 | 充电页/结算页 | order.active + 2004/pending_oid | t_order (ux_order_active) | U9 登录后及进充电页前均查活动订单；U10 PendingSettle 强制跳结算页；U11 pile.reserve 原子拦截活动订单（并发一成一败） |
| 5 | 充电全流程（预约-充电-计费-结算） | 充电页 | pile.reserve / charging.start/stop/status/cancel；sim 链路 协议§2.2-2.5 | t_order / t_pile / t_wallet_txn | B1 reserve→start（session 贯穿）→report 计量→stop→PendingSettle；B2 target_kwh 达成自动结束；B3 桩报 done 同效；B4 结算事务原子（余额+订单+流水） |
| 6 | 管理员登录 | ops 登录 | admin.login | t_admin | A1 admin/123456 成功；错密码 2006 |
| 7 | 销售业绩（7/30 日折线 + 三指标） | ops 数据总览 | stats.revenue | t_order / t_wallet_txn | A2 today/month/total 与流水一致；A3 7d series 日期连续 |
| 8 | 电桩状态（数量+占比） | ops 数据总览 | pile.summary | t_pile | A4 四状态数量之和=total，占比和=100% |
| 9 | 充电桩管理（列表+远程重启） | ops 电桩管理 | pile.list / pile.reboot + push.pile.reboot | t_pile / t_alert | A5 重启→桩离线 N 秒→恢复；A6 心跳超时→离线+告警 |
| 10 | 充电站管理（列表/详情/新增完整桩） | ops 电站管理 | station.list/detail/create（piles 数组） | t_station / t_pile | A7 create 后站+桩全落库（code 唯一）；A8 simulator 按 code 认领 |
| 11 | 用户管理（列表/模糊搜索/冻结解冻） | ops 用户管理 | user.list / user.freeze | t_user | A9 冻结后该用户登录 2005；A10 手机号模糊搜索命中 |
| 12 | 订单管理（UI 图 img_05，正文隐含） | ops 订单管理 | order.list / order.settle | t_order | A11 代结算后订单 Settled 且 settle_actor_type=1；A12 日期/状态筛选 |
| 13 | 数据库端（五类数据） | — | — | t_user/t_admin/t_station/t_pile/t_order | S1 schema 约束全绿（CHECK/FK/唯一索引）；S2 事务规则 1-4 通过 |
| 14 | 大数据大屏（ECharts 实时看板） | dashboard | GET /api/v1/dashboard/snapshot | 全库聚合 | D1 快照 JSON schema 校验通过；D2 5s 轮询数值随业务变化；D3 同源部署无 CORS 报错 |
| 15 | ML 负荷预测（1/6/24h，多维特征，推荐+预警） | ml/ + 三消费方 | 协议§1.4 ingest + station.list pred_free + warning.list kind=3 | t_ml_prediction | M1 ingest 校验失败整批拒绝；M2 预测入表含 forecast_at/model_version；M3 user-app 推荐排序开关生效；M4 ops 预警行出现；M5 大屏预测曲线更新 |

## 1.5/1.6 环境与技术

| # | 需求 | 落点 | 验收 |
|---|---|---|---|
| 16 | Qt Creator 6.2+/Ubuntu22.04 可构建 | 全部 CMake 工程 | VM 内一次构建通过（D10 彩排） |
| 17 | SQLite 存储 | ADR-0002 | S1 |
| 18 | Socket 通信 | 协议 v1 | B1/U1 全链路 |
| 19 | 多线程主框架 | biz-core 线程模型 | S3 快照发布无数据竞争（TSAN 可选/代码评审） |
| 20 | 多线程 pthread | biz-core IO/业务线程 | 同上 |

## 2.2/2.3 非功能

| # | 需求 | 落点 | 验收 |
|---|---|---|---|
| 21 | 通信数据结构安全稳定 | 协议 v1（长度头/幂等 seq/错误码/重连） | E1 断 sim.report 幂等去重+resume 校准；E2 杀 biz-core 客户端自动重连恢复 |
| 22 | 数据安全 | 余额非负 CHECK/事务原子/归属校验 2006/token 不入库 | S2/U11/A9 |
| 23 | 完整错误处理机制 | 协议§4 错误码 + t_alert + 客户端降级 | E3 断网降级（地图 mock）；E4 非法输入全错误码路径返回 |

## 未决（评审现场确认后补行）

- 真未决项已集中移至协议 §8：ML 口径/阈值（影响 M 线用例）、snapshot 独立 schema 与否、spec 是否入 git——定稿后回填本矩阵
- 原列三项（头像 200KB / status 轮询 / seq 去重）均已决，规则见协议 §1.1/§2.3，不再未决
- 大屏"用户行为分层"已确认砍除（不在矩阵内）
- 智能风控：评估后不做，验收讲解口径=预测+故障诊断+调度推荐三项覆盖 ML 章节
