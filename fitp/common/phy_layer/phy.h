#ifndef MRF_PHY_LAYER_H
#define MRF_PHY_LAYER_H

#include <stdint.h>
#include <stdbool.h>

#include "constants.h"

// crystal oscillator frequency (reference value in data sheet)
#define FXTAL 12800

// maximal packet size on physical layer
// it is dependent on radio modul, for the MRF89 it is 63
#define MAX_PHY_PAYLOAD_SIZE 63

/**
 * Valid bands.
 */
enum bands {
	BAND_863 = 0,
	BAND_863_C950 = 1,
	BAND_902 = 2,
	BAND_915 = 3
};

/**
 * Structure of parameters on physical layer.
 */
struct PHY_init_t {
	uint8_t channel;
	uint8_t band;
	uint8_t bitrate;
	uint8_t power;
	// maximum value of noise when device is trying to send data
	uint8_t cca_noise_threshold_max;
	// minimum value of noise when device is trying to send data
	uint8_t cca_noise_threshold_min;
};


/**
 * Initializes physical layer.
 * @param params Parameters of physical layer.
 */
void PHY_init (struct PHY_init_t *params);

/**
 * Detaches threads used for HW layer.
 */
void PHY_stop ();

/**
 * Sets band.
 * @param band Band.
 * @return Returns true if band setting is successful, false otherwise.
 */
bool PHY_set_freq (uint8_t band);

/**
 * Sets channel number.
 * @param channel Channel number.
 * @return Returns true if channel number setting is successful, false otherwise.
 */
bool PHY_set_channel (uint8_t channel);

/**
 * Sets bitrate.
 * @param bitrate Bitrate.
 * @return Returns true if bitrate setting is successful, false otherwise.
 */
bool PHY_set_bitrate (uint8_t bitrate);

/**
 * Sets output power.
 * @param power Power.
 * @return Returns true if power setting is successful, false otherwise.
 * Note: Can be used as macro from constants.h format TX_POWER_${value}_DB.
 * power      hex   macro
 * 13 db      0x00  TX_POWER_13_DB
 * 10 db      0x01  TX_POWER_10_DB
 *  7 db      0x02  TX_POWER_7_DB
 *  4 db      0x03  TX_POWER_4_DB
 *  1 db      0x04  TX_POWER_1_DB
 * -2 db      0x05  TX_POWER_N_2_DB
 * -5 db      0x06  TX_POWER_N_5_DB
 * -8 db      0x07  TX_POWER_N_8_DB
 * @return Returns true if power setting is successful, false otherwise.
 */
bool PHY_set_power (uint8_t power);

/**
 * Reads RSSI value.
 * @return Returns RSSI value.
 */
uint8_t PHY_get_noise ();

/**
 * Sends data.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_send (const char *data, uint8_t len);

/**
 * Sends data if noise on medium is acceptable.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_send_with_cca (uint8_t * data, uint8_t len);

extern void PHY_process_packet (uint8_t * data, uint8_t len);

extern void PHY_timer_interrupt (void);

/**
 * Sets MRF89XA transceiver operating mode to sleep, transmit, receive or standby.
 * @param mode Mode.
 */
void set_rf_mode (uint8_t mode);

/**
 * Searches set channel number.
 * @return Returns channel number.
 */
uint8_t PHY_get_channel (void);

/**
 * Reads RSSI value.
 * @return Returns RSSI value.
 */
uint8_t PHY_get_noise (void);

/**
 * Reads stored RSSI value.
 * @return Returns stored RSSI value.
 */
uint8_t PHY_get_measured_noise();
#endif
