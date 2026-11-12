/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Modem profile and identification response tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

extern "C" {
#include <86box/version.h>
#include <86box/modem/modem_voice.h>
#include <86box/modem/modem_common.h>
#include <86box/modem/modem_profile.h>
}

namespace {
std::filesystem::path nvr_dir;
int errors;
}

extern "C" FILE *
nvr_fopen(char *name, char *mode)
{
    return std::fopen((nvr_dir / name).string().c_str(), mode);
}

extern "C" void
pclog(const char *, ...)
{
    ++errors;
}

namespace {
struct Settings {
    int dcd = 1, dtr = 2, flow = 0, speaker = 1, volume = 2;
    int pulse = 0, response = 0;
    int result_mode = MODEM_DEFAULT_X, connect_report = MODEM_DEFAULT_W;
    bool echo = true, numeric = false;
    uint8_t reg[MODEM_REGS] = {};

    modem_profile_state_t state()
    {
        return { &dcd, &dtr, &flow, &speaker, &volume, &pulse, &response,
                 &echo, &numeric, reg, &result_mode, &connect_report };
    }
};

class ModemProfile : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        ASSERT_FALSE(error);
        for (int i = 0; i < 1024; ++i) {
            auto candidate = root / ("86box-modem-profile-test-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, error)) {
                nvr_dir = candidate;
                break;
            }
            ASSERT_TRUE(!error || error == std::errc::file_exists);
        }
        ASSERT_FALSE(nvr_dir.empty());
        errors = 0;
    }

    void TearDown() override
    {
        for (const auto &entry : std::filesystem::directory_iterator(nvr_dir))
            std::filesystem::remove(entry.path());
        std::filesystem::remove(nvr_dir);
        nvr_dir.clear();
    }
};

TEST_F(ModemProfile, RestoresSettingsButNotTheLiveRingCounter)
{
    Settings first;
    modem_profile_t profile = {};
    modem_profile_init(&profile, first.state(), "machine", "net", 1);
    EXPECT_EQ(errors, 0);
    first.dcd = 0; first.dtr = 3; first.flow = 3;
    first.speaker = 2; first.volume = 3; first.pulse = 1;
    first.response = 2; first.echo = false; first.numeric = true;
    first.result_mode = 1; first.connect_report = 2;
    first.reg[0] = 4; first.reg[1] = 9; first.reg[25] = 33;
    first.reg[MREG_INACTIVITY] = 12; first.reg[99] = 255;
    modem_profile_save(&profile, first.state());
    ASSERT_EQ(errors, 0);

    Settings restored;
    restored.reg[1] = 1;
    modem_profile_t reopened = {};
    modem_profile_init(&reopened, restored.state(), "machine", "net", 1);
    EXPECT_EQ(restored.dcd, 0);
    EXPECT_EQ(restored.dtr, 3);
    EXPECT_EQ(restored.flow, 3);
    EXPECT_EQ(restored.speaker, 2);
    EXPECT_EQ(restored.volume, 3);
    EXPECT_EQ(restored.pulse, 1);
    EXPECT_EQ(restored.response, 2);
    EXPECT_FALSE(restored.echo);
    EXPECT_TRUE(restored.numeric);
    EXPECT_EQ(restored.result_mode, 1);
    EXPECT_EQ(restored.connect_report, 2);
    EXPECT_EQ(restored.reg[0], 4);
    EXPECT_EQ(restored.reg[1], 1);
    EXPECT_EQ(restored.reg[25], 33);
    EXPECT_EQ(restored.reg[MREG_INACTIVITY], 12);
    EXPECT_EQ(restored.reg[99], 255);
    EXPECT_EQ(errors, 0);

    Settings defaults;
    modem_profile_save(&reopened, defaults.state());
    Settings reset;
    modem_profile_t after_reset = {};
    modem_profile_init(&after_reset, reset.state(), "machine", "net", 1);
    EXPECT_EQ(reset.dcd, 1);
    EXPECT_EQ(reset.speaker, 1);
    EXPECT_EQ(reset.reg[25], 0);
}

TEST_F(ModemProfile, MigratesLegacyProfilesWithoutDiscardingSettings)
{
    const auto path = nvr_dir / "machine_modem_1.nvr";
    Settings legacy;
    legacy.dcd = 0;
    legacy.reg[8] = 3;
    legacy.reg[MREG_CARRIER_WAIT] = 23;
    modem_profile_t original = {};
    modem_profile_init(&original, legacy.state(), "machine", "net", 1);
    legacy.speaker = 2;
    ASSERT_TRUE(modem_profile_write(&original, legacy.state()));
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(file);
        file.seekp(4);
        file.put(1);
    }
    std::filesystem::resize_file(path, 5 + 9 + MODEM_REGS);

    Settings restored;
    restored.reg[MREG_DIAL_WAIT] = MODEM_DEFAULT_S6;
    restored.reg[MREG_CARRIER_WAIT] = MODEM_DEFAULT_S7;
    restored.reg[MREG_CARRIER_LOSS] = MODEM_DEFAULT_S10;
    restored.reg[MREG_DTMF_DURATION] = MODEM_DEFAULT_S11;
    modem_profile_t migrated = {};
    modem_profile_init(&migrated, restored.state(), "machine", "net", 1);
    EXPECT_EQ(errors, 0);
    EXPECT_EQ(restored.dcd, 0);
    EXPECT_EQ(restored.speaker, 2);
    EXPECT_EQ(restored.reg[8], 3);
    EXPECT_EQ(restored.reg[MREG_DIAL_WAIT], MODEM_DEFAULT_S6);
    EXPECT_EQ(restored.reg[MREG_CARRIER_WAIT], 23);
    EXPECT_EQ(restored.reg[MREG_CARRIER_LOSS], MODEM_DEFAULT_S10);
    EXPECT_EQ(restored.reg[MREG_DTMF_DURATION], MODEM_DEFAULT_S11);
    EXPECT_EQ(restored.result_mode, MODEM_DEFAULT_X);
    EXPECT_EQ(restored.connect_report, MODEM_DEFAULT_W);
    ASSERT_TRUE(modem_profile_write(&migrated, restored.state()));
    EXPECT_EQ(std::filesystem::file_size(path), 5 + 11 + MODEM_REGS);
}

TEST_F(ModemProfile, SharesFrontendsButSeparatesInstancesAndMachines)
{
    Settings changed;
    modem_profile_t profile = {};
    modem_profile_init(&profile, changed.state(), "machine", "net", 1);
    changed.dcd = 0;
    modem_profile_save(&profile, changed.state());
    Settings shared;
    modem_profile_t char_profile = {};
    modem_profile_init(&char_profile, shared.state(), "machine", "char", 1);
    EXPECT_EQ(shared.dcd, 0);
    shared.speaker = 3;
    ASSERT_TRUE(modem_profile_write(&char_profile, shared.state()));
    Settings net;
    modem_profile_t net_profile = {};
    modem_profile_init(&net_profile, net.state(), "machine", "net", 1);
    EXPECT_EQ(net.speaker, 3);
    for (const auto &identity : { std::pair{"other", "char"}, {"other", "net"} }) {
        Settings fresh;
        modem_profile_t other = {};
        modem_profile_init(&other, fresh.state(), identity.first, identity.second, 1);
        EXPECT_EQ(fresh.dcd, 1);
    }
    Settings fresh;
    modem_profile_t other = {};
    modem_profile_init(&other, fresh.state(), "machine", "net", 2);
    EXPECT_EQ(fresh.dcd, 1);
    EXPECT_EQ(errors, 0);
}

TEST_F(ModemProfile, ImportsLegacyFrontendAndPrefersSharedFile)
{
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "net", 1);
    settings.dcd = 0;
    ASSERT_TRUE(modem_profile_write(&profile, settings.state()));
    const auto shared_path = nvr_dir / "machine_modem_1.nvr";
    const auto legacy_path = nvr_dir / "machine_net_modem_1.nvr";
    std::filesystem::rename(shared_path, legacy_path);

    Settings restored;
    modem_profile_t char_profile = {};
    modem_profile_init(&char_profile, restored.state(), "machine", "char", 1);
    EXPECT_EQ(restored.dcd, 0);
    ASSERT_TRUE(std::filesystem::exists(shared_path));
    EXPECT_TRUE(std::filesystem::exists(legacy_path));

    restored.dcd = 1;
    ASSERT_TRUE(modem_profile_write(&char_profile, restored.state()));
    Settings reopened;
    modem_profile_t net_profile = {};
    modem_profile_init(&net_profile, reopened.state(), "machine", "net", 1);
    EXPECT_EQ(reopened.dcd, 1);
    EXPECT_EQ(errors, 0);
}

TEST_F(ModemProfile, ImportsMatchingLegacyFrontendWhenBothExist)
{
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "net", 1);
    settings.dcd = 0;
    ASSERT_TRUE(modem_profile_write(&profile, settings.state()));
    const auto shared_path = nvr_dir / "machine_modem_1.nvr";
    std::filesystem::copy_file(shared_path, nvr_dir / "machine_net_modem_1.nvr");
    settings.dcd = 1;
    settings.speaker = 3;
    ASSERT_TRUE(modem_profile_write(&profile, settings.state()));
    std::filesystem::rename(shared_path, nvr_dir / "machine_char_modem_1.nvr");

    Settings restored;
    modem_profile_t char_profile = {};
    modem_profile_init(&char_profile, restored.state(), "machine", "char", 1);
    EXPECT_EQ(restored.dcd, 1);
    EXPECT_EQ(restored.speaker, 3);
    EXPECT_EQ(errors, 0);
}

TEST_F(ModemProfile, DoesNotImportCorruptLegacyProfile)
{
    const auto legacy_path = nvr_dir / "machine_net_modem_1.nvr";
    { std::ofstream file(legacy_path, std::ios::binary); file << "bad"; }
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "char", 1);
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(settings.dcd, 1);
    EXPECT_FALSE(std::filesystem::exists(nvr_dir / "machine_modem_1.nvr"));
    EXPECT_TRUE(std::filesystem::exists(legacy_path));
}

TEST_F(ModemProfile, ResetRestoresSavedSettingsAndFactoryRemainsSeparate)
{
    Settings settings;
    settings.reg[MREG_DTR_DELAY] = 5;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "net", 1);
    settings.dcd = 0;
    settings.speaker = 0;
    settings.volume = 3;
    settings.result_mode = 1;
    settings.connect_report = 2;
    settings.reg[MREG_DTR_DELAY] = 12;
    modem_profile_save(&profile, settings.state());
    ASSERT_EQ(errors, 0);

    settings.dcd = 1;
    settings.speaker = 2;
    settings.volume = 0;
    settings.result_mode = 4;
    settings.connect_report = 0;
    settings.reg[MREG_DTR_DELAY] = 99;
    settings.reg[MREG_RING_COUNT] = 4;
    modem_profile_restore(&profile, settings.state());
    EXPECT_EQ(settings.dcd, 0);
    EXPECT_EQ(settings.speaker, 0);
    EXPECT_EQ(settings.volume, 3);
    EXPECT_EQ(settings.result_mode, 1);
    EXPECT_EQ(settings.connect_report, 2);
    EXPECT_EQ(settings.reg[MREG_DTR_DELAY], 12);
    EXPECT_EQ(settings.reg[MREG_RING_COUNT], 4);

    modem_profile_factory(&profile, settings.state());
    EXPECT_EQ(settings.dcd, 1);
    EXPECT_EQ(settings.speaker, 1);
    EXPECT_EQ(settings.volume, 2);
    EXPECT_EQ(settings.result_mode, MODEM_DEFAULT_X);
    EXPECT_EQ(settings.connect_report, MODEM_DEFAULT_W);
    EXPECT_EQ(settings.reg[MREG_DTR_DELAY], 5);
    EXPECT_EQ(settings.reg[MREG_RING_COUNT], 4);
    modem_profile_restore(&profile, settings.state());
    EXPECT_EQ(settings.dcd, 0);
    EXPECT_EQ(settings.speaker, 0);
    EXPECT_EQ(settings.volume, 3);
    modem_profile_factory(&profile, settings.state());
    modem_profile_save(&profile, settings.state());
    ASSERT_EQ(errors, 0);
    Settings reopened;
    modem_profile_t reopened_profile = {};
    modem_profile_init(&reopened_profile, reopened.state(), "machine", "net", 1);
    EXPECT_EQ(reopened.dcd, 1);
    EXPECT_EQ(reopened.speaker, 1);
    EXPECT_EQ(reopened.volume, 2);
    EXPECT_EQ(reopened.reg[MREG_DTR_DELAY], 5);
}

TEST_F(ModemProfile, RejectsCorruptProfilesAndReportsSaveErrors)
{
    const auto path = nvr_dir / "machine_modem_1.nvr";
    { std::ofstream file(path, std::ios::binary); file << "bad"; }
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "net", 1);
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(settings.dcd, 1);
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    settings.dcd = 0;
    modem_profile_save(&profile, settings.state());
    EXPECT_EQ(errors, 2);
    std::filesystem::remove(path);
    modem_profile_save(&profile, settings.state());
    EXPECT_EQ(errors, 2);
    Settings restored;
    modem_profile_t reopened = {};
    modem_profile_init(&reopened, restored.state(), "machine", "net", 1);
    EXPECT_EQ(restored.dcd, 0);
}

TEST_F(ModemProfile, RejectsOutOfRangeStoredSettings)
{
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "char", 2);
    settings.dcd = 0;
    modem_profile_save(&profile, settings.state());
    ASSERT_EQ(errors, 0);
    const auto path = nvr_dir / "machine_modem_2.nvr";
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(file);
        file.seekp(5);
        file.put(2);
    }
    Settings restored;
    modem_profile_t reopened = {};
    modem_profile_init(&reopened, restored.state(), "machine", "char", 2);
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(restored.dcd, 1);
}

TEST_F(ModemProfile, ExplicitWriteRecreatesProfileAndReportsErrors)
{
    Settings settings;
    modem_profile_t profile = {};
    modem_profile_init(&profile, settings.state(), "machine", "net", 1);
    const auto path = nvr_dir / "machine_modem_1.nvr";
    EXPECT_TRUE(modem_profile_write(&profile, settings.state()));
    ASSERT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
    EXPECT_TRUE(modem_profile_write(&profile, settings.state()));
    ASSERT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    EXPECT_FALSE(modem_profile_write(&profile, settings.state()));
    EXPECT_EQ(errors, 1);
}

TEST(ModemResponses, IdentificationAndServiceClasses)
{
    EXPECT_STREQ(modem_ati_identification(0), "86Box Emulated Modem");
    EXPECT_EQ(modem_ati_identification(1), nullptr);
    EXPECT_EQ(modem_ati_identification(2), nullptr);
    EXPECT_STREQ(modem_ati_identification(3), "86Box Emulated Modem Firmware V1.00");
    EXPECT_STREQ(modem_ati_identification(4), "Modem compiled for 86Box version " EMU_VERSION);
    EXPECT_EQ(modem_ati_identification(5), nullptr);
    EXPECT_EQ(modem_ati_identification(9), nullptr);
    EXPECT_EQ(modem_ati_identification(UINT32_MAX), nullptr);
    EXPECT_EQ(modem_result_for_mode(ResBUSY, 2), ResNOCARRIER);
    EXPECT_EQ(modem_result_for_mode(ResBUSY, 3), ResBUSY);
    EXPECT_EQ(modem_result_for_mode(ResNODIALTONE, 2), ResNODIALTONE);
    EXPECT_EQ(modem_result_for_mode(ResNODIALTONE, 3), ResNOCARRIER);
    EXPECT_EQ(modem_connect_result_code(9600), 12u);
    EXPECT_STREQ(modem_identification("GMI"), "86Box");
    EXPECT_STREQ(modem_identification("GMM"), "86Box Emulated Modem");
    EXPECT_NE(modem_identification("GMR"), nullptr);
    EXPECT_EQ(modem_identification("GMI?"), nullptr);
    uint8_t fax = 0, voice = 0;
    bool v253 = false;
    int media = -1, rate = 0;
    char response[32];
    auto command = [&](const char *text, uint8_t support) {
        return modem_fclass_command(text, support, &fax, &voice, &v253, &media, &rate,
                                    response, sizeof(response));
    };
    EXPECT_EQ(command("FCLASS=?", 0), -1);
    EXPECT_EQ(command("FCLASS?", 0), -1);
    EXPECT_EQ(command("FCLASS=0", 0), -1);
    EXPECT_EQ(command("FCLASS=?", MODEM_FAX_SUPPORT_CLASS_0), 1);
    EXPECT_STREQ(response, "0");
    EXPECT_EQ(command("FCLASS=?", MODEM_FAX_SUPPORT_CLASS_1), 1);
    EXPECT_STREQ(response, "0,1.0");
    EXPECT_EQ(command("FCLASS=?", MODEM_FAX_SUPPORT_DEFAULT), 1);
    EXPECT_STREQ(response, "0,1.0");
    EXPECT_EQ(command("FCLASS=1", MODEM_FAX_SUPPORT_DEFAULT), 1);
    EXPECT_EQ(fax, 1);
    EXPECT_EQ(command("FCLASS=8", MODEM_FAX_SUPPORT_DEFAULT), -1);
    EXPECT_EQ(command("FCLASS=?", MODEM_FAX_SUPPORT_CLASS_8), 1);
    EXPECT_STREQ(response, "0,8");
    constexpr uint8_t all = MODEM_FAX_SUPPORT_CLASS_1 | MODEM_FAX_SUPPORT_CLASS_8;
    EXPECT_EQ(command("FCLASS=?", all), 1);
    EXPECT_STREQ(response, "0,1.0,8");
    EXPECT_EQ(command("FCLASS=1", MODEM_FAX_SUPPORT_CLASS_8), -1);
    EXPECT_EQ(command("FCLASS=1.0", all), 1);
    EXPECT_EQ(fax, 1);
    EXPECT_EQ(command("FCLASS?", all), 1);
    EXPECT_STREQ(response, "1.0");
    EXPECT_EQ(command("FCLASS=8", MODEM_FAX_SUPPORT_CLASS_1), -1);
    EXPECT_EQ(command("FCLASS=8.0", all), 1);
    EXPECT_EQ(fax, 8);
    EXPECT_EQ(voice, 8);
    EXPECT_TRUE(v253);
    EXPECT_EQ(media, VOICE_FRAME_AUDIO);
    EXPECT_EQ(rate, 8000);
    EXPECT_EQ(command("FCLASS?", all), 1);
    EXPECT_STREQ(response, "8");
    EXPECT_EQ(command("FCLASS=0.0", all), 1);
    EXPECT_EQ(fax, 0);
    EXPECT_EQ(voice, 0);
    EXPECT_EQ(command("FCLASS?", all), 1);
    EXPECT_STREQ(response, "0");
    EXPECT_EQ(command("FCLASS=2", all), -1);
    EXPECT_EQ(command("FCLASS=1.0X", all), -1);
    EXPECT_EQ(command("OTHER", all), 0);
}

TEST(ModemResponses, NominalBaudRatesAllowClockVariation)
{
    const std::pair<uint32_t, uint32_t> examples[] = {
        { 1201, 1200 }, { 2404, 2400 }, { 4808, 4800 }, { 7212, 7200 },
        { 9408, 9600 }, { 9615, 9600 }, { 9792, 9600 },
        { 19230, 19200 }, { 38461, 38400 }, { 38462, 38400 },
        { 57692, 57600 }, { 115384, 115200 },
        { 56000, 56000 }, { 56700, 56000 }, { 56800, 56000 },
        { 56900, 57600 }, { 57600, 57600 },
        { 0, 0 }, { 9407, 9407 }, { 9793, 9793 }, { 12345, 12345 },
        { UINT32_MAX, UINT32_MAX }
    };
    for (const auto &[actual, expected] : examples)
        EXPECT_EQ(modem_nominal_baudrate(actual), expected) << actual;
    EXPECT_EQ(modem_connect_result_code(9615), 12u);
    EXPECT_EQ(modem_connect_result_code(38461), 17u);
    EXPECT_EQ(modem_connect_result_code(12345), 1u);
}
}
