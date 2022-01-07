/****************************************************************************
** Meta object code from reading C++ file 'qt_rendererstack.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_rendererstack.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_rendererstack.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_RendererStack_t {
    const uint offsetsAndSize[26];
    char stringdata0[114];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_RendererStack_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_RendererStack_t qt_meta_stringdata_RendererStack = {
    {
QT_MOC_LITERAL(0, 13), // "RendererStack"
QT_MOC_LITERAL(14, 14), // "blitToRenderer"
QT_MOC_LITERAL(29, 0), // ""
QT_MOC_LITERAL(30, 31), // "const std::unique_ptr<uint8_t>*"
QT_MOC_LITERAL(62, 3), // "img"
QT_MOC_LITERAL(66, 17), // "std::atomic_flag*"
QT_MOC_LITERAL(84, 6), // "in_use"
QT_MOC_LITERAL(91, 4), // "blit"
QT_MOC_LITERAL(96, 1), // "x"
QT_MOC_LITERAL(98, 1), // "y"
QT_MOC_LITERAL(100, 1), // "w"
QT_MOC_LITERAL(102, 1), // "h"
QT_MOC_LITERAL(104, 9) // "mousePoll"

    },
    "RendererStack\0blitToRenderer\0\0"
    "const std::unique_ptr<uint8_t>*\0img\0"
    "std::atomic_flag*\0in_use\0blit\0x\0y\0w\0"
    "h\0mousePoll"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_RendererStack[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    6,   32,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       7,    4,   45,    2, 0x0a,    8 /* Public */,
      12,    0,   54,    2, 0x0a,   13 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, 0x80000000 | 5,    4,    2,    2,    2,    2,    6,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,    8,    9,   10,   11,
    QMetaType::Void,

       0        // eod
};

void RendererStack::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<RendererStack *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->blitToRenderer((*reinterpret_cast< const std::unique_ptr<uint8_t>*(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4])),(*reinterpret_cast< int(*)>(_a[5])),(*reinterpret_cast< std::atomic_flag*(*)>(_a[6]))); break;
        case 1: _t->blit((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4]))); break;
        case 2: _t->mousePoll(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (RendererStack::*)(const std::unique_ptr<uint8_t> * , int , int , int , int , std::atomic_flag * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&RendererStack::blitToRenderer)) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject RendererStack::staticMetaObject = { {
    QMetaObject::SuperData::link<QStackedWidget::staticMetaObject>(),
    qt_meta_stringdata_RendererStack.offsetsAndSize,
    qt_meta_data_RendererStack,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_RendererStack_t
, QtPrivate::TypeAndForceComplete<RendererStack, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const std::unique_ptr<uint8_t> *, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<std::atomic_flag *, std::false_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>


>,
    nullptr
} };


const QMetaObject *RendererStack::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RendererStack::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_RendererStack.stringdata0))
        return static_cast<void*>(this);
    return QStackedWidget::qt_metacast(_clname);
}

int RendererStack::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QStackedWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void RendererStack::blitToRenderer(const std::unique_ptr<uint8_t> * _t1, int _t2, int _t3, int _t4, int _t5, std::atomic_flag * _t6)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t6))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
