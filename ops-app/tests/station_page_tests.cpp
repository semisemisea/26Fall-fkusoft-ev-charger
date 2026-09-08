#include "pages/chargerdialog.h"
#include "pages/mappickerdialog.h"
#include "pages/stationpage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>

#ifdef OPS_APP_HAS_WEBENGINE
#include <QWebEngineView>

#include <memory>
#endif

class StationPageTests : public QObject {
	Q_OBJECT

private slots:
	void selectedStationOwnsDisplayedChargers();
	void chargerDialogLocksSelectedStation();
	void stationDialogOffersMapPicker();
	void mapPickerReportsMissingConfiguration();
	void parsesTencentMapSelection();
	void confirmsRedirectedMapSelection_data();
	void confirmsRedirectedMapSelection();
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

void StationPageTests::confirmsRedirectedMapSelection_data() {
	QTest::addColumn<QString>("origin");
	QTest::addColumn<bool>("fromPicker");
	QTest::addColumn<bool>("validCoordinate");
	QTest::addColumn<bool>("shouldAccept");
	QTest::newRow("redirected-picker") << QStringLiteral("https://mapapi.qq.com") << true << true << true;
	QTest::newRow("original-picker") << QStringLiteral("https://apis.map.qq.com") << true << true << true;
	QTest::newRow("untrusted-origin") << QStringLiteral("https://example.com") << true << true << false;
	QTest::newRow("wrong-window") << QStringLiteral("https://mapapi.qq.com") << false << true << false;
	QTest::newRow("invalid-coordinate") << QStringLiteral("https://mapapi.qq.com") << true << false << false;
}

void StationPageTests::confirmsRedirectedMapSelection() {
#ifdef OPS_APP_HAS_WEBENGINE
	QFETCH(QString, origin);
	QFETCH(bool, fromPicker);
	QFETCH(bool, validCoordinate);
	QFETCH(bool, shouldAccept);
	MapPickerDialog picker(QStringLiteral("test-key"), 38.0, 121.0);
	auto *map = picker.findChild<QWebEngineView *>();
	QVERIFY(map);
	QSignalSpy accepted(&picker, &QDialog::accepted);
	auto ready = std::make_shared<bool>(false);
	QTRY_VERIFY_WITH_TIMEOUT(([&] {
								 map->page()->runJavaScript(QStringLiteral("document.querySelector('iframe') !== null"),
															[ready](const QVariant &value) { *ready = value.toBool(); });
								 return *ready;
							 })(),
							 5000);
	auto dispatched = std::make_shared<bool>(false);
	map->page()->runJavaScript(QStringLiteral(R"JS(
      window.dispatchEvent(new MessageEvent('message', {
        origin: '%1',
        source: %2,
        data: {module: 'locationPicker', latlng: {lat: %3, lng: 121.537}}
      }));
    )JS")
								   .arg(origin, fromPicker ? QStringLiteral("document.querySelector('iframe').contentWindow") : QStringLiteral("window"), validCoordinate ? QStringLiteral("38.889") : QStringLiteral("91")),
							   [dispatched](const QVariant &) { *dispatched = true; });
	QTRY_VERIFY(*dispatched);
	if (!shouldAccept) {
		QTest::qWait(100);
		QCOMPARE(accepted.count(), 0);
		QCOMPARE(picker.latitude(), 38.0);
		QCOMPARE(picker.longitude(), 121.0);
		return;
	}
	QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 3000);
	QCOMPARE(picker.latitude(), 38.889);
	QCOMPARE(picker.longitude(), 121.537);
#else
	QSKIP("Qt WebEngine is unavailable");
#endif
}

QTEST_MAIN(StationPageTests)

#include "station_page_tests.moc"
