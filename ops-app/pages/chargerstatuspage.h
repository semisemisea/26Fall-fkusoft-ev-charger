#pragma once

// 电桩状态页:分别展示占用状态和运维状态的数量与占比。

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
	void populateTable(QTableWidget *table, const QList<ops::ChargerStatusCount> &rows);

	ops::ApiClient *m_api;
	QTableWidget *m_occupancyTable = nullptr;
	QTableWidget *m_operationalTable = nullptr;
	QLabel *m_totalLabel = nullptr;
};
