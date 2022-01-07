/****************************************************************************
** Meta object code from reading C++ file 'qt_softwarerenderer.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_softwarerenderer.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_softwarerenderer.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SoftwareRenderer_t {
    const uint offsetsAndSize[14];
    char stringdata0[86];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SoftwareRenderer_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SoftwareRenderer_t qt_meta_stringdata_SoftwareRenderer = {
    {
QT_MOC_LITERAL(0, 16), // "SoftwareRenderer"
QT_MOC_LITERAL(17, 6), // "onBlit"
QT_MOC_LITERAL(24, 0), // ""
QT_MOC_LITERAL(25, 31), // "const std::unique_ptr<uint8_t>*"
QT_MOC_LITERAL(57, 3), // "img"
QT_MOC_LITERAL(61, 17), // "std::atomic_flag*"
QT_MOC_LITERAL(79, 6) // "in_use"

    },
    "SoftwareRenderer\0onBlit\0\0"
    "const std::unique_ptr<uint8_t>*\0img\0"
    "std::atomic_flag*\0in_use"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SoftwareRenderer[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    6,   20,    2, 0x0a,    1 /* Public */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, 0x80000000 | 5,    4,    2,    2,    2,    2,    6,

       0        // eod
};

void SoftwareRenderer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SoftwareRenderer *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onBlit((*reinterpret_cast< const std::unique_ptr<uint8_t>*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4])),(*reinterpret_cast< int(*)>(_a[5])),(*reinterpret_cast< std::atomic_flag*(*)>(_a[6]))); break;
        default: ;
        }
    }
}

const QMetaObject SoftwareRenderer::staticMetaObject = { {
    QMetaObject::SuperData::link<QRasterWindow::staticMetaObject>(),
    qt_meta_stringdata_SoftwareRenderer.offsetsAndSize,
    qt_meta_data_SoftwareRenderer,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SoftwareRenderer_t
, QtPrivate::TypeAndForceComplete<SoftwareRenderer, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const std::unique_ptr<uint8_t> *, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<std::atomic_flag *, std::false_type>


>,
    nullptr
} };


const QMetaObject *SoftwareRenderer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SoftwareRenderer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SoftwareRenderer.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "RendererCommon"))
        return static_cast< RendererCommon*>(this);
    return QRasterWindow::qt_metacast(_clname);
}

int SoftwareRenderer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QRasterWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
