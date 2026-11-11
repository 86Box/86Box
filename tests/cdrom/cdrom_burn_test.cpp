/*
 * Burns discs through the same calls the emulated drive makes, then reads the
 * resulting Aaru images back through the regular Aaru reader. These tests need
 * libaaruformat.so at run time, and are skipped without it.
 */
#include <gtest/gtest.h>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_image_writable.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
extern uint8_t cdrom_scramble_table[2352];
}

namespace {
constexpr int kTao = 1;
constexpr int kSao = 2;
constexpr int kPacket = 0;

constexpr int kDbtRaw    = 0;
constexpr int kDbtMode1  = 8;
constexpr int kDbtForm1  = 10;
constexpr int kDbtForm2  = 12;

class BurnTest : public ::testing::Test {
protected:
    cdrom_t                   *dev = nullptr;
    std::filesystem::path      dir;
    std::string                image;
    uint8_t                    rti[65536];

    void SetUp() override
    {
        if (!aaru_image_writer_available())
            GTEST_SKIP() << "libaaruformat is not available";

        dir = std::filesystem::temp_directory_path() /
              ("86box-burn-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(dir);
        image = (dir / "disc.aaruf").string();

        cdrom_generate_scramble_lut(cdrom_scramble_table);

        dev           = new cdrom_t{};
        dev->bus_type = CDROM_BUS_ATAPI;
        for (uint32_t j = 0; j < _LUT_SIZE; ++j) {
            dev->_F_LUT[j]                = (j << 1) ^ (j & 0x80 ? 0x11d : 0);
            dev->_B_LUT[j ^ dev->_F_LUT[j]] = j;
        }
        ASSERT_EQ(cdrom_insert_blank(dev, image.c_str(), CD_BLANK_CDR), 0);
    }

    void TearDown() override
    {
        if (dev != nullptr) {
            unload();
            delete dev;
            std::filesystem::remove_all(dir);
        }
    }

    void unload()
    {
        if (dev->local != nullptr)
            dev->ops->close(dev->local);
        dev->local       = nullptr;
        dev->ops         = nullptr;
        dev->blank_media = CD_BLANK_NOT;
    }

    void reopen()
    {
        unload();
        dev->local = aaru_image_open(dev, image.c_str());
        ASSERT_NE(dev->local, nullptr);
    }

    /* Goes through what a media change goes through, except for the guest's side of it. */
    void reinsert()
    {
        unload();
        ASSERT_EQ(cdrom_load(dev, image.c_str(), 1), 0);
        ASSERT_NE(dev->local, nullptr);
    }

    void insertRewritable()
    {
        unload();
        ASSERT_EQ(cdrom_insert_blank(dev, image.c_str(), CD_BLANK_CDRW), 0);
    }

    void writeRaw(const int lba, const int count, const int tno, const int index, const int ctl,
                  const int dbt, const bool scramble = false)
    {
        std::vector<uint8_t> b(cdrom_get_write_block_size(dev, lba, 3, dbt));

        for (int i = 0; i < count; i++) {
            const int sector = lba + i;
            uint8_t   q[12]  = { 0 };

            std::memset(b.data(), 0, b.size());

            if (sector >= 0) {
                const auto p = payload(sector, 2352);
                std::memcpy(b.data(), p.data(), 2352);
            }

            if ((ctl & 4) && (tno != 0xaa)) {
                std::vector<uint8_t> user(2352);
                std::memcpy(user.data() + 16, payload(sector, 2048).data(), 2048);
                cdrom_build_raw_sector(dev, user.data(), sector, 1, 0, 0);
                if (scramble)
                    for (int j = 12; j < 2352; j++)
                        user[j] ^= cdrom_scramble_table[j];
                std::memcpy(b.data(), user.data(), 2352);
            }

            q[0] = (ctl << 4) | 1;
            q[1] = (tno == 0xaa) ? 0xaa : bin2bcd(tno);
            q[2] = bin2bcd(index);

            if (dbt == 1)
                std::memcpy(&b[2352], q, 12);
            else
                for (int j = 0; j < 96; j++)
                    b[2352 + j] = ((q[j >> 3] >> (7 - (j & 7))) & 1) << 6;

            ASSERT_EQ(cdrom_writesector(dev, b.data(), sector, 3, dbt, 0), 1) << "LBA " << sector;
        }
    }

    static std::vector<uint8_t> payload(const int lba, const int size)
    {
        std::vector<uint8_t> b(size);
        for (int i = 0; i < size; i++)
            b[i] = static_cast<uint8_t>((lba * 7) + (i * 13) + (i >> 8));
        return b;
    }

    void write(const int lba, const int count, const int type, const int dbt, const int ctl)
    {
        const int size = cdrom_get_write_block_size(dev, lba, type, dbt);
        ASSERT_NE(size, 0);
        for (int i = 0; i < count; i++)
            ASSERT_EQ(cdrom_writesector(dev, payload(lba + i, size).data(), lba + i, type, dbt, ctl), 1)
                << "LBA " << lba + i;
    }

    std::vector<uint8_t> read(const int lba)
    {
        std::vector<uint8_t> b(2448);
        EXPECT_GT(dev->ops->read_sector(dev->local, b.data(), lba), 0) << "LBA " << lba;
        return b;
    }

    std::vector<raw_track_info_t> toc()
    {
        int num = 0;
        dev->ops->get_raw_track_info(dev->local, &num, rti);
        const auto *t = reinterpret_cast<const raw_track_info_t *>(rti);
        return { t, t + num };
    }

    static const raw_track_info_t *point(const std::vector<raw_track_info_t> &t, const int p,
                                         const int session = 1)
    {
        for (const auto &e : t)
            if ((e.point == p) && (e.session == session))
                return &e;
        return nullptr;
    }

    static int lba(const raw_track_info_t *e)
    {
        return (((e->pm * 60) + e->ps) * 75) + e->pf - 150;
    }

    cdrom_write_info_t info()
    {
        cdrom_write_info_t wi;
        dev->ops->get_write_info(dev->local, &wi);
        return wi;
    }

    void expectUserData(const int lba, const int offset, const int size)
    {
        const auto b = read(lba);
        const auto p = payload(lba, size);
        EXPECT_EQ(std::memcmp(b.data() + offset, p.data(), size), 0) << "LBA " << lba;
    }

    void expectHeader(const int lba, const int mode)
    {
        const auto b   = read(lba);
        const int  pos = lba + 150;
        EXPECT_EQ(b[12], bin2bcd(pos / 4500));
        EXPECT_EQ(b[13], bin2bcd((pos / 75) % 60));
        EXPECT_EQ(b[14], bin2bcd(pos % 75));
        EXPECT_EQ(b[15], mode);
    }

    static uint32_t edc(const uint8_t *b, const int len)
    {
        return cdrom_crc32(0xffffffff, b, len) ^ 0xffffffff;
    }
};

TEST_F(BurnTest, BlankDiscHasNothingToRead)
{
    EXPECT_TRUE(cdrom_is_blank(dev));
    EXPECT_EQ(info().state, CD_WSTATE_BLANK);
    EXPECT_EQ(info().nwa, 0u);
    EXPECT_EQ(info().next_track, 1);
    EXPECT_TRUE(toc().empty());
}

TEST_F(BurnTest, TaoMode1SurvivesTheImage)
{
    write(0, 300, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_FALSE(cdrom_is_blank(dev));
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
    EXPECT_EQ(info().state, CD_WSTATE_CLOSED);
    EXPECT_EQ(dev->cdrom_capacity, 299u);

    const auto before = read(123);
    reopen();

    EXPECT_EQ(dev->ops->get_last_block(dev->local), 299u);
    EXPECT_EQ(std::memcmp(read(123).data(), before.data(), 2352), 0);
    expectUserData(0, 16, 2048);
    expectUserData(299, 16, 2048);
    expectHeader(0, 1);
    expectHeader(299, 1);

    const auto b = read(7);
    EXPECT_EQ(*reinterpret_cast<const uint32_t *>(&b[2064]), edc(b.data(), 2064));

    const auto t = toc();
    ASSERT_NE(point(t, 0xa2), nullptr);
    EXPECT_EQ(lba(point(t, 0xa2)), 300);
    ASSERT_NE(point(t, 1), nullptr);
    EXPECT_EQ(lba(point(t, 1)), 0);
    EXPECT_EQ(point(t, 1)->adr_ctl, 0x14);
}

TEST_F(BurnTest, TaoMode2FormsSurviveTheImage)
{
    write(0, 100, kTao, kDbtForm1, 4);
    write(100, 100, kTao, kDbtForm2, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    const auto form1 = read(50);
    const auto form2 = read(150);
    EXPECT_EQ(*reinterpret_cast<const uint32_t *>(&form1[2072]), edc(&form1[16], 2056));
    EXPECT_EQ(*reinterpret_cast<const uint32_t *>(&form2[2348]), edc(&form2[16], 2332));

    reopen();
    expectHeader(50, 2);
    expectUserData(50, 24, 2048);
    expectUserData(150, 24, 2324);
    EXPECT_EQ(std::memcmp(read(50).data(), form1.data(), 2352), 0);
    EXPECT_EQ(std::memcmp(read(150).data(), form2.data(), 2352), 0);
}

TEST_F(BurnTest, TaoMixedModeDiscKeepsTrackLayout)
{
    write(0, 200, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);

    const int nwa = info().nwa;
    EXPECT_EQ(nwa, 200 + 152);
    EXPECT_EQ(info().next_track, 2);
    EXPECT_EQ(info().state, CD_WSTATE_APPENDABLE);
    EXPECT_NE(cdrom_writesector(dev, payload(0, 2352).data(), 5, kTao, kDbtRaw, 0), 1);

    write(nwa, 200, kTao, kDbtRaw, 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    reopen();

    EXPECT_EQ(dev->ops->has_audio(dev->local), 1);
    expectUserData(nwa + 17, 0, 2352);
    const auto t = toc();
    ASSERT_NE(point(t, 2), nullptr);
    EXPECT_EQ(lba(point(t, 2)), nwa);
    EXPECT_EQ(point(t, 2)->adr_ctl, 0x10);
    EXPECT_EQ(lba(point(t, 0xa2)), nwa + 200);
}

TEST_F(BurnTest, MultiSessionDiscStaysAppendable)
{
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);

    /* The first lead-out, the next lead-in, and a pre-gap go by before session 2. */
    const int nwa = info().nwa;
    EXPECT_EQ(nwa, 100 + 11400);
    EXPECT_EQ(info().state, CD_WSTATE_APPENDABLE);
    EXPECT_EQ(info().sessions, 2);
    EXPECT_EQ(info().next_track, 2);
    EXPECT_EQ(info().session_open, 0);
    EXPECT_EQ(dev->cdrom_capacity, 99u);

    /* Session 1 is on the disc already, the image file included. */
    EXPECT_TRUE(cdrom_image_is_aaru(image.c_str()));
    expectUserData(10, 16, 2048);

    write(nwa, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);

    {
        const auto t = toc();
        ASSERT_NE(point(t, 0xb0, 1), nullptr);
        ASSERT_NE(point(t, 0xb0, 2), nullptr);
        ASSERT_NE(point(t, 0xc0, 1), nullptr);
    }

    reopen();

    const auto t = toc();
    ASSERT_NE(point(t, 2, 2), nullptr);
    EXPECT_EQ(lba(point(t, 2, 2)), nwa);
    EXPECT_EQ(lba(point(t, 0xa2, 1)), 100);
    expectUserData(nwa + 50, 16, 2048);
    expectHeader(nwa + 50, 1);
    expectUserData(50, 16, 2048);
}

TEST_F(BurnTest, ClosingAnAppendableDiscEndsIt)
{
    write(0, 10, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
    EXPECT_EQ(info().state, CD_WSTATE_CLOSED);
    EXPECT_EQ(point(toc(), 0xb0), nullptr);
    EXPECT_NE(cdrom_writesector(dev, payload(0, 2048).data(), 20000, kTao, kDbtMode1, 4), 1);
}

TEST_F(BurnTest, SaoWritesWhatTheCueSheetSays)
{
    const uint8_t cue[] = {
        0x41, 0x00, 0x00, 0x14, 0x00, 0, 0, 0,    /* Lead-in. */
        0x41, 0x01, 0x00, 0x10, 0x00, 0, 0, 0,    /* Pre-gap, the host sends it. */
        0x41, 0x01, 0x01, 0x10, 0x00, 0, 2, 0,    /* Track 1 at LBA 0. */
        0x01, 0x02, 0x00, 0x01, 0x00, 0, 6, 0,    /* A pause the drive generates. */
        0x01, 0x02, 0x01, 0x00, 0x00, 0, 8, 0,    /* Track 2 at LBA 450. */
        0x01, 0xaa, 0x01, 0x01, 0x00, 0, 10, 0,   /* Lead-out at LBA 600. */
    };

    EXPECT_EQ(cdrom_send_cuesheet(dev, cue, sizeof(cue)), 0);
    EXPECT_EQ(cdrom_get_write_block_size(dev, -150, kSao, 0), 2048);
    EXPECT_EQ(cdrom_get_write_block_size(dev, 10, kSao, 0), 2048);
    EXPECT_EQ(cdrom_get_write_block_size(dev, 500, kSao, 0), 2352);
    EXPECT_EQ(cdrom_get_write_block_size(dev, 600, kSao, 0), 0);

    write(-150, 450, kSao, 0, 4);
    write(450, 150, kSao, 0, 0);

    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);
    EXPECT_EQ(info().state, CD_WSTATE_CLOSED);

    reopen();

    const auto t = toc();
    ASSERT_NE(point(t, 2), nullptr);
    EXPECT_EQ(lba(point(t, 1)), 0);
    EXPECT_EQ(lba(point(t, 2)), 450);
    EXPECT_EQ(lba(point(t, 0xa2)), 600);
    EXPECT_EQ(dev->ops->get_last_block(dev->local), 599u);
    expectUserData(0, 16, 2048);
    expectHeader(0, 1);
    expectUserData(449 - 150, 16, 2048);
    expectUserData(450, 0, 2352);
    expectUserData(599, 0, 2352);
}

TEST_F(BurnTest, BadCueSheetsAreRefused)
{
    const uint8_t wrong_track[] = {
        0x41, 0x05, 0x01, 0x10, 0x00, 0, 2, 0,
        0x41, 0xaa, 0x01, 0x14, 0x00, 0, 4, 0,
    };
    const uint8_t no_lead_out[] = {
        0x41, 0x01, 0x01, 0x10, 0x00, 0, 2, 0,
    };

    EXPECT_NE(cdrom_send_cuesheet(dev, wrong_track, sizeof(wrong_track)), 0);
    EXPECT_NE(cdrom_send_cuesheet(dev, no_lead_out, sizeof(no_lead_out)), 0);
    EXPECT_NE(cdrom_send_cuesheet(dev, wrong_track, 7), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));
}

TEST_F(BurnTest, TestWriteLeavesTheDiscBlank)
{
    const uint8_t cue[] = {
        0x41, 0x01, 0x01, 0x10, 0x00, 0, 2, 0,
        0x41, 0xaa, 0x01, 0x14, 0x00, 0, 4, 0,
    };

    EXPECT_EQ(cdrom_send_cuesheet(dev, cue, sizeof(cue)), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_ABORT), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));
    EXPECT_EQ(cdrom_writesector(dev, payload(0, 2048).data(), 0, kTao, kDbtMode1, 4), 1);
}

TEST_F(BurnTest, DriveReportsTheStateOfTheDisc)
{
    uint8_t       b[36];
    const uint8_t next_track[10] = { 0x52, 0x01, 0, 0, 0, 0xff };

    cdrom_read_disc_information(dev, b);
    EXPECT_EQ(b[2], 0x00);

    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);

    cdrom_read_disc_information(dev, b);
    EXPECT_EQ(b[2], 0x05);
    EXPECT_EQ(b[6], 2);

    /* The invisible track is where the next write has to go. */
    EXPECT_EQ(cdrom_read_track_information(dev, next_track, b), 28);
    EXPECT_EQ(b[2], 2);
    EXPECT_EQ((b[12] << 24) | (b[13] << 16) | (b[14] << 8) | b[15], 100 + 152);
    EXPECT_EQ((b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19], 0x57da4 - 252);

    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);
    cdrom_read_disc_information(dev, b);
    EXPECT_EQ(b[2], 0x01);
    EXPECT_EQ(b[4], 2);

    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
    cdrom_read_disc_information(dev, b);
    EXPECT_EQ(b[2], 0x0e);
    EXPECT_NE(cdrom_read_track_information(dev, next_track, b), 28);
}

TEST_F(BurnTest, RawRecordingFollowsTheSubChannel)
{
    for (const int dbt : { 1, 2, 3 }) {
        SCOPED_TRACE(dbt);
        unload();
        ASSERT_EQ(cdrom_insert_blank(dev, image.c_str(), CD_BLANK_CDR), 0);

        writeRaw(-300, 150, 0, 0, 4, dbt);            /* Lead-in. */
        writeRaw(-150, 150, 1, 0, 4, dbt);            /* First pre-gap. */
        writeRaw(0, 100, 1, 1, 4, dbt, dbt != 1);     /* A data track, scrambled by most. */
        writeRaw(100, 150, 2, 0, 0, dbt);             /* An audio track's pre-gap. */
        writeRaw(250, 100, 2, 1, 0, dbt);
        writeRaw(350, 20, 0xaa, 1, 0, dbt);           /* Lead-out. */

        EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);
        EXPECT_EQ(info().state, CD_WSTATE_CLOSED);

        reopen();

        const auto t = toc();
        ASSERT_NE(point(t, 2), nullptr);
        EXPECT_EQ(lba(point(t, 1)), 0);
        EXPECT_EQ(lba(point(t, 2)), 250);
        EXPECT_EQ(lba(point(t, 0xa2)), 350);
        EXPECT_EQ(point(t, 1)->adr_ctl, 0x14);
        EXPECT_EQ(point(t, 2)->adr_ctl, 0x10);
        expectUserData(50, 16, 2048);
        expectHeader(50, 1);
        expectUserData(300, 0, 2352);
    }
}

TEST_F(BurnTest, RawRecordingRefusesTrackGaps)
{
    std::vector<uint8_t> b(2448, 0);
    uint8_t              q[12] = { 0x41, 0x05, 0x01 };

    for (int j = 0; j < 96; j++)
        b[2352 + j] = ((q[j >> 3] >> (7 - (j & 7))) & 1) << 6;

    EXPECT_NE(cdrom_writesector(dev, b.data(), 0, 3, 3, 0), 1);
    EXPECT_NE(cdrom_writesector(dev, b.data(), 0, 3, 8, 0), 1);
}

TEST_F(BurnTest, IncrementalPacketsAreLinked)
{
    write(0, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(info().nwa, 32u);
    EXPECT_EQ(info().track_packet, 1);

    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);
    EXPECT_EQ(info().nwa, 39u);
    EXPECT_NE(cdrom_writesector(dev, payload(0, 2048).data(), 38, kPacket, kDbtMode1, 7), 1);

    write(39, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);

    /* The guest can see the track as it grows, it never has to be closed for that. */
    EXPECT_EQ(dev->cdrom_capacity, 70u);
    expectUserData(45, 16, 2048);

    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    reopen();
    expectUserData(0, 16, 2048);
    expectUserData(70, 16, 2048);
}

TEST_F(BurnTest, IncrementalTrackGoesOnAfterAMediaChange)
{
    write(0, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);

    reinsert();

    EXPECT_EQ(dev->blank_media, CD_BLANK_CDR);
    EXPECT_EQ(info().nwa, 39u);
    EXPECT_EQ(info().track_packet, 1);
    expectUserData(10, 16, 2048);

    write(39, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);

    reinsert();
    expectUserData(10, 16, 2048);
    expectUserData(60, 16, 2048);
}

TEST_F(BurnTest, AppendableDiscGoesOnAfterAMediaChange)
{
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);
    const int nwa = info().nwa;

    reinsert();

    EXPECT_EQ(info().state, CD_WSTATE_APPENDABLE);
    EXPECT_EQ(info().sessions, 2);
    EXPECT_EQ(static_cast<int>(info().nwa), nwa);

    write(nwa, 50, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    /* That was the end of it, and it is a plain read-only disc from now on. */
    reinsert();
    EXPECT_EQ(dev->blank_media, CD_BLANK_NOT);
    expectUserData(20, 16, 2048);
    expectUserData(nwa + 20, 16, 2048);
}

TEST_F(BurnTest, UnfinishedSessionGoesOnAfterAMediaChange)
{
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);

    /* No session close, the guest is simply done for now. */
    reinsert();

    EXPECT_EQ(info().state, CD_WSTATE_APPENDABLE);
    EXPECT_EQ(info().session_open, 1);
    EXPECT_EQ(info().next_track, 2);
    EXPECT_EQ(info().nwa, 100u + 152u);

    write(252, 50, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);

    reinsert();

    EXPECT_EQ(info().sessions, 2);
    EXPECT_EQ(info().session_open, 0);
    const auto t = toc();
    ASSERT_NE(point(t, 2, 1), nullptr);
    EXPECT_EQ(lba(point(t, 2, 1)), 252);
    expectUserData(252, 16, 2048);
    expectUserData(0, 16, 2048);
}

TEST_F(BurnTest, MountingOnlyCopiesTheDiscOnceItChanges)
{
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);
    const int nwa = info().nwa;

    reinsert();

    EXPECT_EQ(std::filesystem::file_size(image + ".spool"), 0u);
    expectUserData(50, 16, 2048);
    EXPECT_EQ(std::filesystem::file_size(image + ".spool"), 0u);

    write(nwa, 10, kTao, kDbtMode1, 4);

    EXPECT_GT(std::filesystem::file_size(image + ".spool"), 2352u * 100u);
    expectUserData(50, 16, 2048);
    expectUserData(nwa + 5, 16, 2048);
}

TEST_F(BurnTest, CrashKeepsWhatWasCheckpointed)
{
    const std::string placeholder = image + ".blank";
    std::filesystem::copy_file(image, placeholder);

    insertRewritable();
    std::filesystem::copy_file(image, placeholder,
                               std::filesystem::copy_options::overwrite_existing);

    EXPECT_EQ(cdrom_format(dev, 0, 32, 0), 0);
    write(64, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);

    /* This is everything that is left after a power cut: no clean close ever happens. */
    const std::string spool = image + ".spool";
    const std::string state = image + ".spool.state";
    ASSERT_TRUE(std::filesystem::exists(state));
    std::filesystem::copy_file(spool, spool + ".saved");
    std::filesystem::copy_file(state, state + ".saved");
    unload();
    std::filesystem::rename(spool + ".saved", spool);
    std::filesystem::rename(state + ".saved", state);
    std::filesystem::copy_file(placeholder, image,
                               std::filesystem::copy_options::overwrite_existing);

    reinsert();

    EXPECT_EQ(dev->blank_media, CD_BLANK_CDRW);
    EXPECT_EQ(info().fixed_packet, 32u);
    expectUserData(70, 16, 2048);

    /* The next clean close puts it all in the image, and tidies up. */
    reinsert();
    EXPECT_FALSE(std::filesystem::exists(state));
    expectUserData(70, 16, 2048);
    EXPECT_EQ(info().fixed_packet, 32u);
}

TEST_F(BurnTest, CatalogAndIsrcStayWithTheDisc)
{
    const uint8_t cue[] = {
        0x02, '1', '2', '3', '4', '5', '6', '7',
        0x02, '8', '9', '0', '1', '2', '3', 0,
        0x03, 0x01, 'U', 'S', 'A', 'B', 'C', '9',
        0x03, 0x01, '9', '0', '0', '0', '0', '1',
        0x41, 0x00, 0x00, 0x14, 0x00, 0, 0, 0,
        0x01, 0x01, 0x01, 0x00, 0x00, 0, 2, 0,
        0x01, 0xaa, 0x01, 0x01, 0x00, 0, 3, 0,
    };
    uint8_t mcn[13] = { 0 };

    EXPECT_EQ(cdrom_send_cuesheet(dev, cue, sizeof(cue)), 0);
    write(0, 75, kSao, 0, 0);
    EXPECT_EQ(cdrom_flush_writes(dev, 0), 0);
    ASSERT_TRUE(dev->ops->get_mcn(dev->local, mcn));
    EXPECT_EQ(std::memcmp(mcn, "1234567890123", 13), 0);

    reopen();
    std::memset(mcn, 0, 13);
    ASSERT_TRUE(dev->ops->get_mcn(dev->local, mcn));
    EXPECT_EQ(std::memcmp(mcn, "1234567890123", 13), 0);

    unload();
    aaru_write_track_t tracks[99];
    uint8_t            again[13];
    int                num    = 99;
    void              *source = aaru_image_source_open(image.c_str(), tracks, &num, again);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(std::memcmp(tracks[0].isrc, "USABC9900001", 12), 0);
    aaru_image_source_close(source);
}

TEST_F(BurnTest, RawCatalogComesOutOfTheSubChannel)
{
    const uint8_t q[12] = { 0x42, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x30 };
    uint8_t       b[2368] = { 0 };
    uint8_t       mcn[13];

    std::memcpy(&b[2352], q, 12);
    EXPECT_EQ(cdrom_writesector(dev, b, -2000, 3, 1, 0), 1);
    ASSERT_TRUE(dev->ops->get_mcn(dev->local, mcn));
    EXPECT_EQ(std::memcmp(mcn, "1234567890123", 13), 0);
}

TEST_F(BurnTest, CdRwTracksCanBeTakenBack)
{
    insertRewritable();
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    write(252, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    /* Track tail. */
    EXPECT_EQ(cdrom_blank(dev, 4, 300), 0);
    EXPECT_EQ(dev->cdrom_capacity, 299u);
    EXPECT_EQ(info().nwa, 300u + 152u);
    EXPECT_NE(cdrom_blank(dev, 4, 10), 0);

    /* The whole track. */
    EXPECT_EQ(cdrom_blank(dev, 2, 252), 0);
    EXPECT_EQ(dev->cdrom_capacity, 99u);
    EXPECT_EQ(info().nwa, 100u + 152u);
    EXPECT_EQ(info().next_track, 2);

    /* The last one takes the session, and so the disc, with it. */
    EXPECT_EQ(cdrom_blank(dev, 2, 0), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));
}

TEST_F(BurnTest, FormattedDiscCanGrow)
{
    insertRewritable();
    EXPECT_NE(cdrom_format(dev, 3200, 32, 1), 0);
    EXPECT_EQ(cdrom_format(dev, 3200, 32, 0), 0);
    write(100, 32, kPacket, kDbtMode1, 7);
    EXPECT_EQ(dev->cdrom_capacity, 3199u);

    EXPECT_EQ(cdrom_format(dev, 6400, 32, 1), 0);
    EXPECT_EQ(dev->cdrom_capacity, 6399u);
    expectUserData(110, 16, 2048);
    EXPECT_NE(cdrom_format(dev, 3200, 32, 1), 0);

    write(6000, 32, kPacket, kDbtMode1, 7);
    reinsert();
    EXPECT_EQ(dev->cdrom_capacity, 6399u);
    expectUserData(6010, 16, 2048);
    expectUserData(110, 16, 2048);
}

TEST_F(BurnTest, ReservedTrackKeepsItsRoom)
{
    EXPECT_EQ(cdrom_reserve_track(dev, 500), 0);
    EXPECT_NE(cdrom_reserve_track(dev, 500), 0);
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_EQ(info().nwa, 500u + 152u);
    EXPECT_EQ(cdrom_reserve_track(dev, 0x60000), -1);
}

TEST_F(BurnTest, CdrCanNotBeBlanked)
{
    write(0, 10, kTao, kDbtMode1, 4);
    EXPECT_NE(cdrom_blank(dev, 0, 0), 0);
    EXPECT_NE(cdrom_format(dev, 0, 0, 0), 0);
}

TEST_F(BurnTest, CdRwBlanksBackToNew)
{
    for (const int type : { 0, 1 }) {
        SCOPED_TRACE(type);
        insertRewritable();
        EXPECT_EQ(wimage_get_blank_file_type(image.c_str()), CD_BLANK_CDRW);

        write(0, 100, kTao, kDbtMode1, 4);
        EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
        EXPECT_FALSE(cdrom_is_blank(dev));

        EXPECT_EQ(cdrom_blank(dev, type, 0), 0);
        EXPECT_TRUE(cdrom_is_blank(dev));
        EXPECT_EQ(dev->cdrom_capacity, 0u);
        EXPECT_TRUE(toc().empty());
        EXPECT_EQ(wimage_get_blank_file_type(image.c_str()), CD_BLANK_CDRW);

        /* And it is good for another round. */
        write(0, 20, kTao, kDbtMode1, 4);
        EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
        expectUserData(19, 16, 2048);
    }
}

TEST_F(BurnTest, CdRwComesBackRewritable)
{
    insertRewritable();
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    reinsert();

    EXPECT_EQ(dev->blank_media, CD_BLANK_CDRW);
    EXPECT_EQ(info().state, CD_WSTATE_CLOSED);
    expectUserData(50, 16, 2048);

    uint8_t b[36];
    cdrom_read_disc_information(dev, b);
    EXPECT_EQ(b[2], 0x1e);

    uint8_t atip[28];
    EXPECT_EQ(cdrom_read_atip(dev, atip, 28), 28);
    EXPECT_EQ(atip[6] & 0x40, 0x40);

    EXPECT_EQ(cdrom_blank(dev, 1, 0), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));

    reinsert();
    EXPECT_TRUE(cdrom_is_blank(dev));
    EXPECT_EQ(dev->blank_media, CD_BLANK_CDRW);
}

TEST_F(BurnTest, CdRwSessionsCanBeTakenBack)
{
    insertRewritable();
    write(0, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 1), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_SESSION), 0);

    const int nwa = info().nwa;
    write(nwa, 100, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, 2), 0);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
    EXPECT_EQ(info().state, CD_WSTATE_CLOSED);

    /* Unclose, and then add to the session. */
    EXPECT_EQ(cdrom_blank(dev, 5, 0), 0);
    EXPECT_EQ(info().state, CD_WSTATE_APPENDABLE);
    EXPECT_EQ(info().session_open, 1);
    EXPECT_EQ(static_cast<int>(info().nwa), nwa + 100 + 152);

    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);

    /* Erase it, the disc is back to where session 2 could be recorded. */
    EXPECT_EQ(cdrom_blank(dev, 6, 0), 0);
    EXPECT_EQ(static_cast<int>(info().nwa), nwa);
    EXPECT_EQ(info().sessions, 2);
    EXPECT_EQ(dev->cdrom_capacity, 99u);
    EXPECT_EQ(point(toc(), 2, 2), nullptr);

    /* Erasing the only session left is a blank disc. */
    EXPECT_EQ(cdrom_blank(dev, 6, 0), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));
}

TEST_F(BurnTest, FormattedCdRwIsOverwrittenInPlace)
{
    uint8_t cap[20];

    insertRewritable();
    EXPECT_EQ(cdrom_get_format_capacities(dev, cap), 20);
    EXPECT_EQ(cap[8], 0x01);
    EXPECT_EQ(cap[16], 0x10 << 2);

    EXPECT_EQ(cdrom_format(dev, 0, 32, 0), 0);
    EXPECT_EQ(info().fixed_packet, 32u);
    EXPECT_EQ(cdrom_get_format_capacities(dev, cap), 20);
    EXPECT_EQ(cap[8], 0x02);

    const uint32_t blocks = dev->cdrom_capacity + 1;
    EXPECT_EQ(blocks, (0x57da4u / 39u) * 32u);

    /* Whatever is not written yet is still a readable sector. */
    expectHeader(1000, 1);

    write(320, 32, kPacket, kDbtMode1, 7);
    write(64, 32, kPacket, kDbtMode1, 7);
    expectUserData(70, 16, 2048);

    /* Going over a packet is the point of formatting. */
    const auto replacement = payload(1234, 2048);
    EXPECT_EQ(cdrom_writesector(dev, replacement.data(), 70, kPacket, kDbtMode1, 7), 1);
    EXPECT_EQ(std::memcmp(read(70).data() + 16, replacement.data(), 2048), 0);

    EXPECT_NE(cdrom_writesector(dev, replacement.data(), blocks, kPacket, kDbtMode1, 7), 1);
    EXPECT_NE(cdrom_writesector(dev, replacement.data(), 70, kTao, kDbtMode1, 7), 1);

    reinsert();

    EXPECT_EQ(dev->blank_media, CD_BLANK_CDRW);
    EXPECT_EQ(info().fixed_packet, 32u);
    EXPECT_EQ(dev->cdrom_capacity + 1, blocks);
    EXPECT_EQ(std::memcmp(read(70).data() + 16, replacement.data(), 2048), 0);
    expectUserData(321, 16, 2048);
    expectHeader(1000, 1);

    const uint8_t  by_number[10] = { 0x52, 0x01, 0, 0, 0, 0x01 };
    uint8_t        b[36];
    EXPECT_EQ(cdrom_read_track_information(dev, by_number, b), 36);
    EXPECT_EQ(b[6] & 0x30, 0x30);
    EXPECT_EQ(b[23], 32);
    EXPECT_EQ(b[27], blocks & 0xff);

    /* Blanking takes the format away. */
    EXPECT_EQ(cdrom_blank(dev, 0, 0), 0);
    EXPECT_TRUE(cdrom_is_blank(dev));
    EXPECT_EQ(info().fixed_packet, 0u);
}

TEST_F(BurnTest, InterruptedBurnKeepsWhatWasWritten)
{
    write(0, 40, kTao, kDbtMode1, 4);
    reopen();
    EXPECT_EQ(dev->ops->get_last_block(dev->local), 39u);
    expectUserData(39, 16, 2048);
    EXPECT_FALSE(std::filesystem::exists(image + ".spool"));
}

TEST_F(BurnTest, UntouchedBlankDiscComesBack)
{
    unload();
    EXPECT_EQ(wimage_get_blank_file_type(image.c_str()), CD_BLANK_CDR);
    EXPECT_FALSE(std::filesystem::exists(image + ".spool"));
    EXPECT_EQ(wimage_get_blank_file_type((dir / "missing.aaruf").string().c_str()), CD_BLANK_NOT);
}

TEST_F(BurnTest, FinishedCdrStaysReadOnly)
{
    write(0, 10, kTao, kDbtMode1, 4);
    EXPECT_EQ(cdrom_write_to_toc(dev, CD_WTOC_CLOSE_DISC), 0);
    unload();
    EXPECT_FALSE(wimage_can_resume(image.c_str()));
}

TEST_F(BurnTest, FailedImageKeepsTheSpool)
{
    write(0, 10, kTao, kDbtMode1, 4);
    std::filesystem::create_directory(image + ".tmp");
    unload();
    EXPECT_TRUE(std::filesystem::exists(image + ".spool"));
    std::filesystem::remove(image + ".tmp");

    /* The next time in the drive picks it back up. */
    reinsert();
    expectUserData(9, 16, 2048);
    unload();
    EXPECT_FALSE(std::filesystem::exists(image + ".spool"));
    EXPECT_TRUE(cdrom_image_is_aaru(image.c_str()));
}
}

extern "C" {
void *log_open(const char *) { return nullptr; }
void log_close(void *) {}
void log_fatal(void *, const char *, ...) { std::abort(); }
void warning(const char *, ...) {}
void fatal(const char *, ...) { std::abort(); }
void pclog(const char *, ...) {}
char *plat_get_string(int) { static char text[] = "image error"; return text; }
char *nvr_path(char *path) { return path; }
FILE *plat_fopen(const char *path, const char *mode) { return std::fopen(path, mode); }
FILE *plat_fopen64(const char *path, const char *mode) { return std::fopen(path, mode); }
FILE *rom_fopen(const char *path, char *mode) { return std::fopen(path, mode); }
void plat_remove(char *path) { std::remove(path); }

void *
dynld_module(const char *name, dllimp_t *table)
{
    void *h = dlopen(name, RTLD_NOW);
    if (h == nullptr)
        return nullptr;
    for (dllimp_t *imp = table; imp->name != nullptr; imp++) {
        void *func = dlsym(h, imp->name);
        if (func == nullptr)
            return nullptr;
        *reinterpret_cast<void **>(imp->func) = func;
    }
    return h;
}
void dynld_close(void *h) { dlclose(h); }

void config_save(void) {}
void ui_sb_update_tip(int) {}
void plat_cdrom_ui_update(uint8_t, uint8_t) {}
void *ioctl_open(cdrom_t *, const char *) { return nullptr; }
void *image_open(cdrom_t *, const char *) { return nullptr; }
void *chd_image_open(cdrom_t *, const char *) { return nullptr; }
int cdrom_image_is_chd(const char *) { return 0; }
}
