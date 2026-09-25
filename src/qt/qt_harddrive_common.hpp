#pragma once

#include <cstdint>
#include "qt_settings_bus_tracking.hpp"

class QString;
class QAbstractItemModel;
class QComboBox;
class SettingsBusTracking;
class SettingsCompleter;
struct bus_owner_t;

namespace Harddrives {
void                        populateBuses(QAbstractItemModel *model);
void                        populateCDROMBuses(QAbstractItemModel *model);
void                        populateRemovableBuses(QAbstractItemModel *model);
void                        populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt = nullptr);
void                        populateSpeeds(QAbstractItemModel *model, SettingsCompleter *sc, int bus);
QString                     BusChannelName(uint8_t bus, uint8_t channel);
int                         idePlan(bus_owner_t *owners);
int                         scsiPlan(bus_owner_t *owners);
QString                     ownerName(int bus, const bus_owner_t *owners, int count);
void                        refreshBusNames(QAbstractItemModel *model);
void                        widenPopup(QComboBox *cbox);
inline SettingsBusTracking *busTrackClass = nullptr;
};
