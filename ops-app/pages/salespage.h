#pragma once

// 销售业绩页:三大营收指标卡片 + 近7日/30日营收趋势折线图(QtCharts)。

#include <QWidget>

#ifdef OPS_APP_HAS_CHARTS
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#endif

#include "api/apiclient.h"

class QLabel;
#ifdef OPS_APP_HAS_CHARTS
class QLineSeries;
class QChart;
#endif

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

#ifdef OPS_APP_HAS_CHARTS
	QChart *m_chart = nullptr;
	QLineSeries *m_series = nullptr;
	QDateTimeAxis *m_axisX = nullptr;
	QValueAxis *m_axisY = nullptr;
#else
	QLabel *m_chartFallback = nullptr;
#endif
	QString m_range = QStringLiteral("7d");
	bool m_loaded = false;
};
