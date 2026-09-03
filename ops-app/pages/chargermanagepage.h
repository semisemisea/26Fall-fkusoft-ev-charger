#pragma once

// 充电桩管理页:电桩列表(编号/电站/类型/功率/状态/累计次数/累计时长),
// 支持按状态筛选;选中后可下发"远程重启"模拟指令(仅 ADMIN)。

#include <QWidget>

#include "api/apiclient.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class ChargerManagePage : public QWidget {
	Q_OBJECT
public:
	explicit ChargerManagePage(ops::ApiClient *api, QWidget *parent = nullptr);

	void refresh();

protected:
	void showEvent(QShowEvent *event) override;

private:
	void applyRows(const QList<ops::Charger> &chargers);
	int selectedChargerRow() const;

	ops::ApiClient *m_api;
	QComboBox *m_statusFilter = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_restartButton = nullptr;
	QLabel *m_hintLabel = nullptr;
	QList<ops::Charger> m_rows;
	bool m_loaded = false;
};
