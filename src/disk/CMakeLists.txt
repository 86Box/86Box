add_library(hdd OBJECT hdd.c hdd_image.c hdd_table.c hdc.c hdc_st506_xt.c
	hdc_st506_at.c hdc_xta.c hdc_esdi_at.c hdc_esdi_mca.c hdc_xtide.c
	hdc_ide.c hdc_ide_opti611.c hdc_ide_cmd640.c hdc_ide_sff8038i.c)

add_library(zip OBJECT zip.c)

add_library(mo OBJECT mo.c)

add_subdirectory(minivhd)
target_link_libraries(86Box minivhd)