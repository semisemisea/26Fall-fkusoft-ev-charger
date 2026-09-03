#pragma once

// 充电站管理页:电站列表(名称/地址/经纬度/价格/电桩数/空闲数/在线率/状态)。
// 点击电站行查看站内电桩明细;新增电站通过 AddStationDialog 完成(仅 ADMIN)。

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

	ops::ApiClient *m_api;
	QLineEdit *m_searchEdit = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_addButton = nullptr;
	QLabel *m_hintLabel = nullptr;
	QList<ops::StationSummary> m_rows;
	qint64 m_currentStationId = -1;
	bool m_loaded = false;
};

// 新增电站对话框:字段 + 快充/慢充数量拆分
class AddStationDialog : public QDialog {
	Q_OBJECT
public:
	explicit AddStationDialog(QWidget *parent = nullptr);

	ops::StationForm form() const;

private:
	QLineEdit *m_nameEdit = nullptr;
	QLineEdit *m_addressEdit = nullptr;
	QLineEdit *m_latEdit = nullptr;
	QLineEdit *m_lonEdit = nullptr;
	QLineEdit *m_priceEdit = nullptr;
	QLineEdit *m_countEdit = nullptr;
	QLineEdit *m_fastEdit = nullptr;
};
