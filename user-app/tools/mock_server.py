#!/usr/bin/env python3
## @file mock_server.py
# @brief 用户端开发使用的本地有状态 HTTP 演示替身。
# @details 仅覆盖原型调试场景，部分样本仍使用旧字段（如 pricePerKwhFen、endedAt 和单一 status）。
# 主接口契约以 docs/apis.md 及 C++ contract_tests.cpp 为准；本文件不宣称完全等价于真实后端。
"""按 docs/apis.md 契约返回演示数据的本地 mock server，仅供客户端开发调试。"""

import json
import math
import re
import uuid
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
import urllib.parse

## @brief 只绑定本机回环地址，供本地客户端调试。
HOST = "127.0.0.1"
## @brief 演示 HTTP 监听端口。
PORT = 8080
## @brief 所有模拟业务路由共用的 API 路径前缀。
BASE = "/api/v1"
## @brief 用于复现 USER_FROZEN 登录错误的固定手机号。
FROZEN_PHONE = "19999999999"
## @brief 每真实秒对应的模拟充电分钟数。
SIM_MINUTES_PER_REAL_SECOND = 1.0

## @brief 手机号到用户字典的内存映射，进程退出即丢失。
users = {}
## @brief Bearer 令牌到手机号的内存映射；未模拟令牌过期。
tokens = {}
## @brief 预约标识到预约状态字典的内存映射。
reservations = {}
## @brief 订单标识到计量和账单字典的内存映射。
orders = {}
## @brief 用户标识到按发生顺序追加的钱包流水列表。
wallet_transactions = {}
## @brief 媒体路径到 (原始字节, MIME 类型) 的映射。
media = {}
## @brief 下一个模拟预约标识。
next_reservation_id = 81
## @brief 下一个模拟订单标识，历史种子和新订单共用。
next_order_id = 203
## @brief 下一个钱包流水标识。
next_transaction_id = 44

## @brief 用于演示启用/停用状态、位置与价格的站点样本。
STATIONS = [
    {"id": 1, "name": "软件园充电站", "address": "大连市甘井子区软件园路", "latitude": 38.889, "longitude": 121.537, "pricePerKwhFen": 98, "status": "active"},
    {"id": 2, "name": "星海广场充电站", "address": "大连市沙河口区星海广场", "latitude": 38.881, "longitude": 121.584, "pricePerKwhFen": 118, "status": "active"},
    {"id": 3, "name": "东港商务区充电站", "address": "大连市中山区东港商务区", "latitude": 38.928, "longitude": 121.663, "pricePerKwhFen": 88, "status": "active"},
    {"id": 4, "name": "机场前充电站", "address": "大连市甘井子区迎客路", "latitude": 38.959, "longitude": 121.539, "pricePerKwhFen": 105, "status": "inactive"},
]

## @brief 按站点标识分组的电桩样本，包含空闲、充电、预约、故障和离线状态。
CHARGERS = {
    1: [
        {"id": 11, "code": "S01-001", "type": "fast", "powerKw": 120.0, "status": "available", "totalChargeCount": 231, "totalChargeMinutes": 18420},
        {"id": 12, "code": "S01-002", "type": "fast", "powerKw": 120.0, "status": "charging", "totalChargeCount": 198, "totalChargeMinutes": 15031},
        {"id": 13, "code": "S01-003", "type": "slow", "powerKw": 7.0, "status": "reserved", "totalChargeCount": 87, "totalChargeMinutes": 22400},
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


## @brief 汇总站内电桩总数和当前空闲数。
# @param station 含 id 的电站字典。
# @return (电桩总数, available 状态数量) 元组。
def charger_summary(station):
    chargers = CHARGERS.get(station["id"], [])
    available = sum(1 for c in chargers if c["status"] == "available")
    return len(chargers), available


## @brief 通过 Haversine 公式估算两点球面距离。
# @param lat1 起点纬度，单位度。
# @param lng1 起点经度，单位度。
# @param lat2 终点纬度，单位度。
# @param lng2 终点经度，单位度。
# @return 以 km 为单位且四舍五入到两位小数的距离。
def distance_km(lat1, lng1, lat2, lng2):
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dl = math.radians(lng2 - lng1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dl / 2) ** 2
    return round(6371.0 * 2 * math.asin(math.sqrt(a)), 2)


## @brief 生成当前 UTC 时刻的接口文本。
# @return 秒精度的 YYYY-MM-DDTHH:MM:SSZ 字符串。
def now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


## @brief 解析演示服务器使用的 UTC 时间格式。
# @param text 秒精度且以 Z 结尾的时间字符串。
# @return 带 UTC 时区信息的 datetime；格式错误由 strptime 抛出 ValueError。
def parse_iso(text):
    return datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


## @brief 按标识查找内存中的电站。
# @param station_id 电站标识。
# @return 电站原字典引用；找不到时为 None。
def find_station(station_id):
    return next((s for s in STATIONS if s["id"] == station_id), None)


## @brief 遍历各站电桩集合按标识查找。
# @param charger_id 电桩标识。
# @return 电桩原字典引用；找不到时为 None。
def find_charger(charger_id):
    return next((c for chargers in CHARGERS.values() for c in chargers if c["id"] == charger_id), None)


## @brief 查找电桩所属站点标识。
# @param charger_id 目标电桩标识。
# @return 所属站点标识；找不到时为 None。
def station_id_of_charger(charger_id):
    return next((sid for sid, chargers in CHARGERS.items() if any(c["id"] == charger_id for c in chargers)), None)


## @brief 按真实流逝时间更新充电订单的模拟计量。
# @param order 被原地更新的订单字典；非 charging 状态直接返回。
# @details 每真实秒模拟一分钟，电量按额定功率乘 0.8 系数估算，再以订单单价计算分单位金额。
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


## @brief 查找用户尚未完成的充电或待支付订单。
# @param user 含 id 的当前用户字典。
# @return 首个 charging 或 awaiting_payment 订单的原引用，未找到时为 None。
def active_order_for(user):
    return next((o for o in orders.values()
                 if o["userId"] == user["id"] and o["status"] in ("charging", "awaiting_payment")), None)


## @brief 查找用户处于 active 状态的预约。
# @param user 含 id 的当前用户字典。
# @return 首个活动预约的原引用，未找到时为 None；此函数不主动处理过期。
def active_reservation_for(user):
    return next((r for r in reservations.values()
                 if r["userId"] == user["id"] and r["status"] == "active"), None)


## @brief 惰性失效已到期预约，并释放仍为 reserved 的对应电桩。
# @details 由相关请求入口触发，没有独立后台扫描线程；只改变内存数据。
def expire_stale_reservations():
    now = datetime.now(timezone.utc)
    for r in reservations.values():
        if r["status"] == "active" and parse_iso(r["expiresAt"]) <= now:
            r["status"] = "expired"
            charger = find_charger(r["chargerId"])
            if charger and charger["status"] == "reserved":
                charger["status"] = "available"


## @brief 为预约响应补充站名和电桩编号。
# @param reservation 内存预约原记录。
# @return 新建的浅拷贝字典，附加 stationName 和 chargerCode，不修改原记录。
def reservation_view(reservation):
    station = find_station(reservation["stationId"])
    charger = find_charger(reservation["chargerId"])
    view = dict(reservation)
    view["stationName"] = station["name"] if station else ""
    view["chargerCode"] = charger["code"] if charger else ""
    return view


## @brief 为订单响应补充站名和电桩编号。
# @param order 内存订单原记录。
# @return 新建的浅拷贝字典，附加 stationName 和 chargerCode，不修改原记录。
def order_view(order):
    station = find_station(order["stationId"])
    charger = find_charger(order["chargerId"])
    view = dict(order)
    view["stationName"] = station["name"] if station else ""
    view["chargerCode"] = charger["code"] if charger else ""
    return view


## @brief 为新用户创建两条已结算的演示历史订单。
# @param user 新建用户字典，历史订单归属其 id。
# @details 仅生成历史展示数据，不回溯扣减钱包或生成钱包流水。
def seed_order_history(user):
    global next_order_id
    station = STATIONS[1]
    samples = [(9, 41.2, 55, 21), (16, 18.6, 24, 22)]
    for days_ago, energy, minutes, charger_id in samples:
        started = datetime.now(timezone.utc) - timedelta(days=days_ago)
        ended = started + timedelta(minutes=minutes)
        orders[next_order_id] = {
            "id": next_order_id,
            "orderNo": started.strftime("%Y%m%d") + f"{next_order_id:06d}",
            "userId": user["id"],
            "stationId": station["id"],
            "chargerId": charger_id,
            "status": "settled",
            "startedAt": started.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "endedAt": ended.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "energyKwh": energy,
            "durationMinutes": minutes,
            "unitPriceFenPerKwh": station["pricePerKwhFen"],
            "amountFen": round(energy * station["pricePerKwhFen"]),
            "updatedAt": ended.strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
        next_order_id += 1


## @brief HTTP 演示路由处理器，每个请求实例共享模块级内存数据。
# @details ThreadingHTTPServer 并发处理请求；此轻量替身没有锁和事务，不作为业务并发正确性的验证依据。
class Handler(BaseHTTPRequestHandler):
    ## @brief 以简化格式输出本地请求日志。
    # @param fmt 标准库提供的格式字符串，本实现不使用它。
    # @param args 标准库传入的格式参数；仅打印首项。
    def log_message(self, fmt, *args):
        print(f"[mock] {args[0]}", flush=True)

    ## @brief 编码并写入 UTF-8 JSON HTTP 响应。
    # @param status HTTP 状态码。
    # @param data 成功时的业务数据，失败时为已构造的错误对象。
    # @param request_id 成功信封 meta.requestId，默认使用固定演示标识。
    # @details 小于 400 的响应封装为 data/meta；错误对象原样发送。
    def send_json(self, status, data, request_id="mock-rid"):
        body = json.dumps({"data": data, "meta": {"requestId": request_id}} if status < 400 else data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    ## @brief 构造客户端可按 code 判断的错误响应。
    # @param status HTTP 错误状态码。
    # @param code 业务错误码。
    # @param message 人类可读的错误原因。
    # @param details 可选的结构化错误细节；假值不写入响应。
    def send_error_code(self, status, code, message, details=None):
        error = {"code": code, "message": message}
        if details:
            error["details"] = details
        self.send_json(status, {"error": error})

    ## @brief 按 Content-Length 读取当前请求的 JSON 正文。
    # @return JSON 解码结果；没有正文时返回空字典，解析异常交由调用栈处理。
    def read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length)) if length else {}

    ## @brief 根据 Authorization 的 Bearer 令牌定位内存用户。
    # @return 令牌有效时返回用户原字典引用，否则为 None；不主动输出 HTTP 响应。
    def current_user(self):
        auth = self.headers.get("Authorization", "")
        match = re.fullmatch(r"Bearer (\S+)", auth)
        phone = tokens.get(match.group(1)) if match else None
        return users.get(phone) if phone else None

    ## @brief 要求当前请求已登录，失败时直接输出 401。
    # @return 登录用户原字典引用；未登录时为 None，调用方必须停止继续处理。
    def require_user(self):
        user = self.current_user()
        if not user:
            self.send_error_code(401, "UNAUTHORIZED", "未登录或令牌已过期")
        return user

    ## @brief 分发 PATCH /me 资料更新请求，其他路径返回 404。
    def do_PATCH(self):
        path = urlparse(self.path).path
        if path == f"{BASE}/me":
            self.handle_update_me()
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    ## @brief 按路径分发登录、上传、充值和预约/订单状态变更请求。
    # @details 数字资源标识通过正则匹配后转为整数，再交给具体处理函数。
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
        elif match := re.fullmatch(rf"{BASE}/reservations/(\d+)/cancel", path):
            self.handle_cancel_reservation(int(match.group(1)))
        elif match := re.fullmatch(rf"{BASE}/orders/(\d+)/stop", path):
            self.handle_stop_order(int(match.group(1)))
        elif match := re.fullmatch(rf"{BASE}/orders/(\d+)/settle", path):
            self.handle_settle_order(int(match.group(1)))
        else:
            self.send_error_code(404, "NOT_FOUND", "接口不存在")

    ## @brief 解析 URL 和查询参数并分发只读接口。
    # @details 查询字典保留 parse_qs 的字符串列表形式，各处理函数按接口约定取首值。
    def do_GET(self):
        url = urlparse(self.path)
        path, query = url.path, parse_qs(url.query)
        if path == f"{BASE}/me":
            self.handle_me()
        elif path == f"{BASE}/me/active-order":
            self.handle_active_order()
        elif path == f"{BASE}/me/wallet/transactions":
            self.handle_transactions()
        elif path == f"{BASE}/reservations":
            self.handle_list_reservations(query)
        elif match := re.fullmatch(rf"{BASE}/reservations/(\d+)", path):
            self.handle_get_reservation(int(match.group(1)))
        elif path == f"{BASE}/orders":
            self.handle_list_orders(query)
        elif path == f"{BASE}/forecasts":
            self.handle_forecasts(query)
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

    ## @brief 校验十一位手机号，自动创建演示用户并返回 Bearer 令牌。
    # @details 固定手机号 FROZEN_PHONE 返回冻结错误；新用户补充历史订单，已有用户保留内存钱包。
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
        is_new_user = user is None
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
            seed_order_history(user)
        token = uuid.uuid4().hex
        tokens[token] = phone
        self.send_json(200, {"accessToken": token, "tokenType": "Bearer", "expiresIn": 604800,
                             "isNewUser": is_new_user, "user": user})

    ## @brief 要求登录后返回当前用户字典。
    def handle_me(self):
        user = self.require_user()
        if user:
            self.send_json(200, user)

    ## @brief 要求登录并校验 1 到 30 字符昵称，原地更新用户资料。
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

    ## @brief 从简化 multipart 请求中提取头像文件并保存到内存。
    # @details 只用于演示：按 boundary 和 filename 查找首个文件；根据 PNG 魔数选择 MIME，否则按 JPEG 处理。
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

    ## @brief 发送之前上传并保存在内存中的媒体字节。
    # @param path 完整媒体路径，用作 media 字典键。
    # @details 文件不存在返回 404；此演示入口不要求登录。
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

    ## @brief 按新到旧顺序返回当前用户的全部钱包流水。
    def handle_transactions(self):
        user = self.require_user()
        if not user:
            return
        self.send_json(200, list(reversed(wallet_transactions.get(user["id"], []))))

    ## @brief 校验坐标并构造腾讯地图 URI 路线链接。
    # @param query 查询参数：fromLatitude/fromLongitude、toLatitude/toLongitude、mode 与可选起终点名称。
    # @details 距离来自球面估算，durationSec 固定为 780，polyline 为空；未调用实际路线服务。
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

    ## @brief 返回当前用户首个活动订单，没有活动订单时返回空 data。
    # @details 充电中订单先更新模拟电量与金额，再补充站名和电桩编号。
    def handle_active_order(self):
        user = self.require_user()
        if not user:
            return
        order = active_order_for(user)
        if order and order["status"] == "charging":
            update_order_meter(order)
        self.send_json(200, order_view(order) if order else None)

    ## @brief 检查订单归属并返回刷新后的模拟计量。
    # @param order_id 路径中的订单标识；不存在或属于他人时统一返回 404。
    def handle_get_order(self, order_id):
        user = self.require_user()
        if not user:
            return
        order = orders.get(order_id)
        if not order or order["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "订单不存在")
            return
        update_order_meter(order)
        self.send_json(200, order_view(order))

    ## @brief 按当前用户筛选历史订单，按开始时间降序分页。
    # @param query 可选 status、page 与 pageSize；页码至少 1，每页限制在 1 到 50 条。
    def handle_list_orders(self, query):
        user = self.require_user()
        if not user:
            return
        items = [order_view(o) for o in orders.values() if o["userId"] == user["id"]]
        status = query.get("status", [None])[0]
        if status:
            items = [o for o in items if o["status"] == status]
        items.sort(key=lambda o: o["startedAt"], reverse=True)
        try:
            page = max(int(query.get("page", ["1"])[0]), 1)
            page_size = min(max(int(query.get("pageSize", ["20"])[0]), 1), 50)
        except ValueError:
            self.send_error_code(400, "VALIDATION_ERROR", "分页参数不正确")
            return
        items = items[(page - 1) * page_size: page * page_size]
        self.send_json(200, items)

    ## @brief 按当前站点电桩数生成可重复解释的演示负荷预测。
    # @param query horizon 为 1h/6h/24h，可用 stationId 限定站点。
    # @details 不调用预测模型，负荷和置信度由固定公式生成，只遍历 active 站点。
    def handle_forecasts(self, query):
        user = self.require_user()
        if not user:
            return
        horizon = query.get("horizon", ["1h"])[0]
        if horizon not in ("1h", "6h", "24h"):
            self.send_error_code(400, "VALIDATION_ERROR", "horizon 仅支持 1h/6h/24h")
            return
        station_id = query.get("stationId", [None])[0]
        hours = {"1h": 1, "6h": 6, "24h": 24}[horizon]
        forecast_at = (datetime.now(timezone.utc) + timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%SZ")
        points = []
        for s in STATIONS:
            if s["status"] != "active":
                continue
            if station_id and s["id"] != int(station_id):
                continue
            total, available = charger_summary(s)
            points.append({
                "stationId": s["id"],
                "forecastAt": forecast_at,
                "loadKw": round(total * 42.5 - available * 38.0, 1),
                "availableChargerCount": available,
                "confidence": round(0.72 + (s["id"] * 7 % 21) / 100.0, 2),
            })
        self.send_json(200, {"modelVersion": "baseline-v1", "generatedAt": now_iso(),
                             "horizon": horizon, "points": points})

    ## @brief 验证分单位充值金额，立即增加钱包并生成成功流水。
    # @details 接受 100 到 10000000 的整数分；无第三方支付流程，响应为新建流水。
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

    ## @brief 校验空闲电桩和用户无活动业务后创建预约并占用电桩。
    # @details 先处理过期预约，保留时间默认 15 分钟、最高 60 分钟；内存记录与电桩状态同步修改。
    def handle_create_reservation(self):
        user = self.require_user()
        if not user:
            return
        expire_stale_reservations()
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
        self.send_json(201, reservation_view(reservation))

    ## @brief 返回当前用户预约记录，先处理过期再按标识降序排列。
    # @param query 可选 status 状态过滤；此接口不分页。
    def handle_list_reservations(self, query):
        user = self.require_user()
        if not user:
            return
        expire_stale_reservations()
        items = [reservation_view(r) for r in reservations.values() if r["userId"] == user["id"]]
        status = query.get("status", [None])[0]
        if status:
            items = [r for r in items if r["status"] == status]
        items.sort(key=lambda r: r["id"], reverse=True)
        self.send_json(200, items)

    ## @brief 检查归属并返回当前预约快照。
    # @param reservation_id 预约标识；不存在或属于他人时统一返回 404。
    def handle_get_reservation(self, reservation_id):
        user = self.require_user()
        if not user:
            return
        expire_stale_reservations()
        reservation = reservations.get(reservation_id)
        if not reservation or reservation["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "预约不存在")
            return
        self.send_json(200, reservation_view(reservation))

    ## @brief 仅允许取消当前用户的 active 预约并释放电桩。
    # @param reservation_id 要取消的预约标识。
    # @details 先处理到期状态，非 active 预约返回 INVALID_STATE_TRANSITION。
    def handle_cancel_reservation(self, reservation_id):
        user = self.require_user()
        if not user:
            return
        expire_stale_reservations()
        reservation = reservations.get(reservation_id)
        if not reservation or reservation["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "预约不存在")
            return
        if reservation["status"] != "active":
            self.send_error_code(409, "INVALID_STATE_TRANSITION", "预约当前不可取消")
            return
        reservation["status"] = "cancelled"
        charger = find_charger(reservation["chargerId"])
        if charger and charger["status"] == "reserved":
            charger["status"] = "available"
        self.send_json(200, reservation_view(reservation))

    ## @brief 从空闲电桩或当前用户的有效预约创建充电订单。
    # @details 有未完成订单时拒绝；找到预约记录时校验用户、电桩和 active 状态，成功后预约变 used、电桩变 charging。
    def handle_create_order(self):
        user = self.require_user()
        if not user:
            return
        expire_stale_reservations()
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
            "endedAt": None,
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

    ## @brief 冻结当前计量并将 charging 订单推进到 awaiting_payment。
    # @param order_id 当前用户要停止的订单标识。
    # @details 先计算最新电量，再写结束时间并释放电桩；钱包在后续结算时扣款。
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
        order["endedAt"] = now_iso()
        order["updatedAt"] = now_iso()
        find_charger(order["chargerId"])["status"] = "available"
        self.send_json(200, order_view(order))

    ## @brief 从钱包扣除待支付账单，生成扣款流水并标记已结算。
    # @param order_id 当前用户要结算的订单标识。
    # @details 已 settled 的订单直接返回，避免顺序重复调用二次扣款；余额不足返回 422，不修改钱包。
    def handle_settle_order(self, order_id):
        user = self.require_user()
        if not user:
            return
        order = orders.get(order_id)
        if not order or order["userId"] != user["id"]:
            self.send_error_code(404, "NOT_FOUND", "订单不存在")
            return
        if order["status"] == "settled":
            self.send_json(200, order_view(order))
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
        self.send_json(200, order_view(order))

    ## @brief 返回启用站点及当前空闲统计，按球面距离升序排列。
    # @param query 可选 latitude 和 longitude，默认大连市中心坐标。
    # @details onlineRate 固定为 1.0，未模拟实际在线率；过期预约在查询前失效。
    def handle_nearby(self, query):
        expire_stale_reservations()
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

    ## @brief 返回指定站点及电桩统计。
    # @param station_id 路径中的站点标识；不存在时返回 404。
    # @details 不过滤 inactive 站点，onlineRate 为固定演示值。
    def handle_station(self, station_id):
        expire_stale_reservations()
        station = next((s for s in STATIONS if s["id"] == station_id), None)
        if not station:
            self.send_error_code(404, "NOT_FOUND", "电站不存在")
            return
        total, available = charger_summary(station)
        data = dict(station)
        data.update({"chargerCount": total, "availableChargerCount": available, "onlineRate": 1.0})
        self.send_json(200, data)

    ## @brief 返回站内电桩浅拷贝并补充所属站点和固定更新时间。
    # @param station_id 路径中的站点标识；不存在时返回 404。
    def handle_station_chargers(self, station_id):
        expire_stale_reservations()
        if not any(s["id"] == station_id for s in STATIONS):
            self.send_error_code(404, "NOT_FOUND", "电站不存在")
            return
        chargers = [dict(c, stationId=station_id, updatedAt="2026-09-01T08:30:00Z") for c in CHARGERS.get(station_id, [])]
        self.send_json(200, chargers)


## @brief 启动本地线程 HTTP 服务；端口被占用时报告原因并以状态 1 退出。
if __name__ == "__main__":
    try:
        ## @brief 本地线程 HTTP 服务实例，使用 Handler 处理请求。
        server = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError:
        print(f"[mock] 端口 {PORT} 已被占用：mock server 可能已在运行，或执行 fuser -k {PORT}/tcp 释放端口", flush=True)
        raise SystemExit(1)
    print(f"[mock] serving http://{HOST}:{PORT}{BASE}", flush=True)
    server.serve_forever()
