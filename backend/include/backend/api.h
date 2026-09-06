#ifndef BACKEND_API_H
#define BACKEND_API_H

#include "backend/config.h"

#include <memory>

namespace EvCharger {
	class Clock;
}

namespace Backend {

	class Database;
	class Router;

	struct ApiDependencies {
		std::shared_ptr<Database> database;
		Config config;
		std::shared_ptr<EvCharger::Clock> clock;
	};

	void registerApiRoutes(Router &router, const ApiDependencies &dependencies);

} // namespace Backend

#endif
