/**
* @file link.c
*/

#include "link.h"

/*! bit mask of data transfer from coordinator to end device */
#define LINK_COORD_TO_ED					0x20
/*! bit mask of device busyness */
#define LINK_BUSY									0x08
/*! maximum number of channels */
#define MAX_CHANNEL 							31
/*! broadcast address */
#define LINK_COORD_ALL 						0xfc
/*! invalid coordinator ID */
#define INVALID_CID 0xff

/**
 * Packet types.
 */
enum LINK_packet_type_ed {
	LINK_DATA_TYPE = 0,					/**< DATA. */
	LINK_COMMIT_TYPE = 1,				/**< COMMIT. */
	LINK_ACK_TYPE = 2,					/**< ACK. */
	LINK_COMMIT_ACK_TYPE = 3		/**< COMMIT ACK. */
};

// TX packet states
enum LINK_tx_packet_state_ed {
	DATA_SENT = 0,							/**< DATA were sent. */
	COMMIT_SENT = 1							/**< COMMIT was sent. */
};

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
	LINK_rx_buffer_record_ed_t ed_rx_buffer;									/**< Array of RX buffer records for end device. */
	LINK_tx_buffer_record_ed_t ed_tx_buffer;									/**< Array of TX buffer records for end device. */
	bool link_ack_join_received;															/**< Flag if ACK JOIN packet was received. */
	uint8_t ack_join_address[MAX_COORD];											/**< Array of coordinators that sent ACK JOIN packet. */
} LINK_STORAGE;

extern void delay_ms (uint16_t t);
extern bool NET_is_move_response(uint8_t type);

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
 * Gets address of coordinator (CID).
 * @param byte Byte containing coordinator ID.
 * @return Returns coordinator ID (6 bits).
 */
uint8_t LINK_cid_mask (uint8_t address)
{
	// the CID is stored on the lower 6 bits => ??xxxxxx
	return address & 0x3f;
}

/**
 * Generates packet header.
 * @param header									 	Array for link packet header.
 * @param packet_type 							Packet type.
 * @param transfer_type 						Transfer type on link layer.
 */
void gen_header (uint8_t* header, uint8_t packet_type,
								 uint8_t transfer_type)
{
	uint8_t header_index = 0;
	// packet type (2 b), dst (COORD, 1 b), src (ED, 1 b), transfer type (4 b)
	header[header_index++] = packet_type << 6 | 0 << 5 | 1 << 4 | (transfer_type & 0x0f);

	// NID
	for (uint8_t i = 0; i < 4; i++) {
		header[header_index++] = GLOBAL_STORAGE.nid[i];
	}
	// COORD address - dst
	if (transfer_type == LINK_DATA_BROADCAST) {
		// ED can send packet for all COORDs
		header[header_index++] = LINK_COORD_ALL;
	}
	else {
		// or to its parent
		header[header_index++] = GLOBAL_STORAGE.parent_cid;
	}
	// ED address - src
	for (uint8_t i = 0; i < EDID_LENGTH; i++) {
		header[header_index++] = GLOBAL_STORAGE.edid[i];
	}
}

/**
 * Sends DATA.
 * @param payload										Payload.
 * @param len												Payload length.
 * @param transfer_type 						Transfer type on link layer.
 */
void send_data (uint8_t* payload, uint8_t len, uint8_t transfer_type)
{
	uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
	// size of link header
	uint8_t packet_index = LINK_HEADER_SIZE;
	gen_header (packet, LINK_DATA_TYPE, transfer_type);
	for (uint8_t i = 0; i < len; i++) {
		packet[packet_index++] = payload[i];
	}
	PHY_send_with_cca (packet, packet_index);
}

/**
 * Sends ACK.
 * @param transfer_type 						Transfer type on link layer.
 */
void send_ack (uint8_t transfer_type)
{
	uint8_t ack_packet[LINK_HEADER_SIZE];
	gen_header (ack_packet, LINK_ACK_TYPE, transfer_type);
	PHY_send_with_cca (ack_packet, LINK_HEADER_SIZE);
}

/**
 * Sends COMMIT.
 */
void send_commit ()
{
	uint8_t commit_packet[LINK_HEADER_SIZE];
	gen_header (commit_packet, LINK_COMMIT_TYPE, LINK_DATA_HS4);
	PHY_send_with_cca (commit_packet, LINK_HEADER_SIZE);
}

/**
 * Sends COMMIT ACK.
 */
void send_commit_ack ()
{
	uint8_t commit_ack_packet[LINK_HEADER_SIZE];
	gen_header (commit_ack_packet, LINK_COMMIT_ACK_TYPE, LINK_DATA_HS4);
	PHY_send_with_cca (commit_ack_packet, LINK_HEADER_SIZE);
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
				send_commit ();
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
				uint8_t i;
				for (i = 0; i < len && i < MAX_PHY_PAYLOAD_SIZE; i++) {
					LINK_STORAGE.ed_rx_buffer.data[i] = data[i];
				}
				LINK_STORAGE.ed_rx_buffer.len = i;
				LINK_STORAGE.ed_rx_buffer.transfer_type = transfer_type;
				LINK_STORAGE.ed_rx_buffer.empty = 0;
				send_ack (LINK_STORAGE.ed_rx_buffer.transfer_type);
				return true;
			}
			// in case of multiple receiving of DATA packet send ACK
			// packet (DATA packet was not received by sender)
			send_ack (LINK_STORAGE.ed_rx_buffer.transfer_type);
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
			send_commit_ack ();
			return result;
		}
		// in case of multiple receiving of COMMIT packet send COMMIT ACK
		// packet (COMMIT packet was not received by sender)
		send_commit_ack ();
		return true;
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
	// check buffers
	if ((!LINK_STORAGE.ed_tx_buffer.empty)
			&& LINK_STORAGE.ed_tx_buffer.expiration_time == LINK_STORAGE.timer_counter) {
		if ((LINK_STORAGE.ed_tx_buffer.transmits_to_error--) == 0) {
			// multiple unsuccessful packet sending, network reinitialization starts
			D_LINK printf("Device movement!\n");
			LINK_error_handler_ed (false);
			LINK_STORAGE.ed_tx_buffer.empty = 1;
		}
		else {
			if (LINK_STORAGE.ed_tx_buffer.state) {
				D_LINK printf ("COMMIT again!\n");
				send_commit ();
			}
			else {
				D_LINK printf ("DATA again!\n");
				send_data (LINK_STORAGE.ed_tx_buffer.data, LINK_STORAGE.ed_tx_buffer.len,
					LINK_STORAGE.ed_tx_buffer.transfer_type);
			}
			// reschedule transmission
			LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 2;
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
			printf("%02x,",data[i]);
		}
		printf("\n");*/
	D_LINK printf ("PHY_process_packet()\n");

	// the packet is too short
	if (len < LINK_HEADER_SIZE)
		return;

	uint8_t packet_type = data[0] >> 6;
	uint8_t transfer_type = data[0] & 0x0f;

	// JOIN packet processing before NID check
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
		D_LINK printf("BROADCAST\n");
		ed_process_packet (data, len);
		return;
	}

	// packet is not for ED
	if (!(data[0] & LINK_COORD_TO_ED)) {
		return;
	}

	//  packet for another ED
	if (!array_cmp (data + 5, GLOBAL_STORAGE.edid)) {
		return;
	}

	// packet is not from my parent and it is not MOVE RESPONSE message
	// (extended message types on network layer are stored in the 20th byte)
	if ((LINK_cid_mask (data[9]) != GLOBAL_STORAGE.parent_cid) && (len > 20 && !NET_is_move_response(data[20])))
		return;

	// packet processing
	ed_process_packet (data, len);
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

/**
 * Initializes link layer and ensures initialization of physical layer.
 * @param phy_params 		Parameters of physical layer.
 * @param link_params 	Parameters of link layer.
 */
void LINK_init (struct PHY_init_t* phy_params, struct LINK_init_t* link_params)
{
	D_LINK printf("LINK_init\n");
	PHY_init (phy_params);
	LINK_STORAGE.tx_max_retries = link_params->tx_max_retries;
	LINK_STORAGE.ed_rx_buffer.empty = 1;
	LINK_STORAGE.ed_tx_buffer.empty = 1;
	LINK_STORAGE.timer_counter = 0;
	LINK_STORAGE.link_ack_join_received = false;
	for(uint8_t i = 0; i < MAX_COORD; i++)
		LINK_STORAGE.ack_join_address[i] = INVALID_CID;
}

/**
 * Sends packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param transfer_type Transfer type on link layer.
 * @return Returns false if TX buffer is full, true otherwise.
 */
bool LINK_send_ed (uint8_t* payload, uint8_t len, uint8_t transfer_type)
{
	if (transfer_type == LINK_DATA_HS4) {
		if (!LINK_STORAGE.ed_tx_buffer.empty)
			return false;
		for (uint8_t i = 0; i < len; i++) {
			LINK_STORAGE.ed_tx_buffer.data[i] = payload[i];
		}
		LINK_STORAGE.ed_tx_buffer.len = len;
		LINK_STORAGE.ed_tx_buffer.state = DATA_SENT;
		LINK_STORAGE.ed_tx_buffer.transmits_to_error = LINK_STORAGE.tx_max_retries;
		LINK_STORAGE.ed_tx_buffer.expiration_time = LINK_STORAGE.timer_counter + 3;
		LINK_STORAGE.ed_tx_buffer.transfer_type = transfer_type;
		LINK_STORAGE.ed_tx_buffer.empty = 0;
		send_data (LINK_STORAGE.ed_tx_buffer.data, LINK_STORAGE.ed_tx_buffer.len, LINK_STORAGE.ed_tx_buffer.transfer_type);
	}

	else if (transfer_type == LINK_DATA_WITHOUT_ACK) {
		uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
		// size of link header
		uint8_t packet_index = LINK_HEADER_SIZE;
		gen_header (packet, LINK_DATA_TYPE, transfer_type);
		for (uint8_t i = 0; i < len; i++) {
			packet[packet_index++] = payload[i];
		}
		PHY_send_with_cca (packet, packet_index);
	}

	else if (transfer_type == LINK_DATA_BROADCAST) {
		LINK_send_broadcast (payload, len);
	}
	return true;
}

/**
 * Broadcasts packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void LINK_send_broadcast (uint8_t* payload, uint8_t len)
{
	uint8_t packet[MAX_PHY_PAYLOAD_SIZE];
	// size of link header
	uint8_t packet_index = LINK_HEADER_SIZE;
	D_LINK printf("LINK_send_broadcast()\n");
	gen_header (packet, LINK_DATA_TYPE, LINK_DATA_BROADCAST);
	for (uint8_t i = 0; i < len; i++) {
		packet[packet_index++] = payload[i];
	}
	PHY_send_with_cca (packet, packet_index);
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
			gen_header (packet, LINK_DATA_TYPE, LINK_DATA_JOIN_REQUEST);
			for (uint8_t i = 0; i < len; i++) {
				packet[packet_index++] = payload[i];
			}
			PHY_send_with_cca (packet, packet_index);
			// delay for ACK JOIN packet receiving
			// PAN -- COORD: minimum = delay_ms(17);
			// COORD -- COORD: minimum = delay_ms(21);
			delay_ms (25);
			// suitable delay for simulator
			//delay_ms(10000);
			if (LINK_STORAGE.link_ack_join_received) {
				return true;
			}
		}
		else {
			D_LINK printf ("Unsuccessful channel setting.\n");
			return false;
		}
	}
	// the original channel setting
	result = PHY_set_channel (my_channel);
	D_LINK printf("The default channel set!\n");
	return false;
}
