#include "glib.h"
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

#include "constants.h"
#include "phy.h"
#include "mqtt_iface.h"
#include "simulator.h"
#include "global.h"

using namespace std;

// process synchronization during sending
bool waiting_send = false;
std::mutex m, mm, mmm;
std::condition_variable cv;

struct PHY_storage_t {
	uint8_t mode;
	uint8_t channel;
	uint8_t band;
	uint8_t bitrate;
	uint8_t power;
	uint8_t received_packet[MAX_PHY_PAYLOAD_SIZE];
	uint8_t cca_noise_threshold_max;
	uint8_t cca_noise_threshold_min;
	uint8_t signal_strength;

	// the PAN coordinator setting
	std::thread irq_interrupt_deamon;
	std::thread timer_interrupt_generator;
	bool irq1_enabled = false;
	bool irq0_enabled = false;
	GMainLoop *gloop;
	bool terminate_timer = false;
} PHY_STORAGE;

/**
 * Extern functions.
 */
extern void PHY_process_packet (uint8_t * data, uint8_t len);
extern void PHY_timer_interrupt (void);

/**
 * Support functions.
 */
void set_register (uint8_t address, uint8_t value);
uint8_t get_register (uint8_t address);
void write_fifo (uint8_t data);
void set_rf_mode (uint8_t mode);
bool set_power (uint8_t power);
bool set_bitrate (uint8_t bitrate);
bool set_channel_freq_rate (uint8_t channel, uint8_t band, uint8_t bitrate);
void send_reload_radio ();
inline uint8_t get_cca_noise ();

/**
 * Implemented functions from hw layer.
 */
void HW_irq0_occurred (void);
void HW_irq1_occurred (void);

// Constant table
// BAND_863, BAND863_C950, BAND_902, BAND_915
const uint16_t _start_freq[] = { 860, 950, 902, 915 };
uint16_t _channel_spacing[] = { 384, 400, 400, 400 };

uint16_t _channel_compare (uint8_t band, uint8_t channel, uint8_t bitrate)
{
	uint32_t freq = (uint32_t) _start_freq[band] * 1000;
	if ((band == BAND_863 || band == BAND_863_C950)
			&& !(bitrate == DATA_RATE_100 || bitrate == DATA_RATE_200)) {
		freq += channel * 300;
	}
	else {
		freq += channel * _channel_spacing[band];
	}
	uint32_t channel_compare_tmp = (freq * 808);
	return (uint16_t) (channel_compare_tmp / ((uint32_t) 9 * FXTAL));
}

/**
 * Finds out the number of the channels.
 * @params band 		The band.
 * @params bitrate 	The bitrate.
 * @return Returns the number of channels for the band and the bitrate.
 */
uint8_t channel_amount (uint8_t band, uint8_t bitrate)
{
	if ((band == BAND_863 || band == BAND_863_C950)
			&& (bitrate == DATA_RATE_100 || bitrate == DATA_RATE_200)) {
		return 25;
	}
	else {
		return 32;
	}
}

/**
 * Defines rvalue.
 * @return Returns the rvalue.
 */
uint8_t r_value ()
{
	return 100;
}

/**
 * Computes pvalue based on the band, the channel and the bitrate.
 * @param band 		The band.
 * @param channel The channel number.
 * @param bitrate The bitrate.
 * @return Returns the pvalue.
 */
uint8_t p_value (uint8_t band, uint8_t channel, uint8_t bitrate)
{
}

/**
 * Accesses the control register of MRF89XA.
 * @param address The address of the register.
 * @param value   The register setting.
 */
void set_register (const uint8_t address, const uint8_t value)
{
}

/**
 * Reads back the register value.
 * @param 	address The address of the register.
 * @return  Return the register value.
 */
uint8_t get_register (uint8_t address)
{
}

/**
 * Reads the FIFO.
 * @return Returns the read char.
 */
uint8_t read_fifo (void)
{
}

/**
 * Fills the FIFO.
 * @param Data The data to be sent to FIFO.
 */
void write_fifo (const uint8_t data)
{
}

/**
 * Sets the MRF89XA transceiver operating mode to sleep, transmit, receive or standby.
 * @param mode The mode.
 */
void set_rf_mode (uint8_t mode)
{
}

/**
 * set the operating channel, band, bitrate and R, P, S registers for the RF transceiver.
 * @param channel		The channel number (0-31, not all channels
 *                	are available under all conditions).
 * @param band 			The band.
 * @param bitrate		The bitrate.
 * @return Returns true, if the channel setting is successful, false otherwise.
 */
bool set_channel_freq_rate (uint8_t channel, uint8_t band, uint8_t bitrate)
{
}

/**
 * Sets the bitrate for the RF transceiver.
 * @param bitrate The bitrate.
 * @return Returns true, if the bitrate setting is successful, false otherwise.
 */
bool set_bitrate (uint8_t bitrate)
{
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

/**
 * Sets the output power for the RF transceiver.
 * @param power RF transceiver output power.
 * @return Returns true, if the output power setting is successful, false otherwise.
 */
bool set_power (uint8_t power)
{

}

/**
 * Synthetise the RF.
 */
void send_reload_radio ()
{
	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_SYNTHESIZER);
	set_register (FTPRIREG, (FTPRIREG_SET & 0xFD) | 0x02);
	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_RECEIVER);
}

/**
 * Reads the RSSI value.
 * @return Returns the RSSI value.
 */
uint8_t get_cca_noise ()
{
	return get_register (RSTSREG) >> 1;
}

/*
 * The HW layer initialization.
 */
void HW_init (void)
{
}

/**
 * The interrupt request 0 (IRQ0) occured.
 */
void HW_irq0_occurred (void)
{
}

/**
 * The interrupt request 1 (IRQ1) occured.
 */
void HW_irq1_occurred (void)
{
	printf("Preruseni\n");
	if (PHY_STORAGE.mode == RF_RECEIVER) {
		printf ("HW_irq1occurred() RF_RECEIVER\n");
		uint8_t received_len = 0;
		//std::lock_guard<std::mutex> lock(mm);
		{
			std::lock_guard < std::mutex > lock (mm);

			PHY_STORAGE.irq1_enabled = false;
			PHY_STORAGE.irq0_enabled = false;

			uint8_t fifo_stat = get_register (FTXRXIREG);

			while (fifo_stat & 0x02) {
				//HW_enableData();
				uint8_t readed_byte = read_fifo ();
				//HW_disableData();
				PHY_STORAGE.received_packet[received_len++] = readed_byte;
				fifo_stat = get_register (FTXRXIREG);
			}
			PHY_STORAGE.irq1_enabled = true;
			PHY_STORAGE.irq0_enabled = true;
		}
		printf ("HW_irq1occurred() received_len: %d\n", received_len);
		if (received_len == 0)
			return;
		//add zero termination needed for string operations
		if (received_len < 63) {
			PHY_STORAGE.received_packet[received_len] = 0;
		}
		PHY_process_packet (PHY_STORAGE.received_packet + 1, received_len - 1);
	}
	else {
		printf ("HW_irq1occurred(): not in RECEIVER mode!\n");
		//PHY_STORAGE.send_done = 1;
	}
}


void irq_interrupt_deamon_f ()
{
}

void timer_interrupt_generator_f ()
{
	while (!PHY_STORAGE.terminate_timer) {
		std::this_thread::sleep_for (std::chrono::milliseconds (300));
		PHY_timer_interrupt ();
	}
}

/**
 * Initializes the physical layer.
 * @param params The parameters for the initialization.
 */
void PHY_init (struct PHY_init_t* phy_params)
{
	HW_init ();
	PHY_STORAGE.irq_interrupt_deamon = std::thread (irq_interrupt_deamon_f);
	PHY_STORAGE.timer_interrupt_generator = std::thread (timer_interrupt_generator_f);

	PHY_STORAGE.cca_noise_threshold_max = phy_params->cca_noise_threshold_max;
	PHY_STORAGE.cca_noise_threshold_min = phy_params->cca_noise_threshold_min;

	for (uint8_t i = 0; i <= 31; i++) {
		if ((i << 1) == R1CNTREG) {
			set_channel_freq_rate (phy_params->channel, phy_params->band, phy_params->bitrate);
			// jump over R1CNTREG, P1CNTREG, S1CNTREG
			i += 3;
		}
		if ((i << 1) == TXPARAMREG) {
			set_power (phy_params->power);
			// jump over TXPARAMREG
			i += 1;
		}
		if ((i << 1) == FDEVREG) {
			set_bitrate (phy_params->bitrate);
			// jump over FDEVREG, BRREG
			i += 2;
		}
		if ((i << 1) == FILCONREG) {
			// already done in previous call of set_bitrate(params.bitrate);
			i += 1;
		}
		set_register (i << 1, init_config_regs[i]);
	}

	for (uint8_t i = 0; i <= 31; i++) {
		uint8_t reg_val = get_register (i << 1);
	}

	send_reload_radio ();
	PHY_STORAGE.irq0_enabled = true;
	PHY_STORAGE.irq1_enabled = true;
	// TODO: PHY initialization is set directly!
	PHY_STORAGE.channel = 5;
	PHY_STORAGE.band = 0;
	PHY_STORAGE.bitrate = 5;
}

void PHY_stop ()
{

	PHY_STORAGE.terminate_timer = true;
	// waiting for threads to end
	PHY_STORAGE.irq_interrupt_deamon.join ();
	PHY_STORAGE.timer_interrupt_generator.join ();
}

/**
 * Sends the data.
 * @param data 	The data.
 * @param len 	The data length.
 */
void PHY_send (const uint8_t * data, uint8_t len)
{
	string msg, msg_tmp;
	msg.clear ();
	msg = create_head ();

	for (uint8_t i = 0; i < len; i++) {
		msg += to_string (data[i]) + ',';
	}
	msg_tmp = "/usr/bin/mosquitto_pub -t BeeeOn/data_from -m " + msg;
	//cout << msg_tmp << endl;
	std::system (msg_tmp.c_str ());
	//mq->publish(NULL, "BeeeOn/data_from", msg.length(), msg.c_str());
}

/**
 * Sends the data if the noise on medium is acceptable.
 * @param data 	The data.
 * @param len 	The data length.
 */
void PHY_send_with_cca (uint8_t * data, uint8_t len)
{
	uint8_t noise;
	std::lock_guard < std::mutex > lock (mm);
	do {
		noise = PHY_get_noise ();
	} while(noise > PHY_STORAGE.cca_noise_threshold_max || noise < PHY_STORAGE.cca_noise_threshold_min);
	PHY_send (data, len);
}

/**
 * Sets the band.
 * @param band The band.
 * @return Returns true if the band setting is successful, false otherwise.
 */
bool PHY_set_freq (uint8_t band)
{
	if (band == PHY_STORAGE.band)
		return true;
	bool holder = set_channel_freq_rate (PHY_STORAGE.channel, band, PHY_STORAGE.bitrate);
	send_reload_radio ();
	return holder;
}

/**
 * Sets the channel number.
 * @param channel The channel number.
 * @return Returns true if the channel number setting is successful, false otherwise.
 */
bool PHY_set_channel (uint8_t channel)
{
	if (channel == PHY_STORAGE.channel)
		return true;
	bool holder = set_channel_freq_rate (channel, PHY_STORAGE.band, PHY_STORAGE.bitrate);
	send_reload_radio ();
	return holder;
}

/**
 * Sets the bitrate.
 * @param bitrate The bitrate.
 * @return Returns true if the bitrate setting is successful, false otherwise.
 */
bool PHY_set_bitrate (uint8_t bitrate)
{
	if (bitrate == PHY_STORAGE.bitrate)
		return true;
	if (set_bitrate (bitrate) == false)
		return false;
	bool holder = set_channel_freq_rate (PHY_STORAGE.channel, PHY_STORAGE.band, bitrate);
	send_reload_radio ();
	return holder;
}

/**
 * Sets the output power.
 * @param power The power.
 * @return Returns true if the power setting is successful, false otherwise.
 */
bool PHY_set_power (uint8_t power)
{
	if (power == PHY_STORAGE.power)
		return true;
	return set_power (power);
}

/**
 * Reads the RSSI value.
 * @return Returns the RSSI value.
 */
uint8_t PHY_get_noise ()
{
	return get_cca_noise ();
}

uint8_t PHY_get_measured_noise()
{
	return PHY_STORAGE.signal_strength;
}
