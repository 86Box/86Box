#ifndef QT_UTIL_HPP
#define QT_UTIL_HPP

#include <QString>
#include <QWidget>

#include <initializer_list>

class QImage;
class QScreen;
namespace util {
static constexpr auto UUID_MIN_LENGTH = 36;
/* Creates extension list for qt filedialog */
QString DlgFilter(std::initializer_list<QString> extensions, bool last = false);
QString DlgFilter(QStringList extensions, bool last = false);
/* Returns screen the widget is on */
QScreen *screenOfWidget(QWidget *widget);
/* Puts an image on the clipboard, offering PNG ahead of the other formats */
void     copyImageToClipboard(const QImage &image);
#ifdef Q_OS_WINDOWS
#    if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
/* Qt can't switch color schemes on its own before 6.8, so dark mode is emulated with QDarkStyle */
#        define USE_QDARKSTYLE
#    endif
bool isWindowsLightTheme(void);
void setWin11RoundedCorners(WId hwnd, bool enable);
#    ifndef USE_QDARKSTYLE
/* Applies the configured color scheme to the built-in Windows theme */
void applyColorScheme(void);
#    endif
#endif
QString currentUuid();
QString generateUuid(const QString &path);
void    storeCurrentUuid();
bool    compareUuid();
void    generateNewMacAdresses();
bool    hasConfiguredNICs();
};

#endif
