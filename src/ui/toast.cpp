#include "ui/toast.h"

#include <QLabel>
#include <QStatusBar>
#include <QTimer>

namespace zp {

Toast::Toast(QStatusBar *bar, QObject *parent)
    : QObject(parent)
    , m_bar(bar)
    , m_label(new QLabel(bar))
    , m_timer(new QTimer(this))
{
    m_label->setObjectName(QStringLiteral("StatusBarMessage"));
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_label->setVisible(false);

    // permanentWidget keeps the text pinned to the right edge of the bar.
    if (m_bar) {
        m_bar->addPermanentWidget(m_label, 0);
    }

    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Toast::clearMessage);
}

void Toast::setPersistentText(const QString &text)
{
    m_persistent = text;
    if (!m_timer->isActive() || m_label->text().isEmpty()) {
        m_label->setText(m_persistent);
        m_label->setProperty("kind", QStringLiteral("persistent"));
        m_label->style()->unpolish(m_label);
        m_label->style()->polish(m_label);
        m_label->setVisible(true);
    }
}

void Toast::showMessage(const QString &text, Kind kind, int msecs)
{
    if (!m_label) {
        return;
    }
    m_label->setText(text);
    const char *k = kind == Success ? "success" : (kind == Error ? "error" : "info");
    m_label->setProperty("kind", QString::fromLatin1(k));
    m_label->style()->unpolish(m_label);
    m_label->style()->polish(m_label);
    m_label->setVisible(true);
    m_timer->start(msecs);
}

void Toast::clearMessage()
{
    m_label->setText(m_persistent);
    m_label->setProperty("kind", QStringLiteral("persistent"));
    m_label->style()->unpolish(m_label);
    m_label->style()->polish(m_label);
}

} // namespace zp
