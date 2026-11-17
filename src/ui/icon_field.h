#ifndef ZP_ICON_FIELD_H
#define ZP_ICON_FIELD_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

namespace zp {

// A rounded input row: leading icon + line edit (+ optional trailing widget).
// Focus state drives the "focused" QSS property on the container.
class IconField : public QWidget {
    Q_OBJECT
public:
    explicit IconField(QWidget *parent = nullptr);

    void setLeadPixmap(const QPixmap &pm);
    void setPlaceholder(const QString &text);
    void setEchoPassword(bool on);
    void setText(const QString &text);
    QString text() const;
    QLineEdit *edit() const { return m_edit; }

signals:
    void returnPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setFocused(bool focused);
    QWidget *m_lead = nullptr;
    QLineEdit *m_edit = nullptr;
};

} // namespace zp

#endif // ZP_ICON_FIELD_H
