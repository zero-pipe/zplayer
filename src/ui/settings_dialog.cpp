#include "ui/settings_dialog.h"
#include "ui/video_settings_page.h"
#include "player/media_factory.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

namespace zp {

SettingsDialog::SettingsDialog(const VideoSettings &video, const QString &themeName,
                               QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("设置"));
    resize(720, 520);

    // ---- Left: category nav ----
    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("SettingsNav"));
    m_nav->setFixedWidth(170);
    m_nav->addItem(tr("视频"));
    m_nav->addItem(tr("外观"));
    m_nav->addItem(tr("关于"));

    // ---- Right: detail pages ----
    m_stack = new QStackedWidget(this);

    m_video = new VideoSettingsPage(this);
    m_video->setSettings(video);
    connect(m_video, &VideoSettingsPage::settingsChanged,
            this, &SettingsDialog::videoSettingsChanged);
    m_stack->addWidget(m_video);

    m_stack->addWidget(buildAppearancePage(themeName));
    m_stack->addWidget(buildAboutPage());

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    // ---- Footer ----
    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    auto *closeBtn = new QPushButton(tr("关闭"), this);
    closeBtn->setObjectName(QStringLiteral("GhostBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(closeBtn);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(16);
    top->addWidget(m_nav);
    top->addWidget(m_stack, 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 14);
    root->setSpacing(14);
    root->addLayout(top, 1);
    root->addLayout(footer);
}

QWidget *SettingsDialog::buildAppearancePage(const QString &themeName)
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);

    auto *title = new QLabel(tr("外观"), page);
    title->setObjectName(QStringLiteral("VsGroupTitle"));
    v->addWidget(title);

    auto *row = new QHBoxLayout();
    auto *label = new QLabel(tr("主题"), page);
    label->setObjectName(QStringLiteral("VsFieldLabel"));
    m_theme = new QComboBox(page);
    m_theme->setObjectName(QStringLiteral("VsCombo"));
    m_theme->addItem(tr("浅色"), QStringLiteral("light"));
    m_theme->addItem(tr("深色"), QStringLiteral("dark"));
    const int idx = m_theme->findData(themeName);
    if (idx >= 0) {
        m_theme->setCurrentIndex(idx);
    }
    row->addWidget(label);
    row->addWidget(m_theme, 1);
    v->addLayout(row);

    auto *hint = new QLabel(tr("切换后立即生效，也可用工具栏右上角的图标快速切换"), page);
    hint->setObjectName(QStringLiteral("VsHint"));
    v->addWidget(hint);
    v->addStretch(1);

    connect(m_theme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        emit themeChanged(m_theme->currentData().toString());
    });
    return page;
}

QWidget *SettingsDialog::buildAboutPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    auto *title = new QLabel(tr("关于"), page);
    title->setObjectName(QStringLiteral("VsGroupTitle"));
    v->addWidget(title);

    auto addLine = [v, page](const QString &k, const QString &val) {
        auto *l = new QLabel(QStringLiteral("%1  %2").arg(k, val), page);
        l->setObjectName(QStringLiteral("VsFieldLabel"));
        v->addWidget(l);
    };
    addLine(tr("产品"), QStringLiteral("ZeroEngine 智能物联视觉引擎"));
    addLine(tr("媒体引擎"),
            media::resolveBackendName(MediaBackend::Auto));
    addLine(tr("授权"), tr("对接 zero-web-server 平台"));
    v->addStretch(1);
    return page;
}

} // namespace zp
