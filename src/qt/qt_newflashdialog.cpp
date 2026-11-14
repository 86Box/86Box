/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          New USB flash drive image dialog.
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */
#include <cstdint>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/usb.h>
}

#include "qt_newflashdialog.hpp"
#include "qt_util.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringBuilder>

/* Size presets in MB; 0 is Custom. */
static const int flash_presets[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 0 };

NewFlashDialog::NewFlashDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New USB flash drive"));

    auto *form = new QFormLayout(this);

    lineEditFile    = new QLineEdit(this);
    auto *browse    = new QPushButton(tr("&Specify…"), this);
    auto *fileRow   = new QHBoxLayout();
    fileRow->addWidget(lineEditFile);
    fileRow->addWidget(browse);
    form->addRow(tr("File name:"), fileRow);

    comboBoxSize = new QComboBox(this);
    for (const int mb : flash_presets) {
        if (mb == 0)
            comboBoxSize->addItem(tr("Custom…"), 0);
        else if (mb >= 1024)
            comboBoxSize->addItem(tr("%1 GB").arg(mb / 1024), mb);
        else
            comboBoxSize->addItem(tr("%1 MB").arg(mb), mb);
    }
    comboBoxSize->setCurrentIndex(comboBoxSize->findData(256));
    form->addRow(tr("Size:"), comboBoxSize);

    spinBoxCustom = new QSpinBox(this);
    comboBoxUnit  = new QComboBox(this);
    comboBoxUnit->addItem(tr("MB"), 1);
    comboBoxUnit->addItem(tr("GB"), 1024);
    auto *customRow = new QHBoxLayout();
    customRow->addWidget(spinBoxCustom);
    customRow->addWidget(comboBoxUnit);
    customRow->addStretch();
    form->addRow(tr("Custom size:"), customRow);

    comboBoxFormat = new QComboBox(this);
    comboBoxFormat->addItem(tr("None (blank)"), USB_FLASH_FS_BLANK);
    comboBoxFormat->addItem(tr("FAT16"), USB_FLASH_FS_FAT16);
    comboBoxFormat->addItem(tr("FAT32"), USB_FLASH_FS_FAT32);
    comboBoxFormat->addItem(tr("exFAT"), USB_FLASH_FS_EXFAT);
    form->addRow(tr("Format:"), comboBoxFormat);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &NewFlashDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &NewFlashDialog::reject);
    connect(lineEditFile, &QLineEdit::textChanged, this, [this] { updateOk(); });
    connect(browse, &QPushButton::clicked, this, [this] {
        /* Replacing a file is confirmed once, on OK. */
        QString name = QFileDialog::getSaveFileName(this, tr("Create…"), QString(),
                                                    tr("Raw images") % util::DlgFilter({ "img" }, true), nullptr,
                                                    QFileDialog::DontConfirmOverwrite);
        if (name.isEmpty())
            return;
        if (QFileInfo(name).suffix().isEmpty())
            name.append(".img");
        lineEditFile->setText(name);
    });
    connect(comboBoxSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSize(); });
    connect(comboBoxUnit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSize(); });
    connect(spinBoxCustom, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { updateSize(); });

    updateSize();
}

uint64_t
NewFlashDialog::sizeBytes() const
{
    uint64_t mb = comboBoxSize->currentData().toInt();

    if (mb == 0)
        mb = (uint64_t) spinBoxCustom->value() * comboBoxUnit->currentData().toInt();

    return mb << 20;
}

/* The custom size is whole MB or GB, 8 MB to 32 GB. The formats the size
   cannot hold are grayed out, and a format that no longer fits gives way
   to the default for the size. */
void
NewFlashDialog::updateSize()
{
    const bool custom = (comboBoxSize->currentData().toInt() == 0);
    const int  unit   = comboBoxUnit->currentData().toInt();

    spinBoxCustom->blockSignals(true);
    spinBoxCustom->setRange((unit == 1) ? 8 : 1, (unit == 1) ? 32768 : 32);
    spinBoxCustom->blockSignals(false);
    spinBoxCustom->setEnabled(custom);
    comboBoxUnit->setEnabled(custom);

    const uint64_t bytes = sizeBytes();
    auto          *model = qobject_cast<QStandardItemModel *>(comboBoxFormat->model());

    for (int i = 0; i < comboBoxFormat->count(); i++) {
        const int fs = comboBoxFormat->itemData(i).toInt();
        model->item(i)->setEnabled((fs == USB_FLASH_FS_BLANK) || usb_flash_fs_ok(bytes, fs));
    }

    const int cur = comboBoxFormat->currentData().toInt();
    if ((cur == USB_FLASH_FS_BLANK) || !usb_flash_fs_ok(bytes, cur))
        comboBoxFormat->setCurrentIndex(comboBoxFormat->findData(usb_flash_fs_default(bytes)));

    updateOk();
}

void
NewFlashDialog::updateOk()
{
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!lineEditFile->text().isEmpty());
}

void
NewFlashDialog::accept()
{
    /* A typed name without a folder is in the VM's folder, where the image
       is created (plat_fopen64 resolves it there), so that is also where
       an existing image must be looked for. */
    const QFileInfo fi(lineEditFile->text());
    const QString   name  = fi.isRelative() ? (usr_path + fi.filePath()) : fi.filePath();
    const uint64_t  bytes = sizeBytes();
    const int       fs    = comboBoxFormat->currentData().toInt();

    if (QFileInfo::exists(name) &&
        (QMessageBox::warning(this, tr("New USB flash drive"), tr("%1 already exists. Replace it?").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes))
        return;

    setCursor(Qt::WaitCursor);
    const int ok = usb_flash_create(name.toUtf8().constData(), bytes, fs);
    unsetCursor();

    if (!ok) {
        QMessageBox::critical(this, tr("New USB flash drive"), tr("Could not create %1.").arg(name));
        return;
    }

    fn = name;
    QDialog::accept();
}
