/****************************************************************************
** Meta object code from reading C++ file 'qt_vulkanwindowrenderer.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_vulkanwindowrenderer.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_vulkanwindowrenderer.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_VulkanWindowRenderer_t {
    QByteArrayData data[10];
    char stringdata0[83];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_VulkanWindowRenderer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_VulkanWindowRenderer_t qt_meta_stringdata_VulkanWindowRenderer = {
    {
QT_MOC_LITERAL(0, 0, 20), // "VulkanWindowRenderer"
QT_MOC_LITERAL(1, 21, 19), // "rendererInitialized"
QT_MOC_LITERAL(2, 41, 0), // ""
QT_MOC_LITERAL(3, 42, 17), // "errorInitializing"
QT_MOC_LITERAL(4, 60, 6), // "onBlit"
QT_MOC_LITERAL(5, 67, 7), // "buf_idx"
QT_MOC_LITERAL(6, 75, 1), // "x"
QT_MOC_LITERAL(7, 77, 1), // "y"
QT_MOC_LITERAL(8, 79, 1), // "w"
QT_MOC_LITERAL(9, 81, 1) // "h"

    },
    "VulkanWindowRenderer\0rendererInitialized\0"
    "\0errorInitializing\0onBlit\0buf_idx\0x\0"
    "y\0w\0h"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_VulkanWindowRenderer[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   29,    2, 0x06 /* Public */,
       3,    0,   30,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    5,   31,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,    5,    6,    7,    8,    9,

       0        // eod
};

void VulkanWindowRenderer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VulkanWindowRenderer *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->rendererInitialized(); break;
        case 1: _t->errorInitializing(); break;
        case 2: _t->onBlit((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4])),(*reinterpret_cast< int(*)>(_a[5]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (VulkanWindowRenderer::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VulkanWindowRenderer::rendererInitialized)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (VulkanWindowRenderer::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VulkanWindowRenderer::errorInitializing)) {
                *result = 1;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject VulkanWindowRenderer::staticMetaObject = { {
    QMetaObject::SuperData::link<QVulkanWindow::staticMetaObject>(),
    qt_meta_stringdata_VulkanWindowRenderer.data,
    qt_meta_data_VulkanWindowRenderer,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *VulkanWindowRenderer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VulkanWindowRenderer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_VulkanWindowRenderer.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "RendererCommon"))
        return static_cast< RendererCommon*>(this);
    return QVulkanWindow::qt_metacast(_clname);
}

int VulkanWindowRenderer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QVulkanWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void VulkanWindowRenderer::rendererInitialized()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void VulkanWindowRenderer::errorInitializing()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
