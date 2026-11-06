#pragma once

#include <cstdint>
#include "qt_settings_bus_tracking.hpp"

class QString;
class QAbstractItemModel;
class QComboBox;
class SettingsBusTracking;
class SettingsCompleter;
struct bus_owner_t;
struct ide_conflict_t;

namespace Harddrives {
void                        populateBuses(QAbstractItemModel *model);
void                        populateCDROMBuses(QAbstractItemModel *model);
void                        populateRemovableBuses(QAbstractItemModel *model);
/* lptBridges: on bus LPT, list each port once per bridge (LptBridgesHdd),
   or once per bridge a CD-ROM drive can sit behind (LptBridgesCdrom). */
enum { LptBridgesNone = 0, LptBridgesHdd = 1, LptBridgesCdrom = 2 };
void                        populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt = nullptr, int lptBridges = LptBridgesNone);
/* Hard disks on bus LPT: one channel per port and bridge, port | bridge << 2. */
QString                     lptBridgeName(int bridge);
bool                        lptPortEnabled(int port);
void                        populateSpeeds(QAbstractItemModel *model, SettingsCompleter *sc, int bus);
QString                     BusChannelName(uint8_t bus, uint8_t channel);
int                         idePlan(bus_owner_t *owners, ide_conflict_t *conflicts = nullptr, int *conflictCount = nullptr);
int                         scsiPlan(bus_owner_t *owners);
QString                     ownerName(int bus, const bus_owner_t *owners, int count,
                                      const ide_conflict_t *conflicts = nullptr, int conflictCount = 0);
QString                     conflictText(int index, const bus_owner_t *owners, int count,
                                         const ide_conflict_t *conflicts, int conflictCount);
void                        refreshBusNames(QAbstractItemModel *model);
void                        widenPopup(QComboBox *cbox);
inline SettingsBusTracking *busTrackClass = nullptr;
};
