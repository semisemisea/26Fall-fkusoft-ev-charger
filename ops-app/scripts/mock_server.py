#!/usr/bin/env python3
## @file
# @brief 本地内存模拟服务，提供历史演示接口子集；不保证与当前客户端全部路径和字段一致。
# @details 线程共用演示数据，通过 LOCK 串行访问；重启后重新随机生成。
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

## @brief 保护线程间共享内存读写的互斥锁。
LOCK = threading.Lock()

## @brief 固定列表分页大小。
PAGE_SIZE = 20  # apis.md: pageSize 默认 20,最大 100

## @brief 返回当前 UTC 时间文本的无参函数。
NOW = lambda: datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

## @brief 演示管理员账号、明文密码和角色记录。
ADMINS = {
    "admin": {"password": "123456", "displayName": "系统管理员", "role": "ADMIN"},
    "readonly": {"password": "123456", "displayName": "只读管理员", "role": "ADMIN_READONLY"},
}

## @brief 访问令牌到账号的内存映射。
TOKENS = {}  # token -> username

# 预置电站与电桩
## @brief 预置和运行时新增的电站列表。
STATIONS = [
    {"id": 1, "name": "软件园充电站",
     "latitude": 38.889, "longitude": 121.537, "pricePerKwhFen": 98, "status": "active"},
    {"id": 2, "name": "星海广场充电站",
     "latitude": 38.877, "longitude": 121.590, "pricePerKwhFen": 108, "status": "active"},
    {"id": 3, "name": "机场快充站",
     "latitude": 38.958, "longitude": 121.541, "pricePerKwhFen": 128, "status": "active"},
]

## @brief 随机初始化时允许的占用与运维状态组合，重复项影响采样概率。
STATUS_PAIRS = [
    ("available", "online"),
    ("available", "online"),
    ("reserved", "online"),
    ("charging", "online"),
    ("charging", "fault"),
    ("available", "fault"),
    ("available", "offline"),
]
## @brief 按电桩 ID 索引的演示电桩字典。
CHARGERS = {}
## @brief 初始化电桩数据时递增的 ID 计数器。
_cid = 1
for st in STATIONS:
    for _ in range(8):
        ## @brief 本轮演示电桩是否采用快充类型。
        fast = random.random() < 0.5
        ## @var occupancy_status
        # @brief 本轮随机选择的占用状态。
        ## @var operational_status
        # @brief 本轮随机选择的独立运维状态。
        occupancy_status, operational_status = random.choice(STATUS_PAIRS)
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

## @brief 按用户 ID 索引的演示用户字典。
USERS = {}
## @brief 初始化用户数据时递增的 ID 计数器。
_uid = 1
for phone in ["13800138000", "13900139000", "15012345678", "18600001111", "17755667788"]:
    USERS[_uid] = {
        "id": _uid, "phone": phone, "nickname": f"用户{phone[-4:]}",
        "walletBalanceFen": random.randint(0, 50000),
        "status": "frozen" if phone == "15012345678" else "active",
        "createdAt": "2026-08-01T08:00:00Z",
    }
    _uid += 1


## @brief 生成过去 7 或 30 天的随机营收点。
# @param range_str 30d 生成 30 天，其他值生成 7 天。
# @return 包含 UTC 日起点、分计营收和订单数的字典列表。
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


## @brief 演示 HTTP 路由处理器；每个请求实例通过模块共享数据交互。
class Handler(BaseHTTPRequestHandler):
    ## @brief 输出精简请求日志，不使用基类的格式化消息。
    # @param fmt 基类提供的日志格式，当前忽略。
    # @param args 基类提供的格式化参数，当前忽略。
    def log_message(self, fmt, *args):  # 精简日志
        # command 和 path 由 BaseHTTPRequestHandler 提供，不在子类重新声明。
        ## @cond DOXYGEN_PYTHON_EXPRESSION
        sys.stderr.write("[mock] %s %s\n" % (self.command, self.path))
        ## @endcond

    # ---- helpers ----
    ## @brief 写入 UTF-8 JSON 响应及准确字节长度。
    # @param status HTTP 状态码。
    # @param payload 可被 JSON 序列化的响应内容。
    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    ## @brief 构造成功 data/meta 信封并发送。
    # @param data 响应业务数据。
    # @param status HTTP 状态码，默认 200。
    # @param extra_meta 附加分页等元数据。
    def ok(self, data, status=200, **extra_meta):
        meta = {"requestId": "mock-req"}
        meta.update(extra_meta)
        self.send_json(status, {"data": data, "meta": meta})

    ## @brief 构造错误信封并发送。
    # @param status HTTP 错误状态码。
    # @param code 业务错误码。
    # @param message 中文错误提示。
    def err(self, status, code, message):
        self.send_json(status, {"error": {"code": code, "message": message},
                                "meta": {"requestId": "mock-req"}})

    ## @brief 按 Content-Length 读取请求 JSON。
    # @return 解码后的 JSON 值；空正文或 JSON 语法错误返回空字典。
    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return {}

    ## @brief 查询演示令牌；此辅助函数未由下方路由统一调用。
    # @return 管理员记录；令牌无效时先发送 401 再返回 None。
    def auth(self):
        auth = self.headers.get("Authorization") or ""
        token = auth.removeprefix("Bearer ").strip()
        user = TOKENS.get(token)
        if not user:
            self.err(401, "UNAUTHORIZED", "未登录或令牌已过期")
            return None
        return ADMINS[user]

    ## @brief 按固定 PAGE_SIZE 切片，不读取请求中的 pageSize。
    # @param items 已筛选的完整列表。
    # @param q 查询字典，非法页码回退第一页。
    # @return items 包装对象与分页 meta 的二元组。
    def paginate(self, items, q):
        # apis.md: 列表响应 meta 带 page/pageSize/total/hasNext,page 从 1 起
        try:
            page = max(1, int(q.get("page", "1")))
        except (TypeError, ValueError):
            page = 1
        total = len(items)
        start = (page - 1) * PAGE_SIZE
        chunk = items[start:start + PAGE_SIZE]
        meta = {"page": page, "pageSize": PAGE_SIZE, "total": total,
                "hasNext": start + PAGE_SIZE < total}
        return {"items": chunk}, meta

    # ---- HTTP ----
    ## @brief 在共享锁内分派看板、电站、电桩和用户读取请求；未知路径返回 404。
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
            elif p == "/api/v1/admin/chargers":
                items = list(CHARGERS.values())
                if q.get("stationId"):
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

    ## @brief 处理演示登录、注销、重启与新增电站；注销仅应答，不移除令牌。
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

    ## @brief 更新演示用户的 active/frozen 状态，检查用户存在及状态枚举。
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
    ## @brief 命令行指定的本地监听端口，缺省为 8080。
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    ## @brief 仅绑定回环地址的多线程演示服务器。
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    # 立即输出启动提示；flush 是 print 的关键字参数。
    ## @cond DOXYGEN_PYTHON_EXPRESSION
    print(f"mock server on http://127.0.0.1:{port}/api/v1", flush=True)
    ## @endcond
    server.serve_forever()
