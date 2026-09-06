#include "chargermanagepage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

	enum Col {
		ColCode = 0,
		ColStationId,
		ColType,
		ColPower,
		ColStatus,
		ColChargeCount,
		ColChargeMinutes,
	};

	QString hoursText(qint64 minutes) {
		return QStringLiteral("%1 h").arg(minutes / 60.0, 0, 'f', 1);
	}

} // namespace

ChargerManagePage::ChargerManagePage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 24, 24, 24);
	root->setSpacing(16);

	auto *topBar = new QHBoxLayout;
	auto *title = new QLabel(tr("充电桩管理"), this);
	title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
	topBar->addWidget(title);
	topBar->addStretch();
	topBar->addWidget(new QLabel(tr("状态筛选:"), this));
	m_statusFilter = new QComboBox(this);
	m_statusFilter->addItem(tr("全部"), QString());
	m_statusFilter->addItem(ops::statusText(QStringLiteral("available")),
							QStringLiteral("available"));
	m_statusFilter->addItem(ops::statusText(QStringLiteral("charging")),
							QStringLiteral("charging"));
	m_statusFilter->addItem(ops::statusText(QStringLiteral("reserved")),
							QStringLiteral("reserved"));
	m_statusFilter->addItem(ops::statusText(QStringLiteral("fault")),
							QStringLiteral("fault"));
	m_statusFilter->addItem(ops::statusText(QStringLiteral("offline")),
							QStringLiteral("offline"));
	topBar->addWidget(m_statusFilter);
	m_restartButton = new QPushButton(tr("远程重启"), this);
	m_restartButton->setObjectName(QStringLiteral("danger"));
	topBar->addWidget(m_restartButton);
	root->addLayout(topBar);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(7);
	m_table->setHorizontalHeaderLabels({tr("电桩编号"), tr("所属电站"), tr("类型"),
										tr("功率 (kW)"), tr("状态"), tr("累计充电次数"),
										tr("累计充电时长")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(36);
	root->addWidget(m_table, 1);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	root->addWidget(m_hintLabel);

	// 分页条:上一页/下一页/页码;服务端未返回分页 meta 时整行隐藏
	auto *pagerRow = new QHBoxLayout;
	pagerRow->addStretch();
	m_prevButton = new QPushButton(tr("上一页"), this);
	m_pageLabel = new QLabel(this);
	m_pageLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	m_nextButton = new QPushButton(tr("下一页"), this);
	pagerRow->addWidget(m_prevButton);
	pagerRow->addWidget(m_pageLabel);
	pagerRow->addWidget(m_nextButton);
	root->addLayout(pagerRow);
	m_prevButton->setEnabled(false);
	m_nextButton->setEnabled(false);
	m_pageLabel->setText(tr("第 1 页"));

	connect(m_prevButton, &QPushButton::clicked, this, [this] {
		if (m_page > 1) {
			--m_page;
			m_api->fetchChargers(m_statusFilter->currentData().toString(), m_page);
		}
	});
	connect(m_nextButton, &QPushButton::clicked, this, [this] {
		if (m_hasNext) {
			++m_page;
			m_api->fetchChargers(m_statusFilter->currentData().toString(), m_page);
		}
	});

	connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this](int) {
		m_page = 1; // 筛选变化回到第 1 页
		m_api->fetchChargers(m_statusFilter->currentData().toString(), m_page);
	});

	connect(m_api, &ops::ApiClient::chargersFetched, this,
			[this](const QList<ops::Charger> &chargers, const ops::PageMeta &meta,
				   const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					QMessageBox::warning(this, tr("加载失败"),
										 tr("电桩列表加载失败(%1),请稍后重试").arg(errorCode));
					return;
				}
				m_rows = chargers;
				applyRows(chargers);
				m_hasNext = meta.valid && meta.hasNext;
				updatePager();
			});

	connect(m_api, &ops::ApiClient::commandFinished, this,
			[this](qint64 chargerId, bool ok, const QString &message) {
				if (ok) {
					QMessageBox::information(this, tr("远程重启"),
											 tr("电桩 %1: %2").arg(chargerId).arg(message));
				} else {
					QMessageBox::warning(this, tr("远程重启失败"), message);
				}
				m_restartButton->setEnabled(true);
			});

	connect(m_restartButton, &QPushButton::clicked, this, [this] {
		const int row = selectedChargerRow();
		if (row < 0 || row >= m_rows.size()) {
			QMessageBox::information(this, tr("远程重启"), tr("请先在列表中选择一个电桩"));
			return;
		}
		const ops::Charger &c = m_rows.at(row);
		if (!m_api->canWrite()) {
			QMessageBox::warning(this, tr("无权限"),
								 tr("只读管理员无法下发远程指令"));
			return;
		}
		const auto confirm = QMessageBox::question(
			this, tr("远程重启"),
			tr("确认向电桩 %1 下发重启指令?").arg(c.code));
		if (confirm != QMessageBox::Yes)
			return;
		m_restartButton->setEnabled(false);
		m_api->restartCharger(c.id, tr("管理员远程重启"));
	});

	// 只读角色禁用写操作
	m_restartButton->setEnabled(m_api->canWrite());
}

void ChargerManagePage::applyRows(const QList<ops::Charger> &chargers) {
	m_table->setRowCount(chargers.size());
	for (int i = 0; i < chargers.size(); ++i) {
		const ops::Charger &c = chargers.at(i);
		m_table->setItem(i, ColCode, new QTableWidgetItem(c.code));
		m_table->setItem(i, ColStationId,
						 new QTableWidgetItem(tr("电站 #%1").arg(c.stationId)));
		m_table->setItem(i, ColType,
						 new QTableWidgetItem(ops::chargerTypeText(c.type)));
		m_table->setItem(i, ColPower,
						 new QTableWidgetItem(QString::number(c.powerKw, 'f', 1)));
		m_table->setItem(i, ColStatus, new QTableWidgetItem(ops::statusText(c.status)));
		m_table->setItem(i, ColChargeCount,
						 new QTableWidgetItem(QString::number(c.totalChargeCount)));
		m_table->setItem(i, ColChargeMinutes,
						 new QTableWidgetItem(hoursText(c.totalChargeMinutes)));
	}
	m_hintLabel->setText(tr("共 %1 台电桩。选中电桩后可执行远程重启。").arg(chargers.size()));
}

void ChargerManagePage::updatePager() {
	const bool show = m_hasNext || m_page > 1;
	m_prevButton->setVisible(show);
	m_nextButton->setVisible(show);
	m_pageLabel->setVisible(show);
	m_pageLabel->setText(tr("第 %1 页").arg(m_page));
	m_prevButton->setEnabled(m_page > 1);
	m_nextButton->setEnabled(m_hasNext);
}

int ChargerManagePage::selectedChargerRow() const {
	const auto indexes = m_table->selectionModel()->selectedRows();
	return indexes.isEmpty() ? -1 : indexes.first().row();
}

void ChargerManagePage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void ChargerManagePage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchChargers(m_statusFilter->currentData().toString(), m_page);
}
