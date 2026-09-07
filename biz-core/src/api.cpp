#include "backend/api.h"

#include "api_support.h"

namespace Backend {

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
