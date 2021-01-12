add_library(sio OBJECT sio_acc3221.c sio_f82c710.c sio_82091aa.c sio_fdc37c661.c
	sio_fdc37c66x.c sio_fdc37c669.c sio_fdc37c93x.c sio_fdc37m60x.c
	sio_pc87306.c sio_pc87307.c sio_pc87309.c sio_pc87311.c sio_pc87332.c
	sio_prime3c.c sio_w83787f.c sio_w83877f.c sio_w83977f.c sio_um8669f.c
	sio_vt82c686.c)

if(SIO_DETECT)
	target_sources(sio PRIVATE sio_detect.c)
endif()