#ifndef QT_SETTINGS_HPP
#define QT_SETTINGS_HPP

#include <QDialog>

namespace Ui {
class Settings;
}

class SettingsMachine;
class SettingsDisplay;
class SettingsInput;
class SettingsSound;
class SettingsNetwork;
class SettingsPorts;
class SettingsStorageControllers;
class SettingsHarddisks;
class SettingsFloppyCDROM;
class SettingsOtherRemovable;
class SettingsOtherPeripherals;

class Settings : public QDialog
{
    Q_OBJECT

public:
     explicit Settings(QWidget *parent = nullptr);
    ~Settings();
     void save();

     static Settings* settings;
protected slots:
     void accept() override;

private:
    Ui::Settings *ui;
    SettingsMachine* machine;
    SettingsDisplay* display;
    SettingsInput* input;
    SettingsSound* sound;
    SettingsNetwork* network;
    SettingsPorts* ports;
    SettingsStorageControllers* storageControllers;
    SettingsHarddisks* harddisks;
    SettingsFloppyCDROM* floppyCdrom;
    SettingsOtherRemovable* otherRemovable;
    SettingsOtherPeripherals* otherPeripherals;
};

#endif // QT_SETTINGS_HPP
