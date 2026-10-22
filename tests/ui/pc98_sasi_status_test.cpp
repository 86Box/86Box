/* Use the production status updater to check independent SASI, other HDD,
 * and network activity. No VM or window needs to be opened. */
#include "../../src/qt/qt_ui.cpp"
#include <cstdio>

extern "C" {
machine_status_t machine_status;
}
static unsigned checks, failures;
#define CHECK(e)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(e)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
            failures++;                                                  \
        }                                                                \
    } while (0)

int
main()
{
    /* Both the C status array and Qt's HDD icon array use this bound. */
    static_assert(HDD_BUS_SASI < HDD_BUS_MAX, "SASI icon must fit in the HDD arrays");
    CHECK(SB_SASI != SB_NETWORK);
    CHECK(SB_SASI != (SB_HDD | HDD_BUS_MFM));
    ui_sb_update_icon(SB_SASI, 1);
    ui_sb_update_icon_write(SB_SASI, 1);
    CHECK(machine_status.hdd[HDD_BUS_SASI].active);
    CHECK(machine_status.hdd[HDD_BUS_SASI].write_active);
    for (int i = 0; i < HDD_BUS_MAX; ++i) {
        if (i != HDD_BUS_SASI) {
            CHECK(!machine_status.hdd[i].active);
            CHECK(!machine_status.hdd[i].write_active);
        }
    }
    for (int i = 0; i < NET_CARD_MAX; ++i) {
        CHECK(!machine_status.net[i].active);
        CHECK(!machine_status.net[i].write_active);
    }
    ui_sb_update_icon(SB_NETWORK, 1);
    ui_sb_update_icon_write(SB_NETWORK, 1);
    ui_sb_update_icon(SB_HDD | HDD_BUS_IDE, 1);
    ui_sb_update_icon_write(SB_HDD | HDD_BUS_IDE, 1);
    ui_sb_update_icon(SB_SASI, 0);
    ui_sb_update_icon_write(SB_SASI, 0);
    CHECK(!machine_status.hdd[HDD_BUS_SASI].active);
    CHECK(!machine_status.hdd[HDD_BUS_SASI].write_active);
    CHECK(machine_status.net[0].active && machine_status.net[0].write_active);
    CHECK(machine_status.hdd[HDD_BUS_IDE].active && machine_status.hdd[HDD_BUS_IDE].write_active);
    ui_sb_update_icon(SB_SASI, 1);
    ui_sb_update_icon_write(SB_SASI, 1);
    ui_sb_update_icon(SB_NETWORK, 0);
    ui_sb_update_icon_write(SB_NETWORK, 0);
    ui_sb_update_icon(SB_HDD | HDD_BUS_IDE, 0);
    ui_sb_update_icon_write(SB_HDD | HDD_BUS_IDE, 0);
    CHECK(machine_status.hdd[HDD_BUS_SASI].active);
    CHECK(machine_status.hdd[HDD_BUS_SASI].write_active);
    CHECK(!machine_status.net[0].active && !machine_status.net[0].write_active);
    CHECK(!machine_status.hdd[HDD_BUS_IDE].active && !machine_status.hdd[HDD_BUS_IDE].write_active);
    std::printf("SASI status isolation: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
