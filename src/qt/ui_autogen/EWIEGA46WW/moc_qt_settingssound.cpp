/****************************************************************************
** Meta object code from reading C++ file 'qt_settingssound.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_settingssound.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingssound.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsSound_t {
    const uint offsetsAndSize[40];
    char stringdata0[555];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SettingsSound_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SettingsSound_t qt_meta_stringdata_SettingsSound = {
    {
QT_MOC_LITERAL(0, 13), // "SettingsSound"
QT_MOC_LITERAL(14, 23), // "onCurrentMachineChanged"
QT_MOC_LITERAL(38, 0), // ""
QT_MOC_LITERAL(39, 9), // "machineId"
QT_MOC_LITERAL(49, 33), // "on_pushButtonConfigureGUS_cli..."
QT_MOC_LITERAL(83, 33), // "on_pushButtonConfigureCMS_cli..."
QT_MOC_LITERAL(117, 37), // "on_pushButtonConfigureSSI2001..."
QT_MOC_LITERAL(155, 36), // "on_pushButtonConfigureMPU401_..."
QT_MOC_LITERAL(192, 27), // "on_checkBoxGUS_stateChanged"
QT_MOC_LITERAL(220, 4), // "arg1"
QT_MOC_LITERAL(225, 27), // "on_checkBoxCMS_stateChanged"
QT_MOC_LITERAL(253, 31), // "on_checkBoxSSI2001_stateChanged"
QT_MOC_LITERAL(285, 30), // "on_checkBoxMPU401_stateChanged"
QT_MOC_LITERAL(316, 36), // "on_pushButtonConfigureMidiIn_..."
QT_MOC_LITERAL(353, 37), // "on_pushButtonConfigureMidiOut..."
QT_MOC_LITERAL(391, 37), // "on_comboBoxMidiIn_currentInde..."
QT_MOC_LITERAL(429, 5), // "index"
QT_MOC_LITERAL(435, 38), // "on_comboBoxMidiOut_currentInd..."
QT_MOC_LITERAL(474, 39), // "on_pushButtonConfigureSoundCa..."
QT_MOC_LITERAL(514, 40) // "on_comboBoxSoundCard_currentI..."

    },
    "SettingsSound\0onCurrentMachineChanged\0"
    "\0machineId\0on_pushButtonConfigureGUS_clicked\0"
    "on_pushButtonConfigureCMS_clicked\0"
    "on_pushButtonConfigureSSI2001_clicked\0"
    "on_pushButtonConfigureMPU401_clicked\0"
    "on_checkBoxGUS_stateChanged\0arg1\0"
    "on_checkBoxCMS_stateChanged\0"
    "on_checkBoxSSI2001_stateChanged\0"
    "on_checkBoxMPU401_stateChanged\0"
    "on_pushButtonConfigureMidiIn_clicked\0"
    "on_pushButtonConfigureMidiOut_clicked\0"
    "on_comboBoxMidiIn_currentIndexChanged\0"
    "index\0on_comboBoxMidiOut_currentIndexChanged\0"
    "on_pushButtonConfigureSoundCard_clicked\0"
    "on_comboBoxSoundCard_currentIndexChanged"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsSound[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      15,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  104,    2, 0x0a,    1 /* Public */,
       4,    0,  107,    2, 0x08,    3 /* Private */,
       5,    0,  108,    2, 0x08,    4 /* Private */,
       6,    0,  109,    2, 0x08,    5 /* Private */,
       7,    0,  110,    2, 0x08,    6 /* Private */,
       8,    1,  111,    2, 0x08,    7 /* Private */,
      10,    1,  114,    2, 0x08,    9 /* Private */,
      11,    1,  117,    2, 0x08,   11 /* Private */,
      12,    1,  120,    2, 0x08,   13 /* Private */,
      13,    0,  123,    2, 0x08,   15 /* Private */,
      14,    0,  124,    2, 0x08,   16 /* Private */,
      15,    1,  125,    2, 0x08,   17 /* Private */,
      17,    1,  128,    2, 0x08,   19 /* Private */,
      18,    0,  131,    2, 0x08,   21 /* Private */,
      19,    1,  132,    2, 0x08,   22 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   16,
    QMetaType::Void, QMetaType::Int,   16,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   16,

       0        // eod
};

void SettingsSound::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsSound *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onCurrentMachineChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_pushButtonConfigureGUS_clicked(); break;
        case 2: _t->on_pushButtonConfigureCMS_clicked(); break;
        case 3: _t->on_pushButtonConfigureSSI2001_clicked(); break;
        case 4: _t->on_pushButtonConfigureMPU401_clicked(); break;
        case 5: _t->on_checkBoxGUS_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->on_checkBoxCMS_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 7: _t->on_checkBoxSSI2001_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 8: _t->on_checkBoxMPU401_stateChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 9: _t->on_pushButtonConfigureMidiIn_clicked(); break;
        case 10: _t->on_pushButtonConfigureMidiOut_clicked(); break;
        case 11: _t->on_comboBoxMidiIn_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 12: _t->on_comboBoxMidiOut_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 13: _t->on_pushButtonConfigureSoundCard_clicked(); break;
        case 14: _t->on_comboBoxSoundCard_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject SettingsSound::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsSound.offsetsAndSize,
    qt_meta_data_SettingsSound,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SettingsSound_t
, QtPrivate::TypeAndForceComplete<SettingsSound, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>


>,
    nullptr
} };


const QMetaObject *SettingsSound::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsSound::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SettingsSound.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsSound::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 15;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
