/**
 * @file map_client.h
 * @brief 地图提供方接口及腾讯地图超时、重试和响应转换。
 */

#ifndef BACKEND_MAP_CLIENT_H
#define BACKEND_MAP_CLIENT_H

#include <QJsonArray>
#include <QString>

namespace Backend {

	/** @brief 地图访问结果分类，使业务层不依赖提供方错误码。 */
	enum class MapStatus {
		Success,	   ///< 提供方请求与业务处理成功。
		NotFound,	   ///< 未找到地址或可用路线。
		ProviderError, ///< 提供方拒绝请求或返回无效数据。
		Unavailable,   ///< 配置缺失、超时或暂时网络故障。
	};

	/** @brief 地址解析结果；仅成功状态下坐标和地址字段可用于业务。 */
	struct GeocodeResult {
		MapStatus status = MapStatus::Unavailable; ///< 地图请求结果分类。
		QString address;						   ///< 原始查询地址。
		QString formattedAddress;				   ///< 提供方返回的规范化地址。
		double latitude = 0;					   ///< 纬度，单位度。
		double longitude = 0;					   ///< 经度，单位度。
	};

	/** @brief 地图路线结果；距离单位米，耗时单位秒。 */
	struct RouteResult {
		MapStatus status = MapStatus::Unavailable; ///< 地图请求结果分类。
		qint64 distanceM = 0;					   ///< 路线距离，单位米。
		qint64 durationSec = 0;					   ///< 预计路线耗时，单位秒。
		QJsonArray polyline;					   ///< 解码后的路线坐标数组。
		QString mapUrl;							   ///< 可供客户端打开的腾讯地图路线链接。
	};

	/** @brief 可替换地图服务边界，测试可注入无网络假实现。 */
	class MapClient {
	public:
		/**
		 * @brief 释放对象；虚析构保证通过接口指针销毁派生实例。
		 */
		virtual ~MapClient() = default;

		/**
		 * @brief 将地址和区域解析为规范地址及经纬度，保留地图提供方状态。
		 * @param address 待解析的地址文本。
		 * @param region 可选的地址搜索区域。
		 * @return 地图查询状态及成功时的规范地址和坐标。
		 */
		virtual GeocodeResult geocode(const QString &address, const QString &region) = 0;
		/**
		 * @brief 按起终点和出行模式请求路线，返回距离、耗时、折线及地图链接。
		 * @param fromLatitude 纬度，单位度。
		 * @param fromLongitude 经度，单位度。
		 * @param toLatitude 纬度，单位度。
		 * @param toLongitude 经度，单位度。
		 * @param mode 地图出行模式。
		 * @return 地图查询状态及成功时的路线距离、耗时、坐标折线和地图链接。
		 */
		virtual RouteResult route(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) = 0;
	};

	/** @brief 封装腾讯地图同步查询、超时重试和折线解码。 */
	class TencentMapClient final : public MapClient {
	public:
		/**
		 * @brief 保存地图密钥、超时与重试配置。
		 * @param key 地图服务密钥。
		 * @param timeoutMs 等待超时，单位毫秒。
		 * @param retryCount 初次请求之外允许的重试次数。
		 */
		TencentMapClient(QString key, int timeoutMs, int retryCount);

		/**
		 * @brief 将地址和区域解析为规范地址及经纬度，保留地图提供方状态。
		 * @param address 待解析的地址文本。
		 * @param region 可选的地址搜索区域。
		 * @return 地图查询状态及成功时的规范地址和坐标。
		 */
		GeocodeResult geocode(const QString &address, const QString &region) override;
		/**
		 * @brief 按起终点和出行模式请求路线，返回距离、耗时、折线及地图链接。
		 * @param fromLatitude 纬度，单位度。
		 * @param fromLongitude 经度，单位度。
		 * @param toLatitude 纬度，单位度。
		 * @param toLongitude 经度，单位度。
		 * @param mode 地图出行模式。
		 * @return 地图查询状态及成功时的路线距离、耗时、坐标折线和地图链接。
		 */
		RouteResult route(double fromLatitude, double fromLongitude, double toLatitude, double toLongitude, const QString &mode) override;

	private:
		QString m_key;	  ///< 腾讯地图 API 密钥。
		int m_timeoutMs;  ///< 每次地图请求超时，单位毫秒。
		int m_retryCount; ///< 初次地图请求之外允许重试的次数。
	};

} // namespace Backend

#endif
