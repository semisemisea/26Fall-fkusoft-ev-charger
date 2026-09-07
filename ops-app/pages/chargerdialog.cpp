#include "chargerdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

ChargerDialog::ChargerDialog(const ops::Charger *charger, QWidget *parent)
	: QDialog(parent) {
	const bool editing = charger != nullptr;
	setWindowTitle(editing ? tr("编辑电桩") : tr("新增电桩"));
	setMinimumWidth(360);

	auto *layout = new QFormLayout(this);
	layout->setLabelAlignment(Qt::AlignRight);
	m_stationIdEdit = new QLineEdit(this);
	m_stationIdEdit->setObjectName(QStringLiteral("stationIdEdit"));
	m_stationIdEdit->setValidator(
		new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[1-9][0-9]{0,18}")),
										this));
	m_stationIdEdit->setPlaceholderText(tr("正整数电站 ID"));
	layout->addRow(tr("所属电站"), m_stationIdEdit);

	m_typeBox = new QComboBox(this);
	m_typeBox->addItem(tr("快充"), QStringLiteral("fast"));
	m_typeBox->addItem(tr("慢充"), QStringLiteral("slow"));
	layout->addRow(tr("类型"), m_typeBox);

	m_powerSpin = new QDoubleSpinBox(this);
	m_powerSpin->setRange(0.001, 1000.0);
	m_powerSpin->setDecimals(3);
	m_powerSpin->setSuffix(tr(" kW"));
	m_powerSpin->setValue(120.0);
	layout->addRow(tr("功率"), m_powerSpin);

	m_operationalBox = new QComboBox(this);
	m_operationalBox->addItem(tr("在线"), QStringLiteral("online"));
	m_operationalBox->addItem(tr("故障"), QStringLiteral("fault"));
	m_operationalBox->addItem(tr("离线"), QStringLiteral("offline"));
	if (editing)
		layout->addRow(tr("运维状态"), m_operationalBox);

	if (editing) {
		m_stationIdEdit->setText(QString::number(charger->stationId));
		m_stationIdEdit->setReadOnly(true);
		m_typeBox->setCurrentIndex(m_typeBox->findData(charger->type));
		m_powerSpin->setValue(charger->powerKw);
		m_operationalBox->setCurrentIndex(
			m_operationalBox->findData(charger->operationalStatus));
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		bool validId = false;
		const qint64 id = m_stationIdEdit->text().toLongLong(&validId);
		if (!validId || id <= 0) {
			QMessageBox::warning(this, tr("电站无效"), tr("请输入有效的正整数电站 ID"));
			return;
		}
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &ChargerDialog::reject);
}

ChargerDialog::ChargerDialog(qint64 stationId, const ops::Charger *charger, QWidget *parent)
	: ChargerDialog(charger, parent) {
	m_stationIdEdit->setText(QString::number(stationId));
	m_stationIdEdit->setReadOnly(true);
}

qint64 ChargerDialog::stationId() const { return m_stationIdEdit->text().toLongLong(); }

ops::ChargerForm ChargerDialog::form() const {
	ops::ChargerForm result;
	result.type = m_typeBox->currentData().toString();
	result.powerKw = m_powerSpin->value();
	result.operationalStatus = m_operationalBox->currentData().toString();
	return result;
}
