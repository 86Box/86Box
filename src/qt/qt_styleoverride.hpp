#ifndef QT_STYLEOVERRIDE_HPP
#define QT_STYLEOVERRIDE_HPP

#include <QProxyStyle>
#include <QWidget>
#include <QLayout>

class StyleOverride : public QProxyStyle
{
public:
    int styleHint(
        StyleHint hint,
        const QStyleOption *option = nullptr,
        const QWidget *widget = nullptr,
        QStyleHintReturn *returnData = nullptr) const override;

    void polish(QWidget* widget) override;
};

#endif
