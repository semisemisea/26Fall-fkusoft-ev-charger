# 数据库 schema v1 草案（PostgreSQL→SQLite 通用写法）

状态：DRAFT，随协议 v1 一起评审冻结。SQLite，WAL 模式；时间统一存 unix 秒 INTEGER；金额分单位存整数（避免浮点）。

```sql
-- 用户
CREATE TABLE t_user (
  uid INTEGER PRIMARY KEY,
  phone TEXT UNIQUE NOT NULL,          -- 11 位
  nickname TEXT NOT NULL,
  avatar BLOB,                         -- 缩略图字节，可空=默认灰头像
  balance_c INTEGER NOT NULL DEFAULT 0,-- 分
  status INTEGER NOT NULL DEFAULT 0,   -- 0 正常 1 冻结
  reg_at INTEGER NOT NULL
);

-- 管理员
CREATE TABLE t_admin (
  aid INTEGER PRIMARY KEY,
  username TEXT UNIQUE NOT NULL,
  password TEXT NOT NULL,              -- 演示项目明文可接受，字段留 hash 位
  name TEXT NOT NULL
);

-- 充电站
CREATE TABLE t_station (
  sid INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  addr TEXT NOT NULL,
  lat REAL NOT NULL, lng REAL NOT NULL,
  price_c INTEGER NOT NULL,            -- 元/度→分/度
  created_at INTEGER NOT NULL
);

-- 电桩
CREATE TABLE t_pile (
  pid INTEGER PRIMARY KEY,
  sid INTEGER NOT NULL REFERENCES t_station(sid),
  type INTEGER NOT NULL,               -- 0 快充 1 慢充
  power_kw REAL NOT NULL,
  status INTEGER NOT NULL DEFAULT 0,   -- 0 空闲 1 预约锁 2 充电中 3 故障 4 离线
  chg_count INTEGER NOT NULL DEFAULT 0,
  chg_sec INTEGER NOT NULL DEFAULT 0,  -- 累计秒（展示换算小时）
  last_seen INTEGER                    -- 心跳时间
);

-- 订单（状态机唯一落点）
CREATE TABLE t_order (
  oid INTEGER PRIMARY KEY,
  uid INTEGER NOT NULL REFERENCES t_user(uid),
  pid INTEGER NOT NULL REFERENCES t_pile(pid),
  sid INTEGER NOT NULL REFERENCES t_station(sid),
  status INTEGER NOT NULL,             -- 0 预约中 1 充电中 2 待结算 3 已结算 4 已取消 5 已超时
  reserve_at INTEGER, start_at INTEGER, end_at INTEGER,
  kwh REAL NOT NULL DEFAULT 0,
  amount_c INTEGER NOT NULL DEFAULT 0, -- 结算时按策略计算
  settle_at INTEGER, settle_by INTEGER -- settle_by: uid=本人 / aid=管理员代结算
);

-- 钱包流水
CREATE TABLE t_wallet_txn (
  txid INTEGER PRIMARY KEY,
  uid INTEGER NOT NULL REFERENCES t_user(uid),
  kind INTEGER NOT NULL,               -- 0 充值 1 结算扣款 2 取消折算扣款
  delta_c INTEGER NOT NULL,            -- 正=入 负=出
  oid INTEGER,                         -- 关联订单可空
  at INTEGER NOT NULL
);

-- 告警（大屏消费）
CREATE TABLE t_alert (
  alid INTEGER PRIMARY KEY,
  kind INTEGER NOT NULL,               -- 0 桩故障 1 心跳超时离线 2 待结算超时
  ref_id INTEGER,                      -- pid/oid
  msg TEXT NOT NULL,
  at INTEGER NOT NULL,
  cleared_at INTEGER                   -- 空=未恢复
);

-- ML 预测结果（ADR-0005 契约）
CREATE TABLE t_ml_prediction (
  mid INTEGER PRIMARY KEY,
  sid INTEGER NOT NULL REFERENCES t_station(sid),
  horizon INTEGER NOT NULL,            -- 1|6|24 (小时)
  load REAL NOT NULL,                  -- 预测负荷（并发充电桩数或 kW，定稿时定口径）
  free_piles INTEGER NOT NULL,
  peak_hour INTEGER,                   -- 预测高峰时段（0-23）
  run_at INTEGER NOT NULL              -- 本次批跑时间
);

-- 计费费率（峰谷策略用；平价策略只读 t_station.price_c）
CREATE TABLE t_pricing (
  pid_cfg INTEGER PRIMARY KEY,
  slot INTEGER NOT NULL,               -- 0 峰 1 平 2 谷
  start_h INTEGER NOT NULL, end_h INTEGER NOT NULL,
  rate REAL NOT NULL                   -- 相对平价倍率
);

-- 全局配置（加速因子等）
CREATE TABLE t_config (key TEXT PRIMARY KEY, value TEXT NOT NULL);
```

## 索引与约束要点

- t_order(uid, status) —— 用户端"未完成订单"前置检查
- t_order(status) / t_order(sid) —— 后台订单页、营收统计
- t_pile(sid, status) —— 站详情与空闲统计
- 并发预约裁决：UPDATE t_pile SET status=1 WHERE pid=? AND status=0，检查受影响行数=1 才插订单（先到先得靠单条原子 UPDATE，不靠先查后写）

## 造数脚本约定（E 线交付物）

- seed.py：4 个演示账号（正常/待结算/低余额/已冻结）+ 3~5 站 + 每站 4~8 桩 + admin
- gen_history.py：批量生成 30~60 天历史订单（含时段分布、周末效应），供 ML 训练与大屏历史曲线
