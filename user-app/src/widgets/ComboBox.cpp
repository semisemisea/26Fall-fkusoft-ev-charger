#include "ComboBox.h"

#include "common/Theme.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QStyleOptionComboBox>
#include <QTimer>

namespace {

class PopupDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? theme::primaryBg() : theme::fillHover());
            painter->drawRoundedRect(option.rect.adjusted(3, 1, -3, -1), 6, 6);
        }
        painter->setPen(selected ? theme::primary() : theme::textPrimary());
        painter->drawText(option.rect.adjusted(12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(34);
        return size;
    }
};

} // namespace

ComboBox::ComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setMinimumHeight(38);
    auto *listView = new QListView(this);
    listView->setItemDelegate(new PopupDelegate(listView));
    listView->setObjectName(QStringLiteral("comboPopup"));
    setView(listView);
    connect(listView, &QAbstractItemView::activated, this, [this] { hidePopup(); });
    connect(listView, &QAbstractItemView::clicked, this, [this] {
        QTimer::singleShot(0, this, [this] { hidePopup(); });
    });
}

void ComboBox::showPopup()
{
    QComboBox::showPopup();
    QWidget *popup = view() ? view()->window() : nullptr;
    if (popup) {
        QPainterPath path;
        path.addRoundedRect(QRectF(popup->rect()), 10, 10);
        popup->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }
}

QSize ComboBox::sizeHint() const
{
    QSize size = QComboBox::sizeHint();
    size.rwidth() += 46;
    size.setHeight(qMax(size.height(), 38));
    return size;
}

void ComboBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(hasFocus() || underMouse() ? theme::primary() : theme::border());
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(frame, 10, 10);

    const QRect textRect = rect().adjusted(12, 0, -34, 0);
    painter.setPen(theme::textPrimary());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width()));

    const QPointF center(width() - 18, height() / 2.0 + 1);
    QPainterPath chevron;
    chevron.moveTo(center.x() - 4.5, center.y() - 2.5);
    chevron.lineTo(center.x(), center.y() + 2.5);
    chevron.lineTo(center.x() + 4.5, center.y() - 2.5);
    painter.setPen(QPen(theme::textSecondary(), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);
}
