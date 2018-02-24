/**
* @file link.cc
*/

#include "common/phy_layer/phy.h"
#include "pan/link_layer/link.h"
#include <stdio.h>
#include "common/log/log.h"

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
enum LINK_packet_type_pan {
	LINK_DATA_TYPE = 0,					/**< DATA. */
	LINK_COMMIT_TYPE = 1,				/**< COMMIT. */
	LINK_ACK_TYPE = 2,					/**< ACK. */
	LINK_COMMIT_ACK_TYPE = 3		/**< COMMIT ACK. */
};

/**
 * Packet states during four-way handshake.
 */
enum LINK_tx_packet_state_pan {
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
 * Structure for link layer.
 */
struct LINK_storage_t {
	uint8_t tx_max_retries;																		/**< Maximum number of packet retransmissions. */
	uint8_t timer_counter;																		/**< Timer for packet expiration time setting. */
	LINK_rx_buffer_record_t rx_buffer[LINK_RX_BUFFER_SIZE];		/**< Array of RX buffer records for coordinator. */
	LINK_tx_buffer_record_t tx_buffer[LINK_TX_BUFFER_SIZE];		/**< Array of TX buffer records for coordinator. */
} LINK_STORAGE;

extern void delay_ms (uint16_t t);
uint8_t LINK_cid_mask (uint8_t address);

/**
 * Checks if identifier of end device equals to zeros.
 * @param edid Identifier of end device (EDID).
 * @return Returns true if EDID equals to zeros, false otherwise.
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
* @param src 		Source array.
* @param dst 		Destination array.
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
void gen_header (uint8_t* header, bool as_ed, bool to_ed,
								 uint8_t* address, uint8_t packet_type,
								 uint8_t transfer_type)
{
	if (as_ed && to_ed) {
		// communication between EDs is not allowed
		return;
	}
	uint8_t header_index = 0;
	// packet type (2 b), dst (1 b), src (1 b), transfer type (4 b)
	header[header_index++] =
		packet_type << 6 | (to_ed ? 1 : 0) << 5 | (as_ed ? 1 : 0) << 4 | (transfer_type & 0x0f);

	// NID
	for (uint8_t i = 0; i < EDID_LENGTH; i++) {
		header[header_index++] = GLOBAL_STORAGE.nid[i];
	}

	// data sending to ED
	if (to_ed) {
		for (uint8_t i = 0; i < EDID_LENGTH; i++)
			// ED address - dst
			header[header_index++] = address[i];
		// COORD address - src
		header[header_index++] = GLOBAL_STORAGE.cid;
	}
	// data sending to COORD
	else {
		if (as_ed) {
			// COORD address - dst
			// ED can send packet to its descendant
			header[header_index++] = *address;
			// ED adress - src
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
	// size of link header
	uint8_t packet_index = LINK_HEADER_SIZE;
	for (uint8_t i = 0; i < len; i++)
		packet[packet_index++] = payload[i];
	D_LINK printf ("send_data()\n");
	PHY_send_with_cca (packet, packet_index);
}

/**
 * Sends ACK.
 * @param as_ed 										True if device sends packet as end device ID, false otherwise.
 * @param to_ed 										True if device sends packet to end device ID, false otherwise.
 * @param address 									Destination coordinator ID or end device ID.
 * @param transfer_type 						Transfer type on link layer.
 */
void send_ack (bool as_ed, bool to_ed, uint8_t* address, uint8_t transfer_type)
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
void send_commit (bool as_ed, bool to_ed, uint8_t* address)
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
								LINK_STORAGE.tx_buffer[i].state = COMMIT_SENT;
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
							if(transfer_type != LINK_BUSY) {
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
							LINK_notify_send_done();
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
						if (array_cmp (LINK_STORAGE.tx_buffer[i].address.ed, data + 6)) {
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
				} else {
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
								D_LINK printf("S: ACK to COORD\n");
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

/*
 * Checks TX and RX buffers periodically.
 * Ensures packet retransmission during four-way handshake and detects
 * unsuccessful four-way handshake.
 */
void check_buffers_state ()
{
	// check the COORD buffers
	for (uint8_t i = 0; i < LINK_TX_BUFFER_SIZE; i++) {
		if ((!LINK_STORAGE.tx_buffer[i].empty)
				&& LINK_STORAGE.tx_buffer[i].expiration_time == LINK_STORAGE.timer_counter) {
			if ((LINK_STORAGE.tx_buffer[i].transmits_to_error--) == 0) {
				// multiple unsuccessful packet sending, network reinitialization starts
				if (LINK_STORAGE.tx_buffer[i].address_type) {
					LINK_error_handler_coord ();
					// delete all messages for unavailable ED
					for (uint8_t j = 0; j < LINK_TX_BUFFER_SIZE; j++) {
						if(!LINK_STORAGE.tx_buffer[j].empty && array_cmp(LINK_STORAGE.tx_buffer[i].address.ed, LINK_STORAGE.tx_buffer[j].address.ed)){
							LINK_STORAGE.tx_buffer[j].empty = 1;
						}
					}
				}
				else {
					LINK_error_handler_coord ();
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
					D_LINK printf("COMMIT again!\n");
					if (LINK_STORAGE.tx_buffer[i].address_type) {
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
											 LINK_STORAGE.tx_buffer[i].data, LINK_STORAGE.tx_buffer[i].len, LINK_STORAGE.tx_buffer[i].transfer_type);
					}
					else {
						send_data (false, false, &LINK_STORAGE.tx_buffer[i].address.coord,
											 LINK_STORAGE.tx_buffer[i].data, LINK_STORAGE.tx_buffer[i].len, LINK_STORAGE.tx_buffer[i].transfer_type);
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
	for(uint8_t i = 0; i < len; i++)
		printf("%02x ",data[i]);
	 printf("\n");*/
	D_LINK printf ("PHY_process_packet()!\n");
	// packet is too short
	if (len < LINK_HEADER_SIZE)
		return;

	uint8_t packet_type = data[0] >> 6;
	uint8_t transfer_type = data[0] & 0x0f;
	// JOIN packet processing before NID check
	if (transfer_type == LINK_DATA_JOIN_REQUEST && packet_type == LINK_DATA_TYPE) {
		D_LINK printf("JOIN REQUEST received\n");
		// JOIN REQUEST message, send ACK JOIN REQUEST message
		if(!NET_is_set_pair_mode()) {
			D_LINK printf("Not in a PAIR MODE!\n");
			return;
		}

		LINK_save_msg_info(data + 10, len - 10);

		uint8_t ack_packet[LINK_HEADER_SIZE];
		gen_header (ack_packet, false, true, data + 6, LINK_ACK_TYPE,
								LINK_ACK_JOIN_REQUEST);
		D_LINK printf("ACK JOIN REQUEST\n");
		PHY_send_with_cca (ack_packet, LINK_HEADER_SIZE);
		// send JOIN REQUEST ROUTE message with RSSI
		uint8_t RSSI = PHY_get_measured_noise();
		LINK_join_request_received (RSSI, data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
		return;
	}

	// packet is not in my network
	if (!array_cmp (data + 1, GLOBAL_STORAGE.nid))
		return;

	LINK_save_msg_info(data + 10, len - 10);

	if (transfer_type == LINK_DATA_BROADCAST) {
		D_LINK printf("BROADCAST received\n");
		LINK_route (data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE, LINK_DATA_BROADCAST);
		return;
	}
	// packets to PAN are sent only as to COORD, not to ED
	if (data[0] & LINK_COORD_TO_ED)
		return;

	// packet for another COORD
	if (LINK_cid_mask (data[5]) != GLOBAL_STORAGE.cid)
		return;

	// if routing is disabled and four-way handshake is not finished
	// do not process next packets
	if (!GLOBAL_STORAGE.routing_enabled && ((transfer_type == LINK_DATA_HS4) && packet_type != LINK_COMMIT_ACK_TYPE)) {
		D_LINK printf ("Routing disabled\n");
		return;
	}

	// packet is from COORD
	if (!(data[0] & LINK_ED_TO_COORD)) {
		uint8_t sender_cid = LINK_cid_mask (data[6]);
		// packet has to be from my child
		if (GLOBAL_STORAGE.routing_tree[sender_cid] != GLOBAL_STORAGE.cid) {
			D_LINK printf ("Not my child!\n");
			return;
		}
	}
	// packet processing and routing
	router_process_packet (data, len);
}

/**
 * Checks buffers, increments counter periodically and
 * alternatively sends JOIN RESPONSE (ROUTE) and MOVE RESPONSE (ROUTE) messages.
 */
void PHY_timer_interrupt ()
{
	LINK_STORAGE.timer_counter++;
	LINK_timer_counter();
	/*if(GLOBAL_STORAGE.pair_mode)
		NET_joining();
	NET_moving();*/
	check_buffers_state ();
}

/**
 * Gets address of coordinator (CID).
 * @param byte Byte containing coordinator ID.
 * @return Returns coordinator ID (6 bits).
 */
uint8_t LINK_cid_mask (uint8_t address)
{
	return address & 0x3f;
}

/**
 * Initializes link layer and ensures initialization of physical layer.
 * @param phy_params 		Parameters of physical layer.
 * @param link_params 	Parameters of link layer.
 */
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

	LINK_STORAGE.timer_counter = 0;
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

/**
 * Broadcasts packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
bool LINK_send_broadcast (uint8_t * payload, uint8_t len)
{
	uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
	uint8_t packet_index = LINK_HEADER_SIZE;
	// size of link header
	uint8_t address_next_coord = LINK_COORD_ALL;
	gen_header (packet, true, false, &address_next_coord, LINK_DATA_TYPE,
							LINK_DATA_BROADCAST);
	for (uint8_t index = 0; index < len; index++) {
		packet[packet_index++] = payload[index];
	}
	PHY_send_with_cca (packet, packet_index);
	return true;
}

/**
 * Sends packet.
 * @param to_ed 				True if destination device is ED, false otherwise.
 * @param address 			Destination address.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param transfer_type Transfer type on link layer.
 * @return Returns false if no space is in TX buffer, true otherwise.
 */
bool LINK_send_coord (bool to_ed, uint8_t * address, uint8_t * payload,
											uint8_t len, uint8_t transfer_type)
{
	D_LINK printf("LINK_send_coord()\n");
	if(transfer_type == LINK_DATA_HS4) {
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
			send_data (false, false, &LINK_STORAGE.tx_buffer[free_index].address.coord,
								 LINK_STORAGE.tx_buffer[free_index].data,
								 LINK_STORAGE.tx_buffer[free_index].len, transfer_type);
	}
	else if (transfer_type == LINK_DATA_WITHOUT_ACK) {
		// send data without waiting for ACK message
		uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
		//size of link header
		uint8_t packet_index = LINK_HEADER_SIZE;
		if(to_ed)
			gen_header (packet, false, true, address, LINK_DATA_TYPE, LINK_DATA_WITHOUT_ACK);
		else
			gen_header (packet, false, false, address, LINK_DATA_TYPE, LINK_DATA_WITHOUT_ACK);
		for (uint8_t index = 0; index < len; index++) {
			packet[packet_index++] = payload[index];
		}
		PHY_send_with_cca (packet, packet_index);
	}
	else if (transfer_type == LINK_DATA_BROADCAST) {
		// send broadcast message
		D_LINK printf("BROADCAST sent!\n");
		LINK_send_broadcast(payload, len);
	}
	return true;
}

/**
* Gets RSSI value.
* @return Returns RSSI value.
*/
uint8_t LINK_get_measured_noise()
{
	return PHY_get_measured_noise();
}

void LINK_stop()
{
	PHY_stop();
}
