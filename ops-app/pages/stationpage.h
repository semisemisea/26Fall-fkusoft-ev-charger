#pragma once

// 充电站管理页:管理电站，并在选中电站后管理其站内电桩。

#include <QDialog>
#include <QWidget>

#include "api/apiclient.h"
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class StationPage : public QWidget {
	Q_OBJECT
public:
	explicit StationPage(ops::ApiClient *api, QWidget *parent = nullptr);

	void refresh();

protected:
	void showEvent(QShowEvent *event) override;

private:
	void showStationChargers(qint64 stationId, const QString &stationName);
	void applyChargerRows(const QList<ops::Charger> &chargers);
	int selectedChargerRow() const;
	void updateChargerActions();
	void reloadSelectedStation();
	void updatePager();

	ops::ApiClient *m_api;
	QLineEdit *m_searchEdit = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_addButton = nullptr;
	QLabel *m_stationHintLabel = nullptr;
	QLabel *m_pageLabel = nullptr;
	QPushButton *m_prevButton = nullptr;
	QPushButton *m_nextButton = nullptr;
	QLabel *m_chargerHeading = nullptr;
	QTableWidget *m_chargerTable = nullptr;
	QPushButton *m_addChargerButton = nullptr;
	QPushButton *m_editChargerButton = nullptr;
	QPushButton *m_deleteChargerButton = nullptr;
	QPushButton *m_restartChargerButton = nullptr;
	QLabel *m_chargerHintLabel = nullptr;
	QList<ops::StationSummary> m_rows;
	QList<ops::Charger> m_chargerRows;
	qint64 m_currentStationId = -1;
	QString m_currentStationName;
	int m_page = 1;
	bool m_hasNext = false;
	bool m_loaded = false;
	bool m_chargerMutationPending = false;
};

// 新增电站对话框；创建后可在当前页面逐个添加电桩。
class AddStationDialog : public QDialog {
	Q_OBJECT
public:
	explicit AddStationDialog(QWidget *parent = nullptr);

	ops::StationForm form() const;

private:
	QLineEdit *m_nameEdit = nullptr;
	QLineEdit *m_latEdit = nullptr;
	QLineEdit *m_lonEdit = nullptr;
	QLineEdit *m_priceEdit = nullptr;
};
