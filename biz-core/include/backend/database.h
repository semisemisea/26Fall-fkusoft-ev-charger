/**
 * @file database.h
 * @brief SQLite 连接作用域、模式初始化与启动时业务状态恢复。
 */

#ifndef BACKEND_DATABASE_H
#define BACKEND_DATABASE_H

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <functional>

namespace Backend {

	/** @brief 为每次操作创建独立连接的 SQLite 访问入口，不跨线程共享连接。 */
	class Database {
	public:
		/// @brief 连接作用域回调；返回 false 时通过错误指针说明原因。
		using Operation = std::function<bool(QSqlDatabase &, QString *)>;

		/**
		 * @brief 保存数据库路径和锁等待期限，连接在每次操作时创建。
		 * @param path SQLite 数据库文件路径。
		 * @param busyTimeoutMs SQLite 锁争用等待上限，单位毫秒。
		 */
		Database(QString path, int busyTimeoutMs);

		/**
		 * @brief 创建数据库目录，依次执行模式迁移、一致性检查、状态恢复及服务凭据更新。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 * @param serviceToken 服务身份令牌；空值表示不启用该身份。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
		 * @return 目录、迁移、一致性、恢复及服务配置全部成功返回 true；任一步失败返回 false。
		 */
		bool initialize(const QDateTime &nowUtc, const QString &serviceToken, QString *errorMessage) const;
		/**
		 * @brief 在调用线程创建独立 SQLite 连接，启用外键、WAL 和忙等待后执行回调，结束时关闭并移除连接。
		 * @param operation 在连接有效期内同步执行的回调；不得让查询或连接逃逸此作用域。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
		 * @return 连接配置成功且回调返回 true 时返回 true；打开/配置失败或回调返回 false 时返回 false。
		 * @note 回调负责事务提交或回滚，不得在回调结束后继续使用连接和查询。
		 */
		bool withConnection(const Operation &operation, QString *errorMessage) const;

	private:
		/**
		 * @brief 在事务中创建版本 1 模式和缺失的默认管理员，拒绝不支持的模式版本。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 模式版本有效且建表/默认管理员事务提交成功返回 true；SQL 失败或不支持的版本返回 false。
		 */
		bool migrate(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const;
		/**
		 * @brief 检查预约与未完成订单之间的用户、充电桩独占关系及充电桩有效性。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 所有占用一致性查询均成功且未发现冲突返回 true；SQL 失败或发现业务冲突返回 false。
		 */
		bool validateConsistency(QSqlDatabase &database, QString *errorMessage) const;
		/**
		 * @brief 启动时释放预约占用；到期预约设为 expired，其余活动预约设为 cancelled，并恢复充电中桩占用。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 预约释放、预约终止和充电占用恢复全部提交成功返回 true；失败回滚并返回 false。
		 */
		bool recoverState(QSqlDatabase &database, const QDateTime &nowUtc, QString *errorMessage) const;
		/**
		 * @brief 在事务中替换服务凭据；配置为空时清除服务身份，否则保存令牌摘要。
		 * @param database 当前调用线程的数据库连接；不得跨线程保存。
		 * @param serviceToken 服务身份令牌；空值表示不启用该身份。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；调用方必须提供有效指针。
		 * @return 旧凭据清理和可选新凭据保存提交成功返回 true；失败回滚并返回 false。
		 */
		bool configureService(QSqlDatabase &database, const QString &serviceToken, const QDateTime &nowUtc, QString *errorMessage) const;

		QString m_path;		 ///< SQLite 文件路径。
		int m_busyTimeoutMs; ///< 每个连接配置的锁等待期限，单位毫秒。
	};

	/**
	 * @brief 转换为包含毫秒的 UTC ISO 字符串，用作数据库时间文本。
	 * @param dateTime 需要转为数据库 UTC 文本的时刻。
	 * @return UTC ISO 8601 毫秒时间文本。
	 */
	QString toDatabaseTimestamp(const QDateTime &dateTime);

} // namespace Backend

#endif
