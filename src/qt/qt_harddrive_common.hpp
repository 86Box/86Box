#pragma once

#include <cstdint>
#include "qt_settings_bus_tracking.hpp"

class QString;
class QAbstractItemModel;
class SettingsBusTracking;

namespace Harddrives {
void                        populateBuses(QAbstractItemModel *model);
void                        populateCDROMBuses(QAbstractItemModel *model);
void                        populateRemovableBuses(QAbstractItemModel *model);
void                        populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt = nullptr);
void                        populateSpeeds(QAbstractItemModel *model, int bus);
QString                     BusChannelName(uint8_t bus, uint8_t channel);
inline SettingsBusTracking *busTrackClass = nullptr;
};
