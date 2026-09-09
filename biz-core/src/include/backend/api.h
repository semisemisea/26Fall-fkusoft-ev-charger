/**
 * @file api.h
 * @brief 后端 API 依赖装配与全部路由注册入口。
 */

#ifndef BACKEND_API_H
#define BACKEND_API_H

#include "backend/config.h"

#include <memory>

namespace EvCharger {
	class Clock;
}

namespace Backend {

	class Database;
	class MapClient;
	class Router;

	/** @brief 由进程入口手动装配的共享依赖；路由捕获副本延长服务对象生命周期。 */
	struct ApiDependencies {
		std::shared_ptr<Database> database;		 ///< 共享数据库入口；每次操作在调用线程创建连接。
		Config config;							 ///< 路由使用的业务限制和运行配置快照。
		std::shared_ptr<EvCharger::Clock> clock; ///< 可注入时钟，统一提供过期判断和充电计量时间。
		std::shared_ptr<MapClient> mapClient;	 ///< 可替换地图服务，用于地址与路线查询。
	};

	/**
	 * @brief 注册所有业务路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerApiRoutes(Router &router, const ApiDependencies &dependencies);

} // namespace Backend

#endif
