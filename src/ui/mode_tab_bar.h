#ifndef ZP_MODE_TAB_BAR_H
#define ZP_MODE_TAB_BAR_H

#include <QWidget>

namespace zp {

// 一级功能切换：扁平文字 + 底部高亮下划线。
// 仅表达“当前顶层工作模式”，不携带任何分组/目录维度信息。
// 视觉刻意弱于二级药丸滑块（PillSegment）。
class ModeTabBar : public QWidget {
    Q_OBJECT
public:
    explicit ModeTabBar(QWidget *parent = nullptr);

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);

signals:
    void currentModeChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect tabRect(int index) const;
    QString tabText(int index) const;

    int m_current = 0;
    int m_hover = -1;
};

} // namespace zp

#endif // ZP_MODE_TAB_BAR_H
