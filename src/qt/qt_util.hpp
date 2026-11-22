#ifndef QT_UTIL_HPP
#define QT_UTIL_HPP

#include <QString>
#include <QWidget>

#include <initializer_list>

class QImage;
class QStyle;
class QScreen;
namespace util {
static constexpr auto UUID_MIN_LENGTH = 36;
/* Creates extension list for qt filedialog */
QString DlgFilter(std::initializer_list<QString> extensions, bool last = false);
QString DlgFilter(QStringList extensions, bool last = false);
/* Returns screen the widget is on */
QScreen *screenOfWidget(QWidget *widget);
/* Returns the smallest size in device-independent pixels that covers at least the given
   number of device pixels: sizes are multiplied by the scale factor, so some device pixel
   sizes can't be asked for exactly */
int      logicalSizeForPixels(int pixels, qreal dpr);
/* Puts an image on the clipboard, offering PNG ahead of the other formats */
void     copyImageToClipboard(const QImage &image);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
/* Qt can switch color schemes on its own from 6.8 on, where the platform supports it */
#    define HAS_COLOR_SCHEME_HINT
/* Applies the configured color scheme */
void applyColorScheme(void);
#endif
/* Applies the configured Qt style, falling back to the default one. With the default
   style on Windows, Fusion stands in for the windowsvista style in dark mode. */
void applyStyle(void);
#if defined(Q_OS_WINDOWS) && defined(HAS_COLOR_SCHEME_HINT)
/* The dark palette of Windows also tints alternating rows and checkboxes with the accent
   color, which Fusion and the classic Windows style draw as they are */
#    define NEUTRAL_DARK_ACCENT
/* Returns whether the style should keep the accent color to selections */
bool neutralDarkAccent(const QStyle *style);
#endif
#ifdef Q_OS_WINDOWS
bool isWindowsLightTheme(void);
void setWin11RoundedCorners(WId hwnd, bool enable);
#endif
QString currentUuid();
QString generateUuid(const QString &path);
void    storeCurrentUuid();
bool    compareUuid();
void    generateNewMacAdresses();
bool    hasConfiguredNICs();
};

#endif
