#pragma once

#include <cstdint>
#include "qt_settings_bus_tracking.hpp"

class QString;
class QAbstractItemModel;
class SettingsBusTracking;
class SettingsCompleter;
struct ide_owner_t;

namespace Harddrives {
void                        populateBuses(QAbstractItemModel *model);
void                        populateCDROMBuses(QAbstractItemModel *model);
void                        populateRemovableBuses(QAbstractItemModel *model);
void                        populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt = nullptr);
void                        populateSpeeds(QAbstractItemModel *model, SettingsCompleter *sc, int bus);
QString                     BusChannelName(uint8_t bus, uint8_t channel);
int                         idePlan(ide_owner_t *owners);
QString                     ideOwnerName(int board, const ide_owner_t *owners);
void                        refreshBusNames(QAbstractItemModel *model);
inline SettingsBusTracking *busTrackClass = nullptr;
};
