#include <glib.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <string>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>

#define CS_CONF GPIOL0
#define CS_DATA GPIOL1
#define RESET GPIOL2
#define IRQ1 GPIOL3

extern "C" {
#include <mpsse.h>
}
#include "phy.h"
#include "ftdi_phy.h"
#include "constants.h"

using namespace std;

std::mutex io_mutex;
std::mutex send_mutex;

/* Polling support functions */
void irq_start_polling ();
gboolean irq_poll (gpointer data);
void irq1_occured ();

/*GLOBAL VARIABLES*/
struct PHY_storage_t {
	uint8_t mode;
	uint8_t channel;
	uint8_t band;
	uint8_t bitrate;
	uint8_t power;
	uint8_t recived_packet[MAX_PHY_PAYLOAD_SIZE];
	uint8_t cca_noise_threshold;

	//PAN coordinator specific
	std::thread irq_polling_deamon;
	GMainLoop *gloop;

} PHY_STORAGE;

struct mpsse_context *PAN;

/****** PHYSICAL INTERFACE FUNCTIONS *******/
void PHY_init (PHY_init_t params) {
	/* Init connection with ftdi H232 chip */

	printf ("INIT channel %d\n", params.channel);
	printf ("INIT band %d\n", params.band);
	printf ("INIT bitrate %d\n", params.bitrate);
	printf ("INIT power %d\n", params.power);

	PHY_STORAGE.cca_noise_threshold = params.cca_noise_threshold;

	PAN = MPSSE (SPI0, 10000, MSB);
	if (PAN == NULL) {
		fprintf (stderr, "Error ftdi module not found, fitprotocold won't work.");
		return;
	}
	PinHigh (PAN, CS_CONF);
	PinHigh (PAN, CS_DATA);
	PinHigh (PAN, RESET);
	PinLow (PAN, IRQ1);
	reset_pan ();

	set_register (0, 0);					//DUMMY READ- 2 bytes

	/*Set registers */
	for (uint8_t i = 0; i <= 31; i++) {

		if ((i << 1) == R1CNTREG) {
			set_chanel_freq_rate (params.channel, params.band, params.bitrate);
			i += 3;
		}

		if ((i << 1) == TXPARAMREG) {
			set_power (params.power);
			i += 1;
		}

		if ((i << 1) == FDEVREG) {
			set_bitrate (params.bitrate);
			i += 2;
		}

		if ((i << 1) == FILCONREG) {
			i += 1;
		}

		set_register (i << 1, InitConfigRegs[i]);
	}

	send_reload_radio ();

	PHY_STORAGE.irq_polling_deamon = std::thread (irq_start_polling);
}

void PHY_stop () {
	Close (PAN);
	g_main_loop_quit (PHY_STORAGE.gloop);
	PHY_STORAGE.irq_polling_deamon.join ();
}

bool PHY_set_freq (uint8_t band) {
	if (band == PHY_STORAGE.band)
		return true;
	bool holder = set_chanel_freq_rate (PHY_STORAGE.channel, band, PHY_STORAGE.bitrate);
	send_reload_radio ();
	return holder;
}

bool PHY_set_channel (uint8_t channel) {
	if (channel == PHY_STORAGE.channel)
		return true;
	bool holder = set_chanel_freq_rate (channel, PHY_STORAGE.band, PHY_STORAGE.bitrate);
	send_reload_radio ();
	return holder;
}

bool PHY_set_bitrate (uint8_t bitrate) {
	if (bitrate == PHY_STORAGE.bitrate)
		return true;
	if (set_bitrate (bitrate) == false)
		return false;
	bool holder = set_chanel_freq_rate (PHY_STORAGE.channel, PHY_STORAGE.band, bitrate);
	send_reload_radio ();
	return holder;
}

bool PHY_set_power (uint8_t power) {
	if (power == PHY_STORAGE.power)
		return true;
	return set_power (power);
}

uint8_t PHY_get_noise () {
	return get_cca_noise ();
}

void PHY_send (const uint8_t * data, uint8_t len) {

	set_rf_mode (RF_STANDBY);
	set_register (FTXRXIREG, FTXRXIREG_SET | 0x01);

	write_fifo (len);
	for (int i = 0; i < len; i++) {
		write_fifo (data[i]);
	}

	set_rf_mode (RF_TRANSMITTER);

	while ((get_register (FTPRIREG) & 0x20) == 0) {
		usleep (500);
	}

	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_RECEIVER);

}

void PHY_send_with_cca (uint8_t * data, uint8_t len) {

	uint8_t noise;
	std::lock_guard < std::mutex > lock (send_mutex);
	while ((noise = PHY_get_noise ()) > PHY_STORAGE.cca_noise_threshold) {
	}

	PHY_send (data, len);
}


/****** SUPPORT FUNCTIONS *******/
void set_register (char address, char value) {
	io_mutex.lock ();
	PinLow (PAN, CS_CONF);
	Start (PAN);
	Write (PAN, &address, 1);
	Write (PAN, &value, 1);
	Stop (PAN);
	PinHigh (PAN, CS_CONF);
	io_mutex.unlock ();
}

char get_register (char address) {
	io_mutex.lock ();
	char value = 0x00;
	char *read_ptr;
	/*Modify the address first for write operation */
	address = (address | 0x40) & 0x7e;

	PinLow (PAN, CS_CONF);
	Start (PAN);
	Write (PAN, &address, 1);
	read_ptr = Read (PAN, 1);
	Stop (PAN);
	PinHigh (PAN, CS_CONF);
	if (read_ptr == NULL) {				// TODO ERROR
		io_mutex.unlock ();
		return 0;
	}
	value = *read_ptr;
	free (read_ptr);
	io_mutex.unlock ();
	return value;
}

uint8_t read_fifo (void) {
	io_mutex.lock ();
	char *data_ptr;
	char data;
	PinLow (PAN, CS_DATA);
	Start (PAN);
	data_ptr = Read (PAN, 1);
	Stop (PAN);
	data = *data_ptr;
	free (data_ptr);
	PinHigh (PAN, CS_DATA);
	io_mutex.unlock ();
	return data;
}

void write_fifo (const uint8_t data) {
	io_mutex.lock ();
	char wr_data = (char) data;
	PinLow (PAN, CS_DATA);
	Start (PAN);
	Write (PAN, &wr_data, 1);
	Stop (PAN);
	PinHigh (PAN, CS_DATA);
	io_mutex.unlock ();
}

bool set_chanel_freq_rate (char channel, char band, char bitrate) {

	PHY_STORAGE.channel = channel;
	PHY_STORAGE.band = band;
	PHY_STORAGE.bitrate = bitrate;

	set_register (R1CNTREG, rValue ());
	set_register (P1CNTREG, pValue (band, channel, bitrate));
	set_register (S1CNTREG, sValue (band, channel, bitrate));
}

bool set_power (char power) {

	if (power > TX_POWER_N_8_DB) {
		return false;
	}
	set_register (TXPARAMREG, 0xF0 | (power << 1));
	PHY_STORAGE.power = power;
	return true;

}

bool set_bitrate (char bitrate) {
	uint8_t datarate;
	uint8_t bandwidth;
	uint8_t freq_dev;
	uint8_t filcon_set;
	if (bitrate > DATA_RATE_200) {
		return false;
	}

	switch (bitrate) {
	case DATA_RATE_5:
		datarate = BITRATE_5;
		bandwidth = BW_50;
		freq_dev = FREQ_DEV_33;
		filcon_set = FILCON_SET_157;
		break;
	case DATA_RATE_10:
		datarate = BITRATE_10;
		bandwidth = BW_50;
		freq_dev = FREQ_DEV_33;
		filcon_set = FILCON_SET_157;
		break;
	case DATA_RATE_20:
		datarate = BITRATE_20;
		bandwidth = BW_75;
		freq_dev = FREQ_DEV_40;
		filcon_set = FILCON_SET_234;
		break;
	case DATA_RATE_40:
		datarate = BITRATE_40;
		bandwidth = BW_150;
		freq_dev = FREQ_DEV_80;
		filcon_set = FILCON_SET_414;
		break;
	case DATA_RATE_50:
		datarate = BITRATE_50;
		bandwidth = BW_175;
		freq_dev = FREQ_DEV_100;
		filcon_set = FILCON_SET_514;
		break;
	case DATA_RATE_66:
		datarate = BITRATE_66;
		bandwidth = BW_250;
		freq_dev = FREQ_DEV_133;
		filcon_set = FILCON_SET_676;
		break;
	case DATA_RATE_100:
		datarate = BITRATE_100;
		bandwidth = BW_400;
		freq_dev = FREQ_DEV_200;
		filcon_set = FILCON_SET_987;
		break;
	case DATA_RATE_200:
		datarate = BITRATE_200;
		bandwidth = BW_400;
		freq_dev = FREQ_DEV_200;
		filcon_set = FILCON_SET_987;
		break;
	}
	set_register (BRREG, datarate);
	set_register (FILCONREG, filcon_set | bandwidth);
	set_register (FDEVREG, freq_dev);
	return true;

}

uint8_t rValue () {
	return 100;
}

uint8_t pValue (uint8_t band, uint8_t chanel, uint8_t bitrate) {
	uint8_t pvalue =
		(uint8_t) (((uint16_t) (_channelCompare (band, chanel, bitrate) - 75) / 76) + 1);
	return pvalue;
}

uint8_t sValue (uint8_t band, uint8_t chanel, uint8_t bitrate) {
	uint16_t channel_cmp = _channelCompare (band, chanel, bitrate);
	uint8_t pvalue = (uint8_t) (((uint16_t) (channel_cmp - 75) / 76) + 1);
	uint8_t svalue =
		(uint8_t) (((uint16_t) channel_cmp - ((uint16_t) (75 * (pvalue + 1)))));
	return svalue;
}

/*
CONSTANT TABLE
                             BAND_863             BAND863_C950                BAND_902                 BAND_915 */
const uint16_t _start_freq[] = { 860, 950, 902, 915 };
uint16_t _chanel_spacing[] = { 384, 400, 400, 400 };

uint16_t _channelCompare (uint8_t band, uint8_t channel, uint8_t bitrate) {
	uint32_t freq = (uint32_t) _start_freq[band] * 1000;
	if ((band == BAND_863 || band == BAND_863_C950)
			&& !(bitrate == DATA_RATE_100 || bitrate == DATA_RATE_200)) {
		freq += channel * 300;
	} else {
		freq += channel * _chanel_spacing[band];
	}
	uint32_t chanelCompareTmp = (freq * 808);
	return (uint16_t) (chanelCompareTmp / ((uint32_t) 9 * FXTAL));
}

void reset_pan (void) {
	PinHigh (PAN, RESET);
	sleep (1);
	PinLow (PAN, RESET);
	sleep (1);
}

void set_rf_mode (uint8_t mode) {

	if ((mode == RF_TRANSMITTER) || (mode == RF_RECEIVER) ||
			(mode == RF_SYNTHESIZER) || (mode == RF_STANDBY) || (mode == RF_SLEEP)) {

		set_register (GCONREG, (GCONREG_SET & 0x1F) | mode);
		PHY_STORAGE.mode = mode;
	}
}

void send_reload_radio () {

	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_SYNTHESIZER);
	set_register (FTPRIREG, (FTPRIREG_SET & 0xFD) | 0x02);
	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_RECEIVER);
}

uint8_t get_cca_noise () {
	return get_register (RSTSREG) >> 1;
}

/* Interrupt initialization and handling*/
void irq_start_polling () {
	PHY_STORAGE.gloop = g_main_loop_new (NULL, FALSE);
	g_timeout_add (POLL_INTERVAL, irq_poll, (gpointer) "irq_polling");
	g_main_loop_run (PHY_STORAGE.gloop);
}

gboolean irq_poll (gpointer data) {

	if (PinState (PAN, IRQ1, -1) == 1) {	/*IRQ OCCURED */
		irq1_occured ();
	}
	return true;
}

extern void PHY_process_packet (uint8_t * data, uint8_t len);

void irq1_occured () {
	if (PHY_STORAGE.mode == RF_RECEIVER) {
		printf ("HW_irq1occurred() RF_RECIVER\n");

		uint8_t recived_len = 0;

		{
			std::lock_guard < std::mutex > lock (send_mutex);

			uint8_t fifo_stat = get_register (FTXRXIREG);

			while (fifo_stat & 0x02) {

				uint8_t readed_byte = read_fifo ();

				PHY_STORAGE.recived_packet[recived_len++] = readed_byte;

				fifo_stat = get_register (FTXRXIREG);
			}

		}

		printf ("HW_irq1occurred() recived_len %d\n", recived_len);
		if (recived_len == 0)
			return;

		if (recived_len < 63) {
			PHY_STORAGE.recived_packet[recived_len] = 0;
		}

		PHY_process_packet (PHY_STORAGE.recived_packet + 1, recived_len - 1);

	} else {
		fprintf (stderr, "Error, interrupt occured and RF_RECEIVER mod is not set!\n");
	}
}
