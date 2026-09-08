#pragma once

#include "api/ApiClient.h"
#include "models/Order.h"

#include <QWidget>

class ChargePollThread;
class ChargingSpinnerWidget;
class QLabel;
class QPushButton;

// 充电进行页：ChargePollThread 每 5 秒轮询 GET /orders/{id}，刷新卡片与金额
class ChargingView : public QWidget {
	Q_OBJECT

public:
	explicit ChargingView(ApiClient &api, QWidget *parent = nullptr);
	void open(const Order &order);

signals:
	void orderStopped(const Order &order);

protected:
	void hideEvent(QHideEvent *event) override;

private:
	void updateDisplay(const Order &order);
	void stopCharging();

	ApiClient &m_api;
	Order m_order;
	QString m_chargerType;
	ChargePollThread *m_pollThread = nullptr;
	ChargingSpinnerWidget *m_spinnerWidget = nullptr;
	QLabel *m_rangeValueLabel = nullptr;
	QLabel *m_energyValueLabel = nullptr;
	QLabel *m_typeValueLabel = nullptr;
	QLabel *m_remainValueLabel = nullptr;
	QLabel *m_feeValueLabel = nullptr;
	QPushButton *m_stopButton = nullptr;
};
