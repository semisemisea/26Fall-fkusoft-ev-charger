#include "pages/chargerdialog.h"
#include "pages/mappickerdialog.h"
#include "pages/stationpage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

class StationPageTests : public QObject {
	Q_OBJECT

private slots:
	void selectedStationOwnsDisplayedChargers();
	void chargerDialogLocksSelectedStation();
	void stationDialogOffersMapPicker();
	void mapPickerReportsMissingConfiguration();
	void parsesTencentMapSelection();
};

void StationPageTests::selectedStationOwnsDisplayedChargers() {
	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:1/api/v1"));
	StationPage page(&client);

	auto *stationTable = page.findChild<QTableWidget *>(QStringLiteral("stationTable"));
	auto *chargerTable =
		page.findChild<QTableWidget *>(QStringLiteral("stationChargerTable"));
	auto *heading = page.findChild<QLabel *>(QStringLiteral("stationChargerHeading"));
	QVERIFY(stationTable);
	QVERIFY(chargerTable);
	QVERIFY(heading);

	ops::StationSummary station;
	station.id = 7;
	station.name = QStringLiteral("软件园充电站");
	client.stationsFetched({station}, {}, {});
	QCOMPARE(stationTable->rowCount(), 1);
	QCOMPARE(stationTable->columnCount(), 7);
	for (int column = 0; column < stationTable->columnCount(); ++column)
		QVERIFY(stationTable->horizontalHeaderItem(column)->text() != QStringLiteral("地址"));

	stationTable->cellClicked(0, 0);
	QCOMPARE(heading->text(), QStringLiteral("软件园充电站 · 站内电桩"));

	ops::Charger charger;
	charger.id = 23;
	charger.code = QStringLiteral("23");
	charger.stationId = station.id;
	charger.type = QStringLiteral("fast");
	charger.powerKw = 120.0;
	charger.occupancyStatus = QStringLiteral("available");
	charger.operationalStatus = QStringLiteral("online");

	client.stationChargersFetched(8, {charger}, {});
	QCOMPARE(chargerTable->rowCount(), 0);
	client.stationChargersFetched(station.id, {charger}, {});
	QCOMPARE(chargerTable->rowCount(), 1);
	QCOMPARE(chargerTable->item(0, 0)->text(), QStringLiteral("23"));
	QCOMPARE(chargerTable->item(0, 1)->text(), QStringLiteral("快充"));
}

void StationPageTests::chargerDialogLocksSelectedStation() {
	ChargerDialog dialog(42, nullptr);
	auto *stationIdEdit = dialog.findChild<QLineEdit *>(QStringLiteral("stationIdEdit"));
	QVERIFY(stationIdEdit);
	QCOMPARE(stationIdEdit->text(), QStringLiteral("42"));
	QVERIFY(stationIdEdit->isReadOnly());
}

void StationPageTests::stationDialogOffersMapPicker() {
	AddStationDialog dialog;
	QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("pickStationLocationButton")));
	QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("stationLatitudeEdit")));
	QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("stationLongitudeEdit")));
}

void StationPageTests::mapPickerReportsMissingConfiguration() {
	MapPickerDialog picker({}, 38.889, 121.537);
	auto *unavailable =
		picker.findChild<QLabel *>(QStringLiteral("mapUnavailableLabel"));
	QVERIFY(unavailable);
	QCOMPARE(unavailable->text(), QStringLiteral("地图服务未配置"));
}

void StationPageTests::parsesTencentMapSelection() {
	const auto coordinate = MapPickerDialog::coordinateFromTitle(
		QStringLiteral("ev-charger-location:38.88900000,121.53700000"));
	QVERIFY(coordinate.has_value());
	QCOMPARE(coordinate->latitude, 38.889);
	QCOMPARE(coordinate->longitude, 121.537);
	QVERIFY(!MapPickerDialog::coordinateFromTitle(
				 QStringLiteral("ev-charger-location:91,121.537"))
				 .has_value());
	QVERIFY(!MapPickerDialog::coordinateFromTitle(QStringLiteral("untrusted:38,121"))
				 .has_value());
}

QTEST_MAIN(StationPageTests)

#include "station_page_tests.moc"
