/*
 * Burns discs with the commands a guest would send, down the SCSI bus and both
 * ways over ATAPI, into the real command handler. Skipped without libaaruformat.
 */
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_writable.h>
#include <86box/hdc_ide.h>
#include <86box/scsi_cdrom.h>
}

namespace {
enum class Bus { Scsi, AtapiPio, AtapiDma };

ide_t    test_ide;
ide_tf_t test_tf;

class DriveTest : public ::testing::TestWithParam<Bus> {
protected:
    cdrom_t               *drv = &cdrom[0];
    std::filesystem::path  dir;
    std::string            image;
    scsi_cdrom_t          *dev = nullptr;

    void SetUp() override
    {
        if (!aaru_image_writer_available())
            GTEST_SKIP() << "libaaruformat is not available";

        dir = std::filesystem::temp_directory_path() /
              ("86box-drive-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(dir);
        image = (dir / "disc.aaruf").string();

        std::memset(drv, 0x00, sizeof(cdrom_t));
        std::memset(&test_ide, 0x00, sizeof(test_ide));
        std::memset(&test_tf, 0x00, sizeof(test_tf));
        std::memset(scsi_devices, 0x00, sizeof(scsi_devices));
        test_ide.tf = &test_tf;

        for (uint32_t j = 0; j < _LUT_SIZE; ++j) {
            drv->_F_LUT[j]                = (j << 1) ^ (j & 0x80 ? 0x11d : 0);
            drv->_B_LUT[j ^ drv->_F_LUT[j]] = j;
        }

        if (GetParam() == Bus::Scsi) {
            drv->bus_type = CDROM_BUS_SCSI;
            drv->type     = cdrom_get_from_internal_name("yamaha_crw4416s");
        } else {
            drv->bus_type = CDROM_BUS_ATAPI;
            drv->type     = cdrom_get_from_internal_name("yamaha_crw8424");
        }

        /* cdrom_hard_reset() works this out, which takes all the rest of the machine. */
        drv->is_yamaha = !std::strcmp(cdrom_get_vendor(drv->type), "YAMAHA");

        scsi_cdrom_drive_reset(0);

        dev = static_cast<scsi_cdrom_t *>(drv->priv);
        ASSERT_NE(dev, nullptr);
        ASSERT_TRUE(drv->is_yamaha);

        if (GetParam() == Bus::AtapiDma)
            dev->tf->features = 1;
        dev->tf->request_length = 0x8000;
    }

    void TearDown() override
    {
        if (dev != nullptr) {
            if (drv->local != nullptr)
                drv->ops->close(drv->local);
            drv->local = nullptr;
            drv->ops   = nullptr;
            std::filesystem::remove_all(dir);
        }
    }

    bool send(const std::vector<uint8_t> &cdb_in, const uint8_t *out = nullptr, size_t out_len = 0)
    {
        uint8_t cdb[12] = { 0 };
        std::memcpy(cdb, cdb_in.data(), cdb_in.size());

        dev->tf->status = 0;
        dev->tf->pos    = 0;

        scsi_common_t *sc = reinterpret_cast<scsi_common_t *>(dev);

        if (GetParam() == Bus::Scsi) {
            scsi_devices[0][0].buffer_length = -1;
            scsi_cdrom_t *d = dev;
            scsi_devices[0][0].command(sc, cdb);
            if (out != nullptr) {
                std::memcpy(d->buffer, out, out_len);
                scsi_devices[0][0].phase_data_out(sc);
            }
        } else {
            test_ide.packet_command(sc, cdb);
            if (out != nullptr) {
                const bool sectors = (dev->packet_status != PHASE_ERROR) && (cdb[0] == 0x2a);

                if (sectors) {
                    const size_t block = dev->block_len;

                    if (GetParam() == Bus::AtapiDma) {
                        /* As the bus master hands over one sector at a time. */
                        while (dev->sector_len > 0) {
                            std::memcpy(dev->buffer + dev->buffer_pos, out + dev->buffer_pos, block);
                            test_ide.write(sc);
                        }
                    } else {
                        /* As DRQ blocks fill up, which do not line up with the sectors. */
                        const size_t chunk = 0x7ffe;
                        std::memcpy(dev->buffer, out, out_len);
                        for (size_t pos = chunk; ; pos += chunk) {
                            dev->tf->pos = (pos >= out_len) ? 0 : pos;
                            test_ide.write(sc);
                            if (pos >= out_len)
                                break;
                        }
                    }
                } else if (dev->packet_status != PHASE_ERROR)
                    std::memcpy(dev->buffer, out, out_len);

                if (dev->packet_status != PHASE_ERROR)
                    test_ide.phase_data_out(sc);
            }
        }

        return (dev->packet_status != PHASE_ERROR) && !(dev->tf->status & ERR_STAT);
    }

    void insert(const int type)
    {
        ASSERT_EQ(cdrom_insert_blank(drv, image.c_str(), type), 0);

        /* Gets the media change out of the way. */
        for (int i = 0; i < 3; i++)
            send({ 0x00 });

        dev->unit_attention = 0;
    }

    void modeSelect(const int write_type, const int byte3, const int dbt)
    {
        const std::vector<uint8_t> data = { 0, 0, 0, 0, 0, 0, 0, 0,
                                            0x05, 6, static_cast<uint8_t>(write_type),
                                            static_cast<uint8_t>(byte3), static_cast<uint8_t>(dbt),
                                            0, 0, 0 };

        ASSERT_TRUE(send({ 0x55, 0x10, 0, 0, 0, 0, 0, 0, static_cast<uint8_t>(data.size()) },
                         data.data(), data.size()));
    }

    void write(const int lba, const int count, const int block, const uint8_t fill)
    {
        std::vector<uint8_t> data(count * block, fill);

        for (int i = 0; i < count; i++)
            data[i * block] = i;

        ASSERT_TRUE(send({ 0x2a, 0, static_cast<uint8_t>(lba >> 24), static_cast<uint8_t>(lba >> 16),
                           static_cast<uint8_t>(lba >> 8), static_cast<uint8_t>(lba), 0,
                           static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count) },
                         data.data(), data.size()));
    }

    void expectSector(const int lba, const uint8_t first, const uint8_t fill)
    {
        uint8_t b[2448];

        ASSERT_GT(drv->ops->read_sector(drv->local, b, lba), 0);
        EXPECT_EQ(b[15], 1);
        EXPECT_EQ(b[16], first) << "LBA " << lba;
        EXPECT_EQ(b[17], fill) << "LBA " << lba;
        EXPECT_EQ(b[2063], fill) << "LBA " << lba;
    }

    std::vector<uint8_t> dataIn(const std::vector<uint8_t> &cdb, const int len)
    {
        EXPECT_TRUE(send(cdb));
        return { dev->buffer, dev->buffer + len };
    }

    /* Walks the feature list for one feature's header. */
    const uint8_t *feature(const std::vector<uint8_t> &b, const int code)
    {
        const uint32_t len = (b[2] << 8) | b[3];

        for (uint32_t i = 8; (i + 4) <= (len + 4); i += 4 + b[i + 3])
            if (((b[i] << 8) | b[i + 1]) == code)
                return &b[i];

        return nullptr;
    }

    uint8_t askSense()
    {
        return dev->sense[2] & 0x0f;
    }
};

TEST_P(DriveTest, TrackAtOnceFromTheBus)
{
    insert(CD_BLANK_CDR);

    auto disc = dataIn({ 0x51, 0, 0, 0, 0, 0, 0, 0, 34 }, 34);
    EXPECT_EQ(disc[2], 0x00);

    auto track = dataIn({ 0x52, 1, 0, 0, 0, 0xff, 0, 0, 36 }, 36);
    EXPECT_EQ(track[7] & 1, 1);

    modeSelect(1, 0x04, 8);
    write(0, 40, 2048, 0x5a);
    EXPECT_EQ(dev->sector_len, 0u);

    /* More than one DRQ block's worth, and not a whole number of sectors in each. */
    write(40, 60, 2048, 0x5b);
    EXPECT_EQ(dev->sector_len, 0u);

    EXPECT_TRUE(send({ 0x35 }));
    EXPECT_TRUE(send({ 0x5b, 0, 1, 0, 0, 1 }));

    track = dataIn({ 0x52, 1, 0, 0, 0, 0xff, 0, 0, 36 }, 36);
    EXPECT_EQ((track[14] << 8) | track[15], 100 + 152);

    EXPECT_TRUE(send({ 0x5b, 0, 2 }));

    disc = dataIn({ 0x51, 0, 0, 0, 0, 0, 0, 0, 34 }, 34);
    EXPECT_EQ(disc[2], 0x0e);

    expectSector(0, 0, 0x5a);
    expectSector(39, 39, 0x5a);
    expectSector(99, 59, 0x5b);

    auto cap = dataIn({ 0x25 }, 8);
    EXPECT_EQ(cap[3], 99);
}

TEST_P(DriveTest, SessionAtOnceFromTheBus)
{
    const uint8_t cue[] = {
        0x41, 0x00, 0x00, 0x14, 0x00, 0, 0, 0,
        0x41, 0x01, 0x01, 0x10, 0x00, 0, 2, 0,
        0x41, 0xaa, 0x01, 0x14, 0x00, 0, 3, 0,
    };

    insert(CD_BLANK_CDR);
    modeSelect(2, 0x04, 8);

    EXPECT_TRUE(send({ 0x5d, 0, 0, 0, 0, 0, 0, 0, sizeof(cue) }, cue, sizeof(cue)));

    write(0, 75, 2048, 0x33);
    EXPECT_TRUE(send({ 0x35 }));

    auto disc = dataIn({ 0x51, 0, 0, 0, 0, 0, 0, 0, 34 }, 34);
    EXPECT_EQ(disc[2], 0x0e);
    expectSector(74, 74, 0x33);
}

TEST_P(DriveTest, RawFromTheBus)
{
    insert(CD_BLANK_CDR);
    modeSelect(3, 0x00, 3);

    /* Two sectors with track 1 index 1 in their Q sub-channel, Mode 1 at LBA 0. */
    std::vector<uint8_t> data(2 * 2448, 0x00);
    uint8_t              q[12] = { 0x41, 0x01, 0x01 };

    for (int i = 0; i < 2; i++) {
        uint8_t *b = &data[i * 2448];
        std::memset(b + 16, 0x77, 2048);
        cdrom_build_raw_sector(drv, b, i, 1, 0, 0);
        b[16] = i;
        for (int j = 0; j < 96; j++)
            b[2352 + j] = ((q[j >> 3] >> (7 - (j & 7))) & 1) << 6;
    }

    EXPECT_TRUE(send({ 0x2a, 0, 0, 0, 0, 0, 0, 0, 2 }, data.data(), data.size()));
    EXPECT_TRUE(send({ 0x35 }));

    auto disc = dataIn({ 0x51, 0, 0, 0, 0, 0, 0, 0, 34 }, 34);
    EXPECT_EQ(disc[2], 0x0e);
}

TEST_P(DriveTest, FormatsAndOverwritesCdRw)
{
    insert(CD_BLANK_CDRW);

    auto caps = dataIn({ 0x23, 0, 0, 0, 0, 0, 0, 0, 20 }, 20);
    EXPECT_EQ(caps[8], 0x01);

    const uint8_t format[12] = { 0, 0x02, 0, 8, 0, 0, 0, 0, 0x10 << 2, 0, 0, 32 };
    EXPECT_TRUE(send({ 0x04, 0x11 }, format, sizeof(format)));

    caps = dataIn({ 0x23, 0, 0, 0, 0, 0, 0, 0, 20 }, 20);
    EXPECT_EQ(caps[8], 0x02);

    modeSelect(0, 0x27, 8);
    write(64, 32, 2048, 0x11);
    write(64, 32, 2048, 0x22);
    EXPECT_TRUE(send({ 0x35 }));
    expectSector(70, 6, 0x22);

    EXPECT_TRUE(send({ 0xa1, 0x01 }));

    auto disc = dataIn({ 0x51, 0, 0, 0, 0, 0, 0, 0, 34 }, 34);
    EXPECT_EQ(disc[2], 0x10);
}

TEST_P(DriveTest, BlankingCdrIsRefused)
{
    insert(CD_BLANK_CDR);
    EXPECT_FALSE(send({ 0xa1, 0x01 }));
    EXPECT_EQ(askSense(), SENSE_ILLEGAL_REQUEST);
}

TEST_P(DriveTest, PacketOnCdrAndReservation)
{
    insert(CD_BLANK_CDR);
    modeSelect(0, 0x07, 8);

    EXPECT_TRUE(send({ 0x53, 0, 0, 0, 0, 0, 0, 0x10, 0 }));
    write(0, 32, 2048, 0x44);
    EXPECT_TRUE(send({ 0x35 }));

    auto track = dataIn({ 0x52, 1, 0, 0, 0, 0xff, 0, 0, 36 }, 36);
    EXPECT_EQ(track[6] & 0x20, 0x20);
    EXPECT_EQ((track[14] << 8) | track[15], 39);
    expectSector(31, 31, 0x44);
}

TEST_P(DriveTest, ReportsWhatItCanWrite)
{
    insert(CD_BLANK_CDRW);

    const auto b = dataIn({ 0x46, 0, 0, 0, 0, 0, 0, 0x08, 0 }, 0x800);

    EXPECT_EQ((b[6] << 8) | b[7], MMC_PROFILE_CD_RW);

    for (const int code : { 0x21, 0x23, 0x26, 0x27, 0x2d, 0x2e }) {
        const uint8_t *f = feature(b, code);
        ASSERT_NE(f, nullptr) << code;
        EXPECT_EQ(f[2] & 1, 1) << code;
    }

    EXPECT_EQ(feature(b, 0x2d)[4] & 0x02, 0x02);
    EXPECT_EQ(feature(b, 0x2e)[4] & 0x28, 0x28);
    EXPECT_EQ(feature(b, 0x21)[8], 7);

    const auto one = dataIn({ 0x46, 2, 0, 0x2e, 0, 0, 0, 0, 16 }, 16);
    EXPECT_EQ(one[9], 0x2e);
}

TEST_P(DriveTest, TestWriteTouchesNothing)
{
    insert(CD_BLANK_CDR);
    modeSelect(0x11, 0x04, 8);
    write(0, 10, 2048, 0x66);
    EXPECT_TRUE(send({ 0x35 }));
    EXPECT_TRUE(send({ 0x5b, 0, 2 }));
    EXPECT_TRUE(cdrom_is_blank(drv));
}

INSTANTIATE_TEST_SUITE_P(Buses, DriveTest, ::testing::Values(Bus::Scsi, Bus::AtapiPio, Bus::AtapiDma));

class ReadOnlyDriveTest : public ::testing::Test {};

TEST_F(ReadOnlyDriveTest, OnlyRecordersGetTheNewCommands)
{
    std::memset(&cdrom[0], 0x00, sizeof(cdrom_t));
    cdrom[0].bus_type = CDROM_BUS_SCSI;
    cdrom[0].type     = cdrom_get_from_internal_name("chinon_431");
    std::memset(scsi_devices, 0x00, sizeof(scsi_devices));
    scsi_cdrom_drive_reset(0);

    auto   *dev = static_cast<scsi_cdrom_t *>(cdrom[0].priv);
    uint8_t cdb[12] = { 0x23 };

    ASSERT_NE(dev, nullptr);
    EXPECT_FALSE(cdrom[0].is_yamaha);

    cdrom[0].cd_status = CD_STATUS_DATA_ONLY;
    dev->unit_attention = 0;
    scsi_devices[0][0].buffer_length = -1;
    scsi_devices[0][0].command(reinterpret_cast<scsi_common_t *>(dev), cdb);
    EXPECT_EQ(dev->packet_status, PHASE_ERROR);
}
}

extern "C" {
scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];
ide_t        *ide_drives[IDE_NUM];

ide_t *ide_get_drive(int) { return &test_ide; }
void   ide_atapi_attach(ide_t *) {}
double ide_atapi_get_period(uint8_t) { return 1.0; }
void   ide_set_callback(ide_t *, double) {}
void   ide_padstr(char *, const char *, int) {}
void   ide_padstr8(uint8_t *, int, const char *) {}
void   ui_sb_update_icon(int, int) {}
void   ui_sb_update_icon_write(int, int) {}
void   log_out(void *, const char *, va_list) {}
}
