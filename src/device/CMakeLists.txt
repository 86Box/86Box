add_library(dev OBJECT bugger.c hwm.c hwm_lm75.c hwm_lm78.c hwm_gl518sm.c
	hwm_vt82c686.c ibm_5161.c isamem.c isartc.c ../lpt.c pci_bridge.c
	postcard.c serial.c vpc2007.c clock_ics9xxx.c i2c.c i2c_gpio.c
	smbus_piix4.c keyboard.c keyboard_xt.c keyboard_at.c mouse.c mouse_bus.c
	mouse_serial.c mouse_ps2.c phoenix_486_jumper.c)

if(LASERXT)
	target_compile_definitions(dev PRIVATE USE_LASERXT)
endif()