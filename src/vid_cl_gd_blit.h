static uint8_t rop_to_index[256];

#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

void cirrus_bitblt_cputovideo_next(clgd_t *clgd, svga_t *svga);
void cirrus_bitblt_reset(clgd_t *clgd, svga_t *svga);
void cirrus_bitblt_start(clgd_t *clgd, svga_t *svga);
void cirrus_write_bitblt(clgd_t *clgd, svga_t *svga, uint8_t reg_value);
void init_rops();