#ifndef QT_SETTINGS_BUS_TRACKING_HPP
#define QT_SETTINGS_BUS_TRACKING_HPP

#include <QWidget>

#define TRACK_CLEAR      0
#define TRACK_SET        1

#define DEV_HDD          0x01
#define DEV_CDROM        0x02
#define DEV_ZIP          0x04
#define DEV_MO           0x08

#define BUS_MFM          0
#define BUS_ESDI         1
#define BUS_XTA          2
#define BUS_IDE          3
#define BUS_SCSI         4

#define CHANNEL_NONE     0xff

namespace Ui {
class SettingsBusTracking;
}

class SettingsBusTracking
{
public:
    explicit SettingsBusTracking();
    ~SettingsBusTracking() = default;

    /* These return 0xff is none is free. */
    uint8_t next_free_mfm_channel();
    uint8_t next_free_esdi_channel();
    uint8_t next_free_xta_channel();
    uint8_t next_free_ide_channel();
    uint8_t next_free_scsi_id();

    int mfm_bus_full();
    int esdi_bus_full();
    int xta_bus_full();
    int ide_bus_full();
    int scsi_bus_full();

    /* Set: 0 = Clear the device from the tracking, 1 = Set the device on the tracking.
       Device type: 1 = Hard Disk, 2 = CD-ROM, 4 = ZIP, 8 = Magneto-Optical.
       Bus: 0 = MFM, 1 = ESDI, 2 = XTA, 3 = IDE, 4 = SCSI. */
    void device_track(int set, uint8_t dev_type, int bus, int channel);

private:
    /* 1 channel, 2 devices per channel, 8 bits per device = 16 bits. */
    uint64_t mfm_tracking{0};
    /* 1 channel, 2 devices per channel, 8 bits per device = 16 bits. */
    uint64_t esdi_tracking{0};
    /* 1 channel, 2 devices per channel, 8 bits per device = 16 bits. */
    uint64_t xta_tracking{0};
    /* 16 channels (prepatation for that weird IDE card), 2 devices per channel, 8 bits per device = 256 bits. */
    uint64_t ide_tracking[4]{0, 0, 0, 0};
    /* 4 buses, 16 devices per bus, 8 bits per device (future-proofing) = 512 bits. */
    uint64_t scsi_tracking[8]{0, 0, 0, 0, 0, 0, 0, 0};
};

#endif // QT_SETTINGS_BUS_TRACKING_HPP
