#ifndef MRF_FTDI_PHY_LAYER_H
#define MRF_FDTI_PHY_LAYER_H

#define POLL_INTERVAL 100				/* Polling interval in miliseconds */

/*Support functions declarations*/
void set_register (char address, char value);
char get_register (char address);
bool set_chanel_freq_rate (char channel, char offsetFreq, char bitrate);
bool set_power (char power);
bool set_bitrate (char bitrate);
uint8_t read_fifo (void);
void write_fifo (uint8_t);

inline uint8_t get_cca_noise ();

void send_reload_radio ();
void set_rf_mode (uint8_t mode);

void reset_pan (void);

uint8_t rValue ();
uint8_t pValue (uint8_t band, uint8_t chanel, uint8_t bitrate);
uint8_t sValue (uint8_t band, uint8_t chanel, uint8_t bitrate);
uint16_t _channelCompare (uint8_t band, uint8_t channel, uint8_t bitrate);
