/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsinput.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_settingsinput.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsinput.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsInput_t {
    const uint offsetsAndSize[24];
    char stringdata0[292];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SettingsInput_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SettingsInput_t qt_meta_stringdata_SettingsInput = {
    {
QT_MOC_LITERAL(0, 13), // "SettingsInput"
QT_MOC_LITERAL(14, 23), // "onCurrentMachineChanged"
QT_MOC_LITERAL(38, 0), // ""
QT_MOC_LITERAL(39, 9), // "machineId"
QT_MOC_LITERAL(49, 35), // "on_pushButtonConfigureMouse_c..."
QT_MOC_LITERAL(85, 39), // "on_comboBoxJoystick_currentIn..."
QT_MOC_LITERAL(125, 5), // "index"
QT_MOC_LITERAL(131, 36), // "on_comboBoxMouse_currentIndex..."
QT_MOC_LITERAL(168, 30), // "on_pushButtonJoystick1_clicked"
QT_MOC_LITERAL(199, 30), // "on_pushButtonJoystick2_clicked"
QT_MOC_LITERAL(230, 30), // "on_pushButtonJoystick3_clicked"
QT_MOC_LITERAL(261, 30) // "on_pushButtonJoystick4_clicked"

    },
    "SettingsInput\0onCurrentMachineChanged\0"
    "\0machineId\0on_pushButtonConfigureMouse_clicked\0"
    "on_comboBoxJoystick_currentIndexChanged\0"
    "index\0on_comboBoxMouse_currentIndexChanged\0"
    "on_pushButtonJoystick1_clicked\0"
    "on_pushButtonJoystick2_clicked\0"
    "on_pushButtonJoystick3_clicked\0"
    "on_pushButtonJoystick4_clicked"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsInput[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   62,    2, 0x0a,    1 /* Public */,
       4,    0,   65,    2, 0x08,    3 /* Private */,
       5,    1,   66,    2, 0x08,    4 /* Private */,
       7,    1,   69,    2, 0x08,    6 /* Private */,
       8,    0,   72,    2, 0x08,    8 /* Private */,
       9,    0,   73,    2, 0x08,    9 /* Private */,
      10,    0,   74,    2, 0x08,   10 /* Private */,
      11,    0,   75,    2, 0x08,   11 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void SettingsInput::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsInput *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onCurrentMachineChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_pushButtonConfigureMouse_clicked(); break;
        case 2: _t->on_comboBoxJoystick_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->on_comboBoxMouse_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->on_pushButtonJoystick1_clicked(); break;
        case 5: _t->on_pushButtonJoystick2_clicked(); break;
        case 6: _t->on_pushButtonJoystick3_clicked(); break;
        case 7: _t->on_pushButtonJoystick4_clicked(); break;
        default: ;
        }
    }
}

const QMetaObject SettingsInput::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsInput.offsetsAndSize,
    qt_meta_data_SettingsInput,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SettingsInput_t
, QtPrivate::TypeAndForceComplete<SettingsInput, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>


>,
    nullptr
} };


const QMetaObject *SettingsInput::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsInput::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SettingsInput.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsInput::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
