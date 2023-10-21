/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsinput.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_settingsinput.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsinput.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsInput_t {
    QByteArrayData data[12];
    char stringdata0[292];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SettingsInput_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SettingsInput_t qt_meta_stringdata_SettingsInput = {
    {
QT_MOC_LITERAL(0, 0, 13), // "SettingsInput"
QT_MOC_LITERAL(1, 14, 23), // "onCurrentMachineChanged"
QT_MOC_LITERAL(2, 38, 0), // ""
QT_MOC_LITERAL(3, 39, 9), // "machineId"
QT_MOC_LITERAL(4, 49, 35), // "on_pushButtonConfigureMouse_c..."
QT_MOC_LITERAL(5, 85, 39), // "on_comboBoxJoystick_currentIn..."
QT_MOC_LITERAL(6, 125, 5), // "index"
QT_MOC_LITERAL(7, 131, 36), // "on_comboBoxMouse_currentIndex..."
QT_MOC_LITERAL(8, 168, 30), // "on_pushButtonJoystick1_clicked"
QT_MOC_LITERAL(9, 199, 30), // "on_pushButtonJoystick2_clicked"
QT_MOC_LITERAL(10, 230, 30), // "on_pushButtonJoystick3_clicked"
QT_MOC_LITERAL(11, 261, 30) // "on_pushButtonJoystick4_clicked"

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
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    1,   54,    2, 0x0a /* Public */,
       4,    0,   57,    2, 0x08 /* Private */,
       5,    1,   58,    2, 0x08 /* Private */,
       7,    1,   61,    2, 0x08 /* Private */,
       8,    0,   64,    2, 0x08 /* Private */,
       9,    0,   65,    2, 0x08 /* Private */,
      10,    0,   66,    2, 0x08 /* Private */,
      11,    0,   67,    2, 0x08 /* Private */,

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

QT_INIT_METAOBJECT const QMetaObject SettingsInput::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsInput.data,
    qt_meta_data_SettingsInput,
    qt_static_metacall,
    nullptr,
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
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 8;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
