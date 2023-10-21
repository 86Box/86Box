/****************************************************************************
** Meta object code from reading C++ file 'qt_winmanagerfilter.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_winmanagerfilter.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_winmanagerfilter.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_WindowsManagerFilter_t {
    QByteArrayData data[10];
    char stringdata0[108];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_WindowsManagerFilter_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_WindowsManagerFilter_t qt_meta_stringdata_WindowsManagerFilter = {
    {
QT_MOC_LITERAL(0, 0, 20), // "WindowsManagerFilter"
QT_MOC_LITERAL(1, 21, 5), // "pause"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 10), // "ctrlaltdel"
QT_MOC_LITERAL(4, 39, 12), // "showsettings"
QT_MOC_LITERAL(5, 52, 5), // "reset"
QT_MOC_LITERAL(6, 58, 16), // "request_shutdown"
QT_MOC_LITERAL(7, 75, 14), // "force_shutdown"
QT_MOC_LITERAL(8, 90, 12), // "dialogstatus"
QT_MOC_LITERAL(9, 103, 4) // "open"

    },
    "WindowsManagerFilter\0pause\0\0ctrlaltdel\0"
    "showsettings\0reset\0request_shutdown\0"
    "force_shutdown\0dialogstatus\0open"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_WindowsManagerFilter[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    0,   50,    2, 0x06 /* Public */,
       4,    0,   51,    2, 0x06 /* Public */,
       5,    0,   52,    2, 0x06 /* Public */,
       6,    0,   53,    2, 0x06 /* Public */,
       7,    0,   54,    2, 0x06 /* Public */,
       8,    1,   55,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    9,

       0        // eod
};

void WindowsManagerFilter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<WindowsManagerFilter *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->pause(); break;
        case 1: _t->ctrlaltdel(); break;
        case 2: _t->showsettings(); break;
        case 3: _t->reset(); break;
        case 4: _t->request_shutdown(); break;
        case 5: _t->force_shutdown(); break;
        case 6: _t->dialogstatus((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::pause)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::ctrlaltdel)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::showsettings)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::reset)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::request_shutdown)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::force_shutdown)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (WindowsManagerFilter::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&WindowsManagerFilter::dialogstatus)) {
                *result = 6;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject WindowsManagerFilter::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_WindowsManagerFilter.data,
    qt_meta_data_WindowsManagerFilter,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *WindowsManagerFilter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WindowsManagerFilter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WindowsManagerFilter.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QAbstractNativeEventFilter"))
        return static_cast< QAbstractNativeEventFilter*>(this);
    return QObject::qt_metacast(_clname);
}

int WindowsManagerFilter::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void WindowsManagerFilter::pause()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void WindowsManagerFilter::ctrlaltdel()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void WindowsManagerFilter::showsettings()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void WindowsManagerFilter::reset()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void WindowsManagerFilter::request_shutdown()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void WindowsManagerFilter::force_shutdown()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void WindowsManagerFilter::dialogstatus(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
