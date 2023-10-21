/****************************************************************************
** Meta object code from reading C++ file 'qt_settingsharddisks.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_settingsharddisks.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_settingsharddisks.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SettingsHarddisks_t {
    QByteArrayData data[12];
    char stringdata0[257];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SettingsHarddisks_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SettingsHarddisks_t qt_meta_stringdata_SettingsHarddisks = {
    {
QT_MOC_LITERAL(0, 0, 17), // "SettingsHarddisks"
QT_MOC_LITERAL(1, 18, 38), // "on_comboBoxChannel_currentInd..."
QT_MOC_LITERAL(2, 57, 0), // ""
QT_MOC_LITERAL(3, 58, 5), // "index"
QT_MOC_LITERAL(4, 64, 36), // "on_comboBoxSpeed_currentIndex..."
QT_MOC_LITERAL(5, 101, 27), // "on_pushButtonRemove_clicked"
QT_MOC_LITERAL(6, 129, 29), // "on_pushButtonExisting_clicked"
QT_MOC_LITERAL(7, 159, 24), // "on_pushButtonNew_clicked"
QT_MOC_LITERAL(8, 184, 34), // "on_comboBoxBus_currentIndexCh..."
QT_MOC_LITERAL(9, 219, 17), // "onTableRowChanged"
QT_MOC_LITERAL(10, 237, 11), // "QModelIndex"
QT_MOC_LITERAL(11, 249, 7) // "current"

    },
    "SettingsHarddisks\0"
    "on_comboBoxChannel_currentIndexChanged\0"
    "\0index\0on_comboBoxSpeed_currentIndexChanged\0"
    "on_pushButtonRemove_clicked\0"
    "on_pushButtonExisting_clicked\0"
    "on_pushButtonNew_clicked\0"
    "on_comboBoxBus_currentIndexChanged\0"
    "onTableRowChanged\0QModelIndex\0current"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SettingsHarddisks[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    1,   49,    2, 0x08 /* Private */,
       4,    1,   52,    2, 0x08 /* Private */,
       5,    0,   55,    2, 0x08 /* Private */,
       6,    0,   56,    2, 0x08 /* Private */,
       7,    0,   57,    2, 0x08 /* Private */,
       8,    1,   58,    2, 0x08 /* Private */,
       9,    1,   61,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, 0x80000000 | 10,   11,

       0        // eod
};

void SettingsHarddisks::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsHarddisks *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->on_comboBoxChannel_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_comboBoxSpeed_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_pushButtonRemove_clicked(); break;
        case 3: _t->on_pushButtonExisting_clicked(); break;
        case 4: _t->on_pushButtonNew_clicked(); break;
        case 5: _t->on_comboBoxBus_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->onTableRowChanged((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject SettingsHarddisks::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_SettingsHarddisks.data,
    qt_meta_data_SettingsHarddisks,
    qt_static_metacall,
    nullptr,
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
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
