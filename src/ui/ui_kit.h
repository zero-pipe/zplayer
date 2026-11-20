#ifndef ZP_UI_KIT_H
#define ZP_UI_KIT_H

#include <QLabel>
#include <QResizeEvent>

QT_BEGIN_NAMESPACE
class QFrame;
class QWidget;
QT_END_NAMESPACE

namespace zp {
namespace uikit {

// Soft-tinted status chips used across the sample cards / queue / QC pages.
// Colours live in theme.qss (QLabel#Chip[kind="..."]), so both presets work.
enum ChipKind { ChipOk, ChipWarn, ChipBad, ChipInfo, ChipMuted };

const char *chipKindName(ChipKind kind);
QLabel *makeChip(const QString &text, ChipKind kind, QWidget *parent = nullptr);
// Re-tint / re-text an existing chip (repolishes so the property selector applies).
void setChip(QLabel *chip, const QString &text, ChipKind kind);

// White rounded card (QFrame#PanelCard) matching the prototype's surfaces.
QFrame *makeCard(QWidget *parent = nullptr);

// Single-line QLabel that elides its full text to the current width and keeps
// the full string as the tooltip.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setFullText(const QString &text);
    QString fullText() const { return m_full; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout();
    QString m_full;
};

} // namespace uikit
} // namespace zp

#endif // ZP_UI_KIT_H
