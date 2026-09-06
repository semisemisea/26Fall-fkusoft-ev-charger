#include "apiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>

namespace ops {

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
				 const QJsonObject user = r.data.value(QLatin1String("user")).toObject();
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
		send(QStringLiteral("GET"), QStringLiteral("/admin/dashboard/summary"), {}, {},
			 [this](const ApiResult &r) {
				 if (!r.ok) {
					 emit dashboardSummaryFetched(DashboardSummary{}, r.errorCode);
					 return;
				 }
				 DashboardSummary s;
				 s.asOf = jsonStr(r.data, "asOf");
				 s.todayRevenueFen = jsonI64(r.data, "todayRevenueFen");
				 s.monthRevenueFen = jsonI64(r.data, "monthRevenueFen");
				 s.totalRevenueFen = jsonI64(r.data, "totalRevenueFen");
				 s.userCount = jsonI64(r.data, "userCount");
				 s.stationCount = jsonI64(r.data, "stationCount");
				 s.chargerCount = jsonI64(r.data, "chargerCount");
				 s.onlineRate = jsonDbl(r.data, "onlineRate");
				 emit dashboardSummaryFetched(s, {});
			 });
	}

	void ApiClient::fetchRevenueSeries(const QString &range) {
		QUrlQuery seriesQuery;
		seriesQuery.addQueryItem(QStringLiteral("range"), range);
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
				 QList<ChargerStatusCount> rows;
				 const auto arr = r.data.value(QLatin1String("items")).toArray();
				 for (const auto &e : arr) {
					 const QJsonObject o = e.toObject();
					 ChargerStatusCount row;
					 row.status = jsonStr(o, "status");
					 row.count = jsonI64(o, "count");
					 row.percent = jsonDbl(o, "percent");
					 rows.append(row);
				 }
				 emit chargerStatusFetched(rows, {});
			 });
	}

	// ---- 电桩 ----

	void ApiClient::fetchChargers(const QString &statusFilter, int page) {
		QUrlQuery query;
		if (!statusFilter.isEmpty())
			query.addQueryItem(QStringLiteral("status"), statusFilter);
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
					 Charger c;
					 c.id = jsonI64(o, "id");
					 c.stationId = jsonI64(o, "stationId");
					 c.code = jsonStr(o, "code");
					 c.type = jsonStr(o, "type");
					 c.powerKw = jsonDbl(o, "powerKw");
					 c.status = jsonStr(o, "status");
					 c.totalChargeCount = jsonI64(o, "totalChargeCount");
					 c.totalChargeMinutes = jsonI64(o, "totalChargeMinutes");
					 chargers.append(c);
				 }
				 emit chargersFetched(chargers, r.meta, {});
			 });
	}

	void ApiClient::restartCharger(qint64 chargerId, const QString &reason) {
		QJsonObject body;
		body.insert(QStringLiteral("type"), QStringLiteral("restart"));
		body.insert(QStringLiteral("reason"), reason);
		send(QStringLiteral("POST"), QStringLiteral("/admin/chargers/%1/commands").arg(chargerId), {},
			 body, [this, chargerId](const ApiResult &r) {
				 emit commandFinished(chargerId, r.ok,
									  r.ok ? QStringLiteral("重启指令已下发") : r.errorMessage);
			 });
	}

	// ---- 电站 ----

	void ApiClient::fetchStations(const QString &search, int page) {
		QUrlQuery query;
		if (!search.isEmpty())
			query.addQueryItem(QStringLiteral("search"), search);
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
					 s.pricePerKwhFen = jsonI64(o, "pricePerKwhFen");
					 s.chargerCount = jsonI64(o, "chargerCount");
					 s.availableChargerCount = jsonI64(o, "availableChargerCount");
					 s.onlineRate = jsonDbl(o, "onlineRate");
					 s.status = jsonStr(o, "status");
					 stations.append(s);
				 }
				 emit stationsFetched(stations, r.meta, {});
			 });
	}

	void ApiClient::fetchStationChargers(qint64 stationId) {
		send(QStringLiteral("GET"), QStringLiteral("/stations/%1/chargers").arg(stationId), {}, {},
			 [this, stationId](const ApiResult &r) {
				 QList<Charger> chargers;
				 if (r.ok) {
					 for (const auto &e : r.data.value(QLatin1String("items")).toArray()) {
						 const QJsonObject o = e.toObject();
						 Charger c;
						 c.id = jsonI64(o, "id");
						 c.stationId = jsonI64(o, "stationId");
						 c.code = jsonStr(o, "code");
						 c.type = jsonStr(o, "type");
						 c.powerKw = jsonDbl(o, "powerKw");
						 c.status = jsonStr(o, "status");
						 c.totalChargeCount = jsonI64(o, "totalChargeCount");
						 c.totalChargeMinutes = jsonI64(o, "totalChargeMinutes");
						 chargers.append(c);
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
		body.insert(QStringLiteral("pricePerKwhFen"), static_cast<double>(form.pricePerKwhFen));

		// 按界面收集的数量生成电桩清单;编号先占位,由服务端保证唯一
		QJsonArray chargers;
		for (qint64 i = 0; i < form.fastCount && chargers.size() < 64; ++i) {
			QJsonObject c;
			c.insert(QStringLiteral("type"), QStringLiteral("fast"));
			c.insert(QStringLiteral("powerKw"), form.fastPowerKw);
			chargers.append(c);
		}
		const qint64 slowCount = qMax<qint64>(0, form.chargerCount - form.fastCount);
		for (qint64 i = 0; i < slowCount && chargers.size() < 64; ++i) {
			QJsonObject c;
			c.insert(QStringLiteral("type"), QStringLiteral("slow"));
			c.insert(QStringLiteral("powerKw"), form.slowPowerKw);
			chargers.append(c);
		}
		body.insert(QStringLiteral("chargers"), chargers);

		send(QStringLiteral("POST"), QStringLiteral("/admin/stations"), {}, body,
			 [this](const ApiResult &r) { emit stationCreated(r.ok, r.errorCode); });
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
