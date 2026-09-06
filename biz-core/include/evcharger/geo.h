#ifndef EVCHARGER_GEO_H
#define EVCHARGER_GEO_H

namespace EvCharger {

	double haversineDistanceKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB);
	double roundToHundredths(double value);

} // namespace EvCharger

#endif
