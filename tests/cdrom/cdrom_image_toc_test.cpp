/* cdrdao TOC loading, checked against generated images whose sectors carry
   their own position in the file. The platform stubs are shared with
   cdrom_mcn_test.cpp, which is linked into the same test binary. */
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
extern "C" {
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
}

namespace {
constexpr uint32_t kPregap = 150;

class TocTest : public ::testing::Test {
protected:
    cdrom_t               dev{};
    std::filesystem::path dir;

    void SetUp() override
    {
        dir = std::filesystem::temp_directory_path() /
              ("86box-toc-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(dir);
        dev.bus_type = CDROM_BUS_ATAPI;
    }

    void TearDown() override
    {
        close();
        std::filesystem::remove_all(dir);
    }

    void close()
    {
        if (dev.local)
            dev.ops->close(dev.local);
        dev.local = nullptr;
        dev.ops   = nullptr;
    }

    /* Each sector is filled with its little-endian index in the file. */
    void write_image(const std::string &name, uint32_t sector_size, uint32_t sectors)
    {
        std::ofstream            out(dir / name, std::ios::binary);
        std::vector<uint8_t>     sector(sector_size);
        for (uint32_t s = 0; s < sectors; s++) {
            for (uint32_t i = 0; i < sector_size; i += 4) {
                sector[i]     = s & 0xff;
                sector[i + 1] = (s >> 8) & 0xff;
                sector[i + 2] = (s >> 16) & 0xff;
                sector[i + 3] = (s >> 24) & 0xff;
            }
            out.write(reinterpret_cast<const char *>(sector.data()), sector_size);
        }
    }

    bool open(const std::string &toc)
    {
        close();
        {
            std::ofstream out(dir / "disc.toc");
            out << toc;
        }
        dev.local = image_open(&dev, (dir / "disc.toc").string().c_str());
        return dev.local != nullptr;
    }

    /* Absolute frame (LBA + 150) of a track start, or of its end. */
    uint32_t track_pos(int track, bool end = false)
    {
        track_info_t ti{};
        EXPECT_TRUE(dev.ops->get_track_info(dev.local, track, end, &ti));
        return (ti.m * 60 + ti.s) * 75 + ti.f;
    }

    /* Point A2h holds the lead-out start. */
    uint32_t lead_out() { return track_pos(0xa2); }

    /* File sector that the image returns for a disc LBA. Cooked data is
       placed after the 16-byte sync and header, audio at the start. */
    uint32_t file_sector(uint32_t lba, bool audio = false)
    {
        std::array<uint8_t, 2448> buf{};
        EXPECT_GT(dev.ops->read_sector(dev.local, buf.data(), lba), 0) << "LBA " << lba;
        const uint8_t *p = buf.data() + (audio ? 0 : 16);
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
};

TEST_F(TocTest, DataFileMsfLengthDoesNotShiftTrackStart)
{
    write_image("data.bin", 2048, 300);
    ASSERT_TRUE(open("CD_ROM\n\nTRACK MODE1\nDATAFILE \"data.bin\" 00:04:00 // 300 frames\n"));
    EXPECT_EQ(track_pos(1), kPregap);
    EXPECT_EQ(lead_out(), kPregap + 300);
    EXPECT_EQ(file_sector(0), 0u);
    EXPECT_EQ(file_sector(299), 299u);
}

TEST_F(TocTest, DataFileByteLengthDoesNotShiftTrackStart)
{
    write_image("data.bin", 2048, 300);
    ASSERT_TRUE(open("CD_ROM\nTRACK MODE1\nDATAFILE \"data.bin\" 614400\n"));
    EXPECT_EQ(track_pos(1), kPregap);
    EXPECT_EQ(lead_out(), kPregap + 300);
    EXPECT_EQ(file_sector(0), 0u);
}

TEST_F(TocTest, DataFileWithoutLength)
{
    write_image("data.bin", 2048, 300);
    ASSERT_TRUE(open("CD_ROM\r\nTRACK MODE1\r\nDATAFILE \"data.bin\"\r\n"));
    EXPECT_EQ(track_pos(1), kPregap);
    EXPECT_EQ(lead_out(), kPregap + 300);
    EXPECT_EQ(file_sector(0), 0u);
}

TEST_F(TocTest, ByteOffsetsSplitOneFileIntoTracks)
{
    write_image("data.bin", 2048, 300);
    ASSERT_TRUE(open("CD_ROM\n"
                     "TRACK MODE1\n"
                     "DATAFILE \"data.bin\" #0 00:01:25\n"
                     "TRACK MODE1\n"
                     "DATAFILE \"data.bin\" #204800 409600\n"));
    EXPECT_EQ(track_pos(1), kPregap);
    EXPECT_EQ(track_pos(2), kPregap + 100);
    EXPECT_EQ(lead_out(), kPregap + 300);
    EXPECT_EQ(file_sector(99), 99u);
    EXPECT_EQ(file_sector(100), 100u);
    EXPECT_EQ(file_sector(299), 299u);
}

TEST_F(TocTest, ByteOffsetAppliesToSingleTrack)
{
    write_image("data.bin", 2048, 300);
    ASSERT_TRUE(open("CD_ROM\nTRACK MODE1\nDATAFILE \"data.bin\" #20480\n"));
    EXPECT_EQ(lead_out(), kPregap + 290);
    EXPECT_EQ(file_sector(0), 10u);
}

TEST_F(TocTest, UnalignedByteOffsetIsRejected)
{
    write_image("data.bin", 2048, 300);
    EXPECT_FALSE(open("CD_ROM\nTRACK MODE1\nDATAFILE \"data.bin\" #1000\n"));
    EXPECT_FALSE(open("CD_ROM\nTRACK MODE1\nDATAFILE \"data.bin\" #12x\n"));
}

TEST_F(TocTest, AudioFileStartCountsSamples)
{
    write_image("audio.bin", 2352, 100);
    /* 1176 samples of 4 bytes are two 2352-byte frames. */
    for (const auto *start : { "1176", "00:00:02" }) {
        ASSERT_TRUE(open(std::string("CD_DA\nTRACK AUDIO\nFILE \"audio.bin\" ") + start +
                         " 00:00:50\n"))
            << start;
        EXPECT_EQ(track_pos(1), kPregap) << start;
        EXPECT_EQ(file_sector(0, true), 2u) << start;
    }
}

TEST_F(TocTest, AudioFileByteOffsetAndStartAdd)
{
    write_image("audio.bin", 2352, 100);
    ASSERT_TRUE(open("CD_DA\nTRACK AUDIO\nFILE \"audio.bin\" #23520 00:00:05\n"));
    EXPECT_EQ(file_sector(0, true), 15u);
}

TEST_F(TocTest, DataTrackFollowedByAudioTrackInOtherFile)
{
    write_image("data.bin", 2048, 300);
    write_image("audio.bin", 2352, 100);
    ASSERT_TRUE(open("CD_ROM\n"
                     "TRACK MODE1\n"
                     "DATAFILE \"data.bin\" 00:04:00\n"
                     "TRACK AUDIO\n"
                     "FILE \"audio.bin\" 0 00:01:25\n"));
    EXPECT_EQ(track_pos(1), kPregap);
    EXPECT_EQ(file_sector(0), 0u);
    EXPECT_EQ(file_sector(299), 299u);
    const uint32_t audio = track_pos(2) - kPregap;
    EXPECT_GE(audio, 300u);
    EXPECT_EQ(file_sector(audio, true), 0u);
    EXPECT_EQ(lead_out(), track_pos(2) + 100);
}
}
