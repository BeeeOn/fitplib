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
#include "pan/debug.h"
#include "common/phy_layer/constants.h"
#include "phy.h"

using namespace std;

#define SYSGPIO "/sys/class/gpio"
#define PIN_IRQ0 "gpio274"
#define PIN_IRQ1 "gpio275"
#define PIN_RESET "gpio260"

// process synchronization during sending
bool waiting_send = false;
std::mutex m, mm, mmm;
std::condition_variable cv;

//uint8_t rssi[1000] = {0};

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

	// PAN coordinator setting
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
uint8_t get_cca_noise ();
uint8_t PHY_get_noise ();

/**
 * Implemented functions from hw layer.
 */
void HW_irq0_occurred (void);
void HW_irq1_occurred (void);

// SPI config
// http://linux-sunxi.org/SPIdev
// SPI bus 0, chipselect 0
static const char *devspi_config = "/dev/spidev32766.0";
static const char *devspi_config_new = "/dev/spidev0.0";
// SPI bus 0, chipselect 1
static const char *devspi_data = "/dev/spidev32766.1";
static const char *devspi_data_new = "/dev/spidev0.1";
static uint8_t mode = SPI_MODE_0;
const uint8_t spi_bits_per_word = 8;
const uint32_t spi_speed_hz = 1000000;
struct spi_ioc_transfer xfer[2];

static gboolean on_irq0_event (GIOChannel * channel, GIOCondition,
														 gpointer)
{
	GError *error = 0;
	gsize bytes_read = 0;
	const int buf_sz = 1024;
	gchar buf[buf_sz] = { };

	g_io_channel_seek_position (channel, 0, G_SEEK_SET, 0);
	g_io_channel_read_chars (channel, buf, buf_sz - 1, &bytes_read, &error);
	if (PHY_STORAGE.irq0_enabled) {
		HW_irq0_occurred ();
	}
	return 1;
}

static gboolean on_irq1_event (GIOChannel * channel, GIOCondition,
														 gpointer)
{
	GError *error = 0;
	gsize bytes_read = 0;
	const int buf_sz = 1024;
	gchar buf[buf_sz] = { };

	g_io_channel_seek_position (channel, 0, G_SEEK_SET, 0);
	g_io_channel_read_chars (channel, buf, buf_sz - 1, &bytes_read, &error);
	if (PHY_STORAGE.irq0_enabled) {
		HW_irq1_occurred ();
	}
	return 1;
}

int spi_open_config (void)
{
	int fd = open (devspi_config, O_RDWR);
	if (fd < 0) {
		fd = open (devspi_config_new, O_RDWR);
		if (fd < 0) {
			cerr << "Can't open SPI device!" << endl;
		}
	}
	return fd;
}

int spi_open_data (void)
{
	int fd = open (devspi_data, O_RDWR);

	if (fd < 0) {
		fd = open (devspi_data_new, O_RDWR);
		if (fd < 0) {
			cerr << "Can't open SPI device!" << endl;
		}
	}
	if (ioctl (fd, SPI_IOC_WR_MODE, &mode) < 0) {
		cerr << "Can't set spi mode!" << endl;
		return -1;
	}
	return fd;
}

int spi_bus_config (int)
{
	// length of command to write
	//xfer[0].len = 3;
	// keep CS activated
	xfer[0].cs_change = 0;
	// delay in us
	xfer[0].delay_usecs = 0;
	// speed
	xfer[0].speed_hz = spi_speed_hz;
	// bites per word 8
	xfer[0].bits_per_word = spi_bits_per_word;

	// length of command to write
	//xfer[0].len = 3;
	// keep CS activated
	xfer[1].cs_change = 0;
	// delay in us
	xfer[1].delay_usecs = 0;
	// speed
	xfer[1].speed_hz = spi_speed_hz;
	// bites per word 8
	xfer[1].bits_per_word = spi_bits_per_word;
	return 0;
}

void gpio_export (int pin)
{
	ofstream fd;
	fd.open (SYSGPIO "/export");
	// IRQ0
	fd << int (pin);
	fd.close ();
}

void gpio_set_direction (string gpio, string dir)
{
	ofstream fd;
	string dir_file = string (SYSGPIO "/") + gpio + string ("/direction");
	fd.open (dir_file.c_str ());
	fd << dir.c_str ();
	fd.close ();
}

void gpio_set_edge (string gpio, string edge)
{
	ofstream fd;
	string edge_file = string (SYSGPIO "/") + gpio + string ("/edge");
	fd.open (edge_file.c_str ());
	fd << edge.c_str ();
	fd.close ();
}

void gpio_set_value (string gpio, string value)
{
	ofstream fd;
	string value_file = string (SYSGPIO "/") + gpio + string ("/value");
	fd.open (value_file.c_str ());
	fd << value.c_str ();
	fd.close ();
}

bool gpio_get_value (string gpio)
{
	ifstream fd;
	char val[1];
	string value_file = string (SYSGPIO "/") + gpio + string ("/value");
	fd.open (value_file.c_str ());
	fd.read (val, 1);
	//fd << value.c_str();
	fd.close ();
	if (val[0] == '0')
		return false;
	return true;
}

void reset_MRF (void)
{
	gpio_set_value (PIN_RESET, "1");
	usleep (100);
	gpio_set_value (PIN_RESET, "0");
	// waiting for 10 ms, then MRF is ready
	usleep (10000);
}

void init_io (void)
{
	ofstream fd;
	GIOChannel *channel;
	GIOCondition cond;
	int irq0_fd, irq1_fd;

	// export gpio (default input direction)
	// IRQ0
	gpio_export (274);
	// IRQ1
	gpio_export (275);
	// RESET
	gpio_export (260);
	gpio_set_direction (PIN_RESET, "out");
	gpio_set_value (PIN_RESET, "0");

	// set gpio interrupt edge
	gpio_set_edge (PIN_IRQ0, "rising");
	gpio_set_edge (PIN_IRQ1, "rising");

	// init callbacks
	irq0_fd = open (SYSGPIO "/gpio274/value", O_RDONLY | O_NONBLOCK);
	channel = g_io_channel_unix_new (irq0_fd);
	cond = GIOCondition (G_IO_PRI);
	/*guint idIRQ0 = */ g_io_add_watch (channel, cond, on_irq0_event, 0);

	irq1_fd = open (SYSGPIO "/gpio275/value", O_RDONLY | O_NONBLOCK);
	channel = g_io_channel_unix_new (irq1_fd);
	cond = GIOCondition (G_IO_PRI);
	/*guint idIRQ1 = */ g_io_add_watch (channel, cond, on_irq1_event, 0);
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
 * Finds out number of channels.
 * @params band 		Band.
 * @params bitrate 	Bitrate.
 * @return Returns number of channels for band and bitrate.
 */
uint8_t channel_amount (uint8_t band, uint8_t bitrate)
{
	if ((band == BAND_863 || band == BAND_863_C950)
			&& (bitrate == DATA_RATE_100 || bitrate == DATA_RATE_200)) {
		return 25;
	} else {
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
 * @param bitrate Bitrate.
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
 * @param band 		Band.
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
 * Accesses control register of MRF89XA.
 * @param address Address of register.
 * @param value   Register setting.
 */
void set_register (const uint8_t address, const uint8_t value)
{
	int fd;
	char wr_buf[2];

	fd = spi_open_config ();
	spi_bus_config (fd);

	wr_buf[0] = address;
	wr_buf[1] = value;

	xfer[0].tx_buf = (unsigned long) wr_buf;
	xfer[0].len = 2;
	xfer[0].rx_buf = 0;
	if (ioctl (fd, SPI_IOC_MESSAGE (1), xfer) < 0) {
		D_PHY printf ("set_register(): Can't write register!\n");
		return;
	}
	close (fd);
}

/**
 * Reads back register value.
 * @param 	address Address of register.
 * @return  Return register value.
 */
uint8_t get_register (uint8_t address)
{
	int fd;
	char wr_buf[1];
	char rd_buf[1];

	std::lock_guard < std::mutex > lock (mmm);

	fd = spi_open_config ();
	spi_bus_config (fd);

	wr_buf[0] = (address | 0x40) & 0x7e;
	xfer[0].tx_buf = (unsigned long) wr_buf;
	xfer[0].len = 1;
	xfer[0].rx_buf = 0;
	xfer[1].rx_buf = (unsigned long) rd_buf;
	xfer[1].len = 1;

	if (ioctl (fd, SPI_IOC_MESSAGE (2), xfer) < 0) {
		cerr << "Can't read register!" << endl;
		return 0;
	}

	close (fd);
	return (uint8_t) rd_buf[0];
}

/**
 * Reads FIFO.
 * @return Returns read char.
 */
uint8_t read_fifo (void)
{
	int fd = 0;

	char rd_buf[1];
	fd = spi_open_data ();
	spi_bus_config (fd);
	if (read (fd, rd_buf, 1) != 1) {
		cerr << "Read error" << endl;
		return -1;
	}
	close (fd);
	return rd_buf[0];
}

/**
 * Fills FIFO.
 * @param data Data to be sent to FIFO.
 */
void write_fifo (const uint8_t data)
{
	int fd;
	uint8_t data_nostack = data;

	fd = spi_open_data ();
	spi_bus_config (fd);

	xfer[0].tx_buf = (unsigned long) &data_nostack;
	xfer[0].len = 1;
	xfer[0].rx_buf = 0;
	if (ioctl (fd, SPI_IOC_MESSAGE (1), xfer) < 0) {
		D_PHY printf ("Can't write register!\n");
		return;
	}
	close (fd);
}

/**
 * Sets MRF89XA transceiver operating mode to sleep, transmit, receive or standby.
 * @param mode Mode.
 */
void set_rf_mode (uint8_t mode)
{
	if ((mode == RF_TRANSMITTER) || (mode == RF_RECEIVER) ||
			(mode == RF_SYNTHESIZER) || (mode == RF_STANDBY) || (mode == RF_SLEEP)) {
		set_register (GCONREG, (GCONREG_SET & 0x1F) | mode);
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
	D_PHY printf ("channel %d, band %d, bitrate %d\n", channel, band, bitrate);
	// R, P, S register setting
	set_register (R1CNTREG, r_value ());
	set_register (P1CNTREG, p_value (band, channel, bitrate));
	set_register (S1CNTREG, s_value (band, channel, bitrate));

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
	set_register (TXPARAMREG, 0xF0 | (power << 1));
	PHY_STORAGE.power = power;
	return true;
}

/**
 * Synthetises RF.
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
 * Reads RSSI value.
 * @return Returns RSSI value.
 */
uint8_t get_cca_noise ()
{
	return get_register (RSTSREG) >> 1;
}

/**
 * Reads stored RSSI value.
 * @return Returns stored RSSI value.
 */
uint8_t PHY_get_measured_noise()
{
	return PHY_STORAGE.signal_strength;
}

/*
 * Initializes HW layer.
 */
void HW_init (void)
{
	init_io ();
	reset_MRF ();
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
	//cout << "in HW_irq1_occurred\n";
	if (PHY_STORAGE.mode == RF_RECEIVER) {
		PHY_STORAGE.signal_strength = PHY_get_noise();
		//D_PHY printf("RSSI: %d\n", PHY_STORAGE.signal_strength);
		uint8_t received_len = 0;
		//D_PHY printf ("RF_RECEIVER\n");
		{
			std::lock_guard < std::mutex > lock (mm);
			PHY_STORAGE.irq1_enabled = false;
			PHY_STORAGE.irq0_enabled = false;

			uint8_t fifo_stat = get_register (FTXRXIREG);

			while (fifo_stat & 0x02) {
				PHY_STORAGE.received_packet[received_len++] = read_fifo ();
				fifo_stat = get_register (FTXRXIREG);
			}

			/*for(uint8_t i = 0; i < received_len; i++)
				printf("%02x ", PHY_STORAGE.received_packet[i]);
			printf("\n");*/
			PHY_STORAGE.irq1_enabled = true;
			PHY_STORAGE.irq0_enabled = true;
		}

		if (received_len == 0 || received_len - 1 != PHY_STORAGE.received_packet[0]) {
			//cout << "empty\n ";
			return;
		}
		// send data without the first byte
		PHY_process_packet (PHY_STORAGE.received_packet + 1, received_len - 1);
	}
	else {
		D_PHY printf("HW_irq1occurred(): NOT in RF_RECEIVER mode\n");
	}
}

/*
 * Interrupt waiting thread.
 */
void irq_interrupt_deamon_f ()
{
	PHY_STORAGE.gloop = g_main_loop_new (0, 0);
	g_main_loop_run (PHY_STORAGE.gloop);
}


void timer_interrupt_generator_f ()
{
	while (!PHY_STORAGE.terminate_timer) {
		std::this_thread::sleep_for (std::chrono::milliseconds (50/*300*/));

		PHY_timer_interrupt ();
	}
}

/**
 * Initializes physical layer.
 * @param phy_params Parameters of physical layer.
 */
void PHY_init (struct PHY_init_t* phy_params)
{
	HW_init ();
	PHY_STORAGE.irq_interrupt_deamon = std::thread (irq_interrupt_deamon_f);
	PHY_STORAGE.timer_interrupt_generator = std::thread (timer_interrupt_generator_f);

	PHY_STORAGE.cca_noise_threshold_max = phy_params->cca_noise_threshold_max;
	PHY_STORAGE.cca_noise_threshold_min = phy_params->cca_noise_threshold_min;

	D_PHY printf ("channel %d band %d bitrate %d power %d\n", phy_params->channel,
					phy_params->band, phy_params->bitrate, phy_params->power);

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

	/*
	for (uint8_t i = 0; i <= 31; i++) {
		uint8_t reg_val = get_register (i << 1);
	}*/

	send_reload_radio ();
	PHY_STORAGE.irq0_enabled = true;
	PHY_STORAGE.irq1_enabled = true;
}


void PHY_stop ()
{
	g_main_loop_quit (PHY_STORAGE.gloop);
	PHY_STORAGE.terminate_timer = true;
	// waiting for termination of next threads
	PHY_STORAGE.irq_interrupt_deamon.join ();
	PHY_STORAGE.timer_interrupt_generator.join ();
}

/**
 * Sends data.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_send (const uint8_t * data, uint8_t len)
{
	D_PHY printf("PHY_send()\n");
	PHY_STORAGE.irq1_enabled = false;
	PHY_STORAGE.irq0_enabled = false;

	set_rf_mode (RF_STANDBY);
	set_register (FTXRXIREG, FTXRXIREG_SET | 0x01);
	write_fifo (len);
	for (int i = 0; i < len; i++) {
		write_fifo (data[i]);
	}
	set_rf_mode (RF_TRANSMITTER);

	PHY_STORAGE.irq1_enabled = true;
	PHY_STORAGE.irq0_enabled = true;

	uint32_t tout = 0;
	while ((get_register (FTPRIREG) & 0x20) == 0) {
		usleep (500);
		tout++;
		if (tout > 4000)
			break;
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
	std::lock_guard < std::mutex > lock (mm);
	do {
		noise = PHY_get_noise ();
	} while(noise > PHY_STORAGE.cca_noise_threshold_max || noise < PHY_STORAGE.cca_noise_threshold_min);
	PHY_send (data, len);
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
	if (channel == PHY_STORAGE.channel)
		return true;
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
	D_PHY printf ("Channel: %d\n", PHY_STORAGE.channel);
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
