# Logging

All three executables install the shared Qt message handler at startup. Logs are
UTF-8, one event per line, written to stderr. No additional service or logging
library is required. The handler also formats Qt's own diagnostic messages.

```text
[2026-09-09T06:23:45.127Z] [ERROR] [evcharger.backend.http] [thread=0x7f12 name=main] [Backend::HttpServer::start()] [object=http-server] Unable to listen | source=http.cpp:42
```

Fields are UTC time with milliseconds, severity, Qt logging category, current
thread ID (hexadecimal) and name, function, object name, and English message.
Missing names/context appear as `-`. Warning/error/fatal messages include source
file and line when available. Function/source metadata is retained in release
builds. Newlines, carriage returns, tabs, and backslashes are escaped so each
record occupies one physical line. Writes from multiple threads are serialized.
Object names are explicitly supplied at project logging call sites; Qt framework
messages without an object use `object=-`.

## Levels and filtering

- `DEBUG`: request details and diagnostic/polling activity.
- `INFO`: startup/shutdown, user operations, successful state transitions.
- `WARN`: rejected requests, invalid data, recoverable failures.
- `ERROR`: operations that cannot complete, database/network/startup failures.
- `FATAL`: unrecoverable Qt fatal diagnostics; Qt retains its termination behavior.

Categories separate each application's subsystems. Diagnostic categories default
to INFO and above where declared with `QtInfoMsg`. Use Qt's standard rules to
choose what to display; no application-specific configuration layer is needed:

```bash
QT_LOGGING_RULES='evcharger.*.debug=true' ./build/biz-core/biz-core
QT_LOGGING_RULES='evcharger.user.*.debug=true' ./build/user-app/user-app
QT_LOGGING_RULES='evcharger.ops.*.info=false' ./build/ops-app/ops-app
./build/biz-core/biz-core 2>backend.log
```

For an exact category inventory, search the sources for `Q_LOGGING_CATEGORY`.
Messages must not include passwords, authorization headers, service/API keys,
authentication tokens, raw request/response bodies, or personal profile values.
Prefer operation names, numeric IDs, counts, HTTP status and safe error codes.
Avoid unconditional INFO messages on animation frames or periodic polling.

## Adding a log

Include `<evcharger/logging.h>`, declare a category at file scope, and use the
stream macros. Pass `this` (or another QObject) when appropriate, otherwise
`nullptr`. Give important QObject instances stable descriptive object names.
Do not access an object from a foreign thread merely to log its name.

```cpp
Q_LOGGING_CATEGORY(chargingLog, "evcharger.backend.charging", QtInfoMsg)

EV_LOG_INFO(chargingLog, this) << "Charging started" << "orderId=" << orderId;
EV_LOG_WARNING(chargingLog, nullptr) << "Request rejected" << "status=" << status;
```

The macros preserve Qt category filtering, including skipping message-expression
evaluation when disabled. The common logger is infrastructure shared by the
applications; it contains no application or domain dependencies.
