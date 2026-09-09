/** @file
 * @brief 管理员异步 HTTP 客户端及其信号契约。
 */
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

	/// @brief 统一响应信封；保留 HTTP 状态、业务错误、数据和分页信息。
	struct ApiResult {
		bool ok = false;	  ///< HTTP 为 2xx 且没有业务错误码时为 true。
		int httpStatus = 0;	  ///< HTTP 响应码；未收到 HTTP 响应时为零。
		QString errorCode;	  ///< 业务或客户端错误码，通常空值表示未报告错误。
		QString errorMessage; ///< 服务端或客户端生成的错误提示。
		QJsonObject data;	  ///< 统一数据对象；数组响应包裹在 items 字段中。
		PageMeta meta;		  ///< 列表分页元数据。

		// 便捷:直接以列表形式取 data 数组
		/** @brief 提取 data.items 数组为对象列表；非数组返回空列表。
		 * @return items 数组中的对象列表，字段非数组时返回空列表。
		 */
		QList<QJsonObject> items() const;
	};

	/// @brief 管理员 HTTP 适配器；异步请求经领域信号交给界面，网络管理器作为 QObject 子对象释放。
	class ApiClient : public QObject {
		Q_OBJECT
	public:
		/** @brief 构造客户端并创建归其所有的网络管理器。
		 * @param parent Qt 父对象；负责子对象生命周期。
		 */
		explicit ApiClient(QObject *parent = nullptr);

		/** @brief 设置后续请求使用的基础 URL，不自动补充分隔符。
		 * @param url 服务基础地址，包含 API 版本前缀。
		 */
		void setBaseUrl(const QString &url);
		/** @brief 返回当前服务基础 URL。
		 * @return 当前保存的基础 URL。
		 */
		QString baseUrl() const { return m_baseUrl; }

		/** @brief 判断本地是否保存非空令牌；不发请求验证令牌有效性。
		 * @return 本地令牌非空时为 true。
		 */
		bool isAuthenticated() const { return !m_token.isEmpty(); }
		/** @brief 返回最近登录保存的管理员角色。
		 * @return 当前保存的角色字符串，未登录或注销后为空。
		 */
		QString role() const { return m_role; }
		/** @brief 仅当角色为 ADMIN 时返回 true。
		 * @return 当前角色等于 ADMIN 时为 true。
		 */
		bool canWrite() const { return m_role == QLatin1String("ADMIN"); }

		// ---- 认证 ----
		/** @brief 异步提交管理员凭据；成功保存令牌和角色并发送 loginSucceeded。
		 * @param username 管理员登录账号。
		 * @param password 管理员原始密码。
		 */
		void login(const QString &username, const QString &password);
		/** @brief 有令牌时发送注销请求，立即清空本地认证信息并发出失去认证信号。
		 */
		void logout();

		// ---- 看板 ----
		/** @brief 并发获取累计、今日、本月营收和资源统计，全部完成后发出汇总信号。
		 */
		void fetchDashboardSummary();
		/** @brief 请求近 7 或 30 日营收；按本地日界限转换 UTC 区间并传入当前时区偏移。
		 * @param range 时间范围，30d 表示 30 日，其他值按 7 日查询。
		 */
		void fetchRevenueSeries(const QString &range); // "7d" | "30d"
		/** @brief 请求两个独立状态维度，校验响应结构后计算各状态占比。
		 */
		void fetchChargerStatus();

		// ---- 电桩 ----
		/** @brief 分页查询电桩；fault/offline 作为运维筛选，其他非空值作为占用筛选。
		 * @param statusFilter 状态筛选；空值表示不筛选。
		 * @param page 从 1 起的页码，小于 1 时请求第一页。
		 */
		void fetchChargers(const QString &statusFilter = {}, int page = 1);
		/** @brief 向指定电站创建电桩，完成后发送 chargerMutationFinished。
		 * @param stationId 所属或目标电站 ID。
		 * @param form 调用者提供的提交字段。
		 */
		void createCharger(qint64 stationId, const ChargerForm &form);
		/** @brief 修改电桩类型、功率和运维状态，不修改归属和占用状态。
		 * @param chargerId 目标电桩 ID。
		 * @param form 调用者提供的提交字段。
		 */
		void updateCharger(qint64 chargerId, const ChargerForm &form);
		/** @brief 删除指定电桩，结果通过 chargerMutationFinished 返回。
		 * @param chargerId 目标电桩 ID。
		 */
		void deleteCharger(qint64 chargerId);
		/** @brief 发送远程重启请求，结果通过 commandFinished 返回。
		 * @param chargerId 目标电桩 ID。
		 * @param reason 保留的操作原因参数；当前实现不发送此字段。
		 */
		void restartCharger(qint64 chargerId, const QString &reason);

		// ---- 电站 ----
		/** @brief 查询电站并为各站请求在线数量；全部补充请求完成后发送列表。
		 * @param search 站名查询文本；空值表示全部。
		 * @param page 从 1 起的页码，小于 1 时请求第一页。
		 */
		void fetchStations(const QString &search = {}, int page = 1);
		/** @brief 请求指定电站的第一页电桩，pageSize 固定为 100，不继续翻页。
		 * @param stationId 所属或目标电站 ID。
		 */
		void fetchStationChargers(qint64 stationId);
		/** @brief 仅发送站名、坐标和分计价单价，完成后发送 stationCreated。
		 * @param form 调用者提供的提交字段。
		 */
		void createStation(const StationForm &form);
		/// @brief 修改指定电站的基本信息及表单中提供的状态。
		void updateStation(qint64 stationId, const StationForm &form);
		/// @brief 删除指定电站及站内电桩。
		void deleteStation(qint64 stationId);

		// ---- 用户 ----
		/** @brief 按手机号查询用户列表并返回分页信息。
		 * @param phoneSearch 手机号查询文本；空值表示全部。
		 * @param page 从 1 起的页码，小于 1 时请求第一页。
		 */
		void fetchUsers(const QString &phoneSearch = {}, int page = 1);
		/** @brief 将用户状态提交为 frozen 或 active。
		 * @param userId 目标用户 ID。
		 * @param frozen true 冻结，false 解冻。
		 */
		void setUserStatus(qint64 userId, bool frozen);

	signals:
		// 认证状态
		/** @brief 认证成功通知。
		 * @param admin 认证成功的管理员身份。
		 */
		void loginSucceeded(const ops::AdminUser &admin);
		/** @brief 认证失败通知。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 * @param message 供界面展示的结果或错误信息。
		 */
		void loginFailed(const QString &errorCode, const QString &message);
		/** @brief 认证状态变更通知；当前实现用于注销或 401 失效。
		 * @param authenticated 认证是否有效；false 要求界面返回登录。
		 */
		void authenticationChanged(bool authenticated);

		// 看板
		/** @brief 汇总请求全部完成后的结果通知。
		 * @param summary 完成汇总的指标值。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void dashboardSummaryFetched(const ops::DashboardSummary &summary, const QString &errorCode);
		/** @brief 带原请求范围的趋势结果，界面可据此忽略过期响应。
		 * @param range 时间范围，30d 表示 30 日，其他值按 7 日查询。
		 * @param points 按服务端返回顺序排列的营收点。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void revenueSeriesFetched(const QString &range, const QList<ops::RevenuePoint> &points,
								  const QString &errorCode);
		/** @brief 两个状态维度的统计结果通知。
		 * @param snapshot 两种独立状态维度的快照。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void chargerStatusFetched(const ops::ChargerStatusSnapshot &snapshot,
								  const QString &errorCode);

		// 电桩
		/** @brief 分页电桩列表结果通知。
		 * @param chargers 待显示的站内电桩列表。
		 * @param meta 分页元数据，valid 标识是否存在。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void chargersFetched(const QList<ops::Charger> &chargers, const ops::PageMeta &meta,
							 const QString &errorCode);
		/** @brief 电桩新增、修改或删除的完成通知。
		 * @param operation create、update 或 delete。
		 * @param chargerId 目标电桩 ID。
		 * @param succeeded 操作是否成功。
		 * @param message 供界面展示的结果或错误信息。
		 */
		void chargerMutationFinished(const QString &operation, qint64 chargerId, bool succeeded,
									 const QString &message);
		/** @brief 远程重启完成通知。
		 * @param chargerId 目标电桩 ID。
		 * @param succeeded 操作是否成功。
		 * @param message 供界面展示的结果或错误信息。
		 */
		void commandFinished(qint64 chargerId, bool succeeded, const QString &message);

		// 电站
		/** @brief 已补充在线率的电站列表通知。
		 * @param stations 当前页电站列表。
		 * @param meta 分页元数据，valid 标识是否存在。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void stationsFetched(const QList<ops::StationSummary> &stations, const ops::PageMeta &meta,
							 const QString &errorCode);
		/** @brief 带所属电站 ID 的电桩明细通知。
		 * @param stationId 所属或目标电站 ID。
		 * @param chargers 待显示的站内电桩列表。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void stationChargersFetched(qint64 stationId, const QList<ops::Charger> &chargers,
									const QString &errorCode);
		/** @brief 新增电站请求完成通知。
		 * @param succeeded 操作是否成功。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void stationCreated(bool succeeded, const QString &errorCode);
		/// @brief 电站编辑或删除结果，失败时返回服务端消息。
		void stationMutationFinished(const QString &operation, qint64 stationId,
									 bool succeeded, const QString &error);

		// 用户
		/** @brief 分页用户列表结果通知。
		 * @param users 当前页用户列表。
		 * @param meta 分页元数据，valid 标识是否存在。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void usersFetched(const QList<ops::AdminUserRow> &users, const ops::PageMeta &meta,
						  const QString &errorCode);
		/** @brief 冻结或解冻请求完成通知。
		 * @param succeeded 操作是否成功。
		 * @param errorCode 错误码；空值表示未报告业务错误。
		 */
		void userStatusChanged(bool succeeded, const QString &errorCode);

	private:
		/** @brief 构造带请求 ID、可选令牌和 15 秒传输超时的异步 HTTP 请求。
		 * @param method HTTP 方法名称。
		 * @param path 相对基础地址的接口路径。
		 * @param query URL 查询参数。
		 * @param handler 完成回调，以客户端为连接上下文；客户端销毁后不再调用。
		 * @param body 需要发送的 JSON 请求对象。
		 */
		void send(const QString &method, const QString &path, const QUrlQuery &query,
				  const QJsonObject &body, const std::function<void(const ApiResult &)> &handler);
		/** @brief 在响应完成时解包数据和错误；401 发出认证失效信号，回调后延迟销毁响应。
		 * @param reply 由客户端管理、完成后 deleteLater 的网络响应。
		 * @param handler 解包后的结果回调，在 finished 信号处理期间调用。
		 */
		void handleEnvelope(QNetworkReply *reply, const std::function<void(const ApiResult &)> &handler);
		/** @brief 拼接基础地址和接口路径，再附加查询参数。
		 * @param path 相对基础地址的接口路径。
		 * @param query URL 查询参数。
		 * @return 基础地址、路径和查询参数组合成的 URL。
		 */
		QUrl buildUrl(const QString &path, const QUrlQuery &query = {}) const;
		QNetworkAccessManager *m_nam = nullptr;								///< 当前客户端拥有的网络管理器子对象。
		QString m_baseUrl = QStringLiteral("http://localhost:8080/api/v1"); ///< 后续请求使用的基础服务地址。
		QString m_token;													///< 最近成功登录保存的访问令牌。
		QString m_role;														///< 最近成功登录保存的角色。
	};

} // namespace ops
