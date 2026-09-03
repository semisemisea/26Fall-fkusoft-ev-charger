#pragma once

// 统一业务服务层的 HTTP 客户端封装。
// - 请求带 Authorization / X-Request-Id(apis.md 基础约定)
// - 解析 {data, meta} 与 {error:{code,message}} envelope
// - 401 时发 authenticationChanged(false),由 UI 回到登录页
// - 所有请求异步,通过信号返回;对象自身由 UI 层持有(手动依赖注入)

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrlQuery>

#include <functional>

#include "types.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace ops {

	struct ApiResult {
		bool ok = false;
		int httpStatus = 0;
		QString errorCode; // 业务错误码,如 CHARGER_UNAVAILABLE
		QString errorMessage;
		QJsonObject data; // 成功时的 data 对象;data 为数组时放在 "items" 下

		// 便捷:直接以列表形式取 data 数组
		QList<QJsonObject> items() const;
	};

	class ApiClient : public QObject {
		Q_OBJECT
	public:
		explicit ApiClient(QObject *parent = nullptr);

		void setBaseUrl(const QString &url);
		QString baseUrl() const { return m_baseUrl; }

		bool isAuthenticated() const { return !m_token.isEmpty(); }
		QString role() const { return m_role; }
		bool canWrite() const { return m_role == QLatin1String("ADMIN"); }

		// ---- 认证 ----
		void login(const QString &username, const QString &password);
		void logout();

		// ---- 看板 ----
		void fetchDashboardSummary();
		void fetchRevenueSeries(const QString &range); // "7d" | "30d"
		void fetchChargerStatus();

		// ---- 电桩 ----
		void fetchChargers(const QString &statusFilter = {});
		void restartCharger(qint64 chargerId, const QString &reason);

		// ---- 电站 ----
		void fetchStations(const QString &search = {});
		void fetchStationChargers(qint64 stationId);
		void createStation(const StationForm &form);

		// ---- 用户 ----
		void fetchUsers(const QString &phoneSearch = {});
		void setUserStatus(qint64 userId, bool frozen);

	signals:
		// 认证状态
		void loginSucceeded(const ops::AdminUser &admin);
		void loginFailed(const QString &errorCode, const QString &message);
		void authenticationChanged(bool authenticated);

		// 看板
		void dashboardSummaryFetched(const ops::DashboardSummary &summary);
		void revenueSeriesFetched(const QString &range, const QList<ops::RevenuePoint> &points);
		void chargerStatusFetched(const QList<ops::ChargerStatusCount> &rows);

		// 电桩
		void chargersFetched(const QList<ops::Charger> &chargers);
		void commandFinished(qint64 chargerId, bool succeeded, const QString &message);

		// 电站
		void stationsFetched(const QList<ops::StationSummary> &stations);
		void stationChargersFetched(qint64 stationId, const QList<ops::Charger> &chargers);
		void stationCreated(bool succeeded, const QString &errorCode);

		// 用户
		void usersFetched(const QList<ops::AdminUserRow> &users);
		void userStatusChanged(bool succeeded, const QString &errorCode);

	private:
		void send(const QString &method, const QString &path, const QUrlQuery &query,
				  const QJsonObject &body, const std::function<void(const ApiResult &)> &handler);
		void handleEnvelope(QNetworkReply *reply, const std::function<void(const ApiResult &)> &handler);
		QUrl buildUrl(const QString &path, const QUrlQuery &query = {}) const;

		QNetworkAccessManager *m_nam = nullptr;
		QString m_baseUrl = QStringLiteral("http://localhost:8080/api/v1");
		QString m_token;
		QString m_role;
	};

} // namespace ops
