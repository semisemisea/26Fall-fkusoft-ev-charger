/** @file
 * @brief 管理员前端回归测试；用本地响应或直接发送信号隔离真实服务。
 */
#include "evcharger/map_picker_dialog.h"
#include "pages/charger_dialog.h"
#include "pages/station_page.h"

#include <QComboBox>
#include <QFormLayout>
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

/// @brief 电站选择归属、表单和地图消息校验回归测试。
class StationPageTests : public QObject {
	Q_OBJECT

private slots:
	void chargerStatusOnlyAppearsInEditLayout() {
		ChargerDialog createDialog(42, nullptr);
		createDialog.show();
		QTest::qWait(20);
		for (auto *box : createDialog.findChildren<QComboBox *>()) {
			if (box->findData(QStringLiteral("online")) >= 0)
				QVERIFY(!box->isVisible());
		}
		QCOMPARE(createDialog.form().operationalStatus, QStringLiteral("online"));

		ops::Charger charger;
		charger.stationId = 42;
		charger.type = QStringLiteral("fast");
		charger.powerKw = 120.0;
		charger.operationalStatus = QStringLiteral("fault");
		ChargerDialog editDialog(&charger);
		editDialog.show();
		QTest::qWait(20);
		auto *layout = qobject_cast<QFormLayout *>(editDialog.layout());
		QVERIFY(layout);
		QComboBox *status = nullptr;
		for (auto *box : editDialog.findChildren<QComboBox *>()) {
			if (box->findData(QStringLiteral("online")) >= 0)
				status = box;
		}
		QVERIFY(status);
		QVERIFY(status->isVisible());
		QVERIFY(layout->labelForField(status));
		QVERIFY(layout->labelForField(status)->geometry().right() < status->geometry().left());
		QCOMPARE(editDialog.form().operationalStatus, QStringLiteral("fault"));
		status->setCurrentIndex(status->findData(QStringLiteral("offline")));
		QCOMPARE(editDialog.form().operationalStatus, QStringLiteral("offline"));
	}

	/// @brief 直接注入列表信号，验证非当前电站的迟到响应不会覆盖当前明细。
	void selectedStationOwnsDisplayedChargers();
	void refreshPreservesChargerSelection();
	/// @brief 验证从电站管理打开新增电桩时，所属电站填入且只读。
	void chargerDialogLocksSelectedStation();
	/// @brief 验证新增电站对话框具备选点入口及经纬度输入。
	void stationDialogOffersMapPicker();
	void stationEditPrefillsForm() {
		ops::StationSummary station;
		station.name = QStringLiteral("测试电站");
		station.latitude = 38.123456789;
		station.longitude = 121.987654321;
		station.pricePerKwhFen = 123;
		station.status = QStringLiteral("inactive");
		AddStationDialog dialog;
		dialog.setStation(station);
		QCOMPARE(dialog.windowTitle(), QStringLiteral("编辑电站"));
		QCOMPARE(dialog.form().name, station.name);
		QCOMPARE(dialog.form().latitude, station.latitude);
		QCOMPARE(dialog.form().longitude, station.longitude);
		QCOMPARE(dialog.form().pricePerKwhFen, station.pricePerKwhFen);
		QCOMPARE(dialog.form().status, station.status);
		auto *status = dialog.findChild<QComboBox *>(QStringLiteral("stationStatusCombo"));
		QVERIFY(status);
		status->setCurrentIndex(status->findData(QStringLiteral("active")));
		QCOMPARE(dialog.form().status, QStringLiteral("active"));
		dialog.setStation(station);
		QCOMPARE(dialog.form().status, QStringLiteral("inactive"));
	}
	void stationActionsRespectReadOnlyRole() {
		ops::ApiClient client;
		StationPage page(&client);
		auto *edit = page.findChild<QPushButton *>(QStringLiteral("editStationButton"));
		auto *remove = page.findChild<QPushButton *>(QStringLiteral("deleteStationButton"));
		QVERIFY(edit);
		QVERIFY(remove);
		QVERIFY(!edit->isEnabled());
		QVERIFY(!remove->isEnabled());
		ops::StationSummary station;
		station.id = 7;
		client.stationsFetched({station}, {}, {});
		auto *table = page.findChild<QTableWidget *>(QStringLiteral("stationTable"));
		table->selectRow(0);
		QVERIFY(!edit->isEnabled());
		QVERIFY(!remove->isEnabled());
		table->clearSelection();
		QCOMPARE(page.findChild<QLabel *>(QStringLiteral("stationChargerHeading"))->text(), QStringLiteral("请先选择电站"));
	}
	/// @brief 验证空密钥时显示地图服务未配置提示。
	void mapPickerReportsMissingConfiguration();
	/// @brief 验证标题坐标解析，同时拒绝越界纬度和错误前缀。
	void parsesTencentMapSelection();
	/// @brief 准备可信原域、重定向域、错误来源窗口和越界坐标的测试组合。
	void confirmsRedirectedMapSelection_data();
	/// @brief 向 WebEngine 注入 MessageEvent，验证来源与坐标均合法才接受；无 WebEngine 时跳过。
	void confirmsRedirectedMapSelection();
};

/// @brief 直接注入列表信号，验证非当前电站的迟到响应不会覆盖当前明细。

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

	stationTable->selectRow(0);
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

void StationPageTests::refreshPreservesChargerSelection() {
	ops::ApiClient client;
	client.setBaseUrl(QStringLiteral("http://127.0.0.1:1/api/v1"));
	StationPage page(&client);
	auto *stations = page.findChild<QTableWidget *>(QStringLiteral("stationTable"));
	auto *table = page.findChild<QTableWidget *>(QStringLiteral("stationChargerTable"));
	QVERIFY(stations);
	QVERIFY(table);
	ops::StationSummary station;
	station.id = 7;
	ops::Charger first;
	first.id = 23;
	first.code = QStringLiteral("23");
	ops::Charger second = first;
	second.id = 24;
	second.code = QStringLiteral("24");
	client.stationsFetched({station}, {}, {});
	stations->selectRow(0);
	client.stationChargersFetched(station.id, {first, second}, {});
	table->selectRow(0);
	QSignalSpy selectionChanges(table, &QTableWidget::itemSelectionChanged);
	// 定时刷新响应走同一回调；请求明细期间也不能清空选择。
	client.stationsFetched({station}, {}, {});
	QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
	QCOMPARE(table->item(table->selectionModel()->selectedRows().first().row(), 0)->text(), first.code);
	client.stationChargersFetched(station.id, {second, first}, {});
	QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
	QCOMPARE(table->selectionModel()->selectedRows().first().row(), 1);
	QCOMPARE(selectionChanges.count(), 0);
	client.stationChargersFetched(station.id, {second}, {});
	QVERIFY(table->selectionModel()->selectedRows().isEmpty());
	table->selectRow(0);
	station.id = 8;
	client.stationsFetched({station}, {}, {});
	QVERIFY(table->selectionModel()->selectedRows().isEmpty());
	QCOMPARE(table->rowCount(), 0);
}

/// @brief 验证从电站管理打开新增电桩时，所属电站填入且只读。

void StationPageTests::chargerDialogLocksSelectedStation() {
	ChargerDialog dialog(42, nullptr);
	auto *stationIdEdit = dialog.findChild<QLineEdit *>(QStringLiteral("stationIdEdit"));
	QVERIFY(stationIdEdit);
	QCOMPARE(stationIdEdit->text(), QStringLiteral("42"));
	QVERIFY(stationIdEdit->isReadOnly());
}

/// @brief 验证新增电站对话框具备选点入口及经纬度输入。

void StationPageTests::stationDialogOffersMapPicker() {
	AddStationDialog dialog;
	QVERIFY(dialog.form().status.isEmpty());
	QVERIFY(!dialog.findChild<QComboBox *>(QStringLiteral("stationStatusCombo")));
	QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("pickStationLocationButton")));
	QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("stationLatitudeEdit")));
	QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("stationLongitudeEdit")));
}

/// @brief 验证空密钥时显示地图服务未配置提示。

void StationPageTests::mapPickerReportsMissingConfiguration() {
	MapPickerDialog picker({}, 38.889, 121.537);
	auto *unavailable =
		picker.findChild<QLabel *>(QStringLiteral("mapUnavailableLabel"));
	QVERIFY(unavailable);
	QCOMPARE(unavailable->text(), QStringLiteral("地图服务未配置"));
}

/// @brief 验证标题坐标解析，同时拒绝越界纬度和错误前缀。

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

/// @brief 准备可信原域、重定向域、错误来源窗口和越界坐标的测试组合。

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

/// @brief 向 WebEngine 注入 MessageEvent，验证来源与坐标均合法才接受；无 WebEngine 时跳过。

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
