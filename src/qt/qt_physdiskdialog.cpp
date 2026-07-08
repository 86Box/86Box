#ifdef _WIN32
#include <initguid.h>
#endif
#include "qt_physdiskdialog.hpp"
#include "ui_qt_physdiskdialog.h"
#include <QDebug>
#include <QMenu>
#include <QLineEdit>
#include <QClipboard>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#endif

PhysicalHddDialog::PhysicalHddDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::PhysicalHddDialog)
{
    ui->setupUi(this);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
#ifdef _WIN32
    SP_DEVICE_INTERFACE_DETAIL_DATA_A* devIntData = nullptr;
    auto classDevs = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (classDevs != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devInfo = { .cbSize = sizeof(SP_DEVINFO_DATA) };
        for (int i = 0; SetupDiEnumDeviceInfo(classDevs, i, &devInfo); i++) {
            wchar_t device_friendly_name[1024] = { 0 };
            DWORD dataType = 0;
            DWORD reqSizeStr = 0;
            SP_DEVICE_INTERFACE_DATA intData = { .cbSize = sizeof(SP_DEVICE_INTERFACE_DATA) };
            if (!SetupDiGetDeviceRegistryPropertyW(classDevs, &devInfo, SPDRP_FRIENDLYNAME, &dataType, (PBYTE)device_friendly_name, sizeof(device_friendly_name), &reqSizeStr)) {
                wcsncpy(device_friendly_name, tr("Generic Disk Device").toStdWString().c_str(), 1024);
            }
            for (int j = 0; SetupDiEnumDeviceInterfaces(classDevs, &devInfo, &GUID_DEVINTERFACE_DISK, j, &intData); j++) {
                DWORD reqSize = 0;
                auto res = SetupDiGetDeviceInterfaceDetailA(classDevs, &intData, nullptr, 0, &reqSize, &devInfo);
                if (!res) {
                    if (!reqSize)
                        continue;

                    devIntData = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)realloc(devIntData, reqSize * 2 + sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A));
                    devIntData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
                    res = SetupDiGetDeviceInterfaceDetailA(classDevs, &intData, devIntData, reqSize, &reqSize, &devInfo);
                    if (res) {
                        auto handle = CreateFileA(devIntData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
                        if (handle != INVALID_HANDLE_VALUE) {
                            DWORD bytesRet = 0;
                            STORAGE_DEVICE_NUMBER storage_num;
                            if (DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &storage_num, sizeof(STORAGE_DEVICE_NUMBER), &bytesRet, nullptr)) {
                                uint64_t disk_size = 0;
                                QString physDiskStr = QString::asprintf("\\\\.\\PhysicalDrive%lld", (long long)storage_num.DeviceNumber);
                                GET_LENGTH_INFORMATION lengthInfo;
                                if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof(lengthInfo), &bytesRet, NULL)) {
                                    disk_size = (uint64_t)lengthInfo.Length.QuadPart;
                                }
                                ui->tableWidget->setRowCount(ui->tableWidget->rowCount() + 1);
                                auto pathWidget = new QTableWidgetItem(physDiskStr);
                                pathWidget->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                                ui->tableWidget->setItem(ui->tableWidget->rowCount() - 1, 0, pathWidget);
                                auto nameWidget = new QTableWidgetItem(QString::fromWCharArray(device_friendly_name));
                                nameWidget->setFlags(Qt::NoItemFlags);
                                ui->tableWidget->setItem(ui->tableWidget->rowCount() - 1, 1, nameWidget);
                                QTableWidgetItem* sizeWidget = nullptr;
                                sizeWidget = new QTableWidgetItem(QString("%1 %2").arg(disk_size / (1024. * 1024.)).arg(tr("MB")));
                                sizeWidget->setFlags(Qt::NoItemFlags);
                                ui->tableWidget->setItem(ui->tableWidget->rowCount() - 1, 2, sizeWidget);
                            }
                            CloseHandle(handle);
                        }
                    }
                }
            }
        }
    }
    free(devIntData);

    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableWidget, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos)
    {
        const auto indexAt = ui->tableWidget->itemAt(pos);
        if (ui->tableWidget->columnAt(pos.x()) == 0) {
            QMenu contextMenu("", ui->tableWidget);
            contextMenu.addAction(QLineEdit::tr("&Copy"), [this, indexAt] {
                QApplication::clipboard()->setText(indexAt->text());
            });
            contextMenu.exec(ui->tableWidget->viewport()->mapToGlobal(pos));
        }
    });
#endif
}

PhysicalHddDialog::~PhysicalHddDialog()
{
    delete ui;
}