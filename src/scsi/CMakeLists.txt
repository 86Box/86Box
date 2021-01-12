add_library(scsi OBJECT scsi.c scsi_device.c scsi_cdrom.c scsi_disk.c
	scsi_x54x.c scsi_aha154x.c scsi_buslogic.c scsi_ncr5380.c
	scsi_ncr53c8xx.c scsi_pcscsi.c scsi_spock.c)