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

NOW = lambda: datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

ADMINS = {
    "admin": {"password": "123456", "displayName": "系统管理员", "role": "ADMIN"},
    "readonly": {"password": "123456", "displayName": "只读管理员", "role": "ADMIN_READONLY"},
}

TOKENS = {}  # token -> username

# 预置电站与电桩
STATIONS = [
    {"id": 1, "name": "软件园充电站", "address": "大连市甘井子区软件园路1号",
     "latitude": 38.889, "longitude": 121.537, "pricePerKwhFen": 98, "status": "active"},
    {"id": 2, "name": "星海广场充电站", "address": "大连市沙河口区中山路588号",
     "latitude": 38.877, "longitude": 121.590, "pricePerKwhFen": 108, "status": "active"},
    {"id": 3, "name": "机场快充站", "address": "大连市甘井子区迎客路100号",
     "latitude": 38.958, "longitude": 121.541, "pricePerKwhFen": 128, "status": "active"},
]

STATUSES = ["available", "available", "charging", "available", "fault", "offline"]
CHARGERS = {}
_cid = 1
for st in STATIONS:
    for _ in range(8):
        fast = random.random() < 0.5
        CHARGERS[_cid] = {
            "id": _cid, "stationId": st["id"], "code": f"S{st['id']:02d}-{_cid:03d}",
            "type": "fast" if fast else "slow",
            "powerKw": 120.0 if fast else 7.0,
            "status": random.choice(STATUSES),
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

    def ok(self, data, status=200):
        self.send_json(status, {"data": data, "meta": {"requestId": "mock-req"}})

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

    def paginate(self, items):
        return {"items": items}

    # ---- HTTP ----
    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        p = url.path
        with LOCK:
            if p == "/api/v1/admin/dashboard/summary":
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
                counts = {}
                for c in CHARGERS.values():
                    counts[c["status"]] = counts.get(c["status"], 0) + 1
                total = len(CHARGERS)
                rows = [{"status": s, "count": n, "percent": round(n / total, 4)}
                        for s, n in counts.items()]
                self.ok(rows)
            elif p == "/api/v1/admin/chargers":
                status = q.get("status")
                items = [c for c in CHARGERS.values()
                         if not status or c["status"] == status]
                self.ok(items)
            elif p == "/api/v1/admin/stations":
                search = (q.get("search") or "").lower()
                items = []
                for st in STATIONS:
                    if search and search not in st["name"].lower() \
                            and search not in st["address"].lower():
                        continue
                    cs = [c for c in CHARGERS.values() if c["stationId"] == st["id"]]
                    items.append({**st, "chargerCount": len(cs),
                                  "availableChargerCount": sum(
                                      1 for c in cs if c["status"] == "available"),
                                  "onlineRate": round(random.uniform(0.9, 1.0), 2)})
                self.ok(items)
            elif p.startswith("/api/v1/stations/") and p.endswith("/chargers"):
                sid = int(p.split("/")[4])
                self.ok([c for c in CHARGERS.values() if c["stationId"] == sid])
            elif p == "/api/v1/admin/users":
                phone = (q.get("phone") or "").strip()
                items = [u for u in USERS.values()
                         if not phone or phone in u["phone"]]
                self.ok(items)
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
            elif p.endswith("/commands"):
                charger_id = int(p.split("/")[4])
                charger = CHARGERS.get(charger_id)
                if not charger:
                    self.err(404, "NOT_FOUND", "电桩不存在")
                elif charger["status"] == "charging":
                    self.err(409, "INVALID_STATE_TRANSITION", "电桩正在充电, 不能重启")
                else:
                    charger["status"] = "available"
                    self.ok({"id": 1, "chargerId": charger_id, "type": "restart",
                             "status": "succeeded", "createdAt": NOW()})
            elif p == "/api/v1/admin/stations":
                name = body.get("name", "")
                if any(st["name"] == name for st in STATIONS):
                    self.err(409, "VALIDATION_ERROR", "站名已存在")
                    return
                sid = max(s["id"] for s in STATIONS) + 1
                STATIONS.append({"id": sid, "name": name,
                                 "address": body.get("address", ""),
                                 "latitude": body.get("latitude", 0),
                                 "longitude": body.get("longitude", 0),
                                 "pricePerKwhFen": body.get("pricePerKwhFen", 98),
                                 "status": "active"})
                for i, c in enumerate(body.get("chargers", []), start=1):
                    CHARGERS[_cid] = {
                        "id": _cid, "stationId": sid,
                        "code": f"S{sid:02d}-{_cid:03d}", "type": c.get("type", "slow"),
                        "powerKw": c.get("powerKw", 7.0), "status": "available",
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
