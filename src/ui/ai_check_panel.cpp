#include "ui/ai_check_panel.h"

#include "ui/icons.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QVector>

#include <climits>

namespace zp {

namespace {

QColor blend(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

} // namespace

namespace {

// QComboBox with a hand-painted chevron: the QSS "image:" arrow depends on the
// SVG plugin being deployed, and silently disappears when it is not.
class ThresholdCombo : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void showPopup() override
    {
        QComboBox::showPopup();
        // Inside a QMenu popup Qt's default placement is computed against the
        // popup window and lands above/left of the field. Pin the list right
        // under the field instead.
        if (QWidget *pop = view() ? view()->window() : nullptr) {
            const QPoint below = mapToGlobal(QPoint(0, height() + 4));
            pop->move(below);
        }
    }

    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(0x94, 0xA3, 0xB8), 1.4);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        const qreal x = width() - 15.0;
        const qreal y = height() / 2.0 - 1.5;
        QPainterPath chevron;
        chevron.moveTo(x - 3.5, y);
        chevron.lineTo(x, y + 3.5);
        chevron.lineTo(x + 3.5, y);
        p.drawPath(chevron);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// ToggleSwitch
// ---------------------------------------------------------------------------
ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(32, 18);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(120);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pos = v.toReal();
        update();
    });
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        animateTo(on ? 1.0 : 0.0);
    });
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(32, 18);
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return QSize(32, 18);
}

void ToggleSwitch::setOn(bool on)
{
    if (isChecked() != on) {
        const QSignalBlocker blocker(this);
        setChecked(on);
    }
    m_pos = on ? 1.0 : 0.0;
    update();
}

void ToggleSwitch::animateTo(qreal pos)
{
    m_anim->stop();
    m_anim->setStartValue(m_pos);
    m_anim->setEndValue(pos);
    m_anim->start();
}

bool ToggleSwitch::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor offBg(0x2D, 0x36, 0x42);
    const QColor onBg(0x3B, 0x82, 0xF6);
    const QColor offBorder(0x3E, 0x48, 0x56);
    const QColor onBorder(0x3B, 0x82, 0xF6);

    QColor bg = blend(offBg, onBg, m_pos);
    QColor border = blend(offBorder, onBorder, m_pos);
    if (!isEnabled()) {
        bg.setAlpha(110);
        border.setAlpha(110);
    }

    const QRectF track(0.5, 0.5, width() - 1.0, height() - 1.0);
    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);

    // Knob: 14 px disc sliding from x=2 to x=16 with a soft shadow.
    const qreal knob = 14.0;
    const qreal x = 2.0 + m_pos * (width() - knob - 4.0);
    const QRectF knobRect(x, (height() - knob) / 2.0, knob, knob);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 45));
    p.drawEllipse(knobRect.translated(0, 0.8));
    p.setBrush(blend(QColor(0xE2, 0xE8, 0xF0), QColor(0xFF, 0xFF, 0xFF), m_pos));
    p.drawEllipse(knobRect);
}

// ---------------------------------------------------------------------------
// AiCheckPanel
// ---------------------------------------------------------------------------
AiCheckPanel::AiCheckPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("AiCheckPanel"));
    setFixedWidth(kCardWidth + 2 * kShadow);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kShadow + 14, kShadow + 14, kShadow + 14, kShadow + 14);
    root->setSpacing(0);

    // ---- master switch: AI 检查 ----
    // Kept separate from the target pills: turning the check on/off is one
    // click and never depends on what happens to be selected.
    m_master = new ToggleSwitch(this);
    connect(m_master, &QAbstractButton::toggled, this, [this](bool on) {
        // With the check switched off the pills only describe what will be
        // looked for, so they read as idle instead of "already detecting".
        applyPillDim(!on);
        emit aiCheckToggled(on);
    });
    auto *masterRow = makeRow(tr("AI 检查"), m_master);
    if (auto *ml = masterRow->findChild<QLabel *>()) {
        ml->setObjectName(QStringLiteral("AiMasterLabel"));
    }
    root->addWidget(masterRow);

    // ---- group 1: 检查目标 ----
    root->addSpacing(12);
    {
        auto *sep = new QFrame(this);
        sep->setObjectName(QStringLiteral("AiSep"));
        sep->setFixedHeight(1);
        sep->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        root->addWidget(sep);
    }
    root->addSpacing(12);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);
    auto *title = new QLabel(tr("检查目标"), this);
    title->setObjectName(QStringLiteral("AiGroupTitle"));
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("AiTargetCount"));
    head->addWidget(title);
    head->addStretch(1);
    head->addWidget(m_count);
    root->addLayout(head);
    root->addSpacing(9);

    m_pillRow = new QWidget(this);
    auto *pills = new QHBoxLayout(m_pillRow);
    pills->setContentsMargins(0, 0, 0, 0);
    pills->setSpacing(8);

    auto addPill = [this, pills](const QString &text, icons::Glyph g,
                                 const QVector<int> &ids) {
        auto *b = new QPushButton(text, m_pillRow);
        b->setObjectName(QStringLiteral("AiPill"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(30);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setIconSize(QSize(14, 14));
        QIcon ic;
        ic.addPixmap(icons::glyph(g, QColor(0x94, 0xA3, 0xB8), 14), QIcon::Normal, QIcon::Off);
        ic.addPixmap(icons::glyph(g, QColor(0x60, 0xA5, 0xFA), 14), QIcon::Normal, QIcon::On);
        b->setIcon(ic);
        connect(b, &QPushButton::toggled, this, [this, ids](bool on) {
            for (int id : ids) {
                if (on) {
                    m_targets.insert(id);
                } else {
                    m_targets.remove(id);
                }
            }
            refreshCount();
            emit targetsChanged(m_targets);
        });
        pills->addWidget(b);
        m_pills.append(b);
    };
    addPill(tr("人"), icons::Person, {0});
    addPill(tr("车"), icons::Car, {2, 3, 5, 7});
    addPill(tr("狗"), icons::Dog, {16});
    root->addWidget(m_pillRow);
    applyPillDim(true);

    m_customNote = new QLabel(tr("自定义类别模型 · 全部类别参与检查"), this);
    m_customNote->setObjectName(QStringLiteral("AiCustomNote"));
    m_customNote->setWordWrap(true);
    m_customNote->hide();
    root->addWidget(m_customNote);

    root->addSpacing(13);

    // ---- group 2: 相似度阈值 ----
    auto *trow = new QHBoxLayout;
    trow->setContentsMargins(0, 0, 0, 0);
    trow->setSpacing(8);
    auto *tlabel = new QLabel(tr("相似度阈值"), this);
    tlabel->setObjectName(QStringLiteral("AiRowLabel"));
    m_threshold = new ThresholdCombo(this);
    m_threshold->setObjectName(QStringLiteral("AiThreshold"));
    m_threshold->setFixedSize(88, 28);
    m_threshold->setCursor(Qt::PointingHandCursor);
    m_threshold->setFocusPolicy(Qt::NoFocus);
    for (int pct : {30, 50, 60, 70, 80, 90}) {
        m_threshold->addItem(QStringLiteral("%1%").arg(pct), pct);
    }
    connect(m_threshold, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
        const int pct = m_threshold->itemData(idx).toInt();
        if (pct > 0) {
            emit thresholdChanged(pct);
        }
    });
    trow->addWidget(tlabel);
    trow->addStretch(1);
    trow->addWidget(m_threshold);
    root->addLayout(trow);

    const auto addSep = [this, root]() {
        auto *sep = new QFrame(this);
        sep->setObjectName(QStringLiteral("AiSep"));
        sep->setFixedHeight(1);
        sep->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        root->addSpacing(12);
        root->addWidget(sep);
        root->addSpacing(12);
    };

    addSep();

    // ---- group 3: 播放联动 ----
    m_skip = new ToggleSwitch(this);
    connect(m_skip, &QAbstractButton::toggled, this, &AiCheckPanel::skipEmptyToggled);
    root->addWidget(makeRow(tr("无目标跳过"), m_skip));
    root->addSpacing(10);

    m_pause = new ToggleSwitch(this);
    connect(m_pause, &QAbstractButton::toggled, this, &AiCheckPanel::pauseOnHitToggled);
    root->addWidget(makeRow(tr("检出目标暂停"), m_pause));

    addSep();

    // ---- group 4: 调试选项 ----
    m_capture = new ToggleSwitch(this);
    connect(m_capture, &QAbstractButton::toggled, this, &AiCheckPanel::captureToggled);
    root->addWidget(makeRow(tr("样本采集"), m_capture));

    refreshCount();
}

QWidget *AiCheckPanel::makeRow(const QString &text, ToggleSwitch *sw)
{
    auto *row = new QWidget(this);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    auto *label = new QLabel(text, row);
    label->setObjectName(QStringLiteral("AiRowLabel"));
    lay->addWidget(label, 1);
    lay->addWidget(sw, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

void AiCheckPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);

    // Soft drop shadow: a handful of rounded rects fading out towards the edge.
    p.setPen(Qt::NoPen);
    const int layers = 8;
    for (int i = 0; i < layers; ++i) {
        const qreal grow = kShadow - i;
        const QRectF r = card.adjusted(-grow, -grow + 2.0, grow, grow + 2.0);
        const int alpha = qMax(0, int(9.0 * (1.0 - qreal(i) / layers)));
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(r, 8.0 + grow, 8.0 + grow);
    }

    p.setBrush(QColor(0x1E, 0x24, 0x2B));
    p.setPen(QPen(QColor(0x32, 0x3B, 0x46), 1.0));
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
}

void AiCheckPanel::refreshCount()
{
    int n = 0;
    for (QPushButton *p : m_pills) {
        if (p->isChecked()) {
            ++n;
        }
    }
    m_count->setText(tr("已选 %1 项").arg(n));
}

void AiCheckPanel::applyPill(QPushButton *pill, const QVector<int> &ids, bool on)
{
    if (!pill) {
        return;
    }
    const QSignalBlocker blocker(pill);
    pill->setChecked(on);
    for (int id : ids) {
        if (on) {
            m_targets.insert(id);
        } else {
            m_targets.remove(id);
        }
    }
}

void AiCheckPanel::setTargets(const QSet<int> &ids)
{
    m_targets = ids;
    applyPill(m_pills.value(0), {0}, ids.contains(0));
    applyPill(m_pills.value(1), {2, 3, 5, 7}, ids.contains(2));
    applyPill(m_pills.value(2), {16}, ids.contains(16));
    refreshCount();
}

void AiCheckPanel::setThresholdPct(int pct)
{
    const QSignalBlocker blocker(m_threshold);
    int best = 0;
    int bestDelta = INT_MAX;
    for (int i = 0; i < m_threshold->count(); ++i) {
        const int delta = qAbs(m_threshold->itemData(i).toInt() - pct);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = i;
        }
    }
    m_threshold->setCurrentIndex(best);
}

void AiCheckPanel::setSkipEmpty(bool on) { m_skip->setOn(on); }
void AiCheckPanel::setPauseOnHit(bool on) { m_pause->setOn(on); }
void AiCheckPanel::setCaptureEnabled(bool on) { m_capture->setOn(on); }
void AiCheckPanel::setAiCheckEnabled(bool on)
{
    m_master->setOn(on);
    applyPillDim(!on);
}

void AiCheckPanel::setCustomModel(bool custom)
{
    m_custom = custom;
    m_pillRow->setVisible(!custom);
    m_customNote->setVisible(custom);
    m_count->setVisible(!custom);
}

void AiCheckPanel::applyPillDim(bool dim)
{
    for (QPushButton *p : m_pills) {
        p->setProperty("aiDim", dim);
        p->style()->unpolish(p);
        p->style()->polish(p);
    }
}

void AiCheckPanel::openThresholdPopup()
{
    m_threshold->showPopup();
}

} // namespace zp
