/** @file
 * @brief 管理员接口请求、响应信封解析与多请求汇总实现。
 */
#include "api_client.h"
#include <QElapsedTimer>
#include <evcharger/logging.h>

Q_LOGGING_CATEGORY(opsNetwork, "evcharger.ops.network", QtInfoMsg)
Q_LOGGING_CATEGORY(opsAuth, "evcharger.ops.auth", QtInfoMsg)
Q_LOGGING_CATEGORY(opsOperations, "evcharger.ops.operations", QtInfoMsg)

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
#include <QTime>
#include <QTimeZone>
#include <QUuid>

namespace ops {
	namespace {

		/** @brief 将时间区间转为 UTC ISO 文本查询参数。
		 * @param from 统计起始时刻。
		 * @param to 统计结束时刻。
		 * @return 含 UTC ISO 格式 from/to 的查询参数。
		 */
		QUrlQuery intervalQuery(const QDateTime &from, const QDateTime &to) {
			QUrlQuery query;
			query.addQueryItem(QStringLiteral("from"), from.toUTC().toString(Qt::ISODate));
			query.addQueryItem(QStringLiteral("to"), to.toUTC().toString(Qt::ISODate));
			return query;
		}

		/** @brief 格式化给定时刻的 UTC 偏移为带符号的 HH:mm。
		 * @param dateTime 要格式化时区偏移的时刻。
		 * @return 带正负号的 HH:mm 文本。
		 */
		QString utcOffset(const QDateTime &dateTime) {
			const int offsetSeconds = dateTime.offsetFromUtc();
			const int absoluteMinutes = qAbs(offsetSeconds) / 60;
			return QStringLiteral("%1%2:%3")
				.arg(offsetSeconds < 0 ? QLatin1String("-") : QLatin1String("+"))
				.arg(absoluteMinutes / 60, 2, 10, QLatin1Char('0'))
				.arg(absoluteMinutes % 60, 2, 10, QLatin1Char('0'));
		}

	} // namespace

	/// @brief 提取 data.items 数组为对象列表；非数组返回空列表。
	QList<QJsonObject> ApiResult::items() const {
		QList<QJsonObject> list;
		const auto v = data.value(QLatin1String("items"));
		if (v.isArray()) {
			const auto arr = v.toArray();
			list.reserve(arr.size());
			for (const auto &e : arr)
				list.append(e.toObject());
		}
		return list;
	}

	ApiClient::ApiClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {
		setObjectName(QStringLiteral("opsApiClient"));
		m_nam->setObjectName(QStringLiteral("opsNetworkManager"));
		EV_LOG_INFO(opsNetwork, this) << "API client initialized; request timeout_ms=15000";
	}

	/// @brief 设置后续请求使用的基础 URL，不自动补充分隔符。
	void ApiClient::setBaseUrl(const QString &url) {
		m_baseUrl = url;
		EV_LOG_INFO(opsNetwork, this) << "API base URL changed";
	}

	/// @brief 拼接基础地址和接口路径，再附加查询参数。
	QUrl ApiClient::buildUrl(const QString &path, const QUrlQuery &query) const {
		QUrl url(m_baseUrl + path);
		if (!query.isEmpty())
			url.setQuery(query);
		return url;
	}

	/// @brief 构造带请求 ID、可选令牌和 15 秒传输超时的异步 HTTP 请求。
	void ApiClient::send(const QString &method, const QString &path, const QUrlQuery &query,
						 const QJsonObject &body,
						 const std::function<void(const ApiResult &)> &handler) {
		QNetworkRequest request(buildUrl(path, query));
		const QByteArray requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
		EV_LOG_DEBUG(opsNetwork, this) << "Sending request; id=" << requestId << "method=" << method << "path=" << path;
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		request.setRawHeader(QByteArrayLiteral("X-Request-Id"),
							 requestId);
		if (!m_token.isEmpty())
			request.setRawHeader(QByteArrayLiteral("Authorization"), "Bearer " + m_token.toUtf8());
		// 读操作失败可见化的前提:请求不能无限挂起(契约要求超时与 503 处理)
		request.setTransferTimeout(15000); // 15s:慢于演示场景一切正常请求

		QNetworkReply *reply = nullptr;
		if (method == QLatin1String("GET"))
			reply = m_nam->get(request);
		else if (method == QLatin1String("POST"))
			reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		else if (method == QLatin1String("PATCH"))
			reply = m_nam->sendCustomRequest(request, "PATCH",
											 QJsonDocument(body).toJson(QJsonDocument::Compact));
		else
			reply = m_nam->sendCustomRequest(request, method.toUtf8());
		reply->setParent(this);
		reply->setObjectName(QStringLiteral("opsRequest-%1").arg(QString::fromUtf8(requestId)));

		handleEnvelope(reply, handler);
	}

	/// @brief 在响应完成时解包数据和错误；401 发出认证失效信号，回调后延迟销毁响应。
	void ApiClient::handleEnvelope(QNetworkReply *reply,
								   const std::function<void(const ApiResult &)> &handler) {
		QElapsedTimer elapsed;
		elapsed.start();
		connect(reply, &QNetworkReply::finished, this,
				[this, reply, handler, elapsed] {
					reply->deleteLater();
					ApiResult result;
					result.httpStatus =
						reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

					const QByteArray body = reply->readAll();
					QJsonDocument doc = QJsonDocument::fromJson(body);
					if (result.httpStatus == 0) {
						if (reply->error() == QNetworkReply::OperationCanceledError) {
							result.errorCode = QStringLiteral("TIMEOUT");
							result.errorMessage = QStringLiteral("请求超时,请稍后重试");
						} else {
							result.errorCode = QStringLiteral("SERVICE_UNAVAILABLE");
							result.errorMessage =
								QStringLiteral("无法连接业务服务层,请确认服务端已启动");
						}
					} else if (doc.isObject()) {
						const QJsonObject root = doc.object();
						const QJsonObject err = root.value(QLatin1String("error")).toObject();
						if (!err.isEmpty()) {
							result.errorCode = jsonStr(err, "code");
							result.errorMessage = jsonStr(err, "message");
						} else {
							// data 可能是对象,也可能是数组;统一包一层便于信号传递
							const QJsonValue dataVal = root.value(QLatin1String("data"));
							if (dataVal.isObject())
								result.data = dataVal.toObject();
							else if (dataVal.isArray())
								result.data.insert(QStringLiteral("items"), dataVal.toArray());
							// 列表接口的分页 meta(apis.md: meta.page/pageSize/total/hasNext)
							const QJsonObject metaObj =
								root.value(QLatin1String("meta")).toObject();
							if (metaObj.contains(QLatin1String("page"))) {
								result.meta.valid = true;
								result.meta.page = static_cast<int>(jsonI64(metaObj, "page", 1));
								result.meta.pageSize =
									static_cast<int>(jsonI64(metaObj, "pageSize", 20));
								result.meta.total = jsonI64(metaObj, "total");
								result.meta.hasNext =
									metaObj.value(QLatin1String("hasNext")).toBool();
							}
						}
					}
					result.ok = result.httpStatus >= 200 && result.httpStatus < 300 &&
								result.errorCode.isEmpty();

					if (result.ok) {
						EV_LOG_DEBUG(opsNetwork, reply) << "Request completed; status=" << result.httpStatus << "elapsed_ms=" << elapsed.elapsed();
					} else {
						EV_LOG_WARNING(opsNetwork, reply) << "Request failed; status=" << result.httpStatus << "network_error=" << static_cast<int>(reply->error()) << "elapsed_ms=" << elapsed.elapsed();
					}
					if (!doc.isObject() && result.httpStatus != 0) {
						EV_LOG_WARNING(opsNetwork, reply) << "Response is not a JSON object";
					}
					if (result.httpStatus == 401) {
						EV_LOG_WARNING(opsAuth, this) << "Authentication rejected; returning to login";
						emit authenticationChanged(false);
					}
					handler(result);
				});
	}

	// ---- 认证 ----

	/// @brief 异步提交管理员凭据；成功保存令牌和角色并发送 loginSucceeded。
	void ApiClient::login(const QString &username, const QString &password) {
		EV_LOG_INFO(opsAuth, this) << "Administrator login requested";
		QJsonObject body;
		body.insert(QStringLiteral("username"), username);
		body.insert(QStringLiteral("password"), password);
		send(QStringLiteral("POST"), QStringLiteral("/auth/admin/login"), {}, body,
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 EV_LOG_WARNING(opsAuth, this) << "Administrator login failed; status=" << r.httpStatus;
					 emit loginFailed(r.errorCode, r.errorMessage);
					 return;
				 }
				 m_token = jsonStr(r.data, "accessToken");
				 const QJsonObject user = r.data.value(QLatin1String("admin")).toObject();
				 m_role = jsonStr(user, "role", QStringLiteral("ADMIN"));
				 AdminUser admin;
				 admin.id = jsonI64(user, "id");
				 admin.username = jsonStr(user, "username");
				 admin.displayName = jsonStr(user, "displayName");
				 admin.role = m_role;
				 admin.status = jsonStr(user, "status");
				 EV_LOG_INFO(opsAuth, this) << "Administrator login succeeded; administrator_id=" << admin.id << "writable=" << canWrite();
				 emit loginSucceeded(admin);
			 });
	}

	/// @brief 有令牌时发送注销请求，立即清空本地认证信息并发出失去认证信号。
	void ApiClient::logout() {
		EV_LOG_INFO(opsAuth, this) << "Administrator logout requested; clearing local session";
		if (!m_token.isEmpty())
			send(QStringLiteral("POST"), QStringLiteral("/auth/logout"), {}, {}, [](const ApiResult &) {});
		m_token.clear();
		m_role.clear();
		emit authenticationChanged(false);
	}

	// ---- 看板 ----

	/// @brief 并发获取累计、今日、本月营收和资源统计，全部完成后发出汇总信号。
	void ApiClient::fetchDashboardSummary() {
		/// @brief 一轮汇总请求的共享状态；由各回调共同持有直到全部结束。
		struct SummaryState {
			DashboardSummary summary; ///< 已完成子请求写入的指标。
			QString errorCode;		  ///< 第一个非空错误码。
			int pending = 0;		  ///< 尚未完成的子请求数量。
		};

		const QDateTime now = QDateTime::currentDateTime();
		const QDateTime todayStart(now.date(), QTime(0, 0), now.timeZone());
		const QDateTime monthStart(QDate(now.date().year(), now.date().month(), 1),
								   QTime(0, 0), now.timeZone());
		auto state = QSharedPointer<SummaryState>::create();
		state->summary.asOf = now.toUTC().toString(Qt::ISODate);
		state->pending = canWrite() ? 6 : 5;
		// 每个子请求无论成功失败都递减 pending；只读角色不请求管理员用户列表。
		const auto complete = [this, state](const ApiResult &result, const auto &apply) {
			if (result.ok) {
				apply(result);
			} else if (state->errorCode.isEmpty()) {
				state->errorCode = result.errorCode;
			}
			if (--state->pending == 0) {
				EV_LOG_DEBUG(opsNetwork, this) << "Dashboard aggregation completed; success=" << state->errorCode.isEmpty();
				emit dashboardSummaryFetched(state->summary, state->errorCode);
			}
		};

		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/revenue"), {}, {},
			 [complete, state](const ApiResult &result) {
				 complete(result, [state](const ApiResult &value) {
					 state->summary.totalRevenueFen = jsonI64(value.data, "revenueFen");
				 });
			 });
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/revenue"),
			 intervalQuery(todayStart, now), {}, [complete, state](const ApiResult &result) {
				 complete(result, [state](const ApiResult &value) {
					 state->summary.todayRevenueFen = jsonI64(value.data, "revenueFen");
				 });
			 });
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/revenue"),
			 intervalQuery(monthStart, now), {}, [complete, state](const ApiResult &result) {
				 complete(result, [state](const ApiResult &value) {
					 state->summary.monthRevenueFen = jsonI64(value.data, "revenueFen");
				 });
			 });
		QUrlQuery pageQuery;
		pageQuery.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));
		send(QStringLiteral("GET"), QStringLiteral("/admin/stations"), pageQuery, {},
			 [complete, state](const ApiResult &result) {
				 complete(result, [state](const ApiResult &value) {
					 state->summary.stationCount = value.meta.total;
				 });
			 });
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/charger-status"), {}, {},
			 [complete, state](const ApiResult &result) {
				 complete(result, [state](const ApiResult &value) {
					 state->summary.chargerCount = jsonI64(value.data, "total");
					 const qint64 online = jsonI64(
						 value.data.value(QLatin1String("operational")).toObject(), "online");
					 if (state->summary.chargerCount > 0) {
						 state->summary.onlineRate =
							 static_cast<double>(online) / state->summary.chargerCount;
					 }
				 });
			 });
		if (canWrite()) {
			send(QStringLiteral("GET"), QStringLiteral("/admin/users"), pageQuery, {},
				 [complete, state](const ApiResult &result) {
					 complete(result, [state](const ApiResult &value) {
						 state->summary.userCount = value.meta.total;
					 });
				 });
		}
	}

	/// @brief 请求近 7 或 30 日营收；按本地日界限转换 UTC 区间并传入当前时区偏移。
	void ApiClient::fetchRevenueSeries(const QString &range) {
		const QDateTime now = QDateTime::currentDateTime();
		const int days = range == QLatin1String("30d") ? 30 : 7;
		const QDateTime from(now.date().addDays(1 - days), QTime(0, 0), now.timeZone());
		QUrlQuery seriesQuery = intervalQuery(from, now);
		seriesQuery.addQueryItem(QStringLiteral("utcOffset"), utcOffset(now));
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/revenue-series"), seriesQuery, {},
			 [this, range](const ApiResult &r) {
				 if (!r.ok) {
					 emit revenueSeriesFetched(range, {}, r.errorCode);
					 return;
				 }
				 QList<RevenuePoint> points;
				 const auto arr = r.data.value(QLatin1String("points")).toArray();
				 for (const auto &e : arr) {
					 const QJsonObject o = e.toObject();
					 RevenuePoint p;
					 p.bucketStart = jsonStr(o, "bucketStart");
					 p.revenueFen = jsonI64(o, "revenueFen");
					 p.orderCount = jsonI64(o, "orderCount");
					 points.append(p);
				 }
				 emit revenueSeriesFetched(range, points, {});
			 });
	}

	/// @brief 请求两个独立状态维度，校验响应结构后计算各状态占比。
	void ApiClient::fetchChargerStatus() {
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/charger-status"), {}, {},
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 emit chargerStatusFetched({}, r.errorCode);
					 return;
				 }
				 const QJsonValue totalValue = r.data.value(QLatin1String("total"));
				 const QJsonValue occupancyValue = r.data.value(QLatin1String("occupancy"));
				 const QJsonValue operationalValue = r.data.value(QLatin1String("operational"));
				 if (!totalValue.isDouble() || !occupancyValue.isObject() ||
					 !operationalValue.isObject()) {
					 EV_LOG_WARNING(opsNetwork, this) << "Charger status response has invalid fields";
					 emit chargerStatusFetched({}, QStringLiteral("INVALID_RESPONSE"));
					 return;
				 }

				 ChargerStatusSnapshot snapshot;
				 snapshot.total = jsonI64(r.data, "total");
				 const auto append = [&snapshot](QList<ChargerStatusCount> &rows,
												 const QJsonObject &group, const char *name) {
					 ChargerStatusCount row;
					 row.status = QLatin1String(name);
					 row.count = jsonI64(group, name);
					 row.percent = snapshot.total > 0
									   ? static_cast<double>(row.count) / snapshot.total
									   : 0.0;
					 rows.append(row);
				 };
				 const QJsonObject occupancy = occupancyValue.toObject();
				 const QJsonObject operational = operationalValue.toObject();
				 append(snapshot.occupancy, occupancy, "available");
				 append(snapshot.occupancy, occupancy, "reserved");
				 append(snapshot.occupancy, occupancy, "charging");
				 append(snapshot.operational, operational, "online");
				 append(snapshot.operational, operational, "fault");
				 append(snapshot.operational, operational, "offline");
				 emit chargerStatusFetched(snapshot, {});
			 });
	}

	// ---- 电桩 ----

	/// @brief 分页查询电桩；fault/offline 作为运维筛选，其他非空值作为占用筛选。
	void ApiClient::fetchChargers(const QString &statusFilter, int page) {
		QUrlQuery query;
		if (statusFilter == QLatin1String("fault") || statusFilter == QLatin1String("offline"))
			query.addQueryItem(QStringLiteral("operationalStatus"), statusFilter);
		else if (!statusFilter.isEmpty())
			query.addQueryItem(QStringLiteral("occupancyStatus"), statusFilter);
		query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
		send(QStringLiteral("GET"), QStringLiteral("/admin/chargers"), query, {},
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 emit chargersFetched({}, PageMeta{}, r.errorCode);
					 return;
				 }
				 QList<Charger> chargers;
				 for (const auto &e : r.data.value(QLatin1String("items")).toArray()) {
					 const QJsonObject o = e.toObject();
					 chargers.append(chargerFromJson(o));
				 }
				 EV_LOG_DEBUG(opsNetwork, this) << "List response ready; resource=chargers count=" << chargers.size() << "page=" << r.meta.page;
				 emit chargersFetched(chargers, r.meta, {});
			 });
	}

	/// @brief 向指定电站创建电桩，完成后发送 chargerMutationFinished。
	void ApiClient::createCharger(qint64 stationId, const ChargerForm &form) {
		EV_LOG_INFO(opsOperations, this) << "Charger creation requested; resource_id=" << stationId;
		QJsonObject body;
		body.insert(QStringLiteral("type"), form.type);
		body.insert(QStringLiteral("powerKw"), form.powerKw);
		send(QStringLiteral("POST"),
			 QStringLiteral("/admin/stations/%1/chargers").arg(stationId), {}, body,
			 [this](const ApiResult &r) {
				 const qint64 chargerId = r.ok ? jsonI64(r.data, "id") : 0;
				 EV_LOG_INFO(opsOperations, this) << "Charger mutation completed; success=" << r.ok << "charger_id=" << chargerId;
				 emit chargerMutationFinished(
					 QStringLiteral("create"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	/// @brief 修改电桩类型、功率和运维状态，不修改归属和占用状态。
	void ApiClient::updateCharger(qint64 chargerId, const ChargerForm &form) {
		EV_LOG_INFO(opsOperations, this) << "Charger update requested; resource_id=" << chargerId;
		QJsonObject body;
		body.insert(QStringLiteral("type"), form.type);
		body.insert(QStringLiteral("powerKw"), form.powerKw);
		body.insert(QStringLiteral("operationalStatus"), form.operationalStatus);
		send(QStringLiteral("PATCH"), QStringLiteral("/admin/chargers/%1").arg(chargerId), {},
			 body, [this, chargerId](const ApiResult &r) {
				 EV_LOG_INFO(opsOperations, this) << "Charger mutation completed; success=" << r.ok << "charger_id=" << chargerId;
				 emit chargerMutationFinished(
					 QStringLiteral("update"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	/// @brief 删除指定电桩，结果通过 chargerMutationFinished 返回。
	void ApiClient::deleteCharger(qint64 chargerId) {
		EV_LOG_INFO(opsOperations, this) << "Charger deletion requested; resource_id=" << chargerId;
		send(QStringLiteral("DELETE"), QStringLiteral("/admin/chargers/%1").arg(chargerId), {},
			 {}, [this, chargerId](const ApiResult &r) {
				 EV_LOG_INFO(opsOperations, this) << "Charger mutation completed; success=" << r.ok << "charger_id=" << chargerId;
				 emit chargerMutationFinished(
					 QStringLiteral("delete"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	/// @brief 发送远程重启请求，结果通过 commandFinished 返回。
	void ApiClient::restartCharger(qint64 chargerId, const QString &reason) {
		EV_LOG_INFO(opsOperations, this) << "Charger restart requested; resource_id=" << chargerId;
		Q_UNUSED(reason)
		send(QStringLiteral("POST"), QStringLiteral("/admin/chargers/%1/restart").arg(chargerId),
			 {}, {}, [this, chargerId](const ApiResult &r) {
				 EV_LOG_INFO(opsOperations, this) << "Charger restart completed; charger_id=" << chargerId << "success=" << r.ok;
				 emit commandFinished(chargerId, r.ok,
									  r.ok ? QStringLiteral("重启指令已下发") : r.errorMessage);
			 });
	}

	// ---- 电站 ----

	/// @brief 查询电站并为各站请求在线数量；全部补充请求完成后发送列表。
	void ApiClient::fetchStations(const QString &search, int page) {
		QUrlQuery query;
		if (!search.isEmpty())
			query.addQueryItem(QStringLiteral("name"), search);
		query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
		send(QStringLiteral("GET"), QStringLiteral("/admin/stations"), query, {},
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 emit stationsFetched({}, PageMeta{}, r.errorCode);
					 return;
				 }
				 QList<StationSummary> stations;
				 for (const auto &e : r.data.value(QLatin1String("items")).toArray()) {
					 const QJsonObject o = e.toObject();
					 StationSummary s;
					 s.id = jsonI64(o, "id");
					 s.name = jsonStr(o, "name");
					 s.latitude = jsonDbl(o, "latitude");
					 s.longitude = jsonDbl(o, "longitude");
					 s.pricePerKwhFen = jsonI64(o, "priceFenPerKwh");
					 s.chargerCount = jsonI64(o, "chargerCount");
					 s.availableChargerCount = jsonI64(o, "availableChargerCount");
					 s.status = jsonStr(o, "status");
					 stations.append(s);
				 }
				 if (stations.isEmpty()) {
					 EV_LOG_DEBUG(opsNetwork, this) << "List response ready; resource=stations count=" << stations.size() << "page=" << r.meta.page;
					 emit stationsFetched(stations, r.meta, {});
					 return;
				 }
				 struct StationState {
					 QList<StationSummary> stations;
					 PageMeta meta;
					 int pending = 0;
				 };
				 auto state = QSharedPointer<StationState>::create();
				 state->stations = stations;
				 state->meta = r.meta;
				 state->pending = stations.size();
				 // 在线数量补充失败时该站保持默认零在线率，不丢弃已成功取得的主列表。
				 for (qsizetype index = 0; index < stations.size(); ++index) {
					 QUrlQuery onlineQuery;
					 onlineQuery.addQueryItem(QStringLiteral("operationalStatus"),
											  QStringLiteral("online"));
					 onlineQuery.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));
					 send(QStringLiteral("GET"),
						  QStringLiteral("/admin/stations/%1/chargers").arg(stations.at(index).id),
						  onlineQuery, {}, [this, state, index](const ApiResult &onlineResult) {
							  if (onlineResult.ok && state->stations[index].chargerCount > 0) {
								  state->stations[index].onlineRate =
									  static_cast<double>(onlineResult.meta.total) / state->stations[index].chargerCount;
							  }
							  if (--state->pending == 0) {
								  EV_LOG_DEBUG(opsNetwork, this) << "Station list enrichment completed; count=" << state->stations.size();
								  emit stationsFetched(state->stations, state->meta, {});
							  }
						  });
				 }
			 });
	}

	/// @brief 请求指定电站的第一页电桩，pageSize 固定为 100，不继续翻页。
	void ApiClient::fetchStationChargers(qint64 stationId) {
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("100"));
		send(QStringLiteral("GET"), QStringLiteral("/admin/stations/%1/chargers").arg(stationId),
			 query, {},
			 [this, stationId](const ApiResult &r) {
				 QList<Charger> chargers;
				 if (r.ok) {
					 for (const auto &e : r.data.value(QLatin1String("items")).toArray()) {
						 const QJsonObject o = e.toObject();
						 chargers.append(chargerFromJson(o));
					 }
				 }
				 emit stationChargersFetched(stationId, chargers, r.errorCode);
			 });
	}

	/// @brief 仅发送站名、坐标和分计价单价，完成后发送 stationCreated。
	void ApiClient::createStation(const StationForm &form) {
		EV_LOG_INFO(opsOperations, this) << "Station creation requested";
		QJsonObject body;
		body.insert(QStringLiteral("name"), form.name);
		body.insert(QStringLiteral("latitude"), form.latitude);
		body.insert(QStringLiteral("longitude"), form.longitude);
		body.insert(QStringLiteral("priceFenPerKwh"), static_cast<double>(form.pricePerKwhFen));

		send(QStringLiteral("POST"), QStringLiteral("/admin/stations"), {}, body,
			 [this](const ApiResult &r) {
				 EV_LOG_INFO(opsOperations, this) << "Station creation completed; success=" << r.ok;
				 emit stationCreated(r.ok, r.ok ? QString() : r.errorCode);
			 });
	}

	// ---- 用户 ----

	/// @brief 按手机号查询用户列表并返回分页信息。
	void ApiClient::fetchUsers(const QString &phoneSearch, int page) {
		QUrlQuery query;
		if (!phoneSearch.isEmpty())
			query.addQueryItem(QStringLiteral("phone"), phoneSearch);
		query.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
		send(QStringLiteral("GET"), QStringLiteral("/admin/users"), query, {},
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 emit usersFetched({}, PageMeta{}, r.errorCode);
					 return;
				 }
				 QList<AdminUserRow> users;
				 for (const auto &e : r.data.value(QLatin1String("items")).toArray()) {
					 const QJsonObject o = e.toObject();
					 AdminUserRow u;
					 u.id = jsonI64(o, "id");
					 u.phone = jsonStr(o, "phone");
					 u.nickname = jsonStr(o, "nickname");
					 u.walletBalanceFen = jsonI64(o, "walletBalanceFen");
					 u.status = jsonStr(o, "status", QStringLiteral("active"));
					 u.createdAt = jsonStr(o, "createdAt");
					 users.append(u);
				 }
				 EV_LOG_DEBUG(opsNetwork, this) << "List response ready; resource=users count=" << users.size() << "page=" << r.meta.page;
				 emit usersFetched(users, r.meta, {});
			 });
	}

	/// @brief 将用户状态提交为 frozen 或 active。
	void ApiClient::setUserStatus(qint64 userId, bool frozen) {
		EV_LOG_INFO(opsOperations, this) << "User status update requested; resource_id=" << userId;
		QJsonObject body;
		body.insert(QStringLiteral("status"), frozen ? QStringLiteral("frozen")
													 : QStringLiteral("active"));
		send(QStringLiteral("PATCH"), QStringLiteral("/admin/users/%1").arg(userId), {}, body,
			 [this](const ApiResult &r) { EV_LOG_INFO(opsOperations, this) << "User status update completed; success=" << r.ok; emit userStatusChanged(r.ok, r.errorCode); });
	}

} // namespace ops
