#ifndef QT_UTIL_HPP
#define QT_UTIL_HPP

#include <QString>
#include <QWidget>

#include <initializer_list>

class QScreen;
namespace util {
static constexpr auto UUID_MIN_LENGTH = 36;
/* Creates extension list for qt filedialog */
QString DlgFilter(std::initializer_list<QString> extensions, bool last = false);
/* Returns screen the widget is on */
QScreen *screenOfWidget(QWidget *widget);
QString currentUuid();
void storeCurrentUuid();
bool compareUuid();
void generateNewMacAdresses();
bool hasConfiguredNICs();
};

#endif
