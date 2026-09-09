CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    phone TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    avatar BLOB,
    avatar_mime TEXT,
    balance_fen INTEGER NOT NULL DEFAULT 0 CHECK(balance_fen >= 0),
    status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','frozen')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS admins (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_salt BLOB NOT NULL,
    password_hash BLOB NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('ADMIN','ADMIN_READONLY')),
    status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','disabled')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS access_tokens (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    token_hash BLOB NOT NULL UNIQUE,
    principal_type TEXT NOT NULL CHECK(principal_type IN ('user','admin')),
    principal_id INTEGER NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('USER','ADMIN','ADMIN_READONLY')),
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    revoked_at TEXT
);

-- statement-breakpoint

CREATE INDEX IF NOT EXISTS access_tokens_lookup ON access_tokens(token_hash, expires_at, revoked_at);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS service_credentials (
    service_name TEXT PRIMARY KEY,
    token_hash BLOB NOT NULL,
    role TEXT NOT NULL CHECK(role = 'SERVICE'),
    updated_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS stations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    latitude REAL NOT NULL CHECK(latitude BETWEEN -90 AND 90),
    longitude REAL NOT NULL CHECK(longitude BETWEEN -180 AND 180),
    price_fen_per_kwh INTEGER NOT NULL CHECK(price_fen_per_kwh > 0),
    status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','inactive')),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    deleted_at TEXT
);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS chargers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL REFERENCES stations(id),
    type TEXT NOT NULL CHECK(type IN ('fast','slow')),
    power_w INTEGER NOT NULL CHECK(power_w > 0),
    occupancy_status TEXT NOT NULL DEFAULT 'available' CHECK(occupancy_status IN ('available','reserved','charging')),
    operational_status TEXT NOT NULL DEFAULT 'online' CHECK(operational_status IN ('online','fault','offline')),
    total_charge_count INTEGER NOT NULL DEFAULT 0 CHECK(total_charge_count >= 0),
    total_charge_seconds INTEGER NOT NULL DEFAULT 0 CHECK(total_charge_seconds >= 0),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    deleted_at TEXT
);

-- statement-breakpoint

CREATE INDEX IF NOT EXISTS chargers_station ON chargers(station_id);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS reservations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    station_id INTEGER NOT NULL REFERENCES stations(id),
    charger_id INTEGER NOT NULL REFERENCES chargers(id),
    status TEXT NOT NULL CHECK(status IN ('active','used','cancelled','expired')),
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE UNIQUE INDEX IF NOT EXISTS one_active_reservation_per_user ON reservations(user_id) WHERE status = 'active';

-- statement-breakpoint

CREATE UNIQUE INDEX IF NOT EXISTS one_active_reservation_per_charger ON reservations(charger_id) WHERE status = 'active';

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    station_id INTEGER NOT NULL REFERENCES stations(id),
    charger_id INTEGER NOT NULL REFERENCES chargers(id),
    reservation_id INTEGER REFERENCES reservations(id),
    status TEXT NOT NULL CHECK(status IN ('charging','awaiting_payment','settled')),
    power_w INTEGER NOT NULL CHECK(power_w > 0),
    unit_price_fen_per_kwh INTEGER NOT NULL CHECK(unit_price_fen_per_kwh > 0),
    started_at TEXT NOT NULL,
    stopped_at TEXT,
    settled_at TEXT,
    duration_seconds INTEGER,
    energy_ws INTEGER,
    amount_fen INTEGER,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE UNIQUE INDEX IF NOT EXISTS one_open_order_per_user ON orders(user_id) WHERE status IN ('charging','awaiting_payment');

-- statement-breakpoint

CREATE UNIQUE INDEX IF NOT EXISTS one_charging_order_per_charger ON orders(charger_id) WHERE status = 'charging';

-- statement-breakpoint

CREATE INDEX IF NOT EXISTS orders_created ON orders(created_at DESC, id DESC);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS wallet_transactions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    order_id INTEGER REFERENCES orders(id),
    type TEXT NOT NULL CHECK(type IN ('top_up','charge_debit')),
    amount_fen INTEGER NOT NULL,
    balance_after_fen INTEGER NOT NULL CHECK(balance_after_fen >= 0),
    created_at TEXT NOT NULL
);

-- statement-breakpoint

CREATE UNIQUE INDEX IF NOT EXISTS one_charge_debit_per_order ON wallet_transactions(order_id) WHERE type = 'charge_debit';

-- statement-breakpoint

CREATE INDEX IF NOT EXISTS wallet_transactions_user ON wallet_transactions(user_id, created_at DESC, id DESC);

-- statement-breakpoint

CREATE TABLE IF NOT EXISTS idempotency_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    principal_type TEXT NOT NULL,
    principal_id INTEGER NOT NULL,
    method TEXT NOT NULL,
    path TEXT NOT NULL,
    idempotency_key TEXT NOT NULL,
    request_hash BLOB NOT NULL,
    http_status INTEGER NOT NULL,
    response_data BLOB NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    UNIQUE(principal_type, principal_id, method, path, idempotency_key)
);

-- statement-breakpoint

CREATE INDEX IF NOT EXISTS idempotency_expiry ON idempotency_records(expires_at);
