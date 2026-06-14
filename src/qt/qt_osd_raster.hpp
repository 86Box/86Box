/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          GL-free ImGui rasterizer for the Qt software renderer.
 */
#ifndef QT_QT_OSD_RASTER_HPP
#define QT_QT_OSD_RASTER_HPP

struct ImDrawData;
class QImage;

void osd_raster_render(ImDrawData *draw_data, QImage &target, float scale);

#endif /* QT_QT_OSD_RASTER_HPP */
