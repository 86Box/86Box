#pragma once

#include <cstdint>

class QString;
class QAbstractItemModel;

namespace Harddrives {
    void populateBuses(QAbstractItemModel* model);
    void populateRemovableBuses(QAbstractItemModel* model);
    void populateBusChannels(QAbstractItemModel* model, int bus);
    QString BusChannelName(uint8_t bus, uint8_t channel);
};
