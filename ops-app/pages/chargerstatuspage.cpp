#include "chargerstatuspage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

	enum Col { ColStatus = 0,
			   ColCount,
			   ColPercent,
			   ColBar };

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
	m_totalLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	topBar->addWidget(m_totalLabel);
	root->addLayout(topBar);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(4);
	m_table->setHorizontalHeaderLabels(
		{tr("状态"), tr("数量"), tr("占比"), tr("分布")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(40);
	root->addWidget(m_table, 1);

	connect(m_api, &ops::ApiClient::chargerStatusFetched, this,
			[this](const QList<ops::ChargerStatusCount> &rows, const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					m_totalLabel->setText(tr("状态分布加载失败(%1)").arg(errorCode));
					return;
				}
				qint64 total = 0;
				for (const auto &r : rows)
					total += r.count;
				m_totalLabel->setText(tr("电桩总数: %1").arg(total));
				m_table->setRowCount(rows.size());
				for (int i = 0; i < rows.size(); ++i) {
					const auto &r = rows.at(i);
					auto *statusItem =
						new QTableWidgetItem(ops::statusText(r.status));
					statusItem->setData(Qt::UserRole, r.status);
					m_table->setItem(i, ColStatus, statusItem);
					m_table->setItem(i, ColCount,
									 new QTableWidgetItem(QString::number(r.count)));
					m_table->setItem(
						i, ColPercent,
						new QTableWidgetItem(
							QStringLiteral("%1%").arg(r.percent * 100, 0, 'f', 1)));
					// 简易条形:用背景色块长度模拟占比展示
					auto *barItem = new QTableWidgetItem();
					barItem->setBackground(
						QColor(0x4d, 0xa3, 0xff, int(40 + 160 * qBound(0.0, r.percent, 1.0))));
					m_table->setItem(i, ColBar, barItem);
				}
			});
}

void ChargerStatusPage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void ChargerStatusPage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchChargerStatus();
}
