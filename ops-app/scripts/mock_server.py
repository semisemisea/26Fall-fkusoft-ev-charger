#!/usr/bin/env python3
"""开发/演示用 mock 业务服务层:实现 docs/apis.md 中 ops-app 所需的接口子集。

用法: python3 mock_server.py [port]     # 默认 8080
数据全部内存态,重启即复位;仅用于本地联调与 GUI 演示,不是生产代码。
"""

import json
import random
import sys
import threading
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LOCK = threading.Lock()

PAGE_SIZE = 20  # apis.md: pageSize 默认 20,最大 100

NOW = lambda: datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

ADMINS = {
    "admin": {"password": "123456", "displayName": "系统管理员", "role": "ADMIN"},
    "readonly": {"password": "123456", "displayName": "只读管理员", "role": "ADMIN_READONLY"},
}

TOKENS = {}  # token -> username

# 预置电站与电桩
STATIONS = [
    {"id": 1, "name": "软件园充电站",
     "latitude": 38.889, "longitude": 121.537, "pricePerKwhFen": 98, "status": "active"},
    {"id": 2, "name": "星海广场充电站",
     "latitude": 38.877, "longitude": 121.590, "pricePerKwhFen": 108, "status": "active"},
    {"id": 3, "name": "机场快充站",
     "latitude": 38.958, "longitude": 121.541, "pricePerKwhFen": 128, "status": "active"},
]

STATUS_PAIRS = [
    ("available", "online"),
    ("available", "online"),
    ("reserved", "online"),
    ("charging", "online"),
    ("charging", "fault"),
    ("available", "fault"),
    ("available", "offline"),
    ("available", "online"),
]
CHARGERS = {}
_cid = 1
for st in STATIONS:
    # 每站固定覆盖快慢充和各类状态，避免随机生成时缺少演示场景。
    for index, (occupancy_status, operational_status) in enumerate(STATUS_PAIRS):
        fast = index % 2 == 0
        CHARGERS[_cid] = {
            "id": _cid, "stationId": st["id"], "code": f"S{st['id']:02d}-{_cid:03d}",
            "type": "fast" if fast else "slow",
            "powerKw": 120.0 if fast else 7.0,
            "occupancyStatus": occupancy_status,
            "operationalStatus": operational_status,
            "totalChargeCount": random.randint(50, 400),
            "totalChargeMinutes": random.randint(2000, 30000),
        }
        _cid += 1

USERS = {}
_uid = 1
for phone in ["13800138000", "13900139000", "15012345678", "18600001111", "17755667788"]:
    USERS[_uid] = {
        "id": _uid, "phone": phone, "nickname": f"用户{phone[-4:]}",
        "walletBalanceFen": random.randint(0, 50000),
        "status": "frozen" if phone == "15012345678" else "active",
        "createdAt": "2026-08-01T08:00:00Z",
    }
    _uid += 1


# 启动时生成近 60 天的模拟已结算订单；重复查询使用同一批数据，金额单位为分。
_revenue_now = datetime.now(timezone.utc).replace(microsecond=0)
SETTLED_ORDERS = [
    {"stationId": station["id"],
     "settledAt": _revenue_now - timedelta(hours=hour, seconds=1),
     "amountFen": random.randint(1_000, 8_000)}
    for hour in range(60 * 24)
    for station in STATIONS
]


def revenue_total(q):
    start, end = q.get("from"), q.get("to")
    if (start is None) != (end is None):
        raise ValueError("from 和 to 必须同时提供")
    if start is not None:
        start = datetime.fromisoformat(start.replace("Z", "+00:00"))
        end = datetime.fromisoformat(end.replace("Z", "+00:00"))
        if start.tzinfo is None or end.tzinfo is None or start >= end:
            raise ValueError("时间范围必须包含时区且 from < to")
    station_id = int(q["stationId"]) if "stationId" in q else None
    if station_id is not None and station_id <= 0:
        raise ValueError("stationId 必须为正整数")
    orders = [order for order in SETTLED_ORDERS
              if (station_id is None or order["stationId"] == station_id)
              and (start is None or start <= order["settledAt"] < end)]
    return {"from": start.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            if start is not None else None,
            "to": end.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            if end is not None else None,
            "revenueFen": sum(order["amountFen"] for order in orders),
            "orderCount": len(orders)}


def revenue_series(range_str):
    days = 30 if range_str == "30d" else 7
    points = []
    for i in range(days, 0, -1):
        d = datetime.now(timezone.utc) - timedelta(days=i)
        points.append({
            "bucketStart": d.strftime("%Y-%m-%dT00:00:00Z"),
            "revenueFen": random.randint(80_000, 500_000),
            "orderCount": random.randint(30, 160),
        })
    return points


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # 精简日志
        sys.stderr.write("[mock] %s %s\n" % (self.command, self.path))

    # ---- helpers ----
    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def ok(self, data, status=200, **extra_meta):
        meta = {"requestId": "mock-req"}
        meta.update(extra_meta)
        self.send_json(status, {"data": data, "meta": meta})

    def err(self, status, code, message):
        self.send_json(status, {"error": {"code": code, "message": message},
                                "meta": {"requestId": "mock-req"}})

    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return {}

    def auth(self):
        auth = self.headers.get("Authorization") or ""
        token = auth.removeprefix("Bearer ").strip()
        user = TOKENS.get(token)
        if not user:
            self.err(401, "UNAUTHORIZED", "未登录或令牌已过期")
            return None
        return ADMINS[user]

    def paginate(self, items, q):
        # apis.md: 列表响应 meta 带 page/pageSize/total/hasNext,page 从 1 起
        try:
            page = max(1, int(q.get("page", "1")))
        except (TypeError, ValueError):
            page = 1
        try:
            page_size = max(1, min(100, int(q.get("pageSize", PAGE_SIZE))))
        except (TypeError, ValueError):
            page_size = PAGE_SIZE
        total = len(items)
        start = (page - 1) * page_size
        chunk = items[start:start + page_size]
        meta = {"page": page, "pageSize": page_size, "total": total,
                "hasNext": start + page_size < total}
        return {"items": chunk}, meta

    # ---- HTTP ----
    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        p = url.path
        with LOCK:
            if p == "/api/v1/admin/dashboard/revenue":
                try:
                    data = revenue_total(q)
                except ValueError as error:
                    self.err(400, "VALIDATION_ERROR", str(error))
                    return
                self.ok(data)
            elif p == "/api/v1/admin/dashboard/summary":
                total = sum(c["totalChargeCount"] for c in CHARGERS.values())
                self.ok({
                    "asOf": NOW(),
                    "todayRevenueFen": random.randint(60_000, 180_000),
                    "monthRevenueFen": random.randint(1_500_000, 4_000_000),
                    "totalRevenueFen": 48_652_000,
                    "userCount": len(USERS), "stationCount": len(STATIONS),
                    "chargerCount": len(CHARGERS),
                    "onlineRate": round(random.uniform(0.88, 1.0), 2),
                })
            elif p == "/api/v1/admin/dashboard/revenue-series":
                self.ok({"range": q.get("range", "7d"), "unit": "day",
                         "points": revenue_series(q.get("range", "7d"))})
            elif p == "/api/v1/admin/dashboard/charger-status":
                chargers = list(CHARGERS.values())
                if q.get("stationId"):
                    station_id = int(q["stationId"])
                    chargers = [c for c in chargers if c["stationId"] == station_id]
                occupancy = {"available": 0, "reserved": 0, "charging": 0}
                operational = {"online": 0, "fault": 0, "offline": 0}
                for charger in chargers:
                    occupancy[charger["occupancyStatus"]] += 1
                    operational[charger["operationalStatus"]] += 1
                self.ok({"total": len(chargers), "occupancy": occupancy,
                         "operational": operational})
            elif (p == "/api/v1/admin/chargers"
                  or (p.startswith("/api/v1/admin/stations/")
                      and p.endswith("/chargers"))):
                items = list(CHARGERS.values())
                if p.startswith("/api/v1/admin/stations/"):
                    station_id = int(p.split("/")[5])
                    if not any(st["id"] == station_id for st in STATIONS):
                        self.err(404, "NOT_FOUND", "电站不存在")
                        return
                    items = [c for c in items if c["stationId"] == station_id]
                elif q.get("stationId"):
                    station_id = int(q["stationId"])
                    items = [c for c in items if c["stationId"] == station_id]
                for field in ("type", "occupancyStatus", "operationalStatus"):
                    if q.get(field):
                        items = [c for c in items if c[field] == q[field]]
                chunk, meta = self.paginate(items, q)
                self.ok(chunk, **meta)
            elif p == "/api/v1/admin/stations":
                search = (q.get("name") or "").lower()
                items = []
                for st in STATIONS:
                    if search and search not in st["name"].lower():
                        continue
                    cs = [c for c in CHARGERS.values() if c["stationId"] == st["id"]]
                    online = sum(1 for c in cs if c["operationalStatus"] == "online")
                    items.append({**st, "chargerCount": len(cs),
                                  "availableChargerCount": sum(
                                      1 for c in cs
                                      if c["occupancyStatus"] == "available"
                                      and c["operationalStatus"] == "online"),
                                  "onlineRate": round(online / len(cs), 4)
                                  if cs else 0.0})
                chunk, meta = self.paginate(items, q)
                self.ok(chunk, **meta)
            elif p.startswith("/api/v1/stations/") and p.endswith("/chargers"):
                sid = int(p.split("/")[4])
                self.ok([c for c in CHARGERS.values() if c["stationId"] == sid])
            elif p == "/api/v1/admin/users":
                phone = (q.get("phone") or "").strip()
                items = [u for u in USERS.values()
                         if not phone or phone in u["phone"]]
                chunk, meta = self.paginate(items, q)
                self.ok(chunk, **meta)
            else:
                self.err(404, "NOT_FOUND", f"未知路径 {p}")

    def do_POST(self):
        url = urlparse(self.path)
        p = url.path
        body = self.body_json()
        with LOCK:
            if p == "/api/v1/auth/admin/login":
                rec = ADMINS.get(body.get("username", ""))
                if not rec or rec["password"] != body.get("password"):
                    self.err(401, "UNAUTHORIZED", "账号或密码错误")
                    return
                token = f"mock-token-{random.randint(10**8, 10**9)}"
                TOKENS[token] = body["username"]
                user = {"id": 1, "username": body["username"],
                        "displayName": rec["displayName"], "role": rec["role"],
                        "status": "active"}
                self.ok({"accessToken": token, "tokenType": "Bearer",
                         "expiresIn": 604800, "user": user})
            elif p == "/api/v1/auth/logout":
                self.send_response(204)
                self.end_headers()
            elif p.startswith("/api/v1/admin/chargers/") and p.endswith("/restart"):
                charger_id = int(p.split("/")[5])
                charger = CHARGERS.get(charger_id)
                if not charger:
                    self.err(404, "NOT_FOUND", "电桩不存在")
                elif charger["occupancyStatus"] != "available":
                    self.err(409, "INVALID_STATE_TRANSITION", "电桩当前非闲置, 不能重启")
                elif charger["operationalStatus"] not in ("fault", "offline"):
                    self.err(409, "INVALID_STATE_TRANSITION", "电桩无需重启")
                else:
                    charger["operationalStatus"] = "online"
                    self.ok(charger)
            elif p == "/api/v1/admin/stations":
                name = body.get("name", "")
                if any(st["name"] == name for st in STATIONS):
                    self.err(409, "VALIDATION_ERROR", "站名已存在")
                    return
                sid = max(s["id"] for s in STATIONS) + 1
                STATIONS.append({"id": sid, "name": name,
                                 "latitude": body.get("latitude", 0),
                                 "longitude": body.get("longitude", 0),
                                 "pricePerKwhFen": body.get("pricePerKwhFen", 98),
                                 "status": "active"})
                for i, c in enumerate(body.get("chargers", []), start=1):
                    CHARGERS[_cid] = {
                        "id": _cid, "stationId": sid,
                        "code": f"S{sid:02d}-{_cid:03d}", "type": c.get("type", "slow"),
                        "powerKw": c.get("powerKw", 7.0),
                        "occupancyStatus": "available", "operationalStatus": "online",
                        "totalChargeCount": 0, "totalChargeMinutes": 0}
                    _cid += 1
                self.ok({"id": sid, "name": name}, status=201)
            else:
                self.err(404, "NOT_FOUND", f"未知路径 {p}")

    def do_PATCH(self):
        url = urlparse(self.path)
        p = url.path
        body = self.body_json()
        with LOCK:
            if p.startswith("/api/v1/admin/users/"):
                uid = int(p.rsplit("/", 1)[1])
                user = USERS.get(uid)
                if not user:
                    self.err(404, "NOT_FOUND", "用户不存在")
                elif body.get("status") not in ("active", "frozen"):
                    self.err(400, "VALIDATION_ERROR", "status 只能是 active/frozen")
                else:
                    user["status"] = body["status"]
                    self.ok(user)
            else:
                self.err(404, "NOT_FOUND", f"未知路径 {p}")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"mock server on http://127.0.0.1:{port}/api/v1", flush=True)
    server.serve_forever()
