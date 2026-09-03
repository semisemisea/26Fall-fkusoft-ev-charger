#include "salespage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QDateTime>
#include <QPainter>
#include <numeric>

namespace {

	// 卡片样式在全局 QSS 之外单独控制大数字排版
	const char *kValueStyle = "font-size: 24px; font-weight: bold; background: transparent;";
	const char *kTitleStyle = "color: #8a8f98; background: transparent;";

	QString dayLabel(const QString &isoUtc) {
		const QDateTime dt = QDateTime::fromString(isoUtc, Qt::ISODate);
		return dt.isValid() ? dt.toString(QStringLiteral("MM-dd"))
							: isoUtc.left(10); // 解析失败退回日期前缀
	}

} // namespace

SalesPage::SalesPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 24, 24, 24);
	root->setSpacing(16);

	// ---- 顶部标题与范围切换 ----
	auto *topBar = new QHBoxLayout;
	auto *title = new QLabel(tr("销售业绩"), this);
	title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
	topBar->addWidget(title);
	topBar->addStretch();
	topBar->addWidget(new QLabel(tr("时间范围:"), this));
	auto *rangeBox = new QComboBox(this);
	rangeBox->addItem(tr("近 7 日"), QStringLiteral("7d"));
	rangeBox->addItem(tr("近 30 日"), QStringLiteral("30d"));
	topBar->addWidget(rangeBox);
	root->addLayout(topBar);

	// ---- 三大指标卡片 ----
	auto *cards = new QHBoxLayout;
	cards->setSpacing(16);
	m_todayCard = makeCard(tr("今日营收 (元)"));
	m_monthCard = makeCard(tr("本月营收 (元)"));
	m_totalCard = makeCard(tr("累计营收 (元)"));
	cards->addWidget(m_todayCard);
	cards->addWidget(m_monthCard);
	cards->addWidget(m_totalCard);
	root->addLayout(cards);

	m_extraLabel = new QLabel(this);
	m_extraLabel->setStyleSheet(
		QStringLiteral("color: #8a8f98; background: transparent;"));
	root->addWidget(m_extraLabel);

	// ---- 营收趋势折线图 ----
	m_series = new QLineSeries(this);
	m_series->setName(QStringLiteral("营收 (元)"));
	m_chart = new QChart();
	m_chart->addSeries(m_series);
	m_chart->legend()->hide();
	m_chart->setTitle(tr("营收趋势"));

	m_axisX = new QDateTimeAxis(m_chart);
	m_axisX->setFormat(QStringLiteral("MM-dd"));
	m_axisX->setLabelsColor(QColor(0xaa, 0xb1, 0xbb));
	m_axisY = new QValueAxis(m_chart);
	m_axisY->setLabelFormat(QStringLiteral("%g"));
	m_axisY->setLabelsColor(QColor(0xaa, 0xb1, 0xbb));
	m_chart->addAxis(m_axisX, Qt::AlignBottom);
	m_chart->addAxis(m_axisY, Qt::AlignLeft);
	m_series->attachAxis(m_axisX);
	m_series->attachAxis(m_axisY);

	auto *chartView = new QChartView(m_chart, this);
	chartView->setRenderHint(QPainter::Antialiasing);
	chartView->setStyleSheet(
		QStringLiteral("background-color: #22262c; border: 1px solid #2d323a;"
					   " border-radius: 10px;"));
	root->addWidget(chartView, 1);

	connect(rangeBox, &QComboBox::currentIndexChanged, this, [this, rangeBox](int) {
		m_range = rangeBox->currentData().toString();
		m_loaded = false; // 强制刷新
		refresh();
	});

	connect(m_api, &ops::ApiClient::dashboardSummaryFetched, this,
			[this](const ops::DashboardSummary &s) {
				m_todayCard->setText(ops::fenCents(s.todayRevenueFen));
				m_monthCard->setText(ops::fenCents(s.monthRevenueFen));
				m_totalCard->setText(ops::fenCents(s.totalRevenueFen));
				m_extraLabel->setText(
					tr("用户 %1 · 电站 %2 · 电桩 %3 · 在线率 %4%")
						.arg(s.userCount)
						.arg(s.stationCount)
						.arg(s.chargerCount)
						.arg(s.onlineRate * 100, 0, 'f', 1));
			});

	connect(m_api, &ops::ApiClient::revenueSeriesFetched, this,
			[this](const QString &range, const QList<ops::RevenuePoint> &points) {
				if (range != m_range)
					return; // 过期响应丢弃
				m_series->clear();
				qint64 maxVal = 1;
				for (const auto &p : points) {
					const QDateTime dt =
						QDateTime::fromString(p.bucketStart, Qt::ISODate);
					if (!dt.isValid())
						continue;
					m_series->append(dt.toMSecsSinceEpoch(),
									 static_cast<double>(p.revenueFen) / 100.0);
					maxVal = qMax(maxVal, p.revenueFen);
				}
				m_axisX->setRange(QDateTime::currentDateTime().addDays(
									  range == QLatin1String("30d") ? -30 : -7),
								  QDateTime::currentDateTime());
				m_axisY->setRange(0, static_cast<double>(maxVal) / 100.0 * 1.2);
				if (points.isEmpty())
					m_chart->setTitle(tr("营收趋势(暂无数据)"));
				else
					m_chart->setTitle(tr("营收趋势(近%1, 共%2单)")
										  .arg(range == QLatin1String("30d") ? tr("30日")
																			 : tr("7日"))
										  .arg(std::accumulate(points.cbegin(), points.cend(),
															   qint64(0),
															   [](qint64 s, const auto &p) {
																   return s + p.orderCount;
															   })));
			});
}

QLabel *SalesPage::makeCard(const QString &title) {
	auto *card = new QLabel(this);
	card->setProperty("card", true);
	card->setAlignment(Qt::AlignCenter);
	card->setText(QStringLiteral("%1\n—").arg(title));
	card->setTextFormat(Qt::RichText);
	card->setText(QStringLiteral(
					  "<div style='color:#8a8f98;font-size:13px;font-weight:normal;'>%1</div>"
					  "<div style='font-size:26px;font-weight:bold;'>—</div>")
					  .arg(title));
	return card;
}

void SalesPage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void SalesPage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchDashboardSummary();
	m_api->fetchRevenueSeries(m_range);
}
