#include "stationpage.h"

#include "chargerdialog.h"

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
	m_searchEdit->setPlaceholderText(tr("按站名搜索"));
	m_searchEdit->setClearButtonEnabled(true);
	topBar->addWidget(m_searchEdit, 0, Qt::AlignRight);
	m_addButton = new QPushButton(tr("新增电站"), this);
	m_addButton->setObjectName(QStringLiteral("primary"));
	topBar->addWidget(m_addButton);
	root->addLayout(topBar);

	m_table = new QTableWidget(this);
	m_table->setObjectName(QStringLiteral("stationTable"));
	m_table->setColumnCount(7);
	m_table->setHorizontalHeaderLabels({tr("站名"), tr("经纬度"),
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

	m_stationHintLabel = new QLabel(this);
	m_stationHintLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	root->addWidget(m_stationHintLabel);

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
			m_api->fetchStations(m_searchEdit->text().trimmed(), m_page);
		}
	});
	connect(m_nextButton, &QPushButton::clicked, this, [this] {
		if (m_hasNext) {
			++m_page;
			m_api->fetchStations(m_searchEdit->text().trimmed(), m_page);
		}
	});

	auto *chargerBar = new QHBoxLayout;
	m_chargerHeading = new QLabel(tr("请先选择电站"), this);
	m_chargerHeading->setObjectName(QStringLiteral("stationChargerHeading"));
	m_chargerHeading->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
	chargerBar->addWidget(m_chargerHeading);
	chargerBar->addStretch();
	m_addChargerButton = new QPushButton(tr("新增电桩"), this);
	m_addChargerButton->setObjectName(QStringLiteral("primary"));
	chargerBar->addWidget(m_addChargerButton);
	m_editChargerButton = new QPushButton(tr("编辑"), this);
	m_editChargerButton->setObjectName(QStringLiteral("editStationChargerButton"));
	chargerBar->addWidget(m_editChargerButton);
	m_deleteChargerButton = new QPushButton(tr("删除"), this);
	m_deleteChargerButton->setObjectName(QStringLiteral("danger"));
	chargerBar->addWidget(m_deleteChargerButton);
	m_restartChargerButton = new QPushButton(tr("远程重启"), this);
	m_restartChargerButton->setObjectName(QStringLiteral("danger"));
	chargerBar->addWidget(m_restartChargerButton);
	root->addLayout(chargerBar);

	// 站内电桩明细:点击电站行时加载
	m_chargerTable = new QTableWidget(this);
	m_chargerTable->setObjectName(QStringLiteral("stationChargerTable"));
	m_chargerTable->setColumnCount(6);
	m_chargerTable->setHorizontalHeaderLabels(
		{tr("电桩编号"), tr("类型"), tr("功率 (kW)"), tr("状态"), tr("累计次数"),
		 tr("累计时长")});
	m_chargerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_chargerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_chargerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_chargerTable->setSelectionMode(QAbstractItemView::SingleSelection);
	m_chargerTable->setAlternatingRowColors(true);
	m_chargerTable->verticalHeader()->setVisible(false);
	m_chargerTable->verticalHeader()->setDefaultSectionSize(32);
	m_chargerTable->setMinimumHeight(190);
	root->addWidget(m_chargerTable, 1);

	m_chargerHintLabel = new QLabel(tr("选择电站后可查看和管理站内电桩。"), this);
	m_chargerHintLabel->setObjectName(QStringLiteral("stationChargerHint"));
	m_chargerHintLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	root->addWidget(m_chargerHintLabel);

	connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
		m_page = 1; // 新搜索回到第 1 页
		m_api->fetchStations(m_searchEdit->text().trimmed(), m_page);
	});

	connect(m_api, &ops::ApiClient::stationsFetched, this,
			[this](const QList<ops::StationSummary> &stations, const ops::PageMeta &meta,
				   const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					QMessageBox::warning(this, tr("加载失败"),
										 tr("电站列表加载失败(%1),请稍后重试").arg(errorCode));
					return;
				}
				const qint64 selectedStationId = m_currentStationId;
				m_rows = stations;
				m_table->setRowCount(stations.size());
				int selectedStationRow = -1;
				for (int i = 0; i < stations.size(); ++i) {
					const auto &s = stations.at(i);
					if (s.id == selectedStationId)
						selectedStationRow = i;
					m_table->setItem(i, ColName, new QTableWidgetItem(s.name));
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
				m_stationHintLabel->setText(tr("共 %1 座电站。点击行管理站内电桩。")
												.arg(stations.size()));
				m_hasNext = meta.valid && meta.hasNext;
				updatePager();
				if (selectedStationRow >= 0) {
					m_table->selectRow(selectedStationRow);
					const auto &station = stations.at(selectedStationRow);
					showStationChargers(station.id, station.name);
				} else {
					m_currentStationId = -1;
					m_currentStationName.clear();
					m_chargerRows.clear();
					m_chargerTable->setRowCount(0);
					m_chargerHeading->setText(tr("请先选择电站"));
					m_chargerHintLabel->setText(tr("选择电站后可查看和管理站内电桩。"));
					updateChargerActions();
				}
			});

	connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
		if (row < 0 || row >= m_rows.size())
			return;
		const auto &s = m_rows.at(row);
		showStationChargers(s.id, s.name);
	});

	connect(m_api, &ops::ApiClient::stationChargersFetched, this,
			[this](qint64 stationId, const QList<ops::Charger> &chargers,
				   const QString &errorCode) {
				if (stationId != m_currentStationId)
					return; // 响应已过期
				if (!errorCode.isEmpty()) {
					m_chargerHintLabel->setText(
						tr("站内电桩加载失败(%1)，请稍后重试。").arg(errorCode));
					return;
				}
				m_chargerRows = chargers;
				applyChargerRows(chargers);
				updateChargerActions();
			});

	connect(m_chargerTable, &QTableWidget::itemSelectionChanged, this,
			&StationPage::updateChargerActions);

	connect(m_addChargerButton, &QPushButton::clicked, this, [this] {
		if (m_currentStationId <= 0)
			return;
		ChargerDialog dialog(m_currentStationId, nullptr, this);
		if (dialog.exec() != QDialog::Accepted)
			return;
		m_chargerMutationPending = true;
		updateChargerActions();
		m_api->createCharger(m_currentStationId, dialog.form());
	});

	connect(m_editChargerButton, &QPushButton::clicked, this, [this] {
		const int row = selectedChargerRow();
		if (row < 0 || row >= m_chargerRows.size())
			return;
		const ops::Charger charger = m_chargerRows.at(row);
		ChargerDialog dialog(m_currentStationId, &charger, this);
		if (dialog.exec() != QDialog::Accepted)
			return;
		m_chargerMutationPending = true;
		updateChargerActions();
		m_api->updateCharger(charger.id, dialog.form());
	});

	connect(m_deleteChargerButton, &QPushButton::clicked, this, [this] {
		const int row = selectedChargerRow();
		if (row < 0 || row >= m_chargerRows.size())
			return;
		const ops::Charger charger = m_chargerRows.at(row);
		if (QMessageBox::question(this, tr("删除电桩"),
								  tr("确认从 %1 删除电桩 %2？")
									  .arg(m_currentStationName, charger.code)) != QMessageBox::Yes)
			return;
		m_chargerMutationPending = true;
		updateChargerActions();
		m_api->deleteCharger(charger.id);
	});

	connect(m_restartChargerButton, &QPushButton::clicked, this, [this] {
		const int row = selectedChargerRow();
		if (row < 0 || row >= m_chargerRows.size())
			return;
		const ops::Charger &charger = m_chargerRows.at(row);
		if (!ops::isRestartable(charger))
			return;
		if (QMessageBox::question(this, tr("远程重启"),
								  tr("确认向电桩 %1 下发重启指令？").arg(charger.code)) !=
			QMessageBox::Yes)
			return;
		m_chargerMutationPending = true;
		updateChargerActions();
		m_api->restartCharger(charger.id, tr("管理员远程重启"));
	});

	connect(m_api, &ops::ApiClient::chargerMutationFinished, this,
			[this](const QString &operation, qint64, bool ok, const QString &message) {
				if (!m_chargerMutationPending)
					return;
				m_chargerMutationPending = false;
				if (!ok) {
					QMessageBox::warning(this, tr("操作失败"), message);
					updateChargerActions();
					return;
				}
				const QString action = operation == QLatin1String("create")
										   ? tr("新增")
									   : operation == QLatin1String("update") ? tr("编辑")
																			  : tr("删除");
				QMessageBox::information(this, tr("电桩管理"), tr("电桩%1成功").arg(action));
				updateChargerActions();
				reloadSelectedStation();
			});

	connect(m_api, &ops::ApiClient::commandFinished, this,
			[this](qint64 chargerId, bool ok, const QString &message) {
				if (!m_chargerMutationPending)
					return;
				m_chargerMutationPending = false;
				if (!ok) {
					QMessageBox::warning(this, tr("远程重启失败"), message);
					updateChargerActions();
					return;
				}
				QMessageBox::information(this, tr("远程重启"),
										 tr("电桩 %1：%2").arg(chargerId).arg(message));
				updateChargerActions();
				reloadSelectedStation();
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
					m_page = 1; // 新电站按时间倒序出现在第 1 页
					m_api->fetchStations(m_searchEdit->text().trimmed(), m_page);
				} else {
					QMessageBox::warning(
						this, tr("新增失败"),
						tr("错误码: %1").arg(errorCode));
				}
			});

	m_addButton->setEnabled(m_api->canWrite());
	updateChargerActions();
}

void StationPage::showStationChargers(qint64 stationId, const QString &stationName) {
	m_currentStationId = stationId;
	m_currentStationName = stationName;
	m_chargerRows.clear();
	m_chargerTable->setRowCount(0);
	m_chargerHeading->setText(tr("%1 · 站内电桩").arg(stationName));
	m_chargerHintLabel->setText(tr("正在加载站内电桩..."));
	updateChargerActions();
	m_api->fetchStationChargers(stationId);
}

void StationPage::applyChargerRows(const QList<ops::Charger> &chargers) {
	m_chargerTable->setRowCount(chargers.size());
	for (int i = 0; i < chargers.size(); ++i) {
		const auto &charger = chargers.at(i);
		m_chargerTable->setItem(i, CColCode, new QTableWidgetItem(charger.code));
		m_chargerTable->setItem(
			i, CColType, new QTableWidgetItem(ops::chargerTypeText(charger.type)));
		m_chargerTable->setItem(
			i, CColPower, new QTableWidgetItem(QString::number(charger.powerKw, 'f', 1)));
		m_chargerTable->setItem(
			i, CColStatus, new QTableWidgetItem(ops::chargerStatusText(charger)));
		m_chargerTable->setItem(
			i, CColChargeCount, new QTableWidgetItem(QString::number(charger.totalChargeCount)));
		m_chargerTable->setItem(
			i, CColChargeMinutes,
			new QTableWidgetItem(
				QStringLiteral("%1 h").arg(charger.totalChargeMinutes / 60.0, 0, 'f', 1)));
	}
	m_chargerHintLabel->setText(tr("共 %1 台电桩。选择电桩后可编辑、删除或远程重启。")
									.arg(chargers.size()));
}

int StationPage::selectedChargerRow() const {
	const auto indexes = m_chargerTable->selectionModel()->selectedRows();
	return indexes.isEmpty() ? -1 : indexes.first().row();
}

void StationPage::updateChargerActions() {
	const int row = selectedChargerRow();
	const bool selected = row >= 0 && row < m_chargerRows.size();
	const bool writable = m_api->canWrite() && !m_chargerMutationPending;
	m_addChargerButton->setEnabled(writable && m_currentStationId > 0);
	m_editChargerButton->setEnabled(writable && selected);
	m_deleteChargerButton->setEnabled(writable && selected);
	m_restartChargerButton->setEnabled(
		writable && selected && ops::isRestartable(m_chargerRows.at(row)));
}

void StationPage::reloadSelectedStation() {
	m_stationHintLabel->setText(tr("正在刷新电站和电桩数据..."));
	m_api->fetchStations(m_searchEdit->text().trimmed(), m_page);
}

void StationPage::updatePager() {
	const bool show = m_hasNext || m_page > 1;
	m_prevButton->setVisible(show);
	m_nextButton->setVisible(show);
	m_pageLabel->setVisible(show);
	m_pageLabel->setText(tr("第 %1 页").arg(m_page));
	m_prevButton->setEnabled(m_page > 1);
	m_nextButton->setEnabled(m_hasNext);
}

void StationPage::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);
	refresh();
}

void StationPage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchStations({}, m_page);
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

	m_latEdit = new QLineEdit(this);
	m_latEdit->setPlaceholderText(tr("-90 .. 90"));
	form->addRow(tr("纬度"), m_latEdit);

	m_lonEdit = new QLineEdit(this);
	m_lonEdit->setPlaceholderText(tr("-180 .. 180"));
	form->addRow(tr("经度"), m_lonEdit);

	m_priceEdit = new QLineEdit(this);
	m_priceEdit->setPlaceholderText(tr("元/度, 如 0.98"));
	form->addRow(tr("充电价格"), m_priceEdit);

	auto *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		if (m_nameEdit->text().trimmed().isEmpty()) {
			QMessageBox::warning(this, tr("信息不完整"), tr("请填写站名"));
			return;
		}
		bool latitudeOk = false;
		bool longitudeOk = false;
		bool priceOk = false;
		const double lat = m_latEdit->text().toDouble(&latitudeOk);
		const double lon = m_lonEdit->text().toDouble(&longitudeOk);
		const double priceYuan = m_priceEdit->text().toDouble(&priceOk);
		if (!latitudeOk || !longitudeOk || !qIsFinite(lat) || !qIsFinite(lon) || lat < -90 ||
			lat > 90 || lon < -180 || lon > 180) {
			QMessageBox::warning(this, tr("坐标无效"),
								 tr("纬度范围 -90..90, 经度范围 -180..180"));
			return;
		}
		if (!priceOk || !qIsFinite(priceYuan) || qRound64(priceYuan * 100) <= 0) {
			QMessageBox::warning(this, tr("价格无效"), tr("请输入大于 0 的充电价格"));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &AddStationDialog::reject);
}

ops::StationForm AddStationDialog::form() const {
	ops::StationForm f;
	f.name = m_nameEdit->text().trimmed();
	f.latitude = m_latEdit->text().toDouble();
	f.longitude = m_lonEdit->text().toDouble();
	f.pricePerKwhFen = qRound64(m_priceEdit->text().toDouble() * 100);
	return f;
}
