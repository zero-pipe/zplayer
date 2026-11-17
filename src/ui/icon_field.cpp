#include "ui/icon_field.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QStyle>
#include <QEvent>

namespace zp {

IconField::IconField(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("IconField"));
    setProperty("focused", false);
    setAttribute(Qt::WA_StyledBackground, true);

    m_lead = new QWidget(this);
    m_lead->setFixedSize(20, 20);

    m_edit = new QLineEdit(this);
    m_edit->setObjectName(QStringLiteral("IconInput"));
    m_edit->setFrame(false);
    m_edit->installEventFilter(this);

    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(14, 0, 12, 0);
    hl->setSpacing(11);
    hl->addWidget(m_lead);
    hl->addWidget(m_edit, 1);

    setFixedHeight(48);

    connect(m_edit, &QLineEdit::returnPressed, this, &IconField::returnPressed);
}

void IconField::setLeadPixmap(const QPixmap &pm)
{
    auto *lbl = qobject_cast<QLabel *>(m_lead);
    if (!lbl) {
        lbl = new QLabel(m_lead);
        lbl->setAlignment(Qt::AlignCenter);
        auto *ll = new QVBoxLayout(m_lead);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->addWidget(lbl);
    }
    lbl->setPixmap(pm);
}

void IconField::setPlaceholder(const QString &text) { m_edit->setPlaceholderText(text); }

void IconField::setEchoPassword(bool on)
{
    m_edit->setEchoMode(on ? QLineEdit::Password : QLineEdit::Normal);
}

void IconField::setText(const QString &text) { m_edit->setText(text); }
QString IconField::text() const { return m_edit->text(); }

void IconField::setFocused(bool focused)
{
    setProperty("focused", focused);
    style()->unpolish(this);
    style()->polish(this);
}

bool IconField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::FocusIn) {
            setFocused(true);
        } else if (event->type() == QEvent::FocusOut) {
            setFocused(false);
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace zp
