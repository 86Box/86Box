/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Keybind dialog
 *
 * Authors: Cathode Ray Dude
 *          Cacodemon345
 *
 *          Copyright 2025 Cathode Ray Dude
 *          Copyright 2025 Cacodemon345
 */
#include "qt_keybind.hpp"
#include "ui_qt_keybind.h"
#include "qt_settings.hpp"
#include "qt_singlekeyseqedit.hpp"

#include <QDebug>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QDir>
#include <QSettings>
#include <QKeyEvent>
#include <QKeySequence>
#include <QKeySequenceEdit>

extern "C" {
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/midi_rtmidi.h>
#include <86box/mem.h>
#include <86box/random.h>
#include <86box/rom.h>
}

#include "qt_filefield.hpp"
#include "qt_models_common.hpp"
#ifdef Q_OS_LINUX
#    include <sys/stat.h>
#    include <sys/sysmacros.h>
#endif
#ifdef Q_OS_WINDOWS
#    include <windows.h>
#endif

KeyBinder::KeyBinder(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::KeyBinder)
{
    ui->setupUi(this);
    singleKeySequenceEdit *seq = new singleKeySequenceEdit();
    ui->formLayout->addRow(seq);
    seq->setObjectName("keySequence");
    this->setTabOrder(seq, ui->buttonBox);
}

KeyBinder::~KeyBinder()
{
    delete ui;
}

void
KeyBinder::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    this->findChild<QKeySequenceEdit *>()->setFocus();
}

bool
KeyBinder::eventFilter(QObject *obj, QEvent *event)
{
    return QObject::eventFilter(obj, event);
}

QKeySequence
KeyBinder::BindKey(QWidget *widget, QString CurValue)
{
    KeyBinder kb(widget);
    kb.setWindowTitle(tr("Bind Key"));
    kb.setFixedSize(kb.minimumSizeHint());
    kb.findChild<QKeySequenceEdit *>()->setKeySequence(QKeySequence::fromString(CurValue, QKeySequence::NativeText));
    kb.setEnabled(true);

    if (kb.exec() == QDialog::Accepted) {
        QKeySequenceEdit *seq = kb.findChild<QKeySequenceEdit *>();
        return (seq->keySequence());
    } else {
        return (false);
    }
}