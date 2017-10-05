#include "phy.h"
#include "log.h"

//#define _XTAL_FREQ 8000000

/**
 * Structure with parameters of physical layer.
 */
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
	//uint8_t send_done;
} PHY_STORAGE;

/**
 * Extern functions.
 */
extern void HW_spi_put (const uint8_t data);
extern uint8_t HW_spi_get (void);
extern void HW_disable_config (void);
extern void HW_enable_config (void);
extern void HW_disable_data (void);
extern void HW_enable_data (void);
extern bool HW_is_irq0_enabled (void);
extern bool HW_is_irq0_active (void);
extern bool HW_is_irq0_clear (void);
extern void HW_clear_irq0 (void);
extern void HW_disable_irq0 (void);
extern void HW_enable_irq0 (void);
extern bool HW_is_irq1_enabled (void);
extern bool HW_is_irq1_active (void);
extern bool HW_is_irq1_clear (void);
extern void HW_clear_irq1 (void);
extern void HW_disable_irq1 (void);
extern void HW_enable_irq1 (void);
extern void HW_init (void);
extern void HW_sniffRX (uint8_t len, const char *data);
extern void HW_sniffTX (uint8_t len, const char *data);

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
uint8_t get_cca_noise ();
uint8_t PHY_get_noise ();

/**
 * Support functions for frequency.
 */
uint8_t channel_amount (uint8_t band, uint8_t bitrate);
uint16_t channel_compare (uint8_t band, uint8_t channel, uint8_t bitrate);
uint8_t r_value ();
uint8_t p_value (uint8_t band, uint8_t channel, uint8_t bitrate);
uint8_t s_value (uint8_t band, uint8_t channel, uint8_t bitrate);

/**
 * Implemented functions from hw layer.
 */
void HW_irq0_occurred (void);
void HW_irq1_occurred (void);
void HW_timeoccurred (void);

/**
 * Accesses control register of MRF89XA.
 * @param address Address of register.
 * @param value   Register setting.
 */
void set_register (uint8_t address, uint8_t value)
{
	uint8_t irq1_select = HW_is_irq1_enabled ();
	uint8_t irq0_select = HW_is_irq0_enabled ();

	HW_disable_irq0 ();
	HW_disable_irq1 ();

	HW_enable_config ();
	//0x3E = 0b00111110 START_BIT|_W_/R|D|D|D|D|D|STOP_BIT
	HW_spi_put (address & 0x3e);
	HW_spi_put (value);
	HW_disable_config ();

	if (irq1_select)
		HW_enable_irq1 ();
	if (irq0_select)
		HW_enable_irq0 ();
}

/**
 * Reads back register value.
 * @param 	address Address of register.
 * @return  Returns register value.
 */
uint8_t get_register (uint8_t address)
{
	uint8_t value;
	uint8_t irq1_select = HW_is_irq1_enabled ();
	uint8_t irq0_select = HW_is_irq0_enabled ();

	HW_disable_irq0 ();
	HW_disable_irq1 ();

	HW_enable_config ();
	//0x7E = 0b01111110 START_BIT|W/_R_|D|D|D|D|D|STOP_BIT
	address = (address | 0x40) & 0x7e;
	HW_spi_put (address);
	value = HW_spi_get ();
	HW_disable_config ();
	if (irq1_select)
		HW_enable_irq1 ();
	if (irq0_select)
		HW_enable_irq0 ();

	return value;
}

/**
 * Fills FIFO.
 * @param data Data to be sent to FIFO.
 */
void write_fifo (uint8_t data)
{
	uint8_t irq1_select = HW_is_irq1_enabled ();
	uint8_t irq0_select = HW_is_irq0_enabled ();

	HW_disable_irq0 ();
	HW_disable_irq1 ();

	HW_enable_data ();
	HW_spi_put (data);
	HW_disable_data ();

	if (irq1_select)
		HW_enable_irq1 ();
	if (irq0_select)
		HW_enable_irq0 ();
}

/**
 * Sets MRF89XA transceiver operating mode to sleep, transmit, receive or standby.
 * @param mode Mode.
 */
void set_rf_mode (uint8_t mode)
{
	if ((mode == RF_TRANSMITTER) || (mode == RF_RECEIVER) ||
			(mode == RF_SYNTHESIZER) || (mode == RF_STANDBY) || (mode == RF_SLEEP)) {
		set_register (GCONREG, (GCONREG_SET & 0x1f) | mode);
		PHY_STORAGE.mode = mode;
	}
}

/**
 * Set operating channel, band, bitrate and R, P, S registers for RF transceiver.
 * @param channel		Channel number (0-31, not all channels
 *                	are available under all conditions).
 * @param band 			Band.
 * @param bitrate		Bitrate.
 * @return Returns true, if channel setting is successful, false otherwise.
 */
bool set_channel_freq_rate (uint8_t channel, uint8_t band, uint8_t bitrate)
{
	if (channel >= channel_amount (band, bitrate)) {
		return false;
	}
	PHY_STORAGE.channel = channel;
	PHY_STORAGE.band = band;
	PHY_STORAGE.bitrate = bitrate;
	// R, P, S register setting
	set_register (R1CNTREG, r_value ());
	set_register (P1CNTREG, p_value (band, channel, bitrate));
	set_register (S1CNTREG, s_value (band, channel, bitrate));
	return true;
}

/**
 * Sets output power for RF transceiver.
 * @param power RF transceiver output power.
 * @return Returns true, if output power setting is successful, false otherwise.
 */
bool set_power (uint8_t power)
{
	if (power > TX_POWER_N_8_DB) {
		return false;
	}
	//set value 1111xxx(r)
	set_register (TXPARAMREG, 0xf0 | (power << 1));
	PHY_STORAGE.power = power;
	return true;
}

/**
 * Sets bitrate for RF transceiver.
 * @param bitrate Bitrate.
 * @return Returns true, if bitrate setting is successful, false otherwise.
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
 * Synthetises RF.
 */
void send_reload_radio ()
{
	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_SYNTHESIZER);
	set_register (FTPRIREG, (FTPRIREG_SET & 0xfd) | 0x02);
	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_RECEIVER);
}

/**
 * Gets RSSI value.
 * @return Returns RSSI value.
 */
uint8_t get_cca_noise ()
{
	uint8_t RSSI = get_register (RSTSREG) >> 1;
	return RSSI;
}

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
	} else {
		freq += channel * _channel_spacing[band];
	}
	uint32_t channel_compare_tmp = (freq * 808);
	return (uint16_t) (channel_compare_tmp / ((uint32_t) 9 * FXTAL));
}

/**
 * Serches number of channels.
 * @params band 		Band.
 * @params bitrate 	Bitrate.
 * @return Returns number of channels for band and bitrate.
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
 * @return Returns rvalue.
 */
uint8_t r_value ()
{
	return 100;
}

/**
 * Computes pvalue based on band, channel and bitrate.
 * @param band 		Band.
 * @param channel Channel number.
 * @param bitrate bitrate.
 * @return Returns pvalue.
 */
uint8_t p_value (uint8_t band, uint8_t channel, uint8_t bitrate)
{
	uint8_t p_value =
		(uint8_t) (((uint16_t) (_channel_compare (band, channel, bitrate) - 75) / 76) + 1);
	return p_value;
}

/**
 * Computes svalue based on band, channel and bitrate.
 * @param band 		band.
 * @param channel Channel number.
 * @param bitrate Bitrate.
 * @return Returns svalue.
 */
uint8_t s_value (uint8_t band, uint8_t channel, uint8_t bitrate)
{
	uint16_t channel_cmp = _channel_compare (band, channel, bitrate);
	uint8_t p_value = (uint8_t) (((uint16_t) (channel_cmp - 75) / 76) + 1);
	uint8_t s_value =
		(uint8_t) (((uint16_t) channel_cmp - ((uint16_t) (75 * (p_value + 1)))));
	return s_value;
}

/**
 * Initializes physical layer.
 * @param params Parameters of physical layer.
 */
void PHY_init (struct PHY_init_t *params)
{
	HW_init ();
	HW_disable_config ();
	HW_disable_data ();

	PHY_STORAGE.cca_noise_threshold_max = params->cca_noise_threshold_max;
	PHY_STORAGE.cca_noise_threshold_min = params->cca_noise_threshold_min;
	//PHY_STORAGE.send_done = 0;

	// configuring RF link
	for (uint8_t i = 0; i <= 31; i++) {
		// setup frequency
		if ((i << 1) == R1CNTREG) {
			set_channel_freq_rate (params->channel, params->band, params->bitrate);
			// jump over R1CNTREG, P1CNTREG, S1CNTREG
			i += 3;
		}

		if ((i << 1) == TXPARAMREG) {
			set_power (params->power);
			// jump over TXPARAMREG
			i += 1;
		}

		if ((i << 1) == FDEVREG) {
			set_bitrate (params->bitrate);
			// jump over FDEVREG, BRREG
			i += 2;
		}
		if ((i << 1) == FILCONREG) {
			// already done in previous call of set_bitrate(params.bitrate);
			i += 1;
		}
		set_register (i << 1, init_config_regs[i]);
	}
	send_reload_radio ();

	HW_clear_irq0 ();
	HW_enable_irq0 ();

	HW_clear_irq1 ();
	HW_enable_irq1 ();
}

/**
 * Sets band.
 * @param band Band.
 * @return Returns true if band setting is successful, false otherwise.
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
 * Sets channel number.
 * @param channel Channel number.
 * @return Returns true if channel number setting is successful, false otherwise.
 */
bool PHY_set_channel (uint8_t channel)
{
	bool holder = set_channel_freq_rate (channel, PHY_STORAGE.band, PHY_STORAGE.bitrate);
	send_reload_radio ();
	return holder;
}

/**
 * Searches set channel number.
 * @return Returns channel number.
 */
uint8_t PHY_get_channel (void)
{
	D_PHY printf ("channel: %d\n", PHY_STORAGE.channel);
	return PHY_STORAGE.channel;
}

/**
 * Sets bitrate.
 * @param bitrate Bitrate.
 * @return Returns true if bitrate setting is successful, false otherwise.
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
 * Sets output power.
 * @param power Power.
 * @return Returns true if power setting is successful, false otherwise.
 */
bool PHY_set_power (uint8_t power)
{
	if (power == PHY_STORAGE.power)
		return true;
	return set_power (power);
}

/**
 * Reads RSSI value.
 * @return Returns RSSI value.
 */
uint8_t PHY_get_noise ()
{
	return get_cca_noise ();
}

/**
 * Reads stored RSSI value.
 * @return Returns stored RSSI value.
 */
uint8_t PHY_get_measured_noise()
{
	return PHY_STORAGE.signal_strength;
}

/**
 * Sends data.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_send (const char *data, uint8_t len)
{
	D_PHY printf("PHY_send()\n");
	HW_disable_irq0 ();
	HW_disable_irq1 ();

	set_rf_mode (RF_STANDBY);
	set_register (FTXRXIREG, FTXRXIREG_SET | 0x01);

	write_fifo (len);

	for (int i = 0; i < len; i++) {
		write_fifo (data[i]);
	}
	set_rf_mode (RF_TRANSMITTER);

	HW_enable_irq0 ();
	HW_enable_irq1 ();

	while ((get_register (FTPRIREG) & 0x20) == 0) {
	}

	/*for(uint8_t i = 0; i < len; i++)
		printf("%02x ", data[i]);
	printf("\n");*/

	set_rf_mode (RF_STANDBY);
	set_rf_mode (RF_RECEIVER);
}

/**
 * Sends data if noise on medium is acceptable.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_send_with_cca (uint8_t * data, uint8_t len)
{
	uint8_t noise;
	do {
		noise = PHY_get_noise ();
	} while(noise > PHY_STORAGE.cca_noise_threshold_max || noise < PHY_STORAGE.cca_noise_threshold_min);
	PHY_send (data, len);
}

/**
 * Interrupt request 0 (IRQ0) occured.
 */
void HW_irq0_occurred (void)
{
}

/**
 * Interrupt request 1 (IRQ1) occured.
 */
void HW_irq1_occurred (void)
{
	if (PHY_STORAGE.mode == RF_RECEIVER) {
		do {
		PHY_STORAGE.signal_strength = PHY_get_noise();
		D_PHY printf("RSSI: %d\n", PHY_STORAGE.signal_strength);
		PORTAbits.RA6 = ~PORTAbits.RA6;
		uint8_t received_len = 0;
		uint8_t fifo_stat = get_register (FTXRXIREG);

		while (fifo_stat & 0x02) {
			HW_enable_data ();
			uint8_t readed_byte = HW_spi_get ();
			HW_disable_data ();
			PHY_STORAGE.received_packet[received_len++] = readed_byte;
			fifo_stat = get_register (FTXRXIREG);
		}
		if (received_len == 0)
			return;
		// zero termination is needed for string operations
		if (received_len < MAX_PHY_PAYLOAD_SIZE) {
			PHY_STORAGE.received_packet[received_len] = 0;
		}
		PHY_process_packet (PHY_STORAGE.received_packet + 1, received_len - 1);
		} while (get_register (FTXRXIREG) & 0x02);
	}
	else {
		//PHY_STORAGE.send_done = 1;
	}
}

/**
 * Ensures increase of counters and TX and RX buffer check.
 */
void HW_timeoccurred (void)
{
	PHY_timer_interrupt ();
}
