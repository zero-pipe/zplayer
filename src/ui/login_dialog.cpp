#include "ui/login_dialog.h"
#include "ui/icon_field.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "app/SessionManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
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

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowTitle(tr("登录 - Zeroo Player"));
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
    card->setFixedSize(860, 520);

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(56);
    shadow->setColor(QColor(32, 33, 36, 45));
    shadow->setOffset(0, 14);
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
    brand->setFixedWidth(430);

    auto *bv = new QVBoxLayout(brand);
    bv->setContentsMargins(42, 44, 42, 36);
    bv->setSpacing(0);

    auto *hero = new QLabel(brand);
    hero->setPixmap(icons::brandHero(190));
    hero->setFixedSize(190, 190);
    bv->addWidget(hero);
    bv->addSpacing(26);

    auto *name = new QLabel(QStringLiteral("Zeroo Player"), brand);
    name->setObjectName(QStringLiteral("BrandName"));
    bv->addWidget(name);

    bv->addStretch(1);

    cardLay->addWidget(brand);

    // ---- Form panel ----
    auto *form = new QWidget(card);
    form->setObjectName(QStringLiteral("FormPanel"));
    auto *fv = new QVBoxLayout(form);
    fv->setContentsMargins(52, 40, 52, 40);
    fv->setSpacing(0);

    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    const ThemeTokens t = ThemeManager::instance().tokens();
    auto *closeBtn = new QToolButton(form);
    closeBtn->setObjectName(QStringLiteral("FormClose"));
    closeBtn->setIcon(QIcon(icons::glyph(icons::Close, t.muted, 16)));
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    closeRow->addWidget(closeBtn);
    fv->addLayout(closeRow);

    auto *title = new QLabel(tr("欢迎回来"), form);
    title->setObjectName(QStringLiteral("FormTitle"));
    fv->addSpacing(8);
    fv->addWidget(title);
    fv->addSpacing(28);

    m_user = new IconField(form);
    m_user->setLeadPixmap(icons::glyph(icons::User, t.muted, 20));
    m_user->setPlaceholder(tr("用户名"));
    fv->addWidget(m_user);
    fv->addSpacing(12);

    m_pass = new IconField(form);
    m_pass->setLeadPixmap(icons::glyph(icons::Lock, t.muted, 20));
    m_pass->setPlaceholder(tr("密码"));
    m_pass->setEchoPassword(true);
    fv->addWidget(m_pass);
    fv->addSpacing(14);

    auto *optRow = new QHBoxLayout();
    m_remember = new QCheckBox(tr("记住登录"), form);
    // Generous hit area so the option is easy to toggle on scaled displays,
    // and the choice is persisted the moment it changes (not only on a
    // successful login), so "remember me" never silently loses the setting.
    m_remember->setMinimumHeight(26);
    m_remember->setCursor(Qt::PointingHandCursor);
    connect(m_remember, &QCheckBox::toggled, this, [](bool on) {
        SessionManager::instance().saveLoginPrefs(on);
    });

    auto *serverBtn = new QToolButton(form);
    serverBtn->setObjectName(QStringLiteral("ServerCfg"));
    serverBtn->setText(tr("服务器设置"));
    serverBtn->setIcon(QIcon(icons::glyph(icons::Gear, t.text2, 16)));
    serverBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    serverBtn->setCursor(Qt::PointingHandCursor);
    connect(serverBtn, &QToolButton::clicked, this, &LoginDialog::openServerSettings);

    optRow->addWidget(m_remember);
    optRow->addStretch(1);
    optRow->addWidget(serverBtn);
    fv->addLayout(optRow);

    // Shows the server the next login will use, so saving the sheet is visible.
    m_serverHint = new QLabel(form);
    m_serverHint->setObjectName(QStringLiteral("ServerHint"));
    m_serverHint->setStyleSheet(QStringLiteral("color:%1;font-size:12px;")
                                .arg(t.muted.name()));
    fv->addSpacing(4);
    fv->addWidget(m_serverHint);
    fv->addSpacing(16);

    m_error = new QLabel(QString(), form);
    m_error->setObjectName(QStringLiteral("ErrorLabel"));
    m_error->setWordWrap(true);
    fv->addWidget(m_error);

    m_loginBtn = new QPushButton(tr("登 录"), form);
    m_loginBtn->setObjectName(QStringLiteral("LoginButton"));
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setDefault(true);
    fv->addWidget(m_loginBtn);
    fv->addStretch(1);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::submit);
    connect(m_pass, &IconField::returnPressed, this, &LoginDialog::submit);

    cardLay->addWidget(form, 1);

    syncFromPrefs();
}

void LoginDialog::syncFromPrefs()
{
    const auto prefs = SessionManager::instance().loadLoginPrefs();
    m_baseUrl = prefs.baseUrl.isEmpty() ? QStringLiteral("http://127.0.0.1:18080") : prefs.baseUrl;
    m_user->setText(prefs.username);
    m_pass->setText(prefs.password);
    m_remember->setChecked(prefs.remember);
    updateServerHint();
}

void LoginDialog::updateServerHint()
{
    if (!m_serverHint) {
        return;
    }
    const QString alias = SessionManager::instance().serverAlias();
    m_serverHint->setText(alias.isEmpty()
            ? tr("当前平台：%1").arg(m_baseUrl)
            : tr("当前平台：%1（%2）").arg(alias, m_baseUrl));
}

void LoginDialog::openServerSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("服务器设置"));
    dlg.setMinimumWidth(400);

    auto *edit = new QLineEdit(m_baseUrl, &dlg);
    edit->setPlaceholderText(QStringLiteral("http://127.0.0.1:18080"));

    auto *alias = new QLineEdit(SessionManager::instance().serverAlias(), &dlg);
    alias->setPlaceholderText(tr("可选，例如：总部平台"));

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("平台地址"), edit);
    form->addRow(tr("备注名称"), alias);

    auto *hint = new QLabel(tr("配置保存在本机，下次登录自动使用"), &dlg);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:12px;")
                        .arg(ThemeManager::instance().tokens().muted.name()));

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Save)->setText(tr("保存"));
    btns->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *vl = new QVBoxLayout(&dlg);
    vl->setContentsMargins(20, 20, 20, 16);
    vl->setSpacing(12);
    vl->addLayout(form);
    vl->addWidget(hint);
    vl->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted) {
        m_baseUrl = edit->text().trimmed();
        if (m_baseUrl.isEmpty()) {
            m_baseUrl = QStringLiteral("http://127.0.0.1:18080");
        }
        SessionManager::instance().setBaseUrl(m_baseUrl);
        SessionManager::instance().setServerAlias(alias->text().trimmed());
        updateServerHint();
    }
}

QString LoginDialog::baseUrl() const { return m_baseUrl; }
QString LoginDialog::username() const { return m_user->text().trimmed(); }
QString LoginDialog::password() const { return m_pass->text(); }
bool LoginDialog::offlineDemo() const { return false; }
bool LoginDialog::rememberPassword() const { return m_remember->isChecked(); }

void LoginDialog::setAuthHandler(AuthHandler handler)
{
    m_handler = std::move(handler);
}

void LoginDialog::submit()
{
    showError(QString());
    if (baseUrl().isEmpty()) {
        showError(tr("请先设置服务器地址"));
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
    setBusy(true);
    if (m_handler) {
        m_handler(baseUrl(), username(), password(), false);
    } else {
        setBusy(false);
        showError(tr("内部错误：未设置登录处理器"));
    }
}

void LoginDialog::setBusy(bool busy)
{
    m_loginBtn->setEnabled(!busy);
    m_loginBtn->setText(busy ? tr("登录中…") : tr("登 录"));
    m_user->setEnabled(!busy);
    m_pass->setEnabled(!busy);
    m_remember->setEnabled(!busy);
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
