#ifndef ZP_AI_CHECK_PANEL_H
#define ZP_AI_CHECK_PANEL_H

#include <QAbstractButton>
#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVariantAnimation;

namespace zp {

// Animated pill switch (32x18) used inside the AI check popover. Painted by
// hand so it keeps its own dark palette independently of the app theme.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    // Set the state without emitting toggled() and without animating (used
    // when the panel is synced from the tile's stored settings).
    void setOn(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool hitButton(const QPoint &pos) const override;

private:
    void animateTo(qreal pos);

    qreal m_pos = 0.0;   // knob position, 0 = off, 1 = on
    QVariantAnimation *m_anim = nullptr;
};

// Body of the "AI 播放检查" floating card. The card is 264 px wide, painted by
// this widget itself (#1E242B, 1 px #323B46 border, 8 px radius) plus a soft
// shadow in the transparent margin around it.
class AiCheckPanel : public QWidget {
    Q_OBJECT
public:
    static const int kCardWidth = 264;
    static const int kShadow = 9;   // transparent margin for the soft shadow

    explicit AiCheckPanel(QWidget *parent = nullptr);

    // COCO person/vehicle/dog ids (0 | 2,3,5,7 | 16).
    void setTargets(const QSet<int> &ids);
    QSet<int> targets() const { return m_targets; }
    void setThresholdPct(int pct);
    void setSkipEmpty(bool on);
    void setPauseOnHit(bool on);
    void setCaptureEnabled(bool on);
    // Master switch: the AI check itself (off = detection does not run at all).
    void setAiCheckEnabled(bool on);
    // Retrained models have no 人/车/狗 meaning: hide the pills, show a note
    // and let every class participate.
    void setCustomModel(bool custom);

    // Dev hook: open the threshold dropdown (nested popup behaviour test).
    void openThresholdPopup();

signals:
    void aiCheckToggled(bool on);
    void targetsChanged(const QSet<int> &ids);
    void thresholdChanged(int pct);
    void skipEmptyToggled(bool on);
    void pauseOnHitToggled(bool on);
    void captureToggled(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyPillDim(bool dim);
    void refreshCount();
    void applyPill(QPushButton *pill, const QVector<int> &ids, bool on);
    QWidget *makeRow(const QString &text, ToggleSwitch *sw);

    QWidget *m_pillRow = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_customNote = nullptr;
    QComboBox *m_threshold = nullptr;
    ToggleSwitch *m_master = nullptr;
    ToggleSwitch *m_skip = nullptr;
    ToggleSwitch *m_pause = nullptr;
    ToggleSwitch *m_capture = nullptr;
    QVector<QPushButton *> m_pills;
    QSet<int> m_targets;
    bool m_custom = false;
};

} // namespace zp

#endif // ZP_AI_CHECK_PANEL_H
