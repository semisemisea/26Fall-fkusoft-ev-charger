#include "Station.h"

#include <QJsonObject>

// 逐字段解析 JSON；distanceKm 由服务端计算，列表接口才有值
Station Station::fromJson(const QJsonObject &object) {
	Station station;
	station.id = object.value(QLatin1String("id")).toInt();
	station.name = object.value(QLatin1String("name")).toString();
	station.address = object.value(QLatin1String("address")).toString();
	station.latitude = object.value(QLatin1String("latitude")).toDouble();
	station.longitude = object.value(QLatin1String("longitude")).toDouble();
	station.pricePerKwhFen = object.value(QLatin1String("priceFenPerKwh")).toInteger();
	station.chargerCount = object.value(QLatin1String("chargerCount")).toInt();
	station.availableChargerCount = object.value(QLatin1String("availableChargerCount")).toInt();
	station.onlineRate = object.value(QLatin1String("onlineRate")).toDouble();
	station.distanceKm = object.value(QLatin1String("distanceKm")).toDouble();
	station.status = object.value(QLatin1String("status")).toString();
	return station;
}
