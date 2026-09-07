#pragma once

#include <QDialog>

#include "api/apiclient.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

class ChargerDialog : public QDialog {
	Q_OBJECT
public:
	explicit ChargerDialog(const ops::Charger *charger = nullptr, QWidget *parent = nullptr);
	ChargerDialog(qint64 stationId, const ops::Charger *charger, QWidget *parent = nullptr);

	qint64 stationId() const;
	ops::ChargerForm form() const;

private:
	QLineEdit *m_stationIdEdit = nullptr;
	QComboBox *m_typeBox = nullptr;
	QDoubleSpinBox *m_powerSpin = nullptr;
	QComboBox *m_operationalBox = nullptr;
};
