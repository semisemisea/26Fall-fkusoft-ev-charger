#include "backend/api.h"

#include "api_support.h"

namespace Backend {

	void registerApiRoutes(Router &router, const ApiDependencies &dependencies) {
		registerHealthRoutes(router, dependencies);
		registerAuthRoutes(router, dependencies);
		registerUserRoutes(router, dependencies);
	}

} // namespace Backend
