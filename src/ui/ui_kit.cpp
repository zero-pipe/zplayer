#include "ui/ui_kit.h"

#include <QFrame>
#include <QStyle>
#include <QVBoxLayout>

namespace zp {
namespace uikit {

const char *chipKindName(ChipKind kind)
{
    switch (kind) {
    case ChipOk: return "ok";
    case ChipWarn: return "warn";
    case ChipBad: return "bad";
    case ChipInfo: return "info";
    case ChipMuted: return "muted";
    }
    return "muted";
}

QLabel *makeChip(const QString &text, ChipKind kind, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("Chip"));
    l->setProperty("kind", QString::fromLatin1(chipKindName(kind)));
    l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

void setChip(QLabel *chip, const QString &text, ChipKind kind)
{
    if (!chip) {
        return;
    }
    chip->setText(text);
    chip->setProperty("kind", QString::fromLatin1(chipKindName(kind)));
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
}

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("PanelCard"));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);
    return card;
}

ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(10);
}

void ElidedLabel::setFullText(const QString &text)
{
    m_full = text;
    setToolTip(text);
    relayout();
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    relayout();
}

void ElidedLabel::relayout()
{
    if (m_full.isEmpty()) {
        setText(QString());
        return;
    }
    const int avail = qMax(10, contentsRect().width());
    setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, avail));
}

} // namespace uikit
} // namespace zp
