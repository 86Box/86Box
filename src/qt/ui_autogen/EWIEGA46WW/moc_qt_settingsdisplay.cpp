/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsdisplay.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_settingsdisplay.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsdisplay.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsDisplay_t {
    const uint offsetsAndSize[20];
    char stringdata0[199];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SettingsDisplay_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SettingsDisplay_t qt_meta_stringdata_SettingsDisplay = {
    {
QT_MOC_LITERAL(0, 15), // "SettingsDisplay"
QT_MOC_LITERAL(16, 23), // "onCurrentMachineChanged"
QT_MOC_LITERAL(40, 0), // ""
QT_MOC_LITERAL(41, 9), // "machineId"
QT_MOC_LITERAL(51, 30), // "on_checkBoxVoodoo_stateChanged"
QT_MOC_LITERAL(82, 5), // "state"
QT_MOC_LITERAL(88, 36), // "on_comboBoxVideo_currentIndex..."
QT_MOC_LITERAL(125, 5), // "index"
QT_MOC_LITERAL(131, 36), // "on_pushButtonConfigureVoodoo_..."
QT_MOC_LITERAL(168, 30) // "on_pushButtonConfigure_clicked"

    },
    "SettingsDisplay\0onCurrentMachineChanged\0"
    "\0machineId\0on_checkBoxVoodoo_stateChanged\0"
    "state\0on_comboBoxVideo_currentIndexChanged\0"
    "index\0on_pushButtonConfigureVoodoo_clicked\0"
    "on_pushButtonConfigure_clicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsDisplay[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   44,    2, 0x0a,    1 /* Public */,
       4,    1,   47,    2, 0x08,    3 /* Private */,
       6,    1,   50,    2, 0x08,    5 /* Private */,
       8,    0,   53,    2, 0x08,    7 /* Private */,
       9,    0,   54,    2, 0x08,    8 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void SettingsDisplay::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsDisplay *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onCurrentMachineChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_checkBoxVoodoo_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_comboBoxVideo_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->on_pushButtonConfigureVoodoo_clicked(); break;
        case 4: _t->on_pushButtonConfigure_clicked(); break;
        default: ;
        }
    }
}

const QMetaObject SettingsDisplay::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsDisplay.offsetsAndSize,
    qt_meta_data_SettingsDisplay,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SettingsDisplay_t
, QtPrivate::TypeAndForceComplete<SettingsDisplay, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>


>,
    nullptr
} };


const QMetaObject *SettingsDisplay::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsDisplay::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SettingsDisplay.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsDisplay::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
