/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsharddisks.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_settingsharddisks.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsharddisks.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsHarddisks_t {
    const uint offsetsAndSize[22];
    char stringdata0[220];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_SettingsHarddisks_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_SettingsHarddisks_t qt_meta_stringdata_SettingsHarddisks = {
    {
QT_MOC_LITERAL(0, 17), // "SettingsHarddisks"
QT_MOC_LITERAL(18, 38), // "on_comboBoxChannel_currentInd..."
QT_MOC_LITERAL(57, 0), // ""
QT_MOC_LITERAL(58, 5), // "index"
QT_MOC_LITERAL(64, 27), // "on_pushButtonRemove_clicked"
QT_MOC_LITERAL(92, 29), // "on_pushButtonExisting_clicked"
QT_MOC_LITERAL(122, 24), // "on_pushButtonNew_clicked"
QT_MOC_LITERAL(147, 34), // "on_comboBoxBus_currentIndexCh..."
QT_MOC_LITERAL(182, 17), // "onTableRowChanged"
QT_MOC_LITERAL(200, 11), // "QModelIndex"
QT_MOC_LITERAL(212, 7) // "current"

    },
    "SettingsHarddisks\0"
    "on_comboBoxChannel_currentIndexChanged\0"
    "\0index\0on_pushButtonRemove_clicked\0"
    "on_pushButtonExisting_clicked\0"
    "on_pushButtonNew_clicked\0"
    "on_comboBoxBus_currentIndexChanged\0"
    "onTableRowChanged\0QModelIndex\0current"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsHarddisks[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   50,    2, 0x08,    1 /* Private */,
       4,    0,   53,    2, 0x08,    3 /* Private */,
       5,    0,   54,    2, 0x08,    4 /* Private */,
       6,    0,   55,    2, 0x08,    5 /* Private */,
       7,    1,   56,    2, 0x08,    6 /* Private */,
       8,    1,   59,    2, 0x08,    8 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, 0x80000000 | 9,   10,

       0        // eod
};

void SettingsHarddisks::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsHarddisks *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->on_comboBoxChannel_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_pushButtonRemove_clicked(); break;
        case 2: _t->on_pushButtonExisting_clicked(); break;
        case 3: _t->on_pushButtonNew_clicked(); break;
        case 4: _t->on_comboBoxBus_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->onTableRowChanged((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject SettingsHarddisks::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsHarddisks.offsetsAndSize,
    qt_meta_data_SettingsHarddisks,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_SettingsHarddisks_t
, QtPrivate::TypeAndForceComplete<SettingsHarddisks, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>


>,
    nullptr
} };


const QMetaObject *SettingsHarddisks::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SettingsHarddisks::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SettingsHarddisks.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SettingsHarddisks::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
