/** @file
 * @brief 营收指标卡片与 7/30 日趋势，包含 Qt Charts 绘图和无图表组件时的文本汇总。
 */
#include "sales_page.h"
#include <evcharger/logging.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#ifdef OPS_APP_HAS_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif

#include <QDateTime>
#include <QPainter>
#include <numeric>

Q_LOGGING_CATEGORY(opsSalespageLog, "evcharger.ops.sales", QtInfoMsg)

namespace {

	/// @brief 初始加载和数据更新共用卡片排版，始终保留指标名称与单位。
	QString cardText(const QString &title, const QString &value) {
		return QStringLiteral("<div style='color:#8a8f98;font-size:13px;font-weight:normal;'>%1</div>"
							  "<div style='font-size:26px;font-weight:bold;'>%2</div>")
			.arg(title.toHtmlEscaped(), value.toHtmlEscaped());
	}

	// 卡片样式在全局 QSS 之外单独控制大数字排版
	/// @brief 预留的大数字排版样式；当前卡片实际使用内联富文本样式。
	const char *kValueStyle = "font-size: 24px; font-weight: bold; background: transparent;";
	/// @brief 预留的卡片标题样式；当前卡片实际使用内联富文本样式。
	const char *kTitleStyle = "color: #8a8f98; background: transparent;";

	/** @brief 将 ISO 时间显示为月日，解析失败时保留前十个字符。
	 * @param isoUtc ISO 时间文本。
	 * @return 有效日期的 MM-dd 文本，解析失败时为原字符串前十字符。
	 */
	QString dayLabel(const QString &isoUtc) {
		const QDateTime dt = QDateTime::fromString(isoUtc, Qt::ISODate);
		return dt.isValid() ? dt.toString(QStringLiteral("MM-dd"))
							: isoUtc.left(10); // 解析失败退回日期前缀
	}

} // namespace

/// @brief 建立营收卡片和可选图表，并连接范围切换和异步指标结果。
SalesPage::SalesPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	setObjectName(QStringLiteral("opsSalesPage"));
	EV_LOG_DEBUG(opsSalespageLog, this) << "SalesPage initialized";
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
#ifdef OPS_APP_HAS_CHARTS
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
#else
	m_chartFallback = new QLabel(tr("营收趋势\n当前 Qt 安装未包含 Charts 组件"), this);
	m_chartFallback->setAlignment(Qt::AlignCenter);
	m_chartFallback->setStyleSheet(
		QStringLiteral("background-color: #22262c; border: 1px solid #2d323a;"
					   " border-radius: 10px; color: #aab1bb;"));
	root->addWidget(m_chartFallback, 1);
#endif

	connect(rangeBox, &QComboBox::currentIndexChanged, this, [this, rangeBox](int) {
		m_range = rangeBox->currentData().toString();
		m_loaded = false; // 强制刷新
		refresh();
	});

	connect(m_api, &ops::ApiClient::dashboardSummaryFetched, this,
			[this](const ops::DashboardSummary &s, const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					EV_LOG_WARNING(opsSalespageLog, this) << "Dashboard or revenue data loading failed";
					m_extraLabel->setText(tr("指标加载失败(%1),请切换时间范围重试").arg(errorCode));
					return;
				}
				m_todayCard->setText(cardText(tr("今日营收 (元)"), ops::fenCents(s.todayRevenueFen)));
				m_monthCard->setText(cardText(tr("本月营收 (元)"), ops::fenCents(s.monthRevenueFen)));
				m_totalCard->setText(cardText(tr("累计营收 (元)"), ops::fenCents(s.totalRevenueFen)));
				m_extraLabel->setText(
					tr("用户 %1 · 电站 %2 · 电桩 %3 · 在线率 %4%")
						.arg(s.userCount)
						.arg(s.stationCount)
						.arg(s.chargerCount)
						.arg(s.onlineRate * 100, 0, 'f', 1));
			});

	connect(m_api, &ops::ApiClient::revenueSeriesFetched, this,
			[this](const QString &range, const QList<ops::RevenuePoint> &points,
				   const QString &errorCode) {
				if (range != m_range)
					return; // 过期响应丢弃
				if (!errorCode.isEmpty()) {
					EV_LOG_WARNING(opsSalespageLog, this) << "Dashboard or revenue data loading failed";
#ifdef OPS_APP_HAS_CHARTS
					m_chart->setTitle(tr("营收趋势(加载失败: %1)").arg(errorCode));
#else
			m_chartFallback->setText(tr("营收趋势(加载失败: %1)").arg(errorCode));
#endif
					return;
				}
#ifdef OPS_APP_HAS_CHARTS
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
#else
		const qint64 orderCount = std::accumulate(
			points.cbegin(), points.cend(), qint64(0),
			[](qint64 sum, const auto &point) { return sum + point.orderCount; });
		const qint64 revenueFen = std::accumulate(
			points.cbegin(), points.cend(), qint64(0),
			[](qint64 sum, const auto &point) { return sum + point.revenueFen; });
		m_chartFallback->setText(
			tr("营收趋势(近%1)\n共 %2 单 · 营收 %3 元")
				.arg(range == QLatin1String("30d") ? tr("30日") : tr("7日"))
				.arg(orderCount)
				.arg(ops::fenCents(revenueFen)));
#endif
			});
}

QLabel *SalesPage::makeCard(const QString &title) {
	auto *card = new QLabel(this);
	card->setProperty("card", true);
	card->setAlignment(Qt::AlignCenter);
	card->setTextFormat(Qt::RichText);
	card->setText(cardText(title, QStringLiteral("—")));
	return card;
}

/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
void SalesPage::showEvent(QShowEvent *event) {
	/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	QWidget::showEvent(event);
	refresh();
}

/// @brief 按页面加载策略发起数据请求，结果由已连接的信号更新控件。
void SalesPage::refresh() {
	EV_LOG_DEBUG(opsSalespageLog, this) << "Page refresh requested";
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchDashboardSummary();
	m_api->fetchRevenueSeries(m_range);
}
