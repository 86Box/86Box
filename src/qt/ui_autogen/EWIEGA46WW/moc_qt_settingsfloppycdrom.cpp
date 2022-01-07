/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsfloppycdrom.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_settingsfloppycdrom.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsfloppycdrom.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsFloppyCDROM_t {
    const uint offsetsAndSize[30];
    char stringdata0[307];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SettingsFloppyCDROM_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SettingsFloppyCDROM_t qt_meta_stringdata_SettingsFloppyCDROM = {
    {
QT_MOC_LITERAL(0, 19), // "SettingsFloppyCDROM"
QT_MOC_LITERAL(20, 28), // "on_comboBoxChannel_activated"
QT_MOC_LITERAL(49, 0), // ""
QT_MOC_LITERAL(50, 5), // "index"
QT_MOC_LITERAL(56, 24), // "on_comboBoxBus_activated"
QT_MOC_LITERAL(81, 26), // "on_comboBoxSpeed_activated"
QT_MOC_LITERAL(108, 34), // "on_comboBoxBus_currentIndexCh..."
QT_MOC_LITERAL(143, 31), // "on_comboBoxFloppyType_activated"
QT_MOC_LITERAL(175, 32), // "on_checkBoxCheckBPB_stateChanged"
QT_MOC_LITERAL(208, 4), // "arg1"
QT_MOC_LITERAL(213, 36), // "on_checkBoxTurboTimings_state..."
QT_MOC_LITERAL(250, 18), // "onFloppyRowChanged"
QT_MOC_LITERAL(269, 11), // "QModelIndex"
QT_MOC_LITERAL(281, 7), // "current"
QT_MOC_LITERAL(289, 17) // "onCDROMRowChanged"

    },
    "SettingsFloppyCDROM\0on_comboBoxChannel_activated\0"
    "\0index\0on_comboBoxBus_activated\0"
    "on_comboBoxSpeed_activated\0"
    "on_comboBoxBus_currentIndexChanged\0"
    "on_comboBoxFloppyType_activated\0"
    "on_checkBoxCheckBPB_stateChanged\0arg1\0"
    "on_checkBoxTurboTimings_stateChanged\0"
    "onFloppyRowChanged\0QModelIndex\0current\0"
    "onCDROMRowChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsFloppyCDROM[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   68,    2, 0x08,    1 /* Private */,
       4,    1,   71,    2, 0x08,    3 /* Private */,
       5,    1,   74,    2, 0x08,    5 /* Private */,
       6,    1,   77,    2, 0x08,    7 /* Private */,
       7,    1,   80,    2, 0x08,    9 /* Private */,
       8,    1,   83,    2, 0x08,   11 /* Private */,
      10,    1,   86,    2, 0x08,   13 /* Private */,
      11,    1,   89,    2, 0x08,   15 /* Private */,
      14,    1,   92,    2, 0x08,   17 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void, 0x80000000 | 12,   13,

       0        // eod
};

void SettingsFloppyCDROM::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsFloppyCDROM *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->on_comboBoxChannel_activated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_comboBoxBus_activated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_comboBoxSpeed_activated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->on_comboBoxBus_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->on_comboBoxFloppyType_activated((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->on_checkBoxCheckBPB_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->on_checkBoxTurboTimings_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 7: _t->onFloppyRowChanged((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        case 8: _t->onCDROMRowChanged((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject SettingsFloppyCDROM::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsFloppyCDROM.offsetsAndSize,
    qt_meta_data_SettingsFloppyCDROM,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SettingsFloppyCDROM_t
, QtPrivate::TypeAndForceComplete<SettingsFloppyCDROM, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>


>,
    nullptr
} };


const QMetaObject *SettingsFloppyCDROM::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsFloppyCDROM::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SettingsFloppyCDROM.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsFloppyCDROM::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
