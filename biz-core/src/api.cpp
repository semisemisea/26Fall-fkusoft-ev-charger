/**
 * @file api.cpp
 * @brief 后端 API 依赖装配与全部路由注册入口。
 */

#include "backend/api.h"

#include "api_support.h"

namespace Backend {

	/**
	 * @brief 注册所有业务路由；处理器按值捕获依赖，供服务运行期间使用。
	 * @param router 接收路由注册或用于分派请求的路由器。
	 * @param dependencies 数据库、配置、时钟与地图客户端依赖。
	 */
	void registerApiRoutes(Router &router, const ApiDependencies &dependencies) {
		registerHealthRoutes(router, dependencies);
		registerAuthRoutes(router, dependencies);
		registerLocationRoutes(router, dependencies);
		registerAdminRoutes(router, dependencies);
		registerAdminUserRoutes(router, dependencies);
		registerUserRoutes(router, dependencies);
		registerStationRoutes(router, dependencies);
		registerChargerRoutes(router, dependencies);
		registerReservationRoutes(router, dependencies);
		registerOrderRoutes(router, dependencies);
	}

} // namespace Backend
