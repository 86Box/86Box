#ifndef QT_UTIL_HPP
#define QT_UTIL_HPP

#include <QString>

#include <initializer_list>

namespace util
{
    /* Creates extension list for qt filedialog */
    QString DlgFilter(std::initializer_list<QString> extensions, bool last = false);
};

#endif