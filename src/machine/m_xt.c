#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/port_6x.h>

extern const device_t vendex_xt_rtc_onboard_device;

static void
machine_xt_common_init(const machine_t *model)
{
    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    if (fdc_type == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    nmi_init();
    standalone_gameport_type = &gameport_device;
}

int
machine_pc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibmpc/BIOS_5150_24APR81_U33.BIN",
                           0x000fe000, 40960, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U29 - 5700019.bin",
                             0x000f6000, 8192, 0);
        bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U30 - 5700027.bin",
                             0x000f8000, 8192, 0);
        bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U31 - 5700035.bin",
                             0x000fa000, 8192, 0);
        bios_load_aux_linear("roms/machines/ibmpc/IBM 5150 - Cassette BASIC version C1.00 - U32 - 5700043.bin",
                             0x000fc000, 8192, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_pc82_init(const machine_t *model)
{
    int ret;
    int ret2;

    ret = bios_load_linear("roms/machines/ibmpc82/pc102782.bin",
                           0x000fe000, 40960, 0);
    if (ret) {
        ret2 = bios_load_aux_linear("roms/machines/ibmpc82/ibm-basic-1.10.rom",
                                    0x000f6000, 32768, 0);
        if (!ret2) {
            bios_load_aux_linear("roms/machines/ibmpc82/basicc11.f6",
                                 0x000f6000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/basicc11.f8",
                                 0x000f8000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/basicc11.fa",
                                 0x000fa000, 8192, 0);
            bios_load_aux_linear("roms/machines/ibmpc82/basicc11.fc",
                                 0x000fc000, 8192, 0);
        }
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);
    device_add(&ibm_5161_device);

    machine_xt_common_init(model);

    return ret;
}

static void
machine_xt_init_ex(const machine_t *model)
{
    device_add(&keyboard_xt_device);

    machine_xt_common_init(model);
}

int
machine_xt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibmxt/xt.rom",
                           0x000f0000, 65536, 0);
    if (!ret) {
        ret = bios_load_linear("roms/machines/ibmxt/1501512.u18",
                               0x000fe000, 65536, 0x6000);
        if (ret) {
            bios_load_aux_linear("roms/machines/ibmxt/1501512.u18",
                                 0x000f8000, 24576, 0);
            bios_load_aux_linear("roms/machines/ibmxt/5000027.u19",
                                 0x000f0000, 32768, 0);
        }
    }

    if (bios_only || !ret)
        return ret;

    machine_xt_init_ex(model);

    device_add(&ibm_5161_device);

    return ret;
}

int
machine_genxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/genxt/pcxt.rom",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_init_ex(model);

    return ret;
}

int
machine_xt86_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibmxt86/BIOS_5160_09MAY86_U18_59X7268_62X0890_27256_F800.BIN",
                           0x000fe000, 65536, 0x6000);
    if (ret) {
        (void) bios_load_aux_linear("roms/machines/ibmxt86/BIOS_5160_09MAY86_U18_59X7268_62X0890_27256_F800.BIN",
                                    0x000f8000, 24576, 0);
        (void) bios_load_aux_linear("roms/machines/ibmxt86/BIOS_5160_09MAY86_U19_62X0819_68X4370_27256_F000.BIN",
                                    0x000f0000, 32768, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt86_device);
    device_add(&ibm_5161_device);

    machine_xt_common_init(model);

    return ret;
}

static void
machine_xt_clone_init(const machine_t *model)
{
    device_add(&keyboard_xtclone_device);

    machine_xt_common_init(model);
}

int
machine_xt_americxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/americxt/AMERICXT.ROM",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_amixt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/amixt/ami_8088_bios_31jan89.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_znic_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/znic/ibmzen.rom",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_dtk_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk/dtk_erso_2.42_2764.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_jukopc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/jukopc/000o001.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_openxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/openxt/pcxt31.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_pcxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pcxt/u18.rom",
                           0x000f8000, 65536, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/pcxt/u19.rom",
                             0x000f0000, 32768, 0);
    }

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_pxxt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pxxt/000p001.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_xt_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_iskra3104_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/iskra3104/198.bin",
                                "roms/machines/iskra3104/199.bin",
                                0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_pravetz16_imko4_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pravetz16/BIOS_IMKO4_FE00.bin",
                           0x000fe000, 65536, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F400.BIN",
                             0x000f4000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F600.BIN",
                             0x000f6000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_FA00.BIN",
                             0x000fa000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_F800.BIN",
                             0x000f8000, 8192, 0);

        bios_load_aux_linear("roms/machines/pravetz16/BIOS_IMKO4_FC00.BIN",
                             0x000fc000, 8192, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pravetz_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_micoms_xl7turbo_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mxl7t/XL7_TURBO.BIN",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_init_ex(model);
    return ret;
}

int
machine_xt_pc4i_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc4i/NCR_PC4i_BIOSROM_1985.BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_mpc1600_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/mpc1600/mpc4.34_merged.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_pcspirit_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pcspirit/u1101.bin",
                           0x000fe000, 16384, 0);

    if (ret) {
        bios_load_aux_linear("roms/machines/pcspirit/u1103.bin",
                             0x000fc000, 8192, 0);
    }

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc82_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_pc700_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc700/multitech pc-700 3.1.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_pc500_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pc500/rom404.bin",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    device_add(&keyboard_pc_device);

    machine_xt_common_init(model);

    return ret;
}

int
machine_xt_vendex_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vendex/Vendex Turbo 888 XT - ROM BIOS - VER 2.03C.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);
    device_add(&vendex_xt_rtc_onboard_device);

    return ret;
}

static void
machine_xt_hyundai_common_init(const machine_t *model)
{
    device_add(&keyboard_xt_hyundai_device);

    machine_xt_common_init(model);
}

int
machine_xt_super16t_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super16t/Hyundai SUPER-16T - System BIOS HEA v1.12Ta (16k)(MBM27128)(1986).BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_hyundai_common_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    return ret;
}

int
machine_xt_super16te_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/super16te/Hyundai SUPER-16TE - System BIOS v2.00Id (16k)(D27128A)(1989).BIN",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_hyundai_common_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    return ret;
}

int
machine_xt_top88_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/top88/Hyosung Topstar 88T - BIOS version 3.0.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    return ret;
}

int
machine_xt_kaypropc_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/kaypropc/Kaypro_v2.03K.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_sansx16_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/sansx16/tmm27128ad.bin.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    /* On-board FDC cannot be disabled */
    device_add(&fdc_xt_device);

    return ret;
}

int
machine_xt_bw230_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/bw230/bondwell.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_v20xt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/v20xt/V20XTBios.bin",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_pb8810_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb8810/pb8088-8810-633acc631aba0345517682.bin",
                           0x000fc000, 16384, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_clone_init(model);

    return ret;
}

int
machine_xt_glabios_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/glabios/GLABIOS_0.2.6_8X_012324.ROM",
                           0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    machine_xt_init_ex(model);

    return ret;
}
