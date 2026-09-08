#include "ChargingView.h"

#include "app/ChargePollThread.h"
#include "common/Format.h"
#include "widgets/AppIcons.h"
#include "widgets/BatteryWaveWidget.h"
#include "widgets/Toast.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// 创建白色圆角信息卡片（样式见 style.qss #chargingMetricCard）
QFrame *makeCard(QWidget *parent, int height)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("chargingMetricCard"));
    card->setFixedHeight(height);
    return card;
}
} // namespace

// 构造：搭建电池、两行信息卡片、预估费用与结束充电按钮
ChargingView::ChargingView(ApiClient &api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setAutoFillBackground(true);
    QPalette pal;
    QPixmap bg(QStringLiteral(":/backgrounds/bg.png"));
    pal.setBrush(QPalette::Window, QBrush(bg.scaled(390, 780, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    setPalette(pal);

    m_batteryWidget = new BatteryWaveWidget(this);
    m_batteryWidget->setFixedSize(134, 266);
    m_batteryWidget->setPercent(0.0);

    // ===== 第一行卡片：续航里程 / 已充入电量（宽高比约 1.5）=====
    auto *row1 = new QHBoxLayout;
    row1->setContentsMargins(16, 0, 16, 0);
    row1->setSpacing(14);

    auto *rangeCard = makeCard(this, 84);
    auto *rangeLayout = new QVBoxLayout(rangeCard);
    rangeLayout->setContentsMargins(14, 8, 14, 10);
    rangeLayout->setSpacing(0);
    m_rangeValueLabel = new QLabel(rangeCard);
    m_rangeValueLabel->setTextFormat(Qt::RichText);
    rangeLayout->addWidget(m_rangeValueLabel);
    row1->addWidget(rangeCard, 1);

    auto *energyCard = makeCard(this, 84);
    auto *energyLayout = new QVBoxLayout(energyCard);
    energyLayout->setContentsMargins(14, 8, 14, 10);
    energyLayout->setSpacing(0);
    m_energyValueLabel = new QLabel(energyCard);
    m_energyValueLabel->setTextFormat(Qt::RichText);
    energyLayout->addWidget(m_energyValueLabel);
    row1->addWidget(energyCard, 1);

    // ===== 第二行卡片：充电方式 / 剩余时间（宽高比约 2）=====
    auto *row2 = new QHBoxLayout;
    row2->setContentsMargins(16, 0, 16, 0);
    row2->setSpacing(14);

    auto *typeCard = makeCard(this, 68);
    auto *typeLayout = new QVBoxLayout(typeCard);
    typeLayout->setContentsMargins(14, 8, 14, 8);
    typeLayout->setSpacing(2);
    m_typeValueLabel = new QLabel(typeCard);
    m_typeValueLabel->setTextFormat(Qt::RichText);
    typeLayout->addWidget(m_typeValueLabel);
    row2->addWidget(typeCard, 1);

    auto *remainCard = makeCard(this, 68);
    auto *remainLayout = new QVBoxLayout(remainCard);
    remainLayout->setContentsMargins(14, 8, 14, 8);
    remainLayout->setSpacing(2);
    m_remainValueLabel = new QLabel(remainCard);
    m_remainValueLabel->setTextFormat(Qt::RichText);
    remainLayout->addWidget(m_remainValueLabel);
    row2->addWidget(remainCard, 1);

    // ===== 预估费用（样式见 style.qss #chargingFeeTitle / #chargingFeeValue）=====
    auto *feeTitleLabel = new QLabel(QStringLiteral("预估费用"), this);
    feeTitleLabel->setAlignment(Qt::AlignCenter);
    feeTitleLabel->setObjectName(QStringLiteral("chargingFeeTitle"));

    m_feeValueLabel = new QLabel(this);
    m_feeValueLabel->setAlignment(Qt::AlignCenter);
    m_feeValueLabel->setObjectName(QStringLiteral("chargingFeeValue"));

    // ===== 结束充电按钮（绿底黑字，圆角为高度一半；样式见 style.qss #chargingStopButton）=====
    m_stopButton = new QPushButton(QStringLiteral("结束充电"), this);
    m_stopButton->setObjectName(QStringLiteral("chargingStopButton"));
    m_stopButton->setFixedHeight(50);
    m_stopButton->setCursor(Qt::PointingHandCursor);

    // ===== 主布局 =====
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 20, 16, 16);
    layout->setSpacing(6);
    layout->addStretch(6);
    layout->addSpacing(50);
    layout->addWidget(m_batteryWidget, 0, Qt::AlignCenter);
    layout->addSpacing(20);
    layout->addLayout(row1);
    layout->addLayout(row2);
    layout->addSpacing(6);
    layout->addWidget(feeTitleLabel);
    layout->addWidget(m_feeValueLabel);
    layout->addStretch(1);
    layout->addWidget(m_stopButton);


    m_pollThread = new ChargePollThread(this);
    connect(m_pollThread, &ChargePollThread::meterUpdated, this, [this](const QJsonObject &orderObject) {
        m_order = Order::fromJson(orderObject);
        updateDisplay(m_order);
    });
    connect(m_stopButton, &QPushButton::clicked, this, &ChargingView::stopCharging);
}

// 打开订单：立即刷新一次显示，并配置、启动 5 秒轮询线程
void ChargingView::open(const Order &order)
{
    m_order = order;
    updateDisplay(order);
    m_pollThread->configure(order.id, m_api.baseUrl(), m_api.accessToken());
    m_pollThread->start();
}

// 页面隐藏时请求停止轮询线程
void ChargingView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_pollThread->requestStop();
}

// 用订单数据刷新电池百分比、四张卡片与预估费用
void ChargingView::updateDisplay(const Order &order)
{
    // 显示用电：充满后停止增加
    const double displayEnergy = qMin(order.energyKwh, kFullBatteryKwh);

    // 电池填充百分比
    const double percent = displayEnergy / kFullBatteryKwh;
    m_batteryWidget->setPercent(percent);

    // 续航里程 = 已充电量 × 每度电公里数
    const double range = displayEnergy * kKmPerKwh;
    m_rangeValueLabel->setText(QStringLiteral(
        "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>续航里程</div>"
        "<div style='font-size:26px; color:#000000; line-height:1.0;'>%1"
        "<span style='font-size:13px; color:#6b7280;'> km</span></div>").arg(range, 0, 'f', 0));

    // 已充入电量
    m_energyValueLabel->setText(QStringLiteral(
        "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>已充入电量</div>"
        "<div style='font-size:26px; color:#000000; line-height:1.0;'>%1"
        "<span style='font-size:13px; color:#6b7280;'> kW/h</span></div>").arg(displayEnergy, 0, 'f', 1));

    // 充电方式（fast->快充，其余->慢充）
    const QString typeText = order.chargerType == QLatin1String("fast")
                                  ? QStringLiteral("快充")
                                  : QStringLiteral("慢充");
    m_typeValueLabel->setText(QStringLiteral(
        "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>充电方式</div>"
        "<div style='font-size:18px; color:#000000; line-height:1.0;'>%1</div>").arg(typeText));

    // 剩余时间 = (满电电量 - 已充电量) × 每度电分钟数
    const double remainKwh = qMax(0.0, kFullBatteryKwh - order.energyKwh);
    const int remainMinutes = qRound(remainKwh * kMinutesPerKwh);
    m_remainValueLabel->setText(QStringLiteral(
        "<div style='font-size:13px; color:#6b7280; line-height:1.0;'>剩余时间</div>"
        "<div style='font-size:20px; color:#000000; line-height:1.0;'>%1"
        "<span style='font-size:13px; color:#6b7280;'> Min</span></div>").arg(remainMinutes));

    // 预估费用
    m_feeValueLabel->setText(QStringLiteral("￥%1").arg(fenToYuan(order.amountFen)));
}

// 弹确认框后请求停止充电；失败时恢复轮询并提示错误
void ChargingView::stopCharging()
{
    const auto choice = QMessageBox::question(this, QStringLiteral("停止充电"),
                                              QStringLiteral("确定停止充电并生成账单吗？"));
    if (choice != QMessageBox::Yes) {
        return;
    }

    m_pollThread->requestStop();
    m_stopButton->setEnabled(false);
    m_api.post(QStringLiteral("/orders/%1/stop").arg(m_order.id), {},
               [this](const QJsonValue &data, const QJsonObject &) {
                   m_stopButton->setEnabled(true);
                   emit orderStopped(Order::fromJson(data.toObject()));
               },
               [this](const ApiError &error) {
                   m_stopButton->setEnabled(true);
                   m_pollThread->configure(m_order.id, m_api.baseUrl(), m_api.accessToken());
                   m_pollThread->start();
                   Toast::error(this, error.message.isEmpty() ? error.code : error.message);
               });
}

// 分钟数转 HH:mm:ss 文本
QString ChargingView::formatDuration(int minutes)
{
    const QTime duration = QTime(0, 0).addSecs(minutes * 60);
    return duration.toString(QStringLiteral("HH:mm:ss"));
}