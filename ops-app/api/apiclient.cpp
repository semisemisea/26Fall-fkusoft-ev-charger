#include "apiclient.h"

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

		QUrlQuery intervalQuery(const QDateTime &from, const QDateTime &to) {
			QUrlQuery query;
			query.addQueryItem(QStringLiteral("from"), from.toUTC().toString(Qt::ISODate));
			query.addQueryItem(QStringLiteral("to"), to.toUTC().toString(Qt::ISODate));
			return query;
		}

		QString utcOffset(const QDateTime &dateTime) {
			const int offsetSeconds = dateTime.offsetFromUtc();
			const int absoluteMinutes = qAbs(offsetSeconds) / 60;
			return QStringLiteral("%1%2:%3")
				.arg(offsetSeconds < 0 ? QLatin1String("-") : QLatin1String("+"))
				.arg(absoluteMinutes / 60, 2, 10, QLatin1Char('0'))
				.arg(absoluteMinutes % 60, 2, 10, QLatin1Char('0'));
		}

	} // namespace

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

	ApiClient::ApiClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

	void ApiClient::setBaseUrl(const QString &url) { m_baseUrl = url; }

	QUrl ApiClient::buildUrl(const QString &path, const QUrlQuery &query) const {
		QUrl url(m_baseUrl + path);
		if (!query.isEmpty())
			url.setQuery(query);
		return url;
	}

	void ApiClient::send(const QString &method, const QString &path, const QUrlQuery &query,
						 const QJsonObject &body,
						 const std::function<void(const ApiResult &)> &handler) {
		QNetworkRequest request(buildUrl(path, query));
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		request.setRawHeader(QByteArrayLiteral("X-Request-Id"),
							 QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
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

		handleEnvelope(reply, handler);
	}

	void ApiClient::handleEnvelope(QNetworkReply *reply,
								   const std::function<void(const ApiResult &)> &handler) {
		connect(reply, &QNetworkReply::finished, this,
				[this, reply, handler] {
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

					if (result.httpStatus == 401)
						emit authenticationChanged(false);
					handler(result);
				});
	}

	// ---- 认证 ----

	void ApiClient::login(const QString &username, const QString &password) {
		QJsonObject body;
		body.insert(QStringLiteral("username"), username);
		body.insert(QStringLiteral("password"), password);
		send(QStringLiteral("POST"), QStringLiteral("/auth/admin/login"), {}, body,
			 [this](const ApiResult &r) {
				 if (!r.ok) {
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
				 emit loginSucceeded(admin);
			 });
	}

	void ApiClient::logout() {
		if (!m_token.isEmpty())
			send(QStringLiteral("POST"), QStringLiteral("/auth/logout"), {}, {}, [](const ApiResult &) {});
		m_token.clear();
		m_role.clear();
		emit authenticationChanged(false);
	}

	// ---- 看板 ----

	void ApiClient::fetchDashboardSummary() {
		struct SummaryState {
			DashboardSummary summary;
			QString errorCode;
			int pending = 0;
		};

		const QDateTime now = QDateTime::currentDateTime();
		const QDateTime todayStart(now.date(), QTime(0, 0), now.timeZone());
		const QDateTime monthStart(QDate(now.date().year(), now.date().month(), 1),
								   QTime(0, 0), now.timeZone());
		auto state = QSharedPointer<SummaryState>::create();
		state->summary.asOf = now.toUTC().toString(Qt::ISODate);
		state->pending = canWrite() ? 6 : 5;
		const auto complete = [this, state](const ApiResult &result, const auto &apply) {
			if (result.ok) {
				apply(result);
			} else if (state->errorCode.isEmpty()) {
				state->errorCode = result.errorCode;
			}
			if (--state->pending == 0) {
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
				 emit chargersFetched(chargers, r.meta, {});
			 });
	}

	void ApiClient::createCharger(qint64 stationId, const ChargerForm &form) {
		QJsonObject body;
		body.insert(QStringLiteral("type"), form.type);
		body.insert(QStringLiteral("powerKw"), form.powerKw);
		send(QStringLiteral("POST"),
			 QStringLiteral("/admin/stations/%1/chargers").arg(stationId), {}, body,
			 [this](const ApiResult &r) {
				 const qint64 chargerId = r.ok ? jsonI64(r.data, "id") : 0;
				 emit chargerMutationFinished(
					 QStringLiteral("create"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	void ApiClient::updateCharger(qint64 chargerId, const ChargerForm &form) {
		QJsonObject body;
		body.insert(QStringLiteral("type"), form.type);
		body.insert(QStringLiteral("powerKw"), form.powerKw);
		body.insert(QStringLiteral("operationalStatus"), form.operationalStatus);
		send(QStringLiteral("PATCH"), QStringLiteral("/admin/chargers/%1").arg(chargerId), {},
			 body, [this, chargerId](const ApiResult &r) {
				 emit chargerMutationFinished(
					 QStringLiteral("update"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	void ApiClient::deleteCharger(qint64 chargerId) {
		send(QStringLiteral("DELETE"), QStringLiteral("/admin/chargers/%1").arg(chargerId), {},
			 {}, [this, chargerId](const ApiResult &r) {
				 emit chargerMutationFinished(
					 QStringLiteral("delete"), chargerId, r.ok,
					 r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
			 });
	}

	void ApiClient::restartCharger(qint64 chargerId, const QString &reason) {
		Q_UNUSED(reason)
		send(QStringLiteral("POST"), QStringLiteral("/admin/chargers/%1/restart").arg(chargerId),
			 {}, {}, [this, chargerId](const ApiResult &r) {
				 emit commandFinished(chargerId, r.ok,
									  r.ok ? QStringLiteral("重启指令已下发") : r.errorMessage);
			 });
	}

	// ---- 电站 ----

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
					 s.address = jsonStr(o, "address");
					 s.latitude = jsonDbl(o, "latitude");
					 s.longitude = jsonDbl(o, "longitude");
					 s.pricePerKwhFen = jsonI64(o, "priceFenPerKwh");
					 s.chargerCount = jsonI64(o, "chargerCount");
					 s.availableChargerCount = jsonI64(o, "availableChargerCount");
					 s.status = jsonStr(o, "status");
					 stations.append(s);
				 }
				 if (stations.isEmpty()) {
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
								  emit stationsFetched(state->stations, state->meta, {});
							  }
						  });
				 }
			 });
	}

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

	void ApiClient::createStation(const StationForm &form) {
		QJsonObject body;
		body.insert(QStringLiteral("name"), form.name);
		body.insert(QStringLiteral("address"), form.address);
		body.insert(QStringLiteral("latitude"), form.latitude);
		body.insert(QStringLiteral("longitude"), form.longitude);
		body.insert(QStringLiteral("priceFenPerKwh"), static_cast<double>(form.pricePerKwhFen));

		send(QStringLiteral("POST"), QStringLiteral("/admin/stations"), {}, body,
			 [this](const ApiResult &r) {
				 emit stationCreated(r.ok, r.ok ? QString() : r.errorCode);
			 });
	}

	// ---- 用户 ----

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
				 emit usersFetched(users, r.meta, {});
			 });
	}

	void ApiClient::setUserStatus(qint64 userId, bool frozen) {
		QJsonObject body;
		body.insert(QStringLiteral("status"), frozen ? QStringLiteral("frozen")
													 : QStringLiteral("active"));
		send(QStringLiteral("PATCH"), QStringLiteral("/admin/users/%1").arg(userId), {}, body,
			 [this](const ApiResult &r) { emit userStatusChanged(r.ok, r.errorCode); });
	}

} // namespace ops
