#ifndef QT_STYLEOVERRIDE_HPP
#define QT_STYLEOVERRIDE_HPP

#include <QProxyStyle>

class StyleOverride : public QProxyStyle
{
public:
    int styleHint(
        StyleHint hint,
        const QStyleOption *option = nullptr,
        const QWidget *widget = nullptr,
        QStyleHintReturn *returnData = nullptr) const override;
};

#endif