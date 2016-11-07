typedef struct mpu401_uart_t
{
        uint8_t status;
        uint8_t rx_data;

        int uart_mode;        
} mpu401_uart_t;

void mpu401_uart_init(mpu401_uart_t *mpu, uint16_t addr);
