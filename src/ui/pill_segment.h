#ifndef ZP_PILL_SEGMENT_H
#define ZP_PILL_SEGMENT_H

#include <QWidget>
#include <QPropertyAnimation>

class QHBoxLayout;

namespace zp {

// 二级目录维度切换：横向药丸分段滑块。
// 深色圆角轨道 + 主题蓝高亮药丸，点击时滑块平滑滑动（属性动画）。
// 只表达“目录数据维度”，不切换顶层工作模式。
class PillSegment : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thumbX READ thumbX WRITE setThumbX)
public:
    explicit PillSegment(QWidget *parent = nullptr);

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index, bool animated = true);

    qreal thumbX() const;
    void setThumbX(qreal x);

    // 维度文字（默认：行政区划 / 业务分组）。
    void setLabels(const QString &first, const QString &second);

signals:
    void currentIndexChanged(int index);
    void currentGroupChanged(int index);   // 语义别名，接目录树刷新逻辑

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateThumb(bool animated);
    int indexAtPosition(int x) const;

    int m_current = 0;
    qreal m_thumbX = 0;
    QPropertyAnimation *m_anim = nullptr;
    QString m_labels[2] = { QStringLiteral("行政区划"), QStringLiteral("业务分组") };
};

} // namespace zp

#endif // ZP_PILL_SEGMENT_H
