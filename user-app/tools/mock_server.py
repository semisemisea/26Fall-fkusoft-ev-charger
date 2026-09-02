#!/usr/bin/env python3
"""按 docs/apis.md 契约返回演示数据的本地 mock server，仅供客户端开发调试。"""

import json
import math
import re
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HOST = "127.0.0.1"
PORT = 8080
BASE = "/api/v1"
FROZEN_PHONE = "19999999999"

users = {}
tokens = {}

STATIONS = [
    {"id": 1, "name": "软件园充电站", "address": "大连市甘井子区软件园路", "latitude": 38.889, "longitude": 121.537, "pricePerKwhFen": 98, "status": "active"},
    {"id": 2, "name": "星海广场充电站", "address": "大连市沙河口区星海广场", "latitude": 38.881, "longitude": 121.584, "pricePerKwhFen": 118, "status": "active"},
    {"id": 3, "name": "东港商务区充电站", "address": "大连市中山区东港商务区", "latitude": 38.928, "longitude": 121.663, "pricePerKwhFen": 88, "status": "active"},
    {"id": 4, "name": "机场前充电站", "address": "大连市甘井子区迎客路", "latitude": 38.959, "longitude": 121.539, "pricePerKwhFen": 105, "status": "inactive"},
]

CHARGERS = {
    1: [
        {"id": 11, "code": "S01-001", "type": "fast", "powerKw": 120.0, "status": "available", "totalChargeCount": 231, "totalChargeMinutes": 18420},
        {"id": 12, "code": "S01-002", "type": "fast", "powerKw": 120.0, "status": "charging", "totalChargeCount": 198, "totalChargeMinutes": 15031},
        {"id": 13, "code": "S01-003", "type": "slow", "powerKw": 7.0, "status": "available", "totalChargeCount": 87, "totalChargeMinutes": 22400},
        {"id": 14, "code": "S01-004", "type": "slow", "powerKw": 7.0, "status": "fault", "totalChargeCount": 64, "totalChargeMinutes": 15980},
    ],
    2: [
        {"id": 21, "code": "S02-001", "type": "fast", "powerKw": 180.0, "status": "available", "totalChargeCount": 402, "totalChargeMinutes": 27110},
        {"id": 22, "code": "S02-002", "type": "fast", "powerKw": 180.0, "status": "available", "totalChargeCount": 355, "totalChargeMinutes": 24008},
        {"id": 23, "code": "S02-003", "type": "slow", "powerKw": 7.0, "status": "charging", "totalChargeCount": 120, "totalChargeMinutes": 31050},
    ],
    3: [
        {"id": 31, "code": "S03-001", "type": "fast", "powerKw": 120.0, "status": "offline", "totalChargeCount": 12, "totalChargeMinutes": 900},
    ],
}


def charger_summary(station):
    chargers = CHARGERS.get(station["id"], [])
    available = sum(1 for c in chargers if c["status"] == "available")
    return len(chargers), available


def distance_km(lat1, lng1, lat2, lng2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dl = math.radians(lng2 - lng1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dl / 2) ** 2
    return round(6371.0 * 2 * math.asin(math.sqrt(a)), 2)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[mock] {args[0]}", flush=True)

    def send_json(self, status, data, request_id="mock-rid"):
        body = json.dumps({"data": data, "meta": {"requestId": request_id}} if status < 400 else data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_code(self, status, code, message, details=None):
        error = {"code": code, "message": message}
        if details:
            error["details"] = details
        self.send_json(status, {"error": error})

    def read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length)) if length else {}

    def current_user(self):
        auth = self.headers.get("Authorization", "")
        match = re.fullmatch(r"Bearer (\S+)", auth)
        phone = tokens.get(match.group(1)) if match else None
        return users.get(phone) if phone else None

    def require_user(self):
        user = self.current_user()
        if not user:
            self.send_error_code(401, "UNAUTHORIZED", "未登录或令牌已过期")
        return user

    def do_POST(self):
        path = urlparse(self.path).path
        if path == f"{BASE}/auth/user/login":
            self.handle_login()
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    def do_GET(self):
        url = urlparse(self.path)
        path, query = url.path, parse_qs(url.query)
        if path == f"{BASE}/me":
            self.handle_me()
        elif path == f"{BASE}/stations/nearby":
            self.handle_nearby(query)
        elif re.fullmatch(rf"{BASE}/stations/(\d+)", path):
            self.handle_station(int(path.rsplit("/", 1)[1]))
        elif re.fullmatch(rf"{BASE}/stations/(\d+)/chargers", path):
            self.handle_station_chargers(int(path.split("/")[3]))
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    def handle_login(self):
        body = self.read_body()
        phone = body.get("phone", "")
        if not re.fullmatch(r"\d{11}", str(phone)):
            self.send_error_code(400, "VALIDATION_ERROR", "手机号必须为 11 位数字")
            return
        if phone == FROZEN_PHONE:
            self.send_error_code(403, "USER_FROZEN", "用户已被冻结")
            return
        user = users.get(phone)
        if not user:
            user = {
                "id": 100 + len(users),
                "phone": phone,
                "nickname": f"用户{phone[-4:]}",
                "avatarUrl": None,
                "walletBalanceFen": 0,
                "status": "active",
                "createdAt": "2026-09-01T08:30:00Z",
            }
            users[phone] = user
        token = uuid.uuid4().hex
        tokens[token] = phone
        self.send_json(200, {"accessToken": token, "tokenType": "Bearer", "expiresIn": 604800, "user": user})

    def handle_me(self):
        user = self.require_user()
        if user:
            self.send_json(200, user)

    def handle_nearby(self, query):
        try:
            lat = float(query.get("latitude", ["38.914"])[0])
            lng = float(query.get("longitude", ["121.614"])[0])
        except ValueError:
            self.send_error_code(400, "VALIDATION_ERROR", "经纬度格式不正确")
            return
        items = []
        for s in STATIONS:
            if s["status"] != "active":
                continue
            total, available = charger_summary(s)
            item = dict(s)
            item.update({"chargerCount": total, "availableChargerCount": available, "onlineRate": 1.0,
                         "distanceKm": distance_km(lat, lng, s["latitude"], s["longitude"])})
            items.append(item)
        items.sort(key=lambda s: s["distanceKm"])
        self.send_json(200, items)

    def handle_station(self, station_id):
        station = next((s for s in STATIONS if s["id"] == station_id), None)
        if not station:
            self.send_error_code(404, "NOT_FOUND", "电站不存在")
            return
        total, available = charger_summary(station)
        data = dict(station)
        data.update({"chargerCount": total, "availableChargerCount": available, "onlineRate": 1.0})
        self.send_json(200, data)

    def handle_station_chargers(self, station_id):
        if not any(s["id"] == station_id for s in STATIONS):
            self.send_error_code(404, "NOT_FOUND", "电站不存在")
            return
        chargers = [dict(c, stationId=station_id, updatedAt="2026-09-01T08:30:00Z") for c in CHARGERS.get(station_id, [])]
        self.send_json(200, chargers)


if __name__ == "__main__":
    try:
        server = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError:
        print(f"[mock] 端口 {PORT} 已被占用：mock server 可能已在运行，或执行 fuser -k {PORT}/tcp 释放端口", flush=True)
        raise SystemExit(1)
    print(f"[mock] serving http://{HOST}:{PORT}{BASE}", flush=True)
    server.serve_forever()
