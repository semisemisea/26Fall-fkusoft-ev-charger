#include "chargerstatuspage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

	enum Col { ColStatus = 0,
			   ColCount,
			   ColPercent,
			   ColBar };

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

ChargerStatusPage::ChargerStatusPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
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
					m_totalLabel->setText(tr("状态分布加载失败(%1)").arg(errorCode));
					return;
				}
				m_totalLabel->setText(tr("电桩总数: %1").arg(snapshot.total));
				populateTable(m_occupancyTable, snapshot.occupancy);
				populateTable(m_operationalTable, snapshot.operational);
			});
}

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

void ChargerStatusPage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void ChargerStatusPage::refresh() {
	m_totalLabel->setText(tr("正在刷新..."));
	m_api->fetchChargerStatus();
}
