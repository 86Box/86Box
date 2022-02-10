#ifndef QT_UTIL_HPP
#define QT_UTIL_HPP

#include <QString>
#include <QWidget>

#include <initializer_list>

class QScreen;
namespace util
{
    /* Creates extension list for qt filedialog */
    QString DlgFilter(std::initializer_list<QString> extensions, bool last = false);
    /* Returns screen the widget is on */
    QScreen* screenOfWidget(QWidget* widget);
};

#endif
