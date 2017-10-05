#include "include.h"

struct PHY_storage_t {
	uint8_t mode;
	uint8_t channel;
	uint8_t band;
	uint8_t bitrate;
	uint8_t power;
	uint8_t send_done;
	uint8_t received_packet[MAX_PHY_PAYLOAD_SIZE];
	uint8_t cca_noise_threshold;
	std::thread timer_interrupt_generator;
} PHY_STORAGE;

/**
 * Support functions.
 */
void set_rf_mode (uint8_t mode);
bool set_power (uint8_t power);
bool set_bitrate (uint8_t bitrate);
bool set_channel_freq_rate (uint8_t channel, uint8_t band, uint8_t bitrate);
uint8_t get_cca_noise ();
uint8_t channel_amount (uint8_t band, uint8_t bitrate);

/**
 * Sets the MRF89XA transceiver operating mode to sleep, transmit, receive or standby.
 * @param mode The mode.
 */
void set_rf_mode (uint8_t mode)
{
	if ((mode == RF_TRANSMITTER) || (mode == RF_RECEIVER) ||
			(mode == RF_SYNTHESIZER) || (mode == RF_STANDBY) || (mode == RF_SLEEP)) {
		PHY_STORAGE.mode = mode;
	}
}

/**
 * Generates the interrupt to increment the counter.
 */
void timer_interrupt_generator_f ()
{
	while (1) {
		std::this_thread::sleep_for (std::chrono::milliseconds (500));
		PHY_timer_interrupt ();
	}
}

/**
 * set the operating channel, band and bitrate for the RF transceiver.
 * @param channel		The channel number (0-31, not all channels
 *                	are available under all conditions).
 * @param band 			The band.
 * @param bitrate		The bitrate.
 * @return Returns true, if the channel setting is successful, false otherwise.
 */
bool set_channel_freq_rate (uint8_t channel, uint8_t band, uint8_t bitrate)
{
	//printf ("Channel amount %d, channel %d", channel_amount (band, bitrate), channel);
	if (channel >= channel_amount (band, bitrate)) {
		return false;
	}
	PHY_STORAGE.channel = channel;
	PHY_STORAGE.band = band;
	PHY_STORAGE.bitrate = bitrate;

	printf ("Channel %d, band %d, bitrate %d\n", channel, band, bitrate);
	return true;
}

/**
 * Sets the output power for the RF transceiver.
 * @param power RF transceiver output power.
 * @return Returns true, if the output power setting is successful, false otherwise.
 */
bool set_power (uint8_t power)
{
	if (power > TX_POWER_N_8_DB) {
		return false;
	}
	PHY_STORAGE.power = power;
	return true;
}

/**
 * Sets the bitrate for the RF transceiver.
 * @param bitrate The bitrate.
 * @return Returns true, if the bitrate setting is successful, false otherwise.
 */
bool set_bitrate (uint8_t bitrate)
{
	if (bitrate > DATA_RATE_200) {
		return false;
	}

	return true;
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
	return holder;
}

/**
 * Finds out the set channel number.
 * @return the channel number.
 */
uint8_t PHY_get_channel (void)
{
	return PHY_STORAGE.channel;
}

/**
 * Initializes the physical layer.
 * @param params The parameters fot the initialization.
 */
void PHY_init (PHY_init_t params)
{
	PHY_STORAGE.send_done = false;
	PHY_STORAGE.cca_noise_threshold = params.cca_noise_threshold;

	set_channel_freq_rate (params.channel, params.band, params.bitrate);
	set_power (params.power);
	set_bitrate (params.bitrate);
	PHY_STORAGE.timer_interrupt_generator = std::thread (timer_interrupt_generator_f);
}

/**
 * Reads the RSSI value.
 * @return Returns the RSSI value.
 */
uint8_t PHY_get_noise ()
{
	return 20;
}

/**
 * Sends the data.
 * @param data 	The data.
 * @param len 	The data length.
 */
void PHY_send (const uint8_t* data, uint8_t len)
{
	string msg, msg_tmp;
	msg.clear ();

	msg = create_head ();

	for (uint8_t i = 0; i < len; i++) {
		msg += to_string (data[i]) + ',';

	}
	cout << endl << "PHY_send: " << msg << endl;

	//  mq->send_message(DATA_FROM_TOPIC, msg.c_str(), msg.length());
	msg_tmp = "/usr/bin/mosquitto_pub -t BeeeOn/data_from -m " + msg;

	usleep (1000);

	std::system (msg_tmp.c_str ());
}

/**
 * Sends the data if the noise on medium is acceptable.
 * @param data 	The data.
 * @param len 	The data length.
 */
void PHY_send_with_cca (uint8_t* data, uint8_t len)
{
	PHY_send (data, len);
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
