#ifndef QT_DEFS_HPP
#define QT_DEFS_HPP

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#define CHECK_STATE_CHANGED checkStateChanged
#else
#define CHECK_STATE_CHANGED stateChanged
#endif

#endif // QT_DEFS_HPP
