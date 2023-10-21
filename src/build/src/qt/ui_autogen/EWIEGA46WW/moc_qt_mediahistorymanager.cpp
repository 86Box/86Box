/****************************************************************************
** Meta object code from reading C++ file 'qt_mediahistorymanager.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_mediahistorymanager.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_mediahistorymanager.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ui_t {
    QByteArrayData data[7];
    char stringdata0[44];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ui_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ui_t qt_meta_stringdata_ui = {
    {
QT_MOC_LITERAL(0, 0, 2), // "ui"
QT_MOC_LITERAL(1, 3, 9), // "MediaType"
QT_MOC_LITERAL(2, 13, 6), // "Floppy"
QT_MOC_LITERAL(3, 20, 7), // "Optical"
QT_MOC_LITERAL(4, 28, 3), // "Zip"
QT_MOC_LITERAL(5, 32, 2), // "Mo"
QT_MOC_LITERAL(6, 35, 8) // "Cassette"

    },
    "ui\0MediaType\0Floppy\0Optical\0Zip\0Mo\0"
    "Cassette"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ui[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       1,   14, // enums/sets
       0,    0, // constructors
       4,       // flags
       0,       // signalCount

 // enums: name, alias, flags, count, data
       1,    1, 0x2,    5,   19,

 // enum data: key, value
       2, uint(ui::MediaType::Floppy),
       3, uint(ui::MediaType::Optical),
       4, uint(ui::MediaType::Zip),
       5, uint(ui::MediaType::Mo),
       6, uint(ui::MediaType::Cassette),

       0        // eod
};

QT_INIT_METAOBJECT const QMetaObject ui::staticMetaObject = { {
    nullptr,
    qt_meta_stringdata_ui.data,
    qt_meta_data_ui,
    nullptr,
    nullptr,
    nullptr
} };

QT_WARNING_POP
QT_END_MOC_NAMESPACE
