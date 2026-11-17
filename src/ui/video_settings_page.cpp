#include "ui/video_settings_page.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QSlider>
#include <QPushButton>
#include <QFileDialog>

namespace zp {

VideoSettingsPage::VideoSettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    // ===================== Hardware decoding =====================
    auto *hwTitle = new QLabel(tr("硬件解码"), this);
    hwTitle->setObjectName(QStringLiteral("VsGroupTitle"));
    root->addWidget(hwTitle);

    auto *hwRow = new QHBoxLayout();
    auto *hwLabel = new QLabel(tr("解码方式"), this);
    hwLabel->setObjectName(QStringLiteral("VsFieldLabel"));
    m_hwdec = new QComboBox(this);
    m_hwdec->setObjectName(QStringLiteral("VsCombo"));
    m_hwdec->addItem(tr("自动（推荐）"), static_cast<int>(HwDecode::Auto));
    m_hwdec->addItem(tr("关闭（软件解码）"), static_cast<int>(HwDecode::Disabled));
    m_hwdec->addItem(QStringLiteral("D3D11VA"), static_cast<int>(HwDecode::D3D11VA));
    m_hwdec->addItem(QStringLiteral("CUDA"), static_cast<int>(HwDecode::Cuda));
    m_hwdec->addItem(QStringLiteral("DXVA2"), static_cast<int>(HwDecode::Dxva2));
    m_hwdec->addItem(QStringLiteral("VideoToolbox"), static_cast<int>(HwDecode::VideoToolbox));
    m_hwdec->addItem(QStringLiteral("VAAPI"), static_cast<int>(HwDecode::Vaapi));
    hwRow->addWidget(hwLabel);
    hwRow->addWidget(m_hwdec, 1);
    root->addLayout(hwRow);

    auto *hwHint = new QLabel(tr("非当前平台支持的模式会自动回退到安全路径，不会报错"), this);
    hwHint->setObjectName(QStringLiteral("VsHint"));
    root->addWidget(hwHint);

    // ===================== Picture adjustments =====================
    auto *picTitle = new QLabel(tr("画面调节"), this);
    picTitle->setObjectName(QStringLiteral("VsGroupTitle"));
    root->addWidget(picTitle);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    int row = 0;

    auto addSlider = [&](const QString &label, QSlider **sliderOut, QLabel **valueOut) {
        auto *l = new QLabel(label, this);
        l->setObjectName(QStringLiteral("VsFieldLabel"));
        auto *sl = new QSlider(Qt::Horizontal, this);
        sl->setRange(-100, 100);
        sl->setValue(0);
        auto *v = new QLabel(QStringLiteral("0"), this);
        v->setObjectName(QStringLiteral("VsValue"));
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(l, row, 0);
        grid->addWidget(sl, row, 1);
        grid->addWidget(v, row, 2);
        ++row;
        *sliderOut = sl;
        *valueOut = v;
    };

    addSlider(tr("亮度"), &m_brightness, &m_brightnessVal);
    addSlider(tr("对比度"), &m_contrast, &m_contrastVal);
    addSlider(tr("饱和度"), &m_saturation, &m_saturationVal);
    addSlider(tr("色相"), &m_hue, &m_hueVal);
    root->addLayout(grid);

    m_deinterlace = new QCheckBox(tr("去隔行（适用于隔行扫描源）"), this);
    root->addWidget(m_deinterlace);

    // ===================== Subtitles =====================
    auto *subTitle = new QLabel(tr("字幕"), this);
    subTitle->setObjectName(QStringLiteral("VsGroupTitle"));
    root->addWidget(subTitle);

    m_subEnabled = new QCheckBox(tr("显示字幕"), this);
    m_subEnabled->setChecked(true);
    root->addWidget(m_subEnabled);

    m_subAutoLoad = new QCheckBox(tr("自动加载影片同名外挂字幕"), this);
    root->addWidget(m_subAutoLoad);

    auto *fileRow = new QHBoxLayout();
    auto *fileLabel = new QLabel(tr("外挂文件"), this);
    fileLabel->setObjectName(QStringLiteral("VsFieldLabel"));
    m_subFile = new QLineEdit(this);
    m_subFile->setObjectName(QStringLiteral("VsCombo"));  // reuse combo styling for the field
    m_subFile->setPlaceholderText(tr("选择 .srt/.ass 字幕文件（可选）"));
    auto *browse = new QPushButton(tr("浏览…"), this);
    browse->setObjectName(QStringLiteral("GhostBtn"));
    browse->setCursor(Qt::PointingHandCursor);
    fileRow->addWidget(fileLabel);
    fileRow->addWidget(m_subFile, 1);
    fileRow->addWidget(browse);
    root->addLayout(fileRow);

    root->addStretch(1);

    // ===================== Wiring: live apply =====================
    connect(m_hwdec, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoSettingsPage::emitChanged);
    connect(m_deinterlace, &QCheckBox::toggled,
            this, &VideoSettingsPage::emitChanged);
    connect(m_subEnabled, &QCheckBox::toggled,
            this, &VideoSettingsPage::emitChanged);
    connect(m_subAutoLoad, &QCheckBox::toggled,
            this, &VideoSettingsPage::emitChanged);
    connect(m_subFile, &QLineEdit::textChanged,
            this, &VideoSettingsPage::emitChanged);

    auto wireSlider = [this](QSlider *sl, QLabel *val) {
        connect(sl, &QSlider::valueChanged, this, [this, val](int v) {
            val->setText(QString::number(v));
            emitChanged();
        });
    };
    wireSlider(m_brightness, m_brightnessVal);
    wireSlider(m_contrast, m_contrastVal);
    wireSlider(m_saturation, m_saturationVal);
    wireSlider(m_hue, m_hueVal);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择字幕文件"), QString(),
            tr("字幕文件 (*.srt *.ass *.ssa *.vtt);;所有文件 (*.*)"));
        if (!f.isEmpty()) {
            m_subFile->setText(f);
        }
    });
}

void VideoSettingsPage::setSettings(const VideoSettings &s)
{
    m_settings = s;
    const QSignalBlocker bHwdec(m_hwdec);
    const QSignalBlocker bDeint(m_deinterlace);
    const QSignalBlocker bSubEn(m_subEnabled);
    const QSignalBlocker bSubAuto(m_subAutoLoad);
    const QSignalBlocker bSubFile(m_subFile);
    const QSignalBlocker bBri(m_brightness);
    const QSignalBlocker bCon(m_contrast);
    const QSignalBlocker bSat(m_saturation);
    const QSignalBlocker bHue(m_hue);

    const int hwIdx = m_hwdec->findData(static_cast<int>(s.hwdec));
    if (hwIdx >= 0) {
        m_hwdec->setCurrentIndex(hwIdx);
    }
    m_deinterlace->setChecked(s.deinterlace);
    m_brightness->setValue(s.filters.brightness);
    m_contrast->setValue(s.filters.contrast);
    m_saturation->setValue(s.filters.saturation);
    m_hue->setValue(s.filters.hue);
    m_brightnessVal->setText(QString::number(s.filters.brightness));
    m_contrastVal->setText(QString::number(s.filters.contrast));
    m_saturationVal->setText(QString::number(s.filters.saturation));
    m_hueVal->setText(QString::number(s.filters.hue));
    m_subEnabled->setChecked(s.subtitle.enabled);
    m_subAutoLoad->setChecked(s.subtitle.autoLoadInFolder);
    m_subFile->setText(s.subtitle.externalFile);
}

void VideoSettingsPage::emitChanged()
{
    VideoSettings s = m_settings;
    s.hwdec = static_cast<HwDecode>(m_hwdec->currentData().toInt());
    s.deinterlace = m_deinterlace->isChecked();
    s.filters.brightness = m_brightness->value();
    s.filters.contrast = m_contrast->value();
    s.filters.saturation = m_saturation->value();
    s.filters.hue = m_hue->value();
    s.subtitle.enabled = m_subEnabled->isChecked();
    s.subtitle.autoLoadInFolder = m_subAutoLoad->isChecked();
    s.subtitle.externalFile = m_subFile->text().trimmed();
    m_settings = s;
    emit settingsChanged(s);
}

} // namespace zp
