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

    void polish(QWidget *widget) override;
    QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap, const QStyleOption *option) const override;
};

#endif
