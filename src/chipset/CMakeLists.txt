add_library(chipset OBJECT acc2168.c cs8230.c ali1429.c headland.c intel_82335.c
	cs4031.c intel_420ex.c intel_4x0.c intel_sio.c intel_piix.c ../ioapic.c
	neat.c opti495.c opti895.c opti5x7.c scamp.c scat.c via_vt82c49x.c
	via_vt82c505.c sis_85c310.c sis_85c4xx.c sis_85c496.c sis_85c50x.c
	opti283.c opti291.c umc491.c via_apollo.c via_pipc.c wd76c10.c
	vl82c480.c)

if(STPC)
	target_sources(chipset PRIVATE stpc.c)
endif()

if(M1489)
	target_sources(chipset PRIVATE ali1489.c)
endif()

if(M6117)
	target_sources(chipset PRIVATE ali6117.c)
endif()