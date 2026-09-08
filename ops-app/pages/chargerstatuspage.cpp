/** @file
 * @brief 双维度电桩统计页面，分别绘制占用与运维分布并在每次显示时刷新。
 */
#include "chargerstatuspage.h"
#include <evcharger/logging.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(opsChargerstatuspageLog, "evcharger.ops.chargers", QtInfoMsg)

namespace {

	/// @brief 表格列索引；与表头和填充位置保持一致。
	enum Col { ColStatus = 0,
			   ColCount,
			   ColPercent,
			   ColBar };

	/** @brief 创建不可编辑、不可选择的四列状态统计表。
	 * @param objectName 用于界面对象查询与测试定位的名称。
	 * @param parent 拥有此表的父控件。
	 * @return 由 parent 管理的表格，包含状态、数量、占比与分布四列。
	 */
	QTableWidget *createStatusTable(const QString &objectName, QWidget *parent) {
		auto *table = new QTableWidget(parent);
		table->setObjectName(objectName);
		table->setColumnCount(4);
		table->setHorizontalHeaderLabels(
			{QObject::tr("状态"), QObject::tr("数量"), QObject::tr("占比"), QObject::tr("分布")});
		table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
		table->setColumnWidth(ColStatus, 90);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionMode(QAbstractItemView::NoSelection);
		table->setAlternatingRowColors(true);
		table->verticalHeader()->setVisible(false);
		table->verticalHeader()->setDefaultSectionSize(40);
		return table;
	}

} // namespace

/// @brief 建立两个独立状态表并连接快照结果，加载由显示事件触发。
ChargerStatusPage::ChargerStatusPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	setObjectName(QStringLiteral("opsChargerStatusPage"));
	EV_LOG_DEBUG(opsChargerstatuspageLog, this) << "ChargerStatusPage initialized";
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 24, 24, 24);
	root->setSpacing(16);

	auto *topBar = new QHBoxLayout;
	auto *title = new QLabel(tr("电桩状态"), this);
	title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
	topBar->addWidget(title);
	topBar->addStretch();
	m_totalLabel = new QLabel(this);
	m_totalLabel->setObjectName(QStringLiteral("chargerTotalLabel"));
	m_totalLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	topBar->addWidget(m_totalLabel);
	root->addLayout(topBar);

	auto *tables = new QHBoxLayout;
	auto *occupancyColumn = new QVBoxLayout;
	auto *occupancyTitle = new QLabel(tr("占用状态"), this);
	occupancyTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
	occupancyColumn->addWidget(occupancyTitle);
	m_occupancyTable = createStatusTable(QStringLiteral("occupancyStatusTable"), this);
	occupancyColumn->addWidget(m_occupancyTable, 1);
	tables->addLayout(occupancyColumn, 1);

	auto *operationalColumn = new QVBoxLayout;
	auto *operationalTitle = new QLabel(tr("运维状态"), this);
	operationalTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
	operationalColumn->addWidget(operationalTitle);
	m_operationalTable = createStatusTable(QStringLiteral("operationalStatusTable"), this);
	operationalColumn->addWidget(m_operationalTable, 1);
	tables->addLayout(operationalColumn, 1);
	root->addLayout(tables, 1);

	connect(m_api, &ops::ApiClient::chargerStatusFetched, this,
			[this](const ops::ChargerStatusSnapshot &snapshot, const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					EV_LOG_WARNING(opsChargerstatuspageLog, this) << "Charger status loading failed";
					m_totalLabel->setText(tr("状态分布加载失败(%1)").arg(errorCode));
					return;
				}
				m_totalLabel->setText(tr("电桩总数: %1").arg(snapshot.total));
				populateTable(m_occupancyTable, snapshot.occupancy);
				populateTable(m_operationalTable, snapshot.operational);
			});
}

/// @brief 填充状态、数量、百分比及进度条，进度条按 0..1 限幅。
void ChargerStatusPage::populateTable(QTableWidget *table,
									  const QList<ops::ChargerStatusCount> &rows) {
	table->setRowCount(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		const auto &row = rows.at(i);
		auto *statusItem = new QTableWidgetItem(ops::statusText(row.status));
		statusItem->setData(Qt::UserRole, row.status);
		table->setItem(i, ColStatus, statusItem);
		table->setItem(i, ColCount, new QTableWidgetItem(QString::number(row.count)));
		table->setItem(
			i, ColPercent,
			new QTableWidgetItem(QStringLiteral("%1%").arg(row.percent * 100, 0, 'f', 1)));
		auto *bar = new QProgressBar(table);
		bar->setRange(0, 1000);
		bar->setValue(qRound(qBound(0.0, row.percent, 1.0) * 1000));
		bar->setTextVisible(false);
		table->setCellWidget(i, ColBar, bar);
	}
}

/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
void ChargerStatusPage::showEvent(QShowEvent *event) {
	/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	QWidget::showEvent(event);
	refresh();
}

/// @brief 按页面加载策略发起数据请求，结果由已连接的信号更新控件。
void ChargerStatusPage::refresh() {
	EV_LOG_DEBUG(opsChargerstatuspageLog, this) << "Page refresh requested";
	m_totalLabel->setText(tr("正在刷新..."));
	m_api->fetchChargerStatus();
}
