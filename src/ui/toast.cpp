#include "ui/toast.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QEvent>
#include <QStyle>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

namespace zp {

Toast::Toast(QWidget *parent)
    : QFrame(parent)
    , m_text(new QLabel(this))
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("Toast"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hide();

    m_text->setObjectName(QStringLiteral("ToastText"));
    m_text->setWordWrap(true);

    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(14, 10, 16, 10);
    hl->addWidget(m_text);

    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);

    if (parent) {
        parent->installEventFilter(this);
    }
}

void Toast::showMessage(const QString &text, Kind kind, int msecs)
{
    m_text->setText(text);
    const char *k = kind == Success ? "success" : (kind == Error ? "error" : "info");
    setProperty("kind", QString::fromLatin1(k));
    style()->unpolish(this);
    style()->polish(this);
    adjustSize();
    setFixedWidth(qMin(360, sizeHint().width()));
    adjustSize();
    reposition();

    show();
    raise();
    m_timer->start(msecs);
}

bool Toast::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QFrame::eventFilter(watched, event);
}

void Toast::reposition()
{
    if (!parentWidget()) {
        return;
    }
    // Top-centre, like modern players' status pills: visible without covering
    // the picture's lower area where the control bar floats.
    const QRect pr = parentWidget()->rect();
    const int margin = 14;
    move(pr.center().x() - width() / 2, pr.top() + margin);
}

} // namespace zp
