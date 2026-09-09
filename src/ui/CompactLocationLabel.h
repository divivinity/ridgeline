#pragma once
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

// Keep long provider/place/file names from increasing toolbar height. QLabel's
// full text remains available to accessibility; only the painted text is elided.
class CompactLocationLabel final : public QLabel {
public:
    explicit CompactLocationLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent) {
        setTextFormat(Qt::PlainText);
        setWordWrap(false);
    }
    QSize sizeHint() const override { return {270, fontMetrics().lineSpacing() * 2 + 8}; }
    QSize minimumSizeHint() const override { return {140, sizeHint().height()}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));
        const auto lines = text().split('\n');
        const int count = qMin(2, int(lines.size()));
        const int lineHeight = fontMetrics().lineSpacing();
        const QRect bounds = contentsRect();
        const int top = bounds.top() + (bounds.height() - count * lineHeight) / 2;
        for (int i = 0; i < count; ++i) {
            const QString line = fontMetrics().elidedText(lines[i], Qt::ElideRight, bounds.width());
            painter.drawText(QRect(bounds.left(), top + i * lineHeight, bounds.width(), lineHeight),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, line);
        }
    }
};
