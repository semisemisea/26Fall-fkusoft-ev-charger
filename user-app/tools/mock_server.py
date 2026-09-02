#!/usr/bin/env python3
"""按 docs/apis.md 契约返回演示数据的本地 mock server，仅供客户端开发调试。"""

import json
import math
import re
import uuid
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
import urllib.parse

HOST = "127.0.0.1"
PORT = 8080
BASE = "/api/v1"
FROZEN_PHONE = "19999999999"
SIM_MINUTES_PER_REAL_SECOND = 1.0

users = {}
tokens = {}
reservations = {}
orders = {}
wallet_transactions = {}
media = {}
next_reservation_id = 81
next_order_id = 203
next_transaction_id = 44

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


def now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(text):
    return datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def find_station(station_id):
    return next((s for s in STATIONS if s["id"] == station_id), None)


def find_charger(charger_id):
    return next((c for chargers in CHARGERS.values() for c in chargers if c["id"] == charger_id), None)


def station_id_of_charger(charger_id):
    return next((sid for sid, chargers in CHARGERS.items() if any(c["id"] == charger_id for c in chargers)), None)


def update_order_meter(order):
    if order["status"] != "charging":
        return
    started = parse_iso(order["startedAt"])
    real_seconds = (datetime.now(timezone.utc) - started).total_seconds()
    sim_minutes = real_seconds * SIM_MINUTES_PER_REAL_SECOND
    charger = find_charger(order["chargerId"])
    order["energyKwh"] = round(charger["powerKw"] * (sim_minutes / 60) * 0.8, 1)
    order["durationMinutes"] = int(sim_minutes)
    order["amountFen"] = round(order["energyKwh"] * order["unitPriceFenPerKwh"])
    order["updatedAt"] = now_iso()


def active_order_for(user):
    return next((o for o in orders.values()
                 if o["userId"] == user["id"] and o["status"] in ("charging", "awaiting_payment")), None)


def active_reservation_for(user):
    return next((r for r in reservations.values()
                 if r["userId"] == user["id"] and r["status"] == "active"), None)


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

    def do_PATCH(self):
        path = urlparse(self.path).path
        if path == f"{BASE}/me":
            self.handle_update_me()
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    def do_POST(self):
        path = urlparse(self.path).path
        if path == f"{BASE}/auth/user/login":
            self.handle_login()
        elif path == f"{BASE}/me/avatar":
            self.handle_avatar()
        elif path == f"{BASE}/me/wallet/topups":
            self.handle_topup()
        elif path == f"{BASE}/reservations":
            self.handle_create_reservation()
        elif path == f"{BASE}/orders":
            self.handle_create_order()
        elif match := re.fullmatch(rf"{BASE}/orders/(\d+)/stop", path):
            self.handle_stop_order(int(match.group(1)))
        elif match := re.fullmatch(rf"{BASE}/orders/(\d+)/settle", path):
            self.handle_settle_order(int(match.group(1)))
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    def do_GET(self):
        url = urlparse(self.path)
        path, query = url.path, parse_qs(url.query)
        if path == f"{BASE}/me":
            self.handle_me()
        elif path == f"{BASE}/me/active-order":
            self.handle_active_order()
        elif path == f"{BASE}/me/wallet/transactions":
            self.handle_transactions()
        elif path == f"{BASE}/locations/routes":
            self.handle_routes(query)
        elif path.startswith(f"{BASE}/media/"):
            self.handle_media(path)
        elif match := re.fullmatch(rf"{BASE}/orders/(\d+)", path):
            self.handle_get_order(int(match.group(1)))
        elif path == f"{BASE}/stations/nearby":
            self.handle_nearby(query)
        elif re.fullmatch(rf"{BASE}/stations/(\d+)", path):
            self.handle_station(int(path.rsplit("/", 1)[1]))
        elif match := re.fullmatch(rf"{BASE}/stations/(\d+)/chargers", path):
            self.handle_station_chargers(int(match.group(1)))
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

    def handle_update_me(self):
        user = self.require_user()
        if not user:
            return
        body = self.read_body()
        nickname = body.get("nickname")
        if not isinstance(nickname, str) or not 1 <= len(nickname) <= 30:
            self.send_error_code(400, "VALIDATION_ERROR", "昵称须为 1..30 个字符")
            return
        user["nickname"] = nickname
        self.send_json(200, user)

    def handle_avatar(self):
        user = self.require_user()
        if not user:
            return
        content_type = self.headers.get("Content-Type", "")
        match = re.search(r'boundary="?([^;"]+)"?', content_type)
        if not match:
            self.send_error_code(400, "VALIDATION_ERROR", "需要 multipart/form-data")
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        payload = None
        for part in body.split(match.group(1).encode()):
            if b"filename" not in part or b"\r\n\r\n" not in part:
                continue
            payload = part.split(b"\r\n\r\n", 1)[1]
            if payload.endswith(b"\r\n"):
                payload = payload[:-2]
            break
        if not payload:
            self.send_error_code(400, "VALIDATION_ERROR", "缺少头像文件")
            return
        media_type = "image/png" if payload.startswith(b"\x89PNG") else "image/jpeg"
        avatar_path = f"{BASE}/media/avatars/{user['phone']}"
        media[avatar_path] = (payload, media_type)
        user["avatarUrl"] = avatar_path
        self.send_json(200, {"avatarUrl": avatar_path})

    def handle_media(self, path):
        item = media.get(path)
        if not item:
            self.send_error_code(404, "NOT_FOUND", "媒体文件不存在")
            return
        payload, media_type = item
        self.send_response(200)
        self.send_header("Content-Type", media_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def handle_transactions(self):
        user = self.require_user()
        if not user:
            return
        self.send_json(200, list(reversed(wallet_transactions.get(user["id"], []))))

    def handle_routes(self, query):
        try:
            from_lat = float(query["fromLatitude"][0])
            from_lng = float(query["fromLongitude"][0])
            to_lat = float(query["toLatitude"][0])
            to_lng = float(query["toLongitude"][0])
        except (KeyError, ValueError):
            self.send_error_code(400, "VALIDATION_ERROR", "缺少或非法的经纬度参数")
            return
        mode = query.get("mode", ["driving"])[0]
        if mode not in ("driving", "walking"):
            self.send_error_code(400, "VALIDATION_ERROR", "mode 仅支持 driving/walking")
            return
        from_name = urllib.parse.quote(query.get("fromName", ["我的位置"])[0])
        to_name = urllib.parse.quote(query.get("toName", ["目的地"])[0])
        map_url = (f"https://apis.map.qq.com/uri/v1/routeplan?from={from_name}&fromcoord={from_lat},{from_lng}"
                   f"&to={to_name}&tocoord={to_lat},{to_lng}&coord_type=1&mode={mode}&policy=0&referer=ev-charger-mock")
        data = {"mode": mode, "distanceM": int(distance_km(from_lat, from_lng, to_lat, to_lng) * 1000),
                "durationSec": 780, "polyline": [], "provider": "tencent", "mapUrl": map_url}
        self.send_json(200, data)

    def handle_active_order(self):
        user = self.require_user()
        if not user:
            return
        order = active_order_for(user)
        if order and order["status"] == "charging":
            update_order_meter(order)
        self.send_json(200, order)

    def handle_get_order(self, order_id):
        user = self.require_user()
        if not user:
            return
        order = orders.get(order_id)
        if not order or order["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "订单不存在")
            return
        update_order_meter(order)
        self.send_json(200, order)

    def handle_topup(self):
        user = self.require_user()
        if not user:
            return
        body = self.read_body()
        amount = body.get("amountFen")
        if not isinstance(amount, int) or not 100 <= amount <= 10000000:
            self.send_error_code(400, "VALIDATION_ERROR", "充值金额须为 100..10000000 的整数分")
            return
        global next_transaction_id
        user["walletBalanceFen"] += amount
        transaction = {
            "id": next_transaction_id,
            "type": "top_up",
            "amountFen": amount,
            "balanceAfterFen": user["walletBalanceFen"],
            "status": "succeeded",
            "createdAt": now_iso(),
        }
        next_transaction_id += 1
        wallet_transactions.setdefault(user["id"], []).append(transaction)
        self.send_json(201, transaction)

    def handle_create_reservation(self):
        user = self.require_user()
        if not user:
            return
        body = self.read_body()
        charger = find_charger(body.get("chargerId", 0))
        if not charger:
            self.send_error_code(404, "NOT_FOUND", "电桩不存在")
            return
        if charger["status"] != "available":
            self.send_error_code(409, "CHARGER_UNAVAILABLE", "电桩当前不可用", {"chargerId": charger["id"], "currentStatus": charger["status"]})
            return
        if active_reservation_for(user) or active_order_for(user):
            self.send_error_code(409, "ACTIVE_ORDER_EXISTS", "您已有进行中的预约或订单")
            return
        global next_reservation_id
        hold_minutes = min(int(body.get("holdMinutes", 15)), 60)
        reservation = {
            "id": next_reservation_id,
            "chargerId": charger["id"],
            "stationId": station_id_of_charger(charger["id"]),
            "userId": user["id"],
            "status": "active",
            "startAt": now_iso(),
            "expiresAt": (datetime.now(timezone.utc) + timedelta(minutes=hold_minutes)).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "createdAt": now_iso(),
        }
        next_reservation_id += 1
        reservations[reservation["id"]] = reservation
        charger["status"] = "reserved"
        self.send_json(201, reservation)

    def handle_create_order(self):
        user = self.require_user()
        if not user:
            return
        body = self.read_body()
        charger = find_charger(body.get("chargerId", 0))
        if not charger:
            self.send_error_code(404, "NOT_FOUND", "电桩不存在")
            return
        if active_order_for(user):
            self.send_error_code(409, "ACTIVE_ORDER_EXISTS", "您有未完成的充电订单，请先结算")
            return
        reservation = reservations.get(body.get("reservationId"))
        if reservation:
            if reservation["userId"] != user["id"] or reservation["status"] != "active" or reservation["chargerId"] != charger["id"]:
                self.send_error_code(409, "INVALID_STATE_TRANSITION", "预约无效或不属于当前用户")
                return
            if charger["status"] != "reserved":
                self.send_error_code(409, "CHARGER_UNAVAILABLE", "电桩当前不可用")
                return
            reservation["status"] = "used"
        elif charger["status"] != "available":
            self.send_error_code(409, "CHARGER_UNAVAILABLE", "电桩当前不可用", {"chargerId": charger["id"], "currentStatus": charger["status"]})
            return
        global next_order_id
        station = find_station(station_id_of_charger(charger["id"]))
        order = {
            "id": next_order_id,
            "orderNo": datetime.now(timezone.utc).strftime("%Y%m%d") + f"{next_order_id:06d}",
            "userId": user["id"],
            "stationId": station["id"],
            "chargerId": charger["id"],
            "status": "charging",
            "startedAt": now_iso(),
            "energyKwh": 0.0,
            "durationMinutes": 0,
            "unitPriceFenPerKwh": station["pricePerKwhFen"],
            "amountFen": 0,
            "updatedAt": now_iso(),
        }
        next_order_id += 1
        orders[order["id"]] = order
        charger["status"] = "charging"
        self.send_json(201, order)

    def handle_stop_order(self, order_id):
        user = self.require_user()
        if not user:
            return
        order = orders.get(order_id)
        if not order or order["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "订单不存在")
            return
        if order["status"] != "charging":
            self.send_error_code(409, "INVALID_STATE_TRANSITION", "订单当前不可停止")
            return
        update_order_meter(order)
        order["status"] = "awaiting_payment"
        order["updatedAt"] = now_iso()
        find_charger(order["chargerId"])["status"] = "available"
        self.send_json(200, order)

    def handle_settle_order(self, order_id):
        user = self.require_user()
        if not user:
            return
        order = orders.get(order_id)
        if not order or order["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "订单不存在")
            return
        if order["status"] == "settled":
            self.send_json(200, order)
            return
        if order["status"] != "awaiting_payment":
            self.send_error_code(409, "INVALID_STATE_TRANSITION", "订单当前不可结算")
            return
        if user["walletBalanceFen"] < order["amountFen"]:
            self.send_error_code(422, "INSUFFICIENT_BALANCE", "钱包余额不足，请先充值", {"amountFen": order["amountFen"], "balanceFen": user["walletBalanceFen"]})
            return
        global next_transaction_id
        user["walletBalanceFen"] -= order["amountFen"]
        wallet_transactions.setdefault(user["id"], []).append({
            "id": next_transaction_id,
            "type": "charge_debit",
            "amountFen": -order["amountFen"],
            "balanceAfterFen": user["walletBalanceFen"],
            "status": "succeeded",
            "createdAt": now_iso(),
        })
        next_transaction_id += 1
        order["status"] = "settled"
        order["updatedAt"] = now_iso()
        self.send_json(200, order)

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
