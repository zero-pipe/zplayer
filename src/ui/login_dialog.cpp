#include "ui/login_dialog.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "app/SessionManager.h"

#include <QCheckBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace zp {

namespace {
QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("FieldLabel"));
    return l;
}
} // namespace

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowTitle(tr("登录 - ZeroPlayer"));
    setObjectName(QStringLiteral("LoginDialog"));
    buildUi();
}

void LoginDialog::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("LoginRoot"));

    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->addWidget(root);

    auto *card = new QFrame(root);
    card->setObjectName(QStringLiteral("LoginCard"));
    card->setFixedSize(880, 520);

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(48);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(0, 12);
    card->setGraphicsEffect(shadow);

    auto *center = new QHBoxLayout(root);
    center->setContentsMargins(0, 0, 0, 0);
    center->addStretch(1);
    center->addWidget(card);
    center->addStretch(1);

    auto *cardLay = new QHBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    // ---- Brand panel ----
    auto *brand = new QWidget(card);
    brand->setObjectName(QStringLiteral("BrandPanel"));
    brand->setFixedWidth(400);

    auto *bv = new QVBoxLayout(brand);
    bv->setContentsMargins(40, 48, 40, 32);
    bv->setSpacing(14);

    auto *logoRow = new QHBoxLayout();
    auto *logo = new QLabel(brand);
    logo->setPixmap(icons::appLogo(44));
    logo->setFixedSize(44, 44);
    auto *name = new QLabel(QStringLiteral("ZeroPlayer"), brand);
    name->setObjectName(QStringLiteral("BrandName"));
    logoRow->addWidget(logo);
    logoRow->addSpacing(10);
    logoRow->addWidget(name);
    logoRow->addStretch(1);
    bv->addLayout(logoRow);

    auto *tagline = new QLabel(tr("跨平台安防视频客户端"), brand);
    tagline->setObjectName(QStringLiteral("BrandTagline"));
    bv->addWidget(tagline);

    bv->addSpacing(18);
    const QStringList feats = {
        tr("· 组织目录  ·  多宫格实时预览"),
        tr("· 云台控制  ·  预置位联动"),
        tr("· 国标 GB28181 / ONVIF 平台接入"),
    };
    for (const QString &f : feats) {
        auto *fl = new QLabel(f, brand);
        fl->setObjectName(QStringLiteral("BrandFeature"));
        bv->addWidget(fl);
    }
    bv->addStretch(1);

    auto *ver = new QLabel(tr("v2.0  ·  powered by QtAV"), brand);
    ver->setObjectName(QStringLiteral("BrandVersion"));
    bv->addWidget(ver);

    cardLay->addWidget(brand);

    // ---- Form panel ----
    auto *form = new QWidget(card);
    form->setObjectName(QStringLiteral("FormPanel"));
    auto *fv = new QVBoxLayout(form);
    fv->setContentsMargins(44, 28, 44, 28);
    fv->setSpacing(8);

    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    auto *closeBtn = new QToolButton(form);
    closeBtn->setObjectName(QStringLiteral("WinClose"));
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setFixedSize(28, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    closeRow->addWidget(closeBtn);
    fv->addLayout(closeRow);

    auto *title = new QLabel(tr("登录到平台"), form);
    title->setObjectName(QStringLiteral("FormTitle"));
    fv->addWidget(title);

    auto *subtitle = new QLabel(tr("使用您的平台账号登录以加载组织与设备"), form);
    subtitle->setObjectName(QStringLiteral("FormSubtitle"));
    fv->addWidget(subtitle);
    fv->addSpacing(14);

    fv->addWidget(fieldLabel(tr("服务器地址"), form));
    m_server = new QLineEdit(form);
    m_server->setObjectName(QStringLiteral("LoginInput"));
    m_server->setPlaceholderText(QStringLiteral("http://127.0.0.1:18080"));
    fv->addWidget(m_server);
    fv->addSpacing(6);

    fv->addWidget(fieldLabel(tr("用户名"), form));
    m_user = new QLineEdit(form);
    m_user->setObjectName(QStringLiteral("LoginInput"));
    m_user->setPlaceholderText(tr("请输入用户名"));
    fv->addWidget(m_user);
    fv->addSpacing(6);

    fv->addWidget(fieldLabel(tr("密码"), form));
    m_pass = new QLineEdit(form);
    m_pass->setObjectName(QStringLiteral("LoginInput"));
    m_pass->setPlaceholderText(tr("请输入密码"));
    m_pass->setEchoMode(QLineEdit::Password);
    fv->addWidget(m_pass);
    fv->addSpacing(10);

    auto *optRow = new QHBoxLayout();
    m_remember = new QCheckBox(tr("记住密码"), form);
    m_offline = new QCheckBox(tr("离线演示"), form);
    m_offline->setToolTip(tr("勾选后使用内置示例数据，不连接服务器"));
    optRow->addWidget(m_remember);
    optRow->addStretch(1);
    optRow->addWidget(m_offline);
    fv->addLayout(optRow);

    m_error = new QLabel(QString(), form);
    m_error->setObjectName(QStringLiteral("ErrorLabel"));
    m_error->setWordWrap(true);
    fv->addWidget(m_error);

    m_loginBtn = new QPushButton(tr("登  录"), form);
    m_loginBtn->setObjectName(QStringLiteral("LoginButton"));
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setDefault(true);
    fv->addWidget(m_loginBtn);
    fv->addStretch(1);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::submit);
    connect(m_pass, &QLineEdit::returnPressed, this, &LoginDialog::submit);

    cardLay->addWidget(form, 1);

    // Prefill from persisted preferences.
    const auto prefs = SessionManager::instance().loadLoginPrefs();
    m_server->setText(prefs.baseUrl);
    m_user->setText(prefs.username);
    m_pass->setText(prefs.password);
    m_remember->setChecked(prefs.remember);
    m_offline->setChecked(prefs.offlineDemo);
    if (m_server->text().isEmpty()) {
        m_server->setText(QStringLiteral("http://127.0.0.1:18080"));
    }
}

QString LoginDialog::baseUrl() const { return m_server->text().trimmed(); }
QString LoginDialog::username() const { return m_user->text().trimmed(); }
QString LoginDialog::password() const { return m_pass->text(); }
bool LoginDialog::offlineDemo() const { return m_offline->isChecked(); }
bool LoginDialog::rememberPassword() const { return m_remember->isChecked(); }

void LoginDialog::setAuthHandler(AuthHandler handler)
{
    m_handler = std::move(handler);
}

void LoginDialog::submit()
{
    showError(QString());
    if (!m_offline->isChecked()) {
        if (baseUrl().isEmpty()) {
            showError(tr("请填写服务器地址"));
            return;
        }
        if (username().isEmpty()) {
            showError(tr("请填写用户名"));
            return;
        }
        if (password().isEmpty()) {
            showError(tr("请填写密码"));
            return;
        }
    }
    setBusy(true);
    if (m_handler) {
        m_handler(baseUrl(), username(), password(), m_offline->isChecked());
    } else {
        setBusy(false);
        showError(tr("内部错误：未设置登录处理器"));
    }
}

void LoginDialog::setBusy(bool busy)
{
    m_loginBtn->setEnabled(!busy);
    m_loginBtn->setText(busy ? tr("登录中…") : tr("登  录"));
    m_server->setEnabled(!busy);
    m_user->setEnabled(!busy);
    m_pass->setEnabled(!busy);
    m_remember->setEnabled(!busy);
    m_offline->setEnabled(!busy);
}

void LoginDialog::showError(const QString &message)
{
    m_error->setText(message);
}

void LoginDialog::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPos() - frameGeometry().topLeft();
        e->accept();
    }
    QDialog::mousePressEvent(e);
}

void LoginDialog::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPos() - m_dragOffset);
        e->accept();
    }
    QDialog::mouseMoveEvent(e);
}

void LoginDialog::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(e);
}

} // namespace zp
