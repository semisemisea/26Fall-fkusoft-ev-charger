#include "stationpage.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

	enum StationCol {
		ColName = 0,
		ColAddress,
		ColLocation,
		ColPrice,
		ColChargers,
		ColAvailable,
		ColOnlineRate,
		ColStatus,
	};

	enum ChargerCol {
		CColCode = 0,
		CColType,
		CColPower,
		CColStatus,
		CColChargeCount,
		CColChargeMinutes,
	};

} // namespace

// ---- StationPage ----

StationPage::StationPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 24, 24, 24);
	root->setSpacing(16);

	auto *topBar = new QHBoxLayout;
	auto *title = new QLabel(tr("充电站管理"), this);
	title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
	topBar->addWidget(title);
	topBar->addStretch();
	m_searchEdit = new QLineEdit(this);
	m_searchEdit->setPlaceholderText(tr("按站名或地址搜索"));
	m_searchEdit->setClearButtonEnabled(true);
	topBar->addWidget(m_searchEdit, 0, Qt::AlignRight);
	m_addButton = new QPushButton(tr("新增电站"), this);
	m_addButton->setObjectName(QStringLiteral("primary"));
	topBar->addWidget(m_addButton);
	root->addLayout(topBar);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(8);
	m_table->setHorizontalHeaderLabels({tr("站名"), tr("地址"), tr("经纬度"),
										tr("价格 (元/度)"), tr("电桩总数"), tr("空闲"),
										tr("在线率"), tr("状态")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(36);
	root->addWidget(m_table, 2);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	root->addWidget(m_hintLabel);

	// 站内电桩明细:点击行时加载
	auto *detailTable = new QTableWidget(this);
	detailTable->setObjectName(QStringLiteral("stationChargerTable"));
	detailTable->setColumnCount(6);
	detailTable->setHorizontalHeaderLabels(
		{tr("电桩编号"), tr("类型"), tr("功率 (kW)"), tr("状态"), tr("累计次数"),
		 tr("累计时长")});
	detailTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	detailTable->setAlternatingRowColors(true);
	detailTable->verticalHeader()->setVisible(false);
	detailTable->verticalHeader()->setDefaultSectionSize(32);
	detailTable->setMaximumHeight(220);
	root->addWidget(detailTable, 1);

	connect(m_searchEdit, &QLineEdit::returnPressed, this,
			[this] { m_api->fetchStations(m_searchEdit->text().trimmed()); });

	connect(m_api, &ops::ApiClient::stationsFetched, this,
			[this](const QList<ops::StationSummary> &stations) {
				m_rows = stations;
				m_table->setRowCount(stations.size());
				for (int i = 0; i < stations.size(); ++i) {
					const auto &s = stations.at(i);
					m_table->setItem(i, ColName, new QTableWidgetItem(s.name));
					m_table->setItem(i, ColAddress, new QTableWidgetItem(s.address));
					m_table->setItem(
						i, ColLocation,
						new QTableWidgetItem(QStringLiteral("%1, %2")
												 .arg(s.latitude, 0, 'f', 4)
												 .arg(s.longitude, 0, 'f', 4)));
					m_table->setItem(
						i, ColPrice,
						new QTableWidgetItem(ops::fenCents(s.pricePerKwhFen)));
					m_table->setItem(i, ColChargers,
									 new QTableWidgetItem(QString::number(s.chargerCount)));
					m_table->setItem(
						i, ColAvailable,
						new QTableWidgetItem(QString::number(s.availableChargerCount)));
					m_table->setItem(
						i, ColOnlineRate,
						new QTableWidgetItem(
							QStringLiteral("%1%").arg(s.onlineRate * 100, 0, 'f', 1)));
					m_table->setItem(i, ColStatus,
									 new QTableWidgetItem(ops::statusText(s.status)));
				}
				m_hintLabel->setText(tr("共 %1 座电站。点击行查看站内电桩明细。")
										 .arg(stations.size()));
				m_currentStationId = -1;
			});

	connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
		if (row < 0 || row >= m_rows.size())
			return;
		const auto &s = m_rows.at(row);
		showStationChargers(s.id, s.name);
	});

	connect(m_api, &ops::ApiClient::stationChargersFetched, this,
			[this, detailTable](qint64 stationId, const QList<ops::Charger> &chargers) {
				if (stationId != m_currentStationId)
					return; // 响应已过期
				detailTable->setRowCount(chargers.size());
				for (int i = 0; i < chargers.size(); ++i) {
					const auto &c = chargers.at(i);
					detailTable->setItem(i, CColCode, new QTableWidgetItem(c.code));
					detailTable->setItem(
						i, CColType, new QTableWidgetItem(ops::chargerTypeText(c.type)));
					detailTable->setItem(
						i, CColPower, new QTableWidgetItem(QString::number(c.powerKw, 'f', 1)));
					detailTable->setItem(i, CColStatus,
										 new QTableWidgetItem(ops::statusText(c.status)));
					detailTable->setItem(
						i, CColChargeCount,
						new QTableWidgetItem(QString::number(c.totalChargeCount)));
					detailTable->setItem(
						i, CColChargeMinutes,
						new QTableWidgetItem(
							QStringLiteral("%1 h").arg(c.totalChargeMinutes / 60.0, 0, 'f', 1)));
				}
			});

	connect(m_addButton, &QPushButton::clicked, this, [this] {
		if (!m_api->canWrite()) {
			QMessageBox::warning(this, tr("无权限"), tr("只读管理员无法新增电站"));
			return;
		}
		AddStationDialog dlg(this);
		if (dlg.exec() != QDialog::Accepted)
			return;
		m_api->createStation(dlg.form());
	});

	connect(m_api, &ops::ApiClient::stationCreated, this,
			[this](bool ok, const QString &errorCode) {
				if (ok) {
					QMessageBox::information(this, tr("新增电站"),
											 tr("电站创建成功"));
					m_api->fetchStations(m_searchEdit->text().trimmed());
				} else {
					QMessageBox::warning(
						this, tr("新增失败"),
						tr("错误码: %1").arg(errorCode));
				}
			});

	m_addButton->setEnabled(m_api->canWrite());
}

void StationPage::showStationChargers(qint64 stationId, const QString &stationName) {
	m_hintLabel->setText(tr("正在加载 %1 的电桩明细...").arg(stationName));
	m_currentStationId = stationId;
	m_api->fetchStationChargers(stationId);
}

void StationPage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void StationPage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchStations();
}

// ---- AddStationDialog ----

AddStationDialog::AddStationDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("新增电站"));
	setMinimumWidth(380);

	auto *form = new QFormLayout(this);
	form->setLabelAlignment(Qt::AlignRight);

	m_nameEdit = new QLineEdit(this);
	m_nameEdit->setPlaceholderText(tr("如: 软件园充电站"));
	form->addRow(tr("站名"), m_nameEdit);

	m_addressEdit = new QLineEdit(this);
	m_addressEdit->setPlaceholderText(tr("详细地址"));
	form->addRow(tr("地址"), m_addressEdit);

	m_latEdit = new QLineEdit(this);
	m_latEdit->setPlaceholderText(tr("-90 .. 90"));
	form->addRow(tr("纬度"), m_latEdit);

	m_lonEdit = new QLineEdit(this);
	m_lonEdit->setPlaceholderText(tr("-180 .. 180"));
	form->addRow(tr("经度"), m_lonEdit);

	m_priceEdit = new QLineEdit(this);
	m_priceEdit->setPlaceholderText(tr("元/度, 如 0.98"));
	form->addRow(tr("充电价格"), m_priceEdit);

	m_countEdit = new QLineEdit(this);
	m_countEdit->setPlaceholderText(tr("1 .. 64"));
	form->addRow(tr("电桩总数"), m_countEdit);

	m_fastEdit = new QLineEdit(this);
	m_fastEdit->setPlaceholderText(tr("其中快充数量, 其余为慢充"));
	form->addRow(tr("快充数量"), m_fastEdit);

	auto *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (m_nameEdit->text().trimmed().isEmpty() ||
			m_addressEdit->text().trimmed().isEmpty()) {
			QMessageBox::warning(this, tr("信息不完整"), tr("请填写站名和地址"));
			return;
		}
		const double lat = m_latEdit->text().toDouble();
		const double lon = m_lonEdit->text().toDouble();
		if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
			QMessageBox::warning(this, tr("坐标无效"),
								 tr("纬度范围 -90..90, 经度范围 -180..180"));
			return;
		}
		const qint64 count = m_countEdit->text().toLongLong();
		const qint64 fast = m_fastEdit->text().toLongLong();
		if (count < 1 || count > 64 || fast < 0 || fast > count) {
			QMessageBox::warning(this, tr("数量无效"),
								 tr("电桩总数 1..64, 快充数量不超过总数"));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &AddStationDialog::reject);
}

ops::StationForm AddStationDialog::form() const {
	ops::StationForm f;
	f.name = m_nameEdit->text().trimmed();
	f.address = m_addressEdit->text().trimmed();
	f.latitude = m_latEdit->text().toDouble();
	f.longitude = m_lonEdit->text().toDouble();
	f.pricePerKwhFen = qRound64(m_priceEdit->text().toDouble() * 100);
	f.chargerCount = m_countEdit->text().toLongLong();
	f.fastCount = m_fastEdit->text().toLongLong();
	return f;
}
