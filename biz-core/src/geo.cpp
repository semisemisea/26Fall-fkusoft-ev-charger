#include "evcharger/geo.h"

#include <QtMath>

#include <cmath>

namespace EvCharger {

	double haversineDistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB) {
		constexpr double earthRadiusKm = 6371.0088;
		const double latitudeDelta = qDegreesToRadians(latitudeB - latitudeA);
		const double longitudeDelta = qDegreesToRadians(longitudeB - longitudeA);
		const double latitudeARadians = qDegreesToRadians(latitudeA);
		const double latitudeBRadians = qDegreesToRadians(latitudeB);
		const double halfChord = qPow(qSin(latitudeDelta / 2.0), 2) + qCos(latitudeARadians) * qCos(latitudeBRadians) * qPow(qSin(longitudeDelta / 2.0), 2);
		return earthRadiusKm * 2.0 * qAtan2(qSqrt(halfChord), qSqrt(1.0 - halfChord));
	}

	double roundToHundredths(double value) {
		return std::round(value * 100.0) / 100.0;
	}

} // namespace EvCharger
