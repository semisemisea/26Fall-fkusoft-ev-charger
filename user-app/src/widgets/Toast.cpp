#include "Toast.h"

#include "common/Demo.h"
#include "common/Theme.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {

void showToast(QWidget *anchor, const QColor &accent, const QString &text)
{
    QWidget *host = anchor ? anchor->window() : nullptr;
    if (!host) {
        return;
    }
    if (auto *existing = host->findChild<QWidget *>(QStringLiteral("toastWrapper"))) {
        existing->deleteLater();
    }

    auto *wrapper = new QWidget(host);
    wrapper->setObjectName(QStringLiteral("toastWrapper"));
    wrapper->setAttribute(Qt::WA_TransparentForMouseEvents);
    wrapper->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *card = new QFrame(wrapper);
    card->setObjectName(QStringLiteral("toast"));

    auto *dot = new QLabel(card);
    dot->setObjectName(QStringLiteral("toastDot"));
    dot->setStyleSheet(QStringLiteral("background: %1;").arg(accent.name()));

    auto *label = new QLabel(text, card);
    label->setObjectName(QStringLiteral("toastText"));

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(14, 9, 16, 9);
    row->setSpacing(8);
    row->addWidget(dot);
    row->addWidget(label);

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(26);
    shadow->setColor(theme::shadowInk());
    shadow->setOffset(0, 4);
    card->setGraphicsEffect(shadow);

    auto *wrapperLayout = new QHBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->addWidget(card);

    wrapper->adjustSize();
    const int x = (host->width() - wrapper->width()) / 2;
    const int targetY = 44;
    wrapper->move(x, targetY - 10);

    auto *opacity = new QGraphicsOpacityEffect(wrapper);
    opacity->setOpacity(0.0);
    wrapper->setGraphicsEffect(opacity);

    auto *fade = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), wrapper);
    fade->setDuration(demo::ms(160));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);

    auto *slide = new QPropertyAnimation(wrapper, QByteArrayLiteral("pos"), wrapper);
    slide->setDuration(demo::ms(200));
    slide->setStartValue(QPoint(x, targetY - 10));
    slide->setEndValue(QPoint(x, targetY));
    slide->setEasingCurve(QEasingCurve::OutCubic);
    slide->start(QAbstractAnimation::DeleteWhenStopped);

    QTimer::singleShot(demo::ms(2400), wrapper, [wrapper, opacity] {
        auto *out = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), wrapper);
        out->setDuration(demo::ms(220));
        out->setStartValue(1.0);
        out->setEndValue(0.0);
        out->setEasingCurve(QEasingCurve::InCubic);
        QObject::connect(out, &QAbstractAnimation::finished, wrapper, &QWidget::deleteLater);
        out->start(QAbstractAnimation::DeleteWhenStopped);
    });

    wrapper->show();
    wrapper->raise();
}

}

namespace Toast {

void success(QWidget *anchor, const QString &text)
{
    showToast(anchor, theme::success(), text);
}

void error(QWidget *anchor, const QString &text)
{
    showToast(anchor, theme::error(), text);
}

void info(QWidget *anchor, const QString &text)
{
    showToast(anchor, theme::primary(), text);
}

}
