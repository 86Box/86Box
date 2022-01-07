/****************************************************************************
** Meta object code from reading C++ file 'qt_harddiskdialog.hpp'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../qt_harddiskdialog.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_harddiskdialog.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_HarddiskDialog_t {
    const uint offsetsAndSize[32];
    char stringdata0[316];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_HarddiskDialog_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_HarddiskDialog_t qt_meta_stringdata_HarddiskDialog = {
    {
QT_MOC_LITERAL(0, 14), // "HarddiskDialog"
QT_MOC_LITERAL(15, 12), // "fileProgress"
QT_MOC_LITERAL(28, 0), // ""
QT_MOC_LITERAL(29, 1), // "i"
QT_MOC_LITERAL(31, 35), // "on_comboBoxType_currentIndexC..."
QT_MOC_LITERAL(67, 5), // "index"
QT_MOC_LITERAL(73, 29), // "on_lineEditSectors_textEdited"
QT_MOC_LITERAL(103, 4), // "arg1"
QT_MOC_LITERAL(108, 27), // "on_lineEditHeads_textEdited"
QT_MOC_LITERAL(136, 31), // "on_lineEditCylinders_textEdited"
QT_MOC_LITERAL(168, 26), // "on_lineEditSize_textEdited"
QT_MOC_LITERAL(195, 34), // "on_comboBoxBus_currentIndexCh..."
QT_MOC_LITERAL(230, 37), // "on_comboBoxFormat_currentInde..."
QT_MOC_LITERAL(268, 15), // "onCreateNewFile"
QT_MOC_LITERAL(284, 22), // "onExistingFileSelected"
QT_MOC_LITERAL(307, 8) // "fileName"

    },
    "HarddiskDialog\0fileProgress\0\0i\0"
    "on_comboBoxType_currentIndexChanged\0"
    "index\0on_lineEditSectors_textEdited\0"
    "arg1\0on_lineEditHeads_textEdited\0"
    "on_lineEditCylinders_textEdited\0"
    "on_lineEditSize_textEdited\0"
    "on_comboBoxBus_currentIndexChanged\0"
    "on_comboBoxFormat_currentIndexChanged\0"
    "onCreateNewFile\0onExistingFileSelected\0"
    "fileName"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_HarddiskDialog[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   74,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    1,   77,    2, 0x08,    3 /* Private */,
       6,    1,   80,    2, 0x08,    5 /* Private */,
       8,    1,   83,    2, 0x08,    7 /* Private */,
       9,    1,   86,    2, 0x08,    9 /* Private */,
      10,    1,   89,    2, 0x08,   11 /* Private */,
      11,    1,   92,    2, 0x08,   13 /* Private */,
      12,    1,   95,    2, 0x08,   15 /* Private */,
      13,    0,   98,    2, 0x08,   17 /* Private */,
      14,    1,   99,    2, 0x08,   18 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, QMetaType::QString,    7,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   15,

       0        // eod
};

void HarddiskDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<HarddiskDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileProgress((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->on_comboBoxType_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->on_lineEditSectors_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->on_lineEditHeads_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->on_lineEditCylinders_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->on_lineEditSize_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->on_comboBoxBus_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 7: _t->on_comboBoxFormat_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 8: _t->onCreateNewFile(); break;
        case 9: _t->onExistingFileSelected((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (HarddiskDialog::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HarddiskDialog::fileProgress)) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject HarddiskDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_HarddiskDialog.offsetsAndSize,
    qt_meta_data_HarddiskDialog,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_HarddiskDialog_t
, QtPrivate::TypeAndForceComplete<HarddiskDialog, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>


>,
    nullptr
} };


const QMetaObject *HarddiskDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HarddiskDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_HarddiskDialog.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int HarddiskDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void HarddiskDialog::fileProgress(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
