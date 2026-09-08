/**
 * @file config.h
 * @brief 配置默认值、环境变量覆盖和业务上下限校验。
 */

#ifndef BACKEND_CONFIG_H
#define BACKEND_CONFIG_H

#include <QProcessEnvironment>
#include <QString>

#include <optional>

namespace Backend {

	/** @brief 后端监听、存储、业务限制与外部服务配置。 */
	struct Config {
		QString host = QStringLiteral("127.0.0.1");	 ///< HTTP 监听 IP 地址，默认仅本机。
		quint16 port = 8080;						 ///< HTTP 监听端口。
		QString databasePath;						 ///< 数据库文件路径；加载配置时解析为规范路径。
		int databaseBusyTimeoutMs = 5000;			 ///< SQLite 锁争用等待上限，单位毫秒。
		int maxReservationMinutes = 60;				 ///< 单次预约最大保留分钟数。
		double maxRadiusKm = 50.0;					 ///< 附近搜索最大半径，单位千米。
		qint64 maxChargerPowerW = 1'000'000;		 ///< 允许配置的最大桩功率，单位瓦。
		qint64 maxStationPriceFenPerKwh = 1'000'000; ///< 允许的站点单价上限，单位分每千瓦时。
		qint64 topUpMinFen = 100;					 ///< 单次充值下限，单位分。
		qint64 topUpMaxFen = 10'000'000;			 ///< 单次充值上限，单位分。
		qint64 maxWalletBalanceFen = 1'000'000'000;	 ///< 充值后钱包余额上限，单位分。
		int idempotencyRetentionHours = 24;			 ///< 幂等响应保留时间，单位小时。
		int mapTimeoutMs = 3000;					 ///< 地图单次请求超时，单位毫秒。
		int mapRetryCount = 1;						 ///< 地图初次请求外的最大重试次数。
		qint64 jsonBodyLimitBytes = 1'048'576;		 ///< 普通 JSON 请求正文上限，单位字节。
		qint64 avatarBodyLimitBytes = 5'242'880;	 ///< 头像请求正文及头像文件校验上限，单位字节。
		int shutdownTimeoutMs = 1000;				 ///< 停止服务时等待工作任务的期限，单位毫秒。
		QString tencentMapKey;						 ///< 从 TENCENT_MAP_KEY 环境变量读取的地图密钥。
		QString serviceToken;						 ///< 从 ML_SERVICE_TOKEN 环境变量读取的服务令牌；空值不启用服务身份。

		/**
		 * @brief 按环境变量、INI、默认值的优先级加载配置，并校验数值边界与钱包上下限的一致性。
		 * @param configPath INI 配置文件路径。
		 * @param executableDirectory 用于解析相对数据库路径的可执行文件目录。
		 * @param environment 配置覆盖所使用的环境变量快照。
		 * @param[out] errorMessage 失败时接收驱动或业务一致性错误；可为 nullptr。
		 * @return 完成优先级覆盖、路径解析和边界校验的配置；INI 读取或配置校验失败返回 std::nullopt。
		 */
		static std::optional<Config> load(const QString &configPath,
										  const QString &executableDirectory,
										  const QProcessEnvironment &environment,
										  QString *errorMessage);
	};

} // namespace Backend

#endif
