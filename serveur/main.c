/****************************************************************************
 *   apps/rf_sub1G/simple/main.c
 *
 * sub1G_module support code - USB version
 *
 * Copyright 2013-2014 Nathael Pajani <nathael.pajani@ed3l.fr>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *************************************************************************** */

#include "core/system.h"
#include "core/systick.h"
#include "core/pio.h"
#include "lib/stdio.h"
#include "drivers/serial.h"
#include "drivers/gpio.h"
#include "drivers/ssp.h"
#include "extdrv/cc1101.h"
#include "extdrv/status_led.h"
#include "drivers/i2c.h"

#include "extdrv/bme280_humidity_sensor.h"
#include "extdrv/veml6070_uv_sensor.h"
#include "extdrv/tsl256x_light_sensor.h"
#include "extdrv/ssd130x_oled_driver.h"
#include "extdrv/ssd130x_oled_buffer.h"
#include "lib/font.h"

#define MODULE_VERSION	0x03
#define MODULE_NAME "RF Sub1G - USB"

#define RF_868MHz  1
#define RF_915MHz  0
#if ((RF_868MHz) + (RF_915MHz) != 1)
#error Either RF_868MHz or RF_915MHz MUST be defined.
#endif

//#define DEBUG 1
#define BUFF_LEN 60
#define RF_BUFF_LEN  64
#define UART_BUFF_LEN  3

#define SELECTED_FREQ  FREQ_SEL_48MHz
#define DEVICE_ADDRESS  0x13 /* Addresses 0x00 and 0xFF are broadcast */
#define NEIGHBOR_ADDRESS 0x02 /* Address of the associated device */
/***************************************************************************** */
/* Pins configuration */
/* pins blocks are passed to set_pins() for pins configuration.
 * Unused pin blocks can be removed safely with the corresponding set_pins() call
 * All pins blocks may be safelly merged in a single block for single set_pins() call..
 */
const struct pio_config common_pins[] = {
	/* UART 0 */
	{ LPC_UART0_RX_PIO_0_1,  LPC_IO_DIGITAL },
	{ LPC_UART0_TX_PIO_0_2,  LPC_IO_DIGITAL },
	/* SPI */
	{ LPC_SSP0_SCLK_PIO_0_14, LPC_IO_DIGITAL },
	{ LPC_SSP0_MOSI_PIO_0_17, LPC_IO_DIGITAL },
	{ LPC_SSP0_MISO_PIO_0_16, LPC_IO_DIGITAL },
	/* I2C 0 */
	{ LPC_I2C0_SCL_PIO_0_10, (LPC_IO_DIGITAL | LPC_IO_OPEN_DRAIN_ENABLE) },
	{ LPC_I2C0_SDA_PIO_0_11, (LPC_IO_DIGITAL | LPC_IO_OPEN_DRAIN_ENABLE) },
	ARRAY_LAST_PIO,
};

const struct pio cc1101_cs_pin = LPC_GPIO_0_15;
const struct pio cc1101_miso_pin = LPC_SSP0_MISO_PIO_0_16;
const struct pio cc1101_gdo0 = LPC_GPIO_0_6;
const struct pio cc1101_gdo2 = LPC_GPIO_0_7;

const struct pio status_led_green = LPC_GPIO_0_28;
const struct pio status_led_red = LPC_GPIO_0_29;

const struct pio button = LPC_GPIO_0_12; /* ISP button */

char format[3] = "THL";

// Réception
struct message 
{
	uint32_t temp;
	uint16_t hum;
	uint32_t lum;
};
typedef struct message message;

// Envoi
struct messageFormat 
{
	char format[3];
	uint32_t temp;
	uint16_t hum;
	uint32_t lum;
};
typedef struct messageFormat messageFormat;


/***************************************************************************** */
void system_init()
{
	/* Stop the watchdog */
	startup_watchdog_disable(); /* Do it right now, before it gets a chance to break in */
	system_set_default_power_state();
	clock_config(SELECTED_FREQ);
	set_pins(common_pins);
	gpio_on();
	/* System tick timer MUST be configured and running in order to use the sleeping
	 * functions */
	systick_timer_on(1); /* 1ms */
	systick_start();
}

/* Define our fault handler. This one is not mandatory, the dummy fault handler
 * will be used when it's not overridden here.
 * Note : The default one does a simple infinite loop. If the watchdog is deactivated
 * the system will hang.
 */
void fault_info(const char* name, uint32_t len)
{
	uprintf(UART0, name);
	while (1);
}

static volatile int check_rx = 0;
void rf_rx_calback(uint32_t gpio)
{
	check_rx = 1;
}

static uint8_t rf_specific_settings[] = {
	CC1101_REGS(gdo_config[2]), 0x07, /* GDO_0 - Assert on CRC OK | Disable temp sensor */
	CC1101_REGS(gdo_config[0]), 0x2E, /* GDO_2 - FIXME : do something usefull with it for tests */
	CC1101_REGS(pkt_ctrl[0]), 0x0F, /* Accept all sync, CRC err auto flush, Append, Addr check and Bcast */
#if (RF_915MHz == 1)
	/* FIXME : Add here a define protected list of settings for 915MHz configuration */
#endif
};

static volatile messageFormat cc_tx_msg;
void send_on_rf(void)
{
	messageFormat data;
	uint8_t cc_tx_data[sizeof(messageFormat)+2];
	cc_tx_data[0]=sizeof(messageFormat)+1;
	cc_tx_data[1]=NEIGHBOR_ADDRESS;
	strcpy(data.format, cc_tx_msg.format);
	data.hum=cc_tx_msg.hum;
	data.lum=cc_tx_msg.lum;
	data.temp=cc_tx_msg.temp;
	memcpy(&cc_tx_data[2], &data, sizeof(messageFormat));

	/* Send */
	if (cc1101_tx_fifo_state() != 0) {
		cc1101_flush_tx_fifo();
	}

	int ret = cc1101_send_packet(cc_tx_data, sizeof(messageFormat)+2);

#ifdef DEBUG
	uprintf(UART0, "Emission: ret: %d\n\r", ret);
    uprintf(UART0, "Emission: data length: %d\n\r", cc_tx_data[0]);
    uprintf(UART0, "Emission: destination: %x\n\r", cc_tx_data[1]);
    uprintf(UART0, "Emission: format: %s\n\r", data.format);
    uprintf(UART0, "Emission: temp: %d\n\r", data.temp);
    uprintf(UART0, "Emission: hum: %d\n\r", data.hum);
    uprintf(UART0, "Emission: lum: %d\n\r", data.lum);
#else
	uprintf(UART0, "{\"format\":\"%s\",\"temp\":%d,\"hum\":%d,\"lum\":%d}", data.format, data.temp, data.hum, data.lum);
#endif
}

/* RF config */
void rf_config(void)
{
	config_gpio(&cc1101_gdo0, LPC_IO_MODE_PULL_UP, GPIO_DIR_IN, 0);
	cc1101_init(0, &cc1101_cs_pin, &cc1101_miso_pin); /* ssp_num, cs_pin, miso_pin */
	/* Set default config */
	cc1101_config();
	/* And change application specific settings */
	cc1101_update_config(rf_specific_settings, sizeof(rf_specific_settings));
	set_gpio_callback(rf_rx_calback, &cc1101_gdo0, EDGE_RISING);
    cc1101_set_address(DEVICE_ADDRESS);
#ifdef DEBUG
	uprintf(UART0, "CC1101 RF link init done.\n\r");
#endif
}

void handle_rf_rx_data(void)
{
	message msg;
	uint8_t data[RF_BUFF_LEN];
	int8_t ret = 0;
	uint8_t status = 0;

	/* Check for received packet (and get it if any) */
	ret = cc1101_receive_packet(data, RF_BUFF_LEN, &status);
	/* Go back to RX mode */
	cc1101_enter_rx_mode();

	memcpy(&msg, &data[2], sizeof(message));

	if (data[0] == 13) {
		strcpy(cc_tx_msg.format, format);
		cc_tx_msg.temp = msg.temp;
		cc_tx_msg.hum = msg.hum;
		cc_tx_msg.lum = msg.lum;

#ifdef DEBUG
		uprintf(UART0, "\n\r*********************\n\r");
		uprintf(UART0, "Reception: ret:%d, st: %d\n\r", ret, status);
    	uprintf(UART0, "Reception: data length: %d\n\r", data[0]);
    	uprintf(UART0, "Reception: destination: %x\n\r", data[1]);
    	uprintf(UART0, "Reception: temp: %d\n\r", msg.temp);
    	uprintf(UART0, "Reception: hum: %d\n\r", msg.hum);
    	uprintf(UART0, "Reception: lum: %d\n\r", msg.lum);
#endif
	
		send_on_rf();
	}
}

/***************************************************************************** */

static volatile uint32_t cc_tx = 0;
static volatile uint8_t cc_tx_buff[UART_BUFF_LEN];
static volatile uint8_t cc_ptr = 0;
void reception_usb(uint8_t c)
{
	if (cc_ptr < UART_BUFF_LEN) {
		cc_tx_buff[cc_ptr++] = c;
		if (cc_ptr == UART_BUFF_LEN) {
			cc_ptr = 0;
			cc_tx = 1;
		}
	} else {
		cc_ptr = 0;
		cc_tx = 1;
	}
	if ((c == '\n') || (c == '\r')) {
		cc_ptr = 0;
		cc_tx = 1;
	}
}

int main(void)
{
	system_init();
	uart_on(UART0, 115200, reception_usb);
	i2c_on(I2C0, I2C_CLK_100KHz, I2C_MASTER);
	ssp_master_on(0, LPC_SSP_FRAME_SPI, 8, 4*1000*1000); /* bus_num, frame_type, data_width, rate */
	status_led_config(&status_led_green, &status_led_red);

	/* Radio */
	rf_config();

	uprintf(UART0, "App started\n\r");

	while (1) {
		uint8_t status = 0;
	
		/* Tell we are alive :) */
		chenillard(250);

		/* on a recu un message USB */
		if (cc_tx == 1) {
#ifdef DEBUG
			uprintf(UART0, "\n\rRECEPTION USB : %sn\r", cc_tx_buff);
#endif
			strcpy(format, cc_tx_buff);
			cc_tx = 0;
		}
		/* Do not leave radio in an unknown or unwated state */
		do {
			status = (cc1101_read_status() & CC1101_STATE_MASK);
		} while (status == CC1101_STATE_TX);

		if (status != CC1101_STATE_RX) {
			static uint8_t loop = 0;
			loop++;
			if (loop > 10) {
				if (cc1101_rx_fifo_state() != 0) {
					cc1101_flush_rx_fifo();
				}
				cc1101_enter_rx_mode();
				loop = 0;
			}
		}
		if (check_rx == 1) {
			check_rx = 0;
			handle_rf_rx_data();
		}

		
	}
	return 0;
}
