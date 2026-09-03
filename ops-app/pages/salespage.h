#pragma once

// 销售业绩页:三大营收指标卡片 + 近7日/30日营收趋势折线图(QtCharts)。

#include <QWidget>

#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#include "api/apiclient.h"

class QLabel;
class QLineSeries;
class QComboBox;
class QChart;

class SalesPage : public QWidget {
	Q_OBJECT
public:
	explicit SalesPage(ops::ApiClient *api, QWidget *parent = nullptr);

	// 切到本页时刷新
	void refresh();

private:
	QLabel *makeCard(const QString &title);
	void showEvent(QShowEvent *event) override;

	ops::ApiClient *m_api;

	QLabel *m_todayCard = nullptr;
	QLabel *m_monthCard = nullptr;
	QLabel *m_totalCard = nullptr;
	QLabel *m_extraLabel = nullptr;

	QChart *m_chart = nullptr;
	QLineSeries *m_series = nullptr;
	QDateTimeAxis *m_axisX = nullptr;
	QValueAxis *m_axisY = nullptr;
	QString m_range = QStringLiteral("7d");
	bool m_loaded = false;
};
