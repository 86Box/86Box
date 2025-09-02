#ifndef EMU_PRT_DEVS_H
#define EMU_PRT_DEVS_H

extern const lpt_device_t lpt_prt_text_device;
extern const device_t prt_text_device;
extern const lpt_device_t lpt_prt_escp_device;
extern const device_t prt_escp_device;
extern const lpt_device_t lpt_prt_ps_device;
extern const device_t prt_ps_device;
#ifdef USE_PCL
extern const lpt_device_t lpt_prt_pcl_device;
extern const device_t prt_pcl_device;
#endif

#endif /*EMU_PRT_DEVS_H*/
