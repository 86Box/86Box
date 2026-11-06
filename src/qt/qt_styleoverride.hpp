#ifndef QT_STYLEOVERRIDE_HPP
#define QT_STYLEOVERRIDE_HPP

#include <QProxyStyle>
#include <QWidget>
#include <QLayout>
#include <QPixmap>
#include <QIcon>
#include <QStyleOption>

class StyleOverride : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(
        StyleHint           hint,
        const QStyleOption *option     = nullptr,
        const QWidget      *widget     = nullptr,
        QStyleHintReturn   *returnData = nullptr) const override;

    void    polish(QWidget *widget) override;
    QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap, const QStyleOption *option) const override;
    void    drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void    drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void    drawItemText(QPainter *painter, const QRect &rect, int flags, const QPalette &pal, bool enabled,
                         const QString &text, QPalette::ColorRole textRole = QPalette::NoRole) const override;
    QSize   sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget = nullptr) const override;

private:
    bool         isWindows11Style() const;
    mutable bool drawingMenuBarItem = false;
};

#endif
