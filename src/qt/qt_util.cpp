#include <QStringBuilder>
#include <QStringList>
#include "qt_util.hpp"

namespace util
{

    QString DlgFilter(std::initializer_list<QString> extensions, bool last)
    {
        QStringList temp;

        for (auto ext : extensions)
        {
#ifdef Q_OS_UNIX
            if (ext == "*")
            {
                temp.append("*");
                continue;
            }
            temp.append("*." % ext.toUpper());
#endif
            temp.append("*." % ext);
        }

#ifdef Q_OS_UNIX
        temp.removeDuplicates();
#endif
        return " (" % temp.join(' ') % ")" % (!last ? ";;" : "");
    }

}