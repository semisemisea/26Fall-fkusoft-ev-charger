#pragma once

// 电桩状态页:当前所有电桩的状态分布(数量与占比),表格呈现。

#include <QWidget>

#include "api/apiclient.h"

class QLabel;
class QTableWidget;

class ChargerStatusPage : public QWidget {
	Q_OBJECT
public:
	explicit ChargerStatusPage(ops::ApiClient *api, QWidget *parent = nullptr);

	void refresh();

protected:
	void showEvent(QShowEvent *event) override;

private:
	ops::ApiClient *m_api;
	QTableWidget *m_table = nullptr;
	QLabel *m_totalLabel = nullptr;
	bool m_loaded = false;
};
