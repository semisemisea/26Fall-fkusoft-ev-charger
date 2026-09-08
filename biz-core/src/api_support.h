/**
 * @file api_support.h
 * @brief 身份验证、JSON 响应、事务与幂等记录的共享 API 支持。
 */

#ifndef BACKEND_API_SUPPORT_H
#define BACKEND_API_SUPPORT_H

#include "backend/api.h"
#include "backend/http.h"

#include <QJsonObject>

#include <optional>

class QSqlDatabase;
class QDateTime;

namespace Backend {

	/** @brief 认证后的主体信息，供接口实施角色与资源归属检查。 */
	struct Principal {
		QString type;		 ///< 认证主体类型：user、admin 或 service。
		qint64 id = 0;		 ///< 主体或资源的数据库标识。
		QString role;		 ///< 用于授权的角色，例如 USER、ADMIN 或 SERVICE。
		QString status;		 ///< 账号或站点的业务状态文本。
		QString serviceName; ///< 服务身份名称；非服务主体不使用。
		QString token;		 ///< 本次认证的明文令牌，用于撤销当前会话。
	};

	/** @brief 幂等查询的三种结果：无记录、可重放、键内容冲突。 */
	enum class IdempotencyState {
		Missing, ///< 没有可用的未过期记录，可执行首次写入。
		Replay,	 ///< 幂等键和规范化请求摘要一致，可重放保存响应。
		Reused,	 ///< 同一幂等键被用于不同请求内容。
	};

	/** @brief 幂等查询状态及存储响应；仅 Replay 状态携带可重放结果。 */
	struct IdempotencyResult {
		IdempotencyState state = IdempotencyState::Missing; ///< 幂等键查询结果分类。
		int status = 0;										///< HTTP 响应状态码。
		QJsonObject data;									///< 保存的业务数据或内部错误包装。
	};

	/**
	 * @brief 检查 JSON 内容类型并解析对象请求体；类型或语法不符时生成 HTTP 错误。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
	 * @return 解析出的 JSON 对象；内容类型不受支持、JSON 语法错误或根值不是对象时返回 std::nullopt，并写入 failure。
	 */
	std::optional<QJsonObject> parseJsonObject(const HttpRequest &request, HttpResponse *failure);
	/**
	 * @brief 验证 Bearer 令牌摘要、有效期与撤销状态，并加载用户、管理员或服务身份。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 * @param[out] failure 必须有效；失败时写入对应 HTTP 错误响应，成功时不应读取该输出。
	 * @return 已加载的用户、管理员或服务主体；令牌无效/过期/已撤销、主体不存在或数据库失败时返回 std::nullopt，并写入 failure。
	 */
	std::optional<Principal> authenticate(const HttpRequest &request, const ApiDependencies &dependencies, HttpResponse *failure);
	/**
	 * @brief 将用户字段投影为 API JSON；头像以是否存在表示，不包含头像二进制。
	 * @param id 用户数据库标识。
	 * @param phone 由 11 个 ASCII 数字组成的手机号文本。
	 * @param nickname 用户昵称。
	 * @param hasAvatar 用户是否已保存头像。
	 * @param balanceFen 钱包余额，单位分。
	 * @param status 用户或资源的业务状态。
	 * @param createdAt 资源创建时间的数据库文本。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject userJson(qint64 id, const QString &phone, const QString &nickname, bool hasAvatar, qint64 balanceFen, const QString &status, const QString &createdAt);
	/**
	 * @brief 将 SQLite 忙或锁错误映射为 503，其余数据库错误映射为通用 500。
	 * @param requestId 本次请求关联标识。
	 * @param[in] errorMessage 已发生的数据库错误文本，用于区分锁忙与内部错误。
	 * @return HTTP 503 数据库忙响应或 HTTP 500 内部错误响应。
	 */
	HttpResponse databaseFailure(const QString &requestId, const QString &errorMessage);
	/**
	 * @brief 通过 Qt SQL 驱动开始事务，将后续关联业务写入纳入同一提交范围。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @return 驱动成功开始事务返回 true，无法开始事务返回 false；错误可从连接读取。
	 */
	bool beginTransaction(QSqlDatabase &database);
	/**
	 * @brief 提交事务，提交失败时回滚以防留下未结束写事务。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @return 提交成功返回 true；提交失败返回 false，并尝试回滚。
	 */
	bool commitTransaction(QSqlDatabase &database);
	/**
	 * @brief 清理过期记录后按身份、方法、路径和幂等键查询，区分首次请求、可重放及请求内容冲突。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param principal 已认证主体，用于权限及幂等记录隔离。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param normalizedBody 去除无关差异后的请求对象，用于计算幂等摘要。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param[out] result SQL 成功时写入 Missing、Replay 或 Reused；Replay 同时含保存的状态码和响应数据。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 清理和查询均成功返回 true，此时 result 指明 Missing、Replay 或 Reused；SQL 失败返回 false。
	 */
	bool checkIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, IdempotencyResult *result, QString *errorMessage);
	/**
	 * @brief 保存请求摘要、响应数据、HTTP 状态和过期时间，与业务写入共享调用方事务。
	 * @param database 当前调用线程的数据库连接；不得跨线程保存。
	 * @param principal 已认证主体，用于权限及幂等记录隔离。
	 * @param request 当前 HTTP 请求，包含查询、请求体、头和路径参数。
	 * @param normalizedBody 去除无关差异后的请求对象，用于计算幂等摘要。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 * @param retentionHours 幂等结果保留小时数。
	 * @param status HTTP 响应状态码。
	 * @param data 返回给调用方或保存用于重放的数据。
	 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
	 * @return 幂等记录插入成功返回 true，插入失败返回 false 并写入 SQL 错误。
	 */
	bool storeIdempotency(QSqlDatabase &database, const Principal &principal, const HttpRequest &request, const QJsonObject &normalizedBody, const QDateTime &nowUtc, int retentionHours, int status, const QJsonObject &data, QString *errorMessage);
	/**
	 * @brief 将业务错误包装为可保存的幂等数据，以便失败响应也能一致重放。
	 * @param code API 错误代码。
	 * @param message 面向调用方的错误说明。
	 * @param details 字段级校验详情。
	 * @return 符合本接口字段约定的 JSON 数据。
	 */
	QJsonObject idempotencyError(const QString &code, const QString &message, const QJsonObject &details = {});
	/**
	 * @brief 用当前请求 ID 重建已保存响应；识别内部错误包装并恢复为错误信封。
	 * @param[in] result checkIdempotency 已加载的可重放记录，包含原始状态码和响应数据。
	 * @param requestId 本次请求关联标识。
	 * @return 保存的业务状态码与数据响应，或由内部错误包装恢复的错误响应；meta 使用当前请求 ID。
	 */
	HttpResponse replayIdempotency(const IdempotencyResult &result, const QString &requestId);

	/**
	 * @brief 注册健康检查路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerHealthRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册认证路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAuthRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册地址和路线路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerLocationRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册管理统计及订单路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAdminRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册管理员用户管理路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerAdminUserRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册个人资料和钱包路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerUserRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册站点路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerStationRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册充电桩路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerChargerRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册预约路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerReservationRoutes(Router &router, const ApiDependencies &dependencies);
	/**
	 * @brief 注册充电订单路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerOrderRoutes(Router &router, const ApiDependencies &dependencies);

} // namespace Backend

#endif
