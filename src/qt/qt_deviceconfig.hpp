#ifndef QT_DEVICECONFIG_HPP
#define QT_DEVICECONFIG_HPP

#include <QDialog>

#include "qt_settings.hpp"

extern "C" {
struct _device_;
}

namespace Ui {
class DeviceConfig;
}

class Settings;

class DeviceConfig : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceConfig(QWidget *parent = nullptr);
    ~DeviceConfig();

    static void ConfigureDevice(const _device_* device, int instance = 0, Settings* settings = nullptr);
    static QString DeviceName(const _device_* device, const char* internalName, int bus);
private:
    Ui::DeviceConfig *ui;
};

#endif // QT_DEVICECONFIG_HPP
