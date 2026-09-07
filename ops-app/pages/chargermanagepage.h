#pragma once

// 充电桩管理页:电桩列表(编号/电站/类型/功率/状态/累计次数/累计时长),
// 支持按状态筛选以及电桩新增、修改、删除和远程重启(仅 ADMIN)。

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
	void updateActionState();
	void updatePager();

	ops::ApiClient *m_api;
	QComboBox *m_statusFilter = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_addButton = nullptr;
	QPushButton *m_editButton = nullptr;
	QPushButton *m_deleteButton = nullptr;
	QPushButton *m_restartButton = nullptr;
	QLabel *m_hintLabel = nullptr;
	QLabel *m_pageLabel = nullptr;
	QPushButton *m_prevButton = nullptr;
	QPushButton *m_nextButton = nullptr;
	QList<ops::Charger> m_rows;
	int m_page = 1;
	bool m_hasNext = false;
	bool m_loaded = false;
	bool m_mutationPending = false;
};
