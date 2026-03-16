#ifndef QT_DEFS_HPP
#define QT_DEFS_HPP

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#    define IMG_FLIPPED flipped(Qt::Vertical)
#else
#    define IMG_FLIPPED mirrored(false, true)
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#    define CHECK_STATE_CHANGED checkStateChanged
#else
#    define CHECK_STATE_CHANGED stateChanged
#endif

#ifdef RELEASE_BUILD
#    define EMU_ICON_PATH ":/settings/qt/icons/86Box-green.ico"
#elif defined ALPHA_BUILD
#    define EMU_ICON_PATH ":/settings/qt/icons/86Box-red.ico"
#elif defined BETA_BUILD
#    define EMU_ICON_PATH ":/settings/qt/icons/86Box-yellow.ico"
#else
#    define EMU_ICON_PATH ":/settings/qt/icons/86Box-gray.ico"
#endif

#define SETTINGS_CHANGED            1
#define SETTINGS_REQUIRE_HARD_RESET 2

#ifdef Q_OS_WINDOWS
#define SETTINGS_HEIGHT 472
#else
#define SETTINGS_HEIGHT 502
#endif

#endif // QT_DEFS_HPP
