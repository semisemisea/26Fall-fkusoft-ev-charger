#!/usr/bin/env python3
"""在后端首次启动前创建演示 SQLite 数据库；仅使用 Python 标准库。"""

import argparse
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import random
import re
import sqlite3
import tempfile


def timestamp(value):
    return value.astimezone(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def insert(db, table, **values):
    columns = ",".join(values)
    placeholders = ",".join("?" for _ in values)
    return db.execute(
        f"INSERT INTO {table} ({columns}) VALUES ({placeholders})", tuple(values.values())
    ).lastrowid


def create_schema(db):
    # 复用当前仓库的 v1 DDL，避免维护与后端分叉的第二份表结构。
    source = Path(__file__).resolve().parents[1] / "src/database.cpp"
    statements = re.findall(r'QStringLiteral\(("CREATE (?:TABLE|(?:UNIQUE )?INDEX)[^\n]+?")\)', source.read_text())
    if len(statements) != 21:
        raise ValueError("后端 DDL 已变化，请更新脚本的 v1 模式读取逻辑")
    for statement in statements:
        db.execute(json.loads(statement))
    insert(db, "schema_version", version=1)
    # 默认管理员和服务凭据由后端首次启动时按真实逻辑创建。


def populate(db, now, days, seed):
    rng = random.Random(seed)
    created = timestamp(now - timedelta(days=days + 10))
    stations = []
    locations = [
        ("人民广场", 31.2304, 121.4737), ("陆家嘴", 31.2397, 121.4998),
        ("徐家汇", 31.1885, 121.4365), ("静安寺", 31.2231, 121.4460),
        ("五角场", 31.2989, 121.5145), ("虹桥枢纽", 31.1945, 121.3271),
        ("张江科技园", 31.2010, 121.5990), ("世纪公园", 31.2150, 121.5510),
    ]
    chargers = []
    for index, (name, lat, lon) in enumerate(locations):
        price = 90 + index * 15
        station = insert(db, "stations", name=f"{name}演示充电站", latitude=lat,
                         longitude=lon, price_fen_per_kwh=price,
                         status="inactive" if index == 7 else "active",
                         created_at=created, updated_at=created)
        stations.append(station)
        for slot in range(6):
            power = (60000, 120000, 90000, 7000, 11000, 22000)[slot]
            charger = insert(db, "chargers", station_id=station,
                             type="fast" if slot < 3 else "slow", power_w=power,
                             operational_status="fault" if slot == 4 else "offline" if slot == 5 else "online",
                             created_at=created, updated_at=created)
            chargers.append((charger, station, power, price))

    users = []
    balances = {}
    for index in range(24):
        user = insert(db, "users", phone=f"1380000{index + 1:04d}",
                      nickname=("演示用户" if index < 22 else "冻结用户") + f"{index + 1:02d}",
                      status="active" if index < 22 else "frozen",
                      created_at=created, updated_at=created)
        users.append(user)
        balances[user] = 0

    def wallet(user, amount, at, order=None):
        # 流水金额始终为正，收支方向由 type 表达，与后端结算保持一致。
        balances[user] += amount if order is None else -amount
        insert(db, "wallet_transactions", user_id=user, order_id=order,
               type="top_up" if order is None else "charge_debit", amount_fen=amount,
               balance_after_fen=balances[user], created_at=at)

    for user in users[:-1]:
        wallet(user, 50000, created)

    def order(user, charger, start, seconds, status, reserved=False):
        cid, sid, power, price = charger
        end = start + timedelta(seconds=seconds)
        at = timestamp(end)
        reservation = None
        if reserved:
            reservation = insert(db, "reservations", user_id=user, station_id=sid,
                                 charger_id=cid, status="used",
                                 created_at=timestamp(start - timedelta(minutes=5)),
                                 expires_at=timestamp(start + timedelta(minutes=10)),
                                 updated_at=timestamp(start))
        energy = power * seconds
        amount = (energy * price + 1800000) // 3600000
        oid = insert(db, "orders", user_id=user, station_id=sid, charger_id=cid,
                     reservation_id=reservation, status=status, power_w=power,
                     unit_price_fen_per_kwh=price, started_at=timestamp(start),
                     stopped_at=at, settled_at=at if status == "settled" else None,
                     duration_seconds=seconds, energy_ws=energy, amount_fen=amount,
                     created_at=timestamp(start), updated_at=at)
        db.execute("UPDATE chargers SET total_charge_count=total_charge_count+1, "
                   "total_charge_seconds=total_charge_seconds+?,updated_at=? WHERE id=?",
                   (seconds, at, cid))
        if status == "settled":
            if balances[user] < amount + 10000:
                wallet(user, 50000, at)
            wallet(user, amount, at, oid)

    # 每天多笔，覆盖今日、近 7/30 天、跨月营收；时间槽避免同桩/用户重叠。
    for day in range(days - 1, -1, -1):
        for slot in range(rng.randint(8, 16)):
            end = now - timedelta(days=day, minutes=(16 - slot) * 85)
            seconds = rng.randint(10, 60) * 60
            order(users[slot % 22], rng.choice(chargers), end - timedelta(seconds=seconds),
                  seconds, "settled", reserved=slot % 3 == 0)

    order(users[1], chargers[1], now - timedelta(minutes=35), 1800, "awaiting_payment")
    for index, status in enumerate(("cancelled", "expired")):
        at = now - timedelta(days=1, hours=index)
        insert(db, "reservations", user_id=users[index], station_id=stations[0],
               charger_id=chargers[index][0], status=status, created_at=timestamp(at),
               expires_at=timestamp(at + timedelta(minutes=15)),
               updated_at=timestamp(at + timedelta(minutes=15)))
    # 不预置 charging：离线放置数据库不会积累失真的超长充电费用。
    for user in users:
        db.execute("UPDATE users SET balance_fen=?,updated_at=? WHERE id=?",
                   (balances[user], timestamp(now), user))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True, type=Path, help="新数据库路径（必须不存在）")
    parser.add_argument("--days", type=int, default=90, help="历史天数，1–365，默认 90")
    parser.add_argument("--seed", type=int, default=20260909, help="随机种子")
    args = parser.parse_args()
    if not 1 <= args.days <= 365:
        parser.error("--days 必须在 1–365 之间")
    destination = args.database.expanduser().absolute()
    temporary = None
    try:
        if destination.exists():
            raise ValueError(f"数据库已存在，拒绝覆盖：{destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=".seed-demo-", dir=destination.parent)
        os.close(fd)
        with sqlite3.connect(temporary) as db:
            db.execute("PRAGMA foreign_keys=ON")
            db.execute("BEGIN")
            create_schema(db)
            populate(db, datetime.now(timezone.utc).replace(microsecond=0), args.days, args.seed)
            if db.execute("PRAGMA foreign_key_check").fetchall():
                raise ValueError("生成数据的外键检查失败")
            counts = {table: db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
                      for table in ("stations", "chargers", "users", "orders", "reservations", "wallet_transactions")}
        db.close()
        # 同目录硬链接原子发布，目标若被其他进程创建则失败，绝不覆盖。
        os.link(temporary, destination)
        print(f"已创建：{destination}")
        print(json.dumps(counts, ensure_ascii=False))
        print("普通用户：13800000001；待支付：13800000002；冻结：13800000023")
        print("首次启动后管理员：admin / 123456")
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f"生成失败：{error}\n")
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
