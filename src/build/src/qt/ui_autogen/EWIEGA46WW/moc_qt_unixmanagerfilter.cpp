/****************************************************************************
** Meta object code from reading C++ file 'qt_unixmanagerfilter.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_unixmanagerfilter.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_unixmanagerfilter.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_UnixManagerSocket_t {
    QByteArrayData data[11];
    char stringdata0[119];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_UnixManagerSocket_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_UnixManagerSocket_t qt_meta_stringdata_UnixManagerSocket = {
    {
QT_MOC_LITERAL(0, 0, 17), // "UnixManagerSocket"
QT_MOC_LITERAL(1, 18, 5), // "pause"
QT_MOC_LITERAL(2, 24, 0), // ""
QT_MOC_LITERAL(3, 25, 10), // "ctrlaltdel"
QT_MOC_LITERAL(4, 36, 12), // "showsettings"
QT_MOC_LITERAL(5, 49, 7), // "resetVM"
QT_MOC_LITERAL(6, 57, 16), // "request_shutdown"
QT_MOC_LITERAL(7, 74, 14), // "force_shutdown"
QT_MOC_LITERAL(8, 89, 12), // "dialogstatus"
QT_MOC_LITERAL(9, 102, 4), // "open"
QT_MOC_LITERAL(10, 107, 11) // "readyToRead"

    },
    "UnixManagerSocket\0pause\0\0ctrlaltdel\0"
    "showsettings\0resetVM\0request_shutdown\0"
    "force_shutdown\0dialogstatus\0open\0"
    "readyToRead"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_UnixManagerSocket[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   54,    2, 0x06 /* Public */,
       3,    0,   55,    2, 0x06 /* Public */,
       4,    0,   56,    2, 0x06 /* Public */,
       5,    0,   57,    2, 0x06 /* Public */,
       6,    0,   58,    2, 0x06 /* Public */,
       7,    0,   59,    2, 0x06 /* Public */,
       8,    1,   60,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      10,    0,   63,    2, 0x09 /* Protected */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    9,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void UnixManagerSocket::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<UnixManagerSocket *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->pause(); break;
        case 1: _t->ctrlaltdel(); break;
        case 2: _t->showsettings(); break;
        case 3: _t->resetVM(); break;
        case 4: _t->request_shutdown(); break;
        case 5: _t->force_shutdown(); break;
        case 6: _t->dialogstatus((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 7: _t->readyToRead(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::pause)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::ctrlaltdel)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::showsettings)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::resetVM)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::request_shutdown)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::force_shutdown)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (UnixManagerSocket::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&UnixManagerSocket::dialogstatus)) {
                *result = 6;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject UnixManagerSocket::staticMetaObject = { {
    QMetaObject::SuperData::link<QLocalSocket::staticMetaObject>(),
    qt_meta_stringdata_UnixManagerSocket.data,
    qt_meta_data_UnixManagerSocket,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *UnixManagerSocket::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *UnixManagerSocket::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_UnixManagerSocket.stringdata0))
        return static_cast<void*>(this);
    return QLocalSocket::qt_metacast(_clname);
}

int UnixManagerSocket::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QLocalSocket::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void UnixManagerSocket::pause()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void UnixManagerSocket::ctrlaltdel()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void UnixManagerSocket::showsettings()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void UnixManagerSocket::resetVM()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void UnixManagerSocket::request_shutdown()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void UnixManagerSocket::force_shutdown()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void UnixManagerSocket::dialogstatus(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
