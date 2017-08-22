uint8_t *scsi_device_sense(uint8_t scsi_id, uint8_t scsi_lun);
void scsi_device_type_data(uint8_t scsi_id, uint8_t scsi_lun, uint8_t *type, uint8_t *rmb);
int scsi_device_read_capacity(uint8_t scsi_id, uint8_t scsi_lun, uint8_t *cdb, uint8_t *buffer, uint32_t *len);
int scsi_device_present(uint8_t scsi_id, uint8_t scsi_lun);
int scsi_device_valid(uint8_t scsi_id, uint8_t scsi_lun);
int scsi_device_cdb_length(uint8_t scsi_id, uint8_t scsi_lun);
int scsi_device_block_shift(uint8_t scsi_id, uint8_t scsi_lun);
void scsi_device_command(int cdb_len, uint8_t scsi_id, uint8_t scsi_lun, uint8_t *cdb);
