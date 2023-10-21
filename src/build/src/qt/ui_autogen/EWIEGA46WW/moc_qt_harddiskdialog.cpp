/****************************************************************************
** Meta object code from reading C++ file 'qt_harddiskdialog.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_harddiskdialog.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_harddiskdialog.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_HarddiskDialog_t {
    QByteArrayData data[18];
    char stringdata0[332];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_HarddiskDialog_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_HarddiskDialog_t qt_meta_stringdata_HarddiskDialog = {
    {
QT_MOC_LITERAL(0, 0, 14), // "HarddiskDialog"
QT_MOC_LITERAL(1, 15, 12), // "fileProgress"
QT_MOC_LITERAL(2, 28, 0), // ""
QT_MOC_LITERAL(3, 29, 1), // "i"
QT_MOC_LITERAL(4, 31, 6), // "accept"
QT_MOC_LITERAL(5, 38, 35), // "on_comboBoxType_currentIndexC..."
QT_MOC_LITERAL(6, 74, 5), // "index"
QT_MOC_LITERAL(7, 80, 29), // "on_lineEditSectors_textEdited"
QT_MOC_LITERAL(8, 110, 4), // "arg1"
QT_MOC_LITERAL(9, 115, 27), // "on_lineEditHeads_textEdited"
QT_MOC_LITERAL(10, 143, 31), // "on_lineEditCylinders_textEdited"
QT_MOC_LITERAL(11, 175, 26), // "on_lineEditSize_textEdited"
QT_MOC_LITERAL(12, 202, 34), // "on_comboBoxBus_currentIndexCh..."
QT_MOC_LITERAL(13, 237, 37), // "on_comboBoxFormat_currentInde..."
QT_MOC_LITERAL(14, 275, 15), // "onCreateNewFile"
QT_MOC_LITERAL(15, 291, 22), // "onExistingFileSelected"
QT_MOC_LITERAL(16, 314, 8), // "fileName"
QT_MOC_LITERAL(17, 323, 8) // "precheck"

    },
    "HarddiskDialog\0fileProgress\0\0i\0accept\0"
    "on_comboBoxType_currentIndexChanged\0"
    "index\0on_lineEditSectors_textEdited\0"
    "arg1\0on_lineEditHeads_textEdited\0"
    "on_lineEditCylinders_textEdited\0"
    "on_lineEditSize_textEdited\0"
    "on_comboBoxBus_currentIndexChanged\0"
    "on_comboBoxFormat_currentIndexChanged\0"
    "onCreateNewFile\0onExistingFileSelected\0"
    "fileName\0precheck"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_HarddiskDialog[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   69,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       4,    0,   72,    2, 0x0a /* Public */,
       5,    1,   73,    2, 0x08 /* Private */,
       7,    1,   76,    2, 0x08 /* Private */,
       9,    1,   79,    2, 0x08 /* Private */,
      10,    1,   82,    2, 0x08 /* Private */,
      11,    1,   85,    2, 0x08 /* Private */,
      12,    1,   88,    2, 0x08 /* Private */,
      13,    1,   91,    2, 0x08 /* Private */,
      14,    0,   94,    2, 0x08 /* Private */,
      15,    2,   95,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,   16,   17,

       0        // eod
};

void HarddiskDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<HarddiskDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->fileProgress((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->accept(); break;
        case 2: _t->on_comboBoxType_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->on_lineEditSectors_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->on_lineEditHeads_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->on_lineEditCylinders_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->on_lineEditSize_textEdited((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 7: _t->on_comboBoxBus_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 8: _t->on_comboBoxFormat_currentIndexChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 9: _t->onCreateNewFile(); break;
        case 10: _t->onExistingFileSelected((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
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

QT_INIT_METAOBJECT const QMetaObject HarddiskDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_HarddiskDialog.data,
    qt_meta_data_HarddiskDialog,
    qt_static_metacall,
    nullptr,
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
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 11;
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
