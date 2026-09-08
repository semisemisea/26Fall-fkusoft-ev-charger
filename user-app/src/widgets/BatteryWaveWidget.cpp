#include "BatteryWaveWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <cmath>

BatteryWaveWidget::BatteryWaveWidget(QWidget *parent)
    : QWidget(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_wavePhase += 0.06;
        if (m_wavePhase > 2 * M_PI) m_wavePhase -= 2 * M_PI;
        update();
    });
    m_timer->start(30);
}

void BatteryWaveWidget::setPercent(double percent)
{
    m_percent = qBound(0.0, percent, 1.0);
    update();
}

void BatteryWaveWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // SVG viewBox: 0 0 17.10 34，等比缩放居中
    const double scale = qMin(width() / 17.10, height() / 34.0);
    const double offsetX = (width() - 17.10 * scale) / 2.0;
    const double offsetY = (height() - 34.0 * scale) / 2.0;

    painter.translate(offsetX, offsetY);
    painter.scale(scale, scale);

    // 正极
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#000000"));
    painter.drawRoundedRect(QRectF(5.80, 2.0, 5.5, 2.0), 1.0, 1.0);

    // 外框（黑色实心）
    painter.setBrush(QColor("#000000"));
    painter.drawRoundedRect(QRectF(1.5, 3.5, 14.10, 29.5), 1.5, 1.5);

    const double fillHeight = 29.0 * m_percent;
    const double fillY = 3.75 + (29.0 - fillHeight);
    const double fillBottom = 3.75 + 29.0;
    const double fillWidth = 13.60;
    const double fillLeft = 1.75;

    if (m_percent > 0.01) {
        // 渐变（底部黄绿 -> 顶部主色）
        QLinearGradient grad(0, fillBottom, 0, fillY);
        grad.setColorAt(0.0, QColor("#FFF200"));
        grad.setColorAt(1.0, QColor("#2BFF7D"));

        const double cornerRadius = 1.25;
        const int wavePoints = 50;
        const double waveAmp = 0.7;

        // 构建带底部圆角的波浪路径
        auto buildWavePath = [&](double baseY, double amp, double freq, double phase) -> QPainterPath {
            QPainterPath p;
            // 底部线：从左下角圆弧终点到右下角圆弧起点
            p.moveTo(fillLeft + cornerRadius, fillBottom);
            p.lineTo(fillLeft + fillWidth - cornerRadius, fillBottom);
            // 右下角圆弧
            p.arcTo(fillLeft + fillWidth - 2 * cornerRadius, fillBottom - 2 * cornerRadius,
                    2 * cornerRadius, 2 * cornerRadius, -90, 90);
            // 右边线到波浪顶部
            p.lineTo(fillLeft + fillWidth, baseY);
            // 波浪顶部（从右到左）
            for (int i = wavePoints; i >= 0; --i) {
                double x = fillLeft + (fillWidth * i / wavePoints);
                double waveY = baseY + amp * std::sin(
                    (x - fillLeft) * freq / fillWidth * 2 * M_PI + phase
                );
                p.lineTo(x, waveY);
            }
            // 左边线到左下角圆弧起点
            p.lineTo(fillLeft, fillBottom - cornerRadius);
            // 左下角圆弧
            p.arcTo(fillLeft, fillBottom - 2 * cornerRadius,
                    2 * cornerRadius, 2 * cornerRadius, 180, 90);
            p.closeSubpath();
            return p;
        };

        // 第一层波浪（主填充）
        QPainterPath path = buildWavePath(fillY, waveAmp, 2.2, m_wavePhase);
        painter.setBrush(grad);
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);

        // 第二层波浪（半透明白色，增加层次感）
        QPainterPath path2 = buildWavePath(fillY + 0.4, waveAmp * 0.55, 3.0, m_wavePhase * 1.4 + 1.2);
        painter.setBrush(QColor(255, 255, 255, 45));
        painter.drawPath(path2);
    }

    // 百分比文字（白色，固定在电池主体区域中心，不随填充高度变化）
    QFont font = painter.font();
    font.setPixelSize(34.0 / scale);
        font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#FFFFFF"));

    const QString text = QString::number(m_percent * 100, 'f', 0) + "%";
    // 电池主体区域：y=3.5 到 y=33.0（黑色圆角矩形）
    const QRectF textRect(0, 3.5, 17.10, 29.5);
    painter.drawText(textRect, Qt::AlignCenter, text);
}