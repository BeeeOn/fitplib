/**
* @file link.c
*/

#include "link.h"
#include "log.h"

/*! bit mask of data transfer from coordinator to end device */
#define LINK_COORD_TO_ED					0x20
/*! bit mask of data transfer from end device to coordinator */
#define LINK_ED_TO_COORD					0x10
/*! bit mask of device busyness */
#define LINK_BUSY									0x08
/*! RX buffer size */
#define LINK_RX_BUFFER_SIZE 4
/*! TX buffer size */
#define LINK_TX_BUFFER_SIZE 4
/*! maximum number of channels */
#define MAX_CHANNEL 							31
/*! broadcast address */
#define LINK_COORD_ALL 						0xfc

/** @enum LINK_packet_type
 * Packet types.
 */
enum LINK_packet_type_coord {
	LINK_DATA_TYPE = 0,					/**< DATA. */
	LINK_COMMIT_TYPE = 1,				/**< COMMIT. */
	LINK_ACK_TYPE = 2,					/**< ACK. */
	LINK_COMMIT_ACK_TYPE = 3		/**< COMMIT ACK. */
};

/**
 * Packet states during four-way handshake.
 */
enum LINK_tx_packet_state_coord {
	DATA_SENT = 0,		/**< DATA were sent. */
	COMMIT_SENT = 1		/**< COMMIT was sent. */
};

/**
 * Structure for coordinator RX buffer record (used during four-way handshake).
 */
typedef struct {
	uint8_t data[MAX_PHY_PAYLOAD_SIZE];	/**< Data. */
	uint8_t address_type:1;							/**< Address type: 0 - coordinator, 1 - end device. */
	uint8_t empty:1;										/**< Flag if record is empty. */
	uint8_t len:6;											/**< Data length. */
	uint8_t expiration_time;						/**< Packet expiration time. */
	uint8_t transfer_type;							/**< Transfer type. */
	union {
		uint8_t coord;										/**< Coordinator address. */
		uint8_t ed[EDID_LENGTH];					/**< End device address. */
	} address;
} LINK_rx_buffer_record_t;

/**
 * Structure for end device RX buffer record (used during four-way handshake).
 */
typedef struct {
	uint8_t data[MAX_PHY_PAYLOAD_SIZE];	/**< Data. */
	uint8_t empty:1;										/**< Flag if record is empty. */
	uint8_t len:6;											/**< Data length. */
	uint8_t expiration_time;						/**< Packet expiration time. */
	uint8_t transfer_type;							/**< Transfer type. */
} LINK_rx_buffer_record_ed_t;

/**
 * Structure for coordinator TX buffer record (used during four-way handshake).
 */
typedef struct {
	uint8_t data[MAX_PHY_PAYLOAD_SIZE];	/**< Data. */
	uint8_t address_type:1;							/**< Address type: 0 - coordinator, 1 - end device. */
	uint8_t empty:1;										/**< Flag if record is empty. */
	uint8_t len:6;											/**< Data length. */
	uint8_t state:1;										/**< Packet states: 0 - DATA were sent, 1 - COMMIT was sent. */
	uint8_t expiration_time;						/**< Packet expiration time. */
	uint8_t transmits_to_error;					/**< Maximum number of packet retransmissions. */
	uint8_t transfer_type;							/**< Transfer type. */
	union {
		uint8_t coord;										/**< Coordinator address. */
		uint8_t ed[EDID_LENGTH];					/**< End device address. */
	} address;
} LINK_tx_buffer_record_t;

/**
 * Structure for end device TX buffer record (used during four-way handshake).
 */
typedef struct {
	uint8_t data[MAX_PHY_PAYLOAD_SIZE];	/**< Data. */
	uint8_t empty:1;										/**< Flag if record is empty. */
	uint8_t len:6;											/**< Data length. */
	uint8_t state:1;										/**< Packet states: 0 - DATA were sent, 1 - COMMIT was sent. */
	uint8_t expiration_time;						/**< Packet expiration time. */
	uint8_t transmits_to_error;					/**< Maximum number of packet retransmissions. */
	uint8_t transfer_type;							/**< Transfer type. */
} LINK_tx_buffer_record_ed_t;

/**
 * Structure for link layer.
 */
struct LINK_storage_t {
	uint8_t tx_max_retries;																		/**< Maximum number of packet retransmissions. */
	uint8_t timer_counter;																		/**< Timer for packet expiration time setting. */
	LINK_rx_buffer_record_t rx_buffer[LINK_RX_BUFFER_SIZE];		/**< Array of RX buffer records for coordinator. */
	LINK_tx_buffer_record_t tx_buffer[LINK_TX_BUFFER_SIZE];		/**< Array of TX buffer records for coordinator. */
	LINK_rx_buffer_record_ed_t ed_rx_buffer;									/**< Array of RX buffer records for end device. */
	LINK_tx_buffer_record_ed_t ed_tx_buffer;									/**< Array of TX buffer records for end device. */
	bool link_ack_join_received;															/**< Flag if ACK JOIN packet was received. */
	uint8_t ack_join_address[MAX_COORD];											/**< Array of coordinators that sent ACK JOIN packet. */
} LINK_STORAGE;

extern void delay_ms (uint16_t maximum);
uint8_t LINK_cid_mask (uint8_t address);

/**
 * Checks if end device identifier equals zeros.
 * @param edid End device ID.
 * @return Returns true if end device ID equals zeros, false otherwise.
 */
bool zero_address (uint8_t* edid)
{
	for (uint8_t i = 0; i < EDID_LENGTH; i++) {
		if (edid[i] != 0)
			return false;
	}
	return true;
}

/**
 * Compares two arrays (4 B).
 * @param array1 	The first array.
 * @param array2 	The second array.
 * @return Returns true if arrays are equal, false otherwise.
 */
bool array_cmp (uint8_t* array1, uint8_t* array2)
{
	for (uint8_t i = 0; i < EDID_LENGTH; i++) {
		if (array1[i] != array2[i])
			return false;
	}
	return true;
}

/**
 * Copies source array to destination array.
 * @param src 	Source array.
 * @param dst 	Destination array.
 * @param size 	Array size.
 */
void array_copy (uint8_t* src, uint8_t* dst, uint8_t size)
{
	for (uint8_t i = 0; i < size; i++) {
		dst[i] = src[i];
	}
}

/**
 * Searches a free index in TX buffer.
 * @return Returns a free index in TX buffer or invalid index in case of
 * full TX buffer.
 */
uint8_t get_free_index_tx ()
{
	uint8_t free_index = LINK_TX_BUFFER_SIZE;
	for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
		if (LINK_STORAGE.tx_buffer[i].empty) {
			free_index = i;
			break;
		}
	}
	return free_index;
}

/**
 * Searches a free index in RX buffer.
 * @return Returns a free index in RX buffer or invalid index in case of
 * full RX buffer.
 */
uint8_t get_free_index_rx ()
{
	uint8_t free_index = LINK_RX_BUFFER_SIZE;
	for (uint8_t i = 0; i < LINK_RX_BUFFER_SIZE; i++) {
		if (LINK_STORAGE.rx_buffer[i].empty) {
			free_index = i;
			break;
		}
	}
	return free_index;
}

/**
 * Generates packet header.
 * @param header									 	Array for link packet header.
 * @param as_ed 										True if device sends packet as end device, false otherwise.
 * @param to_ed 										True if device sends packet to end device, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 * @param packet_type 							Packet type.
 * @param transfer_type 						Transfer type on link layer.
 */
void gen_header (uint8_t * header, bool as_ed, bool to_ed,
								 uint8_t * address, uint8_t packet_type, uint8_t transfer_type)
{
	if (as_ed && to_ed) {
		// communication between EDs is not allowed
		return;
	}
	uint8_t header_index = 0;
	// packet type (2 b), dst (1b), src (1b), transfer type (4 b)
	header[header_index++] =
		packet_type << 6 | (to_ed ? 1 : 0) << 5 | (as_ed ? 1 : 0) << 4 | (transfer_type & 0x0f);

	// NID
	for (uint8_t i = 0; i < 4; i++) {
		header[header_index++] = GLOBAL_STORAGE.nid[i];
	}

	// data sending to ED
	if (to_ed) {
		// ED address - dst
		for (uint8_t i = 0; i < EDID_LENGTH; i++)
			header[header_index++] = address[i];
		// COORD address - src
		header[header_index++] = GLOBAL_STORAGE.cid;
	}
	// data sending to COORD
	else {
		if (as_ed) {
			// COORD address - dst
			if (transfer_type == LINK_DATA_BROADCAST) {
				// ED can send packet to all COORDs
				header[header_index++] = LINK_COORD_ALL;
			}
			else {
				// or to its parent
				header[header_index++] = GLOBAL_STORAGE.parent_cid;
			}
			// ED address - src
			for (uint8_t i = 0; i < EDID_LENGTH; i++)
				header[header_index++] = GLOBAL_STORAGE.edid[i];
		}
		else {
			// COORD address - dst
			header[header_index++] = *address;
			// COORD address - src
			header[header_index++] = GLOBAL_STORAGE.cid;
		}
	}
}

/**
 * Sends DATA.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 * @param payload										Payload.
 * @param len												Payload length.
 * @param transfer_type 						Transfer type on link layer.
 */
void send_data (bool as_ed, bool to_ed, uint8_t* address, uint8_t* payload,
								uint8_t len, uint8_t transfer_type)
{
	uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
	gen_header (packet, as_ed, to_ed, address, LINK_DATA_TYPE,
							transfer_type);
	D_LINK printf("send_data()\n");
	/*for (size_t i = 0; i < 10; i++) {
		printf("%02x ", packet[i]);
	}*/
	// size of link header
	uint8_t packet_index = LINK_HEADER_SIZE;
	for (uint8_t i = 0; i < len; i++)
		packet[packet_index++] = payload[i];
	PHY_send_with_cca (packet, packet_index);
}

/**
 * Sends ACK.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 * @param transfer_type 						Transfer type on link layer.
 */
void send_ack (bool as_ed, bool to_ed, uint8_t * address, uint8_t transfer_type)
{
	uint8_t ack_packet[LINK_HEADER_SIZE];
	gen_header (ack_packet, as_ed, to_ed, address, LINK_ACK_TYPE,
							transfer_type);
	D_LINK printf ("send_ack()\n");
	PHY_send_with_cca (ack_packet, LINK_HEADER_SIZE);
}

/**
 * Sends COMMIT.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 */
void send_commit (bool as_ed, bool to_ed, uint8_t * address)
{
	uint8_t commit_packet[LINK_HEADER_SIZE];
	gen_header (commit_packet, as_ed, to_ed, address, LINK_COMMIT_TYPE,
							LINK_DATA_HS4);
	D_LINK printf ("send_commit()\n");
	PHY_send_with_cca (commit_packet, LINK_HEADER_SIZE);
}

/**
 * Sends COMMIT ACK.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 */
void send_commit_ack (bool as_ed, bool to_ed, uint8_t* address)
{
	uint8_t commit_ack_packet[LINK_HEADER_SIZE];
	gen_header (commit_ack_packet, as_ed, to_ed, address, LINK_COMMIT_ACK_TYPE,
							LINK_DATA_HS4);
	D_LINK printf ("send_commit_ack()\n");
	PHY_send_with_cca (commit_ack_packet, LINK_HEADER_SIZE);
}

/**
 * Sends BUSY ACK.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 */
void send_busy_ack (bool as_ed, bool to_ed, uint8_t* address)
{
	uint8_t ack_packet[LINK_HEADER_SIZE];
	gen_header (ack_packet, as_ed, to_ed, address, LINK_ACK_TYPE, LINK_BUSY);
	D_LINK printf ("send_busy_ack()\n");
	PHY_send_with_cca (ack_packet, LINK_HEADER_SIZE);
}

/**
 * Processes packet for end device.
 * @param data 	Data.
 * @param len 	Data length.
 * @return Returns false if packet is BUSY ACK, if it has invalid network header
 * 				 or if network reinitialization is being performed, true otherwise.
 */
bool ed_process_packet (uint8_t* data, uint8_t len)
{
	uint8_t packet_type = data[0] >> 6;
	uint8_t transfer_type = data[0] & 0x0f;
	D_LINK printf ("packet type: %02x, transfer type: %02x\n", packet_type, transfer_type);

		// processing of ACK packet
		if (packet_type == LINK_ACK_TYPE) {
			if (!LINK_STORAGE.ed_tx_buffer.empty) {
				// it is not BUSY ACK packet, switch state and send COMMIT packet
				if (transfer_type != LINK_BUSY) {
					LINK_STORAGE.ed_tx_buffer.state = COMMIT_SENT;
					LINK_STORAGE.ed_tx_buffer.transmits_to_error = LINK_STORAGE.tx_max_retries;
					LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 2;
					send_commit(true, false, &data[9]);
					return true;
				}
				else {
					// it is BUSY ACK packet, set retransmission count again and set
					// longer timeout (receiving device is busy)
					LINK_STORAGE.ed_tx_buffer.transmits_to_error = LINK_STORAGE.tx_max_retries;
					LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 3;
					return false;
				}
			}
		}
		// processing of COMMIT ACK packet
		else if (packet_type == LINK_COMMIT_ACK_TYPE) {
				// data were successfully delivered
				LINK_STORAGE.ed_tx_buffer.empty = 1;
				LINK_notify_send_done ();
				return true;
		}
		// processing of DATA packet
		else if (packet_type == LINK_DATA_TYPE) {
			if (transfer_type == LINK_DATA_WITHOUT_ACK || transfer_type == LINK_DATA_BROADCAST) {
				return LINK_process_packet (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
			}
			else if (transfer_type == LINK_DATA_HS4) {
				if(LINK_STORAGE.ed_rx_buffer.empty) {
					uint8_t index = 0;
					for (uint8_t data_index = 0;
							 data_index < len && data_index < MAX_PHY_PAYLOAD_SIZE; data_index++)
						LINK_STORAGE.ed_rx_buffer.data[index++] = data[data_index];
					LINK_STORAGE.ed_rx_buffer.len = index;
					LINK_STORAGE.ed_rx_buffer.transfer_type = transfer_type;
					LINK_STORAGE.ed_rx_buffer.empty = 0;
					send_ack (true, false, &data[9], LINK_STORAGE.ed_rx_buffer.transfer_type);
					return true;
				}
				// in case of multiple receiving of DATA packet send ACK
				// packet (DATA packet was not received by sender)
				send_ack (true, false, &data[9], LINK_STORAGE.ed_rx_buffer.transfer_type);
				return true;
			}
		}
		// processing of COMMIT packet
		else if (packet_type == LINK_COMMIT_TYPE) {
			if (!LINK_STORAGE.ed_rx_buffer.empty) {
					// packet can be accepted
					LINK_STORAGE.ed_rx_buffer.empty = 1;
					bool result = LINK_process_packet (LINK_STORAGE.ed_rx_buffer.data + LINK_HEADER_SIZE,
															 LINK_STORAGE.ed_rx_buffer.len - LINK_HEADER_SIZE);
					send_commit_ack (true, false, &data[9]);
					return result;
			}
			// in case of multiple receiving of COMMIT packet send COMMIT ACK
			// packet (COMMIT packet was not received by sender)
			send_commit_ack (true, false, &data[9]);
			return true;
		}
		return true;
}

/**
 * Processes packet for coordinator and routes it towards destination.
 * @param data 	Data.
 * @param len 	Data length.
 * @return Returns false if packet is BUSY ACK,
 *				 if the packet has been already stored in RX buffer
 *				 or if the packet is not successfully sent, true otherwise.
 */
bool router_process_packet (uint8_t* data, uint8_t len)
{
	uint8_t packet_type = data[0] >> 6;
	uint8_t transfer_type = data[0] & 0x0f;
	D_LINK printf("router_process_packet()\n");
	// processing of ACK packet
	if (packet_type == LINK_ACK_TYPE) {
		D_LINK printf ("ACK\n");
		if (data[0] & LINK_ED_TO_COORD) {
			D_LINK printf ("R: ACK to COORD\n");
			for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
				// if some DATA message for ED is in TX buffer and address is the same,
				// COMMIT message can be sent
				if (!LINK_STORAGE.tx_buffer[i].empty) {
					if (LINK_STORAGE.tx_buffer[i].address_type == 1
							&& array_cmp (LINK_STORAGE.tx_buffer[i].address.ed, data + 6)) {
							// it is not BUSY ACK packet, switch state and send COMMIT packet
							if (transfer_type != LINK_BUSY) {
								LINK_STORAGE.tx_buffer[i].state = COMMIT_SENT;;
								LINK_STORAGE.tx_buffer[i].transmits_to_error = LINK_STORAGE.tx_max_retries;
								LINK_STORAGE.tx_buffer[i].expiration_time = LINK_STORAGE.timer_counter + 2;
								D_LINK printf ("S: COMMIT to ED\n");
								send_commit (false, true, LINK_STORAGE.tx_buffer[i].address.ed);
								break;
							}
							else {
								// it is BUSY ACK packet, set retransmission count again and set
								// longer timeout (receiving device is busy)
								LINK_STORAGE.tx_buffer[i].transmits_to_error = LINK_STORAGE.tx_max_retries;
								LINK_STORAGE.tx_buffer[i].expiration_time = LINK_STORAGE.timer_counter + 3;
								return false;
							}
					}
				}
			}
		}
		else {
			for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
				// if some DATA message for COORD is in buffer and address is the same,
				// COMMIT packet can be sent
				if (!LINK_STORAGE.tx_buffer[i].empty) {
					if (LINK_STORAGE.tx_buffer[i].address_type == 0
							&& LINK_STORAGE.tx_buffer[i].address.coord == data[6]) {
							// it is not BUSY ACK packet, switch state and send COMMIT packet
							if (transfer_type != LINK_BUSY) {
								LINK_STORAGE.tx_buffer[i].state = COMMIT_SENT;;
								LINK_STORAGE.tx_buffer[i].transmits_to_error = LINK_STORAGE.tx_max_retries;
								LINK_STORAGE.tx_buffer[i].expiration_time = LINK_STORAGE.timer_counter + 2;
								if(data[0] & LINK_COORD_TO_ED) {
									D_LINK printf ("R: ACK to ED\n");
									D_LINK printf ("S: COMMIT to COORD\n");
									send_commit (true, false,
															&LINK_STORAGE.tx_buffer[i].address.coord);
									break;
								}
								else {
									D_LINK printf ("R: ACK to COORD\n");
									D_LINK printf ("S: COMMIT to COORD\n");
									send_commit (false, false,
															&LINK_STORAGE.tx_buffer[i].address.coord);
									break;
								}
							}
							else {
								// it is BUSY ACK packet, set retransmission count again and set
								// longer timeout (receiving device is busy)
								LINK_STORAGE.tx_buffer[i].transmits_to_error = LINK_STORAGE.tx_max_retries;
								LINK_STORAGE.tx_buffer[i].expiration_time = LINK_STORAGE.timer_counter + 3;
								return false;
							}
					}
				}
			}
		}
	}
	// processing of COMMIT ACK packet
	else if (packet_type == LINK_COMMIT_ACK_TYPE) {
		D_LINK printf ("COMMIT ACK\n");
		if (data[0] & LINK_ED_TO_COORD) {
			D_LINK printf ("R: COMMIT ACK to COORD\n");
			for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
				if (!LINK_STORAGE.tx_buffer[i].empty) {
					if (LINK_STORAGE.tx_buffer[i].address_type == 1
							&& array_cmp (LINK_STORAGE.tx_buffer[i].address.ed, data + 6)) {
							// packet can be accepted
							LINK_STORAGE.tx_buffer[i].empty = 1;
							break;
					}
				}
			}
		}
		else {
			for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
				if (!LINK_STORAGE.tx_buffer[i].empty) {
					if (LINK_STORAGE.tx_buffer[i].address_type == 0
							&& LINK_STORAGE.tx_buffer[i].address.coord == data[6]) {
							// packet can be accepted
							LINK_STORAGE.tx_buffer[i].empty = 1;
							D_LINK printf ("R: COMMIT ACK to ED or COORD\n");
							LINK_notify_send_done ();
							break;
					}
				}
			}
		}
	}
	// processing of DATA packet
	else if (packet_type == LINK_DATA_TYPE) {
		D_LINK printf ("DATA\n");
		if (transfer_type == LINK_DATA_WITHOUT_ACK) {
			return LINK_route (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE, transfer_type);
		}
		else if (transfer_type == LINK_DATA_HS4) {
			for(uint8_t i = 0; i < LINK_RX_BUFFER_SIZE; i++) {
				// some DATA are in RX buffer, verify if received packet has not been
				// already stored in RX buffer
				if(!LINK_STORAGE.rx_buffer[i].empty) {
					if (LINK_STORAGE.rx_buffer[i].address_type == 1) {
						if (array_cmp (LINK_STORAGE.rx_buffer[i].address.ed, data + 6)) {
							D_LINK printf("ED -> COORD: DATA has been already stored!\n");
							send_ack (false, LINK_STORAGE.rx_buffer[i].address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
							return false;
						}
					}
					else {
						if ((LINK_STORAGE.rx_buffer[i].address.coord == data[6])) {
							if (data[0] & LINK_COORD_TO_ED) {
								D_LINK printf("COORD -> ED: DATA has been already stored!\n");
								send_ack (true, LINK_STORAGE.rx_buffer[i].address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
								return false;
							}
							else {
								D_LINK printf("COORD -> COORD: DATA has been already stored!\n");
								send_ack (false, LINK_STORAGE.rx_buffer[i].address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
								return false;
							}
						}
					}
				}
			}
			uint8_t sender_address_type = (data[0] & LINK_ED_TO_COORD) ? 1 : 0;
			uint8_t empty_index = get_free_index_rx ();

			if (empty_index < LINK_RX_BUFFER_SIZE) {
				// RX buffer is not full, save information about DATA packet
				uint8_t data_index = 0;
				for (; data_index < len && data_index < MAX_PHY_PAYLOAD_SIZE; data_index++) {
					LINK_STORAGE.rx_buffer[empty_index].data[data_index] = data[data_index];
				}
				LINK_STORAGE.rx_buffer[empty_index].len = data_index;
				LINK_STORAGE.rx_buffer[empty_index].transfer_type = transfer_type;
				LINK_STORAGE.rx_buffer[empty_index].empty = 0;
				LINK_STORAGE.rx_buffer[empty_index].address_type = sender_address_type;
				if (sender_address_type) {
					for (uint8_t i = 0; i < EDID_LENGTH; i++)
						LINK_STORAGE.rx_buffer[empty_index].address.ed[i] = data[6 + i];
				}
				else {
					LINK_STORAGE.rx_buffer[empty_index].address.coord = LINK_cid_mask (data[6]);
				}
			}
			else {
				// RX buffer is full, send BUSY ACK packet
				send_busy_ack (false, sender_address_type, data + 6);
				return true;
			}
			if (sender_address_type) {
				D_LINK printf("R: DATA to COORD\n");
				for (uint8_t i = 0; i < LINK_RX_BUFFER_SIZE; i++) {
					// if some DATA message for COORD from ED is in RX buffer
					// and address is the same, ACK packet can be sent
					if (!LINK_STORAGE.rx_buffer[i].empty) {
						if (LINK_STORAGE.rx_buffer[i].address_type == 1
								&& array_cmp (LINK_STORAGE.rx_buffer[i].address.ed, data + 6)) {
							D_LINK printf("S: ACK to ED\n");
							send_ack (false, sender_address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
						}
					}
				}
			}
			else {
				for (uint8_t i = 0; i < LINK_RX_BUFFER_SIZE; i++) {
					// if some DATA message for ED or COORD from COORD is in RX buffer
					// and address is the same, ACK packet can be sent
					if (!LINK_STORAGE.rx_buffer[i].empty) {
						if (LINK_STORAGE.rx_buffer[i].address_type == 0
								&& LINK_STORAGE.rx_buffer[i].address.coord == data[6]) {
							if (data[0] & LINK_COORD_TO_ED) {
								D_LINK printf("R: DATA to ED\n");
								D_LINK printf("S: ACK to COORD\n");
								send_ack (true, sender_address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
							}
							else {
								D_LINK printf("R: DATA to COORD\n");
								D_LINK printf("S:ACK to COORD\n");
								send_ack (false, sender_address_type, data + 6, LINK_STORAGE.rx_buffer[i].transfer_type);
							}
						}
					}
				}
			}
		}
	}
	// processing of COMMIT packet
	else if (packet_type == LINK_COMMIT_TYPE) {
		D_LINK printf ("COMMIT\n");
		if (data[0] & LINK_ED_TO_COORD) {
			D_LINK printf ("R: COMMIT to COORD\n");
			uint8_t i = 0;
			for (; i < LINK_RX_BUFFER_SIZE; i++) {
				// if some DATA message for COORD from ED is in RX buffer
				// and address is the same, COMMIT ACK packet can be sent
				if (!LINK_STORAGE.rx_buffer[i].empty) {
					if (LINK_STORAGE.rx_buffer[i].address_type == 1
							&& array_cmp (LINK_STORAGE.rx_buffer[i].address.ed, data + 6)) {
						D_LINK printf("S: COMMIT ACK to ED\n");
						send_commit_ack (false, data[0] & LINK_ED_TO_COORD, data + 6);
						bool result = LINK_route (LINK_STORAGE.rx_buffer[i].data + LINK_HEADER_SIZE,
																				 LINK_STORAGE.rx_buffer[i].len - LINK_HEADER_SIZE, LINK_STORAGE.rx_buffer[i].transfer_type);
						LINK_STORAGE.rx_buffer[i].empty = 1;
						return result;
					}
				}
			}
			// in case of multiple receiving of COMMIT packet send COMMIT ACK
			// packet (COMMIT packet was not received by sender)
			send_commit_ack (false, data[0] & LINK_ED_TO_COORD, data + 6);
		}
		else {
			uint8_t i = 0;
			for (; i < LINK_RX_BUFFER_SIZE; i++) {
				if (!LINK_STORAGE.rx_buffer[i].empty) {
					// if some DATA message for ED or COORD from COORD is in RX buffer
					// and address is the same, COMMIT ACK packet can be sent
					if (LINK_STORAGE.rx_buffer[i].address_type == 0
							&& LINK_STORAGE.rx_buffer[i].address.coord == LINK_cid_mask (data[6])) {
						if(data[0] & LINK_COORD_TO_ED) {
							D_LINK printf ("R: COMMIT to ED\n");
							D_LINK printf ("S: COMMIT ACK to COORD\n");
							send_commit_ack (true, data[0] & LINK_ED_TO_COORD, data + 6);
						}
						else {
							D_LINK printf ("R: COMMIT to COORD\n");
							D_LINK printf ("S: COMMIT ACK to COORD\n");
							send_commit_ack (false, data[0] & LINK_ED_TO_COORD, data + 6);
						}
						bool result = LINK_route (LINK_STORAGE.rx_buffer[i].data + LINK_HEADER_SIZE,
																				 LINK_STORAGE.rx_buffer[i].len - LINK_HEADER_SIZE, LINK_STORAGE.rx_buffer[i].transfer_type);
						LINK_STORAGE.rx_buffer[i].empty = 1;
						return result;
					}
				}
			}
			// in case of multiple receiving of COMMIT packet send COMMIT ACK
			// packet (COMMIT packet was not received by sender)
			if(data[0] & LINK_COORD_TO_ED) {
				send_commit_ack (true, data[0] & LINK_ED_TO_COORD, data + 6);
			}
			else {
				send_commit_ack (false, data[0] & LINK_ED_TO_COORD, data + 6);
			}
		}
	}
	return true;
}

/**
 * Checks TX and RX buffers periodically.
 * Ensures packet retransmission during four-way handshake and detects
 * unsuccessful four-way handshake.
 */
void check_buffers_state ()
{
	// check ED buffers
	if ((!LINK_STORAGE.ed_tx_buffer.empty)
			&& LINK_STORAGE.ed_tx_buffer.expiration_time == LINK_STORAGE.timer_counter) {
		if ((LINK_STORAGE.ed_tx_buffer.transmits_to_error--) == 0) {
				// multiple unsuccessful packet sending, network reinitialization starts
				LINK_error_handler_ed (LINK_STORAGE.ed_tx_buffer.data + LINK_HEADER_SIZE,
					LINK_STORAGE.ed_tx_buffer.len - LINK_HEADER_SIZE);
				LINK_STORAGE.ed_tx_buffer.empty = 1;
		}
		else {
			// try to resend packet
			if (LINK_STORAGE.ed_tx_buffer.state) {
				D_LINK printf("COMMIT again!\n");
				send_commit (true, false, NULL);
			}
			else {
				D_LINK printf("DATA again!\n");
				send_data (true, false, NULL, LINK_STORAGE.ed_tx_buffer.data,
					LINK_STORAGE.ed_tx_buffer.len, LINK_STORAGE.ed_tx_buffer.transfer_type);
			}
			// reschedule the transmission
			LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 2;
		}
	}
	// check COORD buffers
	for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
		if ((!LINK_STORAGE.tx_buffer[i].empty)
				&& LINK_STORAGE.tx_buffer[i].expiration_time == LINK_STORAGE.timer_counter) {
			if ((LINK_STORAGE.tx_buffer[i].transmits_to_error--) == 0) {
				if (LINK_STORAGE.tx_buffer[i].address_type) {
					// multiple unsuccessful packet sending to ED, network reinitialization starts
					LINK_error_handler_coord (LINK_STORAGE.tx_buffer[i].data,
																		LINK_STORAGE.tx_buffer[i].len);
					// delete all messages for unavailable ED
					for (uint8_t j = 0; j < LINK_TX_BUFFER_SIZE; j++) {
						if(!LINK_STORAGE.tx_buffer[j].empty && array_cmp(LINK_STORAGE.tx_buffer[i].address.ed, LINK_STORAGE.tx_buffer[j].address.ed)){
							LINK_STORAGE.tx_buffer[j].empty = 1;
						}
					}
				}
				else {
					// multiple unsuccessful packet sending to COORD, network reinitialization starts
					LINK_error_handler_coord (LINK_STORAGE.tx_buffer[i].data,
																		LINK_STORAGE.tx_buffer[i].len);
					// delete all messages for unavailable COORD
					for (uint8_t j = 0; j < LINK_TX_BUFFER_SIZE; j++) {
						if(!LINK_STORAGE.tx_buffer[j].empty && LINK_STORAGE.tx_buffer[i].address.coord == LINK_STORAGE.tx_buffer[j].address.coord){
							LINK_STORAGE.tx_buffer[j].empty = 1;
						}
					}
				}
			}
			else {
				// try to resend packet
				if (LINK_STORAGE.tx_buffer[i].state) {
					if (LINK_STORAGE.tx_buffer[i].address_type) {
						D_LINK printf("COMMIT again!\n");
						send_commit (false, true, LINK_STORAGE.tx_buffer[i].address.ed);
					}
					else {
						send_commit (false, false,
											   &LINK_STORAGE.tx_buffer[i].address.coord);
					}
				}
				else {
					D_LINK printf("DATA again!\n");
					if (LINK_STORAGE.tx_buffer[i].address_type) {
						send_data (false, true, LINK_STORAGE.tx_buffer[i].address.ed,
											 LINK_STORAGE.tx_buffer[i].data, LINK_STORAGE.tx_buffer[i].len,
											 LINK_STORAGE.tx_buffer[i].transfer_type);
					}

					else {
						send_data (false, false,
											 &LINK_STORAGE.tx_buffer[i].address.coord,
											 LINK_STORAGE.tx_buffer[i].data, LINK_STORAGE.tx_buffer[i].len,
											 LINK_STORAGE.tx_buffer[i].transfer_type);
					}
				}
				LINK_STORAGE.tx_buffer[i].expiration_time = LINK_STORAGE.timer_counter + 2;
			}
		}
	}
}

/**
 * Processes received packet.
 * @param data 	Data.
 * @param len 	Data length.
 */
void PHY_process_packet (uint8_t* data, uint8_t len)
{
	/*printf("RAW: ");
	  for(uint8_t i = 0; i < len; i++) {
	   	printf("%02x ",data[i]);
	  }
	  printf("\n");*/
	D_LINK printf("PHY_process_packet()\n");

	// packet is too short
	if (len < LINK_HEADER_SIZE)
		return;

	uint8_t packet_type = data[0] >> 6;
	uint8_t transfer_type = data[0] & 0x0f;
	// JOIN packet processing before NID check
	if (transfer_type == LINK_DATA_JOIN_REQUEST && packet_type == LINK_DATA_TYPE) {
		// JOIN REQUEST message, send ACK JOIN REQUEST message
		if(!NET_is_set_pair_mode()) {
			D_LINK printf("Not in a PAIR MODE!\n");
			return;
		}
		uint8_t ack_packet[LINK_HEADER_SIZE];
		gen_header (ack_packet, false, true, data + 6, LINK_ACK_TYPE,
								LINK_ACK_JOIN_REQUEST);
		delay_ms(25);
		PHY_send_with_cca (ack_packet, LINK_HEADER_SIZE);
		// send JOIN REQUEST ROUTE message with RSSI
		uint8_t RSSI = PHY_get_measured_noise();
		LINK_join_request_received (RSSI, data + 6, data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
	}
	if (transfer_type == LINK_ACK_JOIN_REQUEST && packet_type == LINK_ACK_TYPE) {
		// ACK JOIN message
		if (GLOBAL_STORAGE.waiting_join_response) {
			D_LINK printf ("LINK_ACK_JOIN_REQUEST from %d\n", data[9]);
			LINK_STORAGE.link_ack_join_received = true;
			// sender of ACK JOIN message is stored into array on index that corresponds
			// with its CID
			// it is necessary for verification that ACK JOIN and RESPONSE messages
			// were received from the same sender
			LINK_STORAGE.ack_join_address[data[9]] = data[9];
		}
		return;
	}
	if (transfer_type == LINK_DATA_JOIN_RESPONSE && packet_type == LINK_DATA_TYPE) {
		// JOIN RESPONSE message
		if (GLOBAL_STORAGE.waiting_join_response && LINK_STORAGE.link_ack_join_received) {
			uint8_t i;
			for(i = 0; i < MAX_COORD; i++) {
				// check if some ACK JOIN message was received from the device
				if(data[9] == LINK_STORAGE.ack_join_address[i])
				break;
			}
			if(i == MAX_COORD) {
				// no ACK JOIN message was received from the device
				return;
			}
			D_LINK printf ("LINK_DATA_JOIN_RESPONSE\n");
			LINK_join_response_received (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
			GLOBAL_STORAGE.waiting_join_response = false;
			// end of joining process, invalidate ack_join_address array
			for(i = 0; i < MAX_COORD; i++)
				LINK_STORAGE.ack_join_address[i] = INVALID_CID;
		}
		return;
	}

	// packet is not in my network
	if (!array_cmp (data + 1, GLOBAL_STORAGE.nid))
		return;

	if (transfer_type == LINK_DATA_BROADCAST) {
		D_LINK printf("BROADCAST received\n");
		ed_process_packet (data, len);
		return;
	}

	// packet is for ED
	if (data[0] & LINK_COORD_TO_ED) {
		// packet is for another ED
		if (!array_cmp (data + 5, GLOBAL_STORAGE.edid)) {
			return;
		}

		// packet is not from my parent and it is not MOVE RESPONSE message
		// (extended message types on network layer are stored in the 20th byte)
		if((LINK_cid_mask (data[9]) != GLOBAL_STORAGE.parent_cid) && (len > 20 && !NET_is_move_response(data[20])))
			return;

		if (transfer_type == LINK_DATA_WITHOUT_ACK) {
			LINK_process_packet (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
			return;
		}

		// packet processing
		ed_process_packet (data, len);
	}
	// packet is for COORD
	else {
		D_LINK printf("dst CID: %d\n", data[5]);
		// packet for another COORD
		if (LINK_cid_mask (data[5]) != GLOBAL_STORAGE.cid) {
			D_LINK printf ("Packet for another COORD!\n");
			return;
		}

		// if routing is disabled and four-way handshake is not finished
		// do not process next packets
		if (!GLOBAL_STORAGE.routing_enabled && ((transfer_type == LINK_DATA_HS4) && packet_type != LINK_COMMIT_ACK_TYPE)) {
			D_LINK printf ("Routing disabled!\n");
			return;
		}

		// packet is from COORD
		if (!(data[0] & LINK_ED_TO_COORD)) {
			uint8_t sender_cid = LINK_cid_mask (data[6]);
			D_LINK printf ("src CID: %d\n", sender_cid);

			// save and send routing table
			if (NET_is_routing_data_message(data[10])) {
				D_LINK printf ("routing table received\n");
				LINK_process_packet (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
				NET_process_routing_table (data + 20, len - 20);
				return;
			}

			// packet has to be from my parent or child
			if (GLOBAL_STORAGE.routing_tree[GLOBAL_STORAGE.cid] != sender_cid
					&& GLOBAL_STORAGE.routing_tree[sender_cid] != GLOBAL_STORAGE.cid){
				D_LINK printf("Not from my neighbours!\n");
				return;
			}
		}
		// packet processing and routing
		router_process_packet (data, len);
	}
}

/**
 * Checks buffers and increments counter periodically.
 */
void PHY_timer_interrupt ()
{
	LINK_STORAGE.timer_counter++;
	LINK_timer_counter();
	check_buffers_state ();
}

uint8_t LINK_cid_mask (uint8_t address)
{
	return address & 0x3f;
}

void LINK_init (struct PHY_init_t* phy_params, struct LINK_init_t* link_params)
{
	D_LINK printf("LINK_init\n");
	PHY_init(phy_params);
	LINK_STORAGE.tx_max_retries = link_params->tx_max_retries;
	for (uint8_t i = 0; i < LINK_RX_BUFFER_SIZE; i++) {
		LINK_STORAGE.rx_buffer[i].empty = 1;
	}
	for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
		LINK_STORAGE.tx_buffer[i].empty = 1;
	}
	LINK_STORAGE.ed_rx_buffer.empty = 1;
	LINK_STORAGE.ed_tx_buffer.empty = 1;

	LINK_STORAGE.timer_counter = 0;

	for(uint8_t i = 0; i < MAX_COORD; i++)
		LINK_STORAGE.ack_join_address[i] = INVALID_CID;
}

/**
 * Sends JOIN REQUEST message.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @return Return true if ACK JOIN packet is received, false otherwise.
 */
bool LINK_send_join_request (uint8_t* payload, uint8_t len)
{
	D_LINK printf ("LINK_send_join_request()\n");
	// JOIN REQUEST length is 20 bytes
	uint8_t packet[20];
	uint8_t packet_index;
	bool result;
	uint8_t my_channel;

	// it has to be set because of repetitive joining
	LINK_STORAGE.link_ack_join_received = false;
	GLOBAL_STORAGE.waiting_join_response = true;

	// current channel
	my_channel = PHY_get_channel ();
	// channel scanning
	for (uint8_t i = 0; i <= MAX_CHANNEL; i++) {
		// size of link header
		packet_index = LINK_HEADER_SIZE;
		result = PHY_set_channel (i);
		if (result == true) {
			// successful channel setting
			gen_header (packet, true, false, &GLOBAL_STORAGE.parent_cid,
									LINK_DATA_TYPE, LINK_DATA_JOIN_REQUEST);
			for (uint8_t index = 0; index < len; index++) {
				packet[packet_index++] = payload[index];
			}
			PHY_send_with_cca (packet, packet_index);
			// delay for ACK JOIN packet receiving
			// PAN -- COORD: minimum = delay_ms(17);
			// COORD -- COORD: minimum = delay_ms(21);
			delay_ms(25);
			// suitable delay for simulator
			//delay_ms (1000);
			if (LINK_STORAGE.link_ack_join_received) {
				return true;
			}
		}
		else {
			D_LINK printf ("Unsuccessful channel setting!\n");
			return false;
		}
	}
	// the original channel setting
	result = PHY_set_channel (my_channel);
	D_LINK printf("Default channel is set!\n");
	return false;
}

void LINK_send_join_response (uint8_t* edid, uint8_t* payload, uint8_t len)
{
	// JOIN RESPONSE length is 25 bytes
	uint8_t packet[25];
	uint8_t packet_index = LINK_HEADER_SIZE;
	gen_header (packet, false, true, edid, LINK_DATA_TYPE, LINK_DATA_JOIN_RESPONSE);
	for (uint8_t i = 0; i < len && packet_index < MAX_PHY_PAYLOAD_SIZE; i++) {
		packet[packet_index++] = payload[i];
	}
	PHY_send_with_cca (packet, packet_index);
}

void LINK_send_broadcast (uint8_t* payload, uint8_t len)
{
	uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
	// size of link header
	uint8_t packet_index = LINK_HEADER_SIZE;
	uint8_t address_coord = LINK_COORD_ALL;
	gen_header (packet, true, false, &address_coord, LINK_DATA_TYPE,
							LINK_DATA_BROADCAST);

	for (uint8_t i = 0; i < len; i++) {
		packet[packet_index++] = payload[i];
	}
	PHY_send_with_cca (packet, packet_index);
}

// this function can be used when coordinator sends packet as end device to coordinator
/*bool LINK_send_ed (uint8_t* payload, uint8_t len,
								uint8_t* tocoord, uint8_t transfer_type)
{
	printf ("LINK_send_ed()\n");
	if (transfer_type == LINK_DATA_HS4) {
		if (!LINK_STORAGE.ed_tx_buffer.empty)
			return false;
		for (uint8_t i = 0; i < len; i++)
			LINK_STORAGE.ed_tx_buffer.data[i] = payload[i];
		LINK_STORAGE.ed_tx_buffer.len = len;
		LINK_STORAGE.ed_tx_buffer.state = DATA_SENT;
		LINK_STORAGE.ed_tx_buffer.transmits_to_error = LINK_STORAGE.tx_max_retries;
		LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 3;
		LINK_STORAGE.ed_tx_buffer.transfer_type = transfer_type;
		LINK_STORAGE.ed_tx_buffer.empty = 0;
		send_data(true, false, tocoord, LINK_STORAGE.ed_tx_buffer.data,LINK_STORAGE.ed_tx_buffer.len, transfer_type, 0);
	}

	else if (transfer_type == LINK_DATA_WITHOUT_ACK) {
		uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
		// size of link header
		uint8_t packet_index = LINK_HEADER_SIZE;
		gen_header (packet, true, false, tocoord, LINK_DATA_TYPE,
								transfer_type, 0);
		for (uint8_t i = 0; i < len; i++)
			packet[packet_index++] = payload[i];
		PHY_send_with_cca (packet, packet_index);
	}

	else if (transfer_type == LINK_DATA_BROADCAST) {
		LINK_send_broadcast (payload, len);
	}
	return true;
}*/

bool LINK_send_coord (bool to_ed, uint8_t * address,
											uint8_t * payload, uint8_t len, uint8_t transfer_type)
{
	D_LINK printf("LINK_send_coord()\n");
	if (transfer_type == LINK_DATA_HS4) {
		// send data using four-way handshake
		uint8_t free_index = get_free_index_tx ();
		if (free_index >= LINK_TX_BUFFER_SIZE)
			return false;
		for (uint8_t i = 0; i < len; i++)
			LINK_STORAGE.tx_buffer[free_index].data[i] = payload[i];
		LINK_STORAGE.tx_buffer[free_index].len = len;
		if (to_ed) {
			for (uint8_t i = 0; i < EDID_LENGTH; i++)
				LINK_STORAGE.tx_buffer[free_index].address.ed[i] = address[i];
		}
		else {
			LINK_STORAGE.tx_buffer[free_index].address.coord = *address;
		}
		// 0 - packet is for ED, 1- packet is for COORD
		LINK_STORAGE.tx_buffer[free_index].address_type = to_ed ? 1 : 0;
		LINK_STORAGE.tx_buffer[free_index].state = DATA_SENT;
		LINK_STORAGE.tx_buffer[free_index].transmits_to_error = LINK_STORAGE.tx_max_retries;
		LINK_STORAGE.tx_buffer[free_index].expiration_time = LINK_STORAGE.timer_counter + 2;
		LINK_STORAGE.tx_buffer[free_index].transfer_type = transfer_type;
		LINK_STORAGE.tx_buffer[free_index].empty = 0;

		if (LINK_STORAGE.tx_buffer[free_index].address_type)
			send_data (false, true, LINK_STORAGE.tx_buffer[free_index].address.ed,
								 LINK_STORAGE.tx_buffer[free_index].data,
								 LINK_STORAGE.tx_buffer[free_index].len, transfer_type);

		else
			send_data (false, false,
								 &LINK_STORAGE.tx_buffer[free_index].address.coord,
								 LINK_STORAGE.tx_buffer[free_index].data,
								 LINK_STORAGE.tx_buffer[free_index].len, transfer_type);
	}

	else if (transfer_type == LINK_DATA_WITHOUT_ACK) {
		// send data without waiting for ACK message
		uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
		// size of link header
		uint8_t packet_index = LINK_HEADER_SIZE;
		if(to_ed)
			gen_header (packet, false, true, address, LINK_DATA_TYPE,
								LINK_DATA_WITHOUT_ACK);
		else
			gen_header (packet, false, false, address, LINK_DATA_TYPE,
								LINK_DATA_WITHOUT_ACK);
		for (uint8_t index = 0; index < len; index++)
			packet[packet_index++] = payload[index];
		PHY_send_with_cca (packet, packet_index);
	}

	else if (transfer_type == LINK_DATA_BROADCAST) {
		// send broadcast message
		D_LINK printf("BROADCAST sent!\n");
		LINK_send_broadcast(payload, len);
	}
	return true;
}

uint8_t LINK_get_measured_noise()
{
	return PHY_get_measured_noise();
}
