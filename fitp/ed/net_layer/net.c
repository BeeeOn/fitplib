/**
* @file net.c
*/

#include "net.h"
#include "net_common.h"
#include "link.h"
#include "log.h"

/*! maximum delay for MOVE REPONSE message */
/*! if 2 second delay if required, then MAX_MOVE_DELAY is: */
/*! MAX_MOVE_DELAY = required_delay [ms] / 50 [ms] */
#define MAX_MOVE_DELAY 40
/*! maximum delay for JOIN REPONSE message */
/*! if 2 second delay if required, then MAX_JOIN_DELAY is: */
/*! MAX_JOIN_DELAY = required_delay [ms] / 50 [ms] */
#define MAX_JOIN_DELAY 40
/*! maximum delay for acknowledgement of DATA REQUEST message */
/*! if 200 milliseconds delay is required, then MAX_DR_ACK_DELAY is: */
/*! MAX_DR_ACK_DELAY = required_delay [ms] / 10 [ms] */
#define MAX_DR_ACK_DELAY 20
/*! maximum delay for required data */
/*! if 1 second delay is required, then MAX_DR_DATA_DELAY is: */
/*! MAX_DR_DATA_DELAY = required_delay [ms] / 10 [ms] */
#define MAX_DR_DATA_DELAY 100

/**
 * Structure for currently processed packet.
 */
typedef struct {
	uint8_t type;														/**< Message type. */
	uint8_t scid;														/**< Source coordinator ID. */
	uint8_t sedid[EDID_LENGTH];							/**< Source end device ID. */
	uint8_t payload[MAX_NET_PAYLOAD_SIZE];	/**< Payload. */
	uint8_t len;														/**< Payload length. */
} NET_current_processing_packet_t;

/**
 * Structure for network layer.
 */
struct NET_storage_t {
	uint8_t dr_state;																		/**< State during data request. */
	NET_current_processing_packet_t processing_packet;	/**< Structure for currently processed packet. */
	bool waiting_move_response;													/**< Flag if network is being reinitialized. */
	uint8_t move_timeout;																/**< Timeout for MOVE RESPONSE message (containing new parent ID). */
} NET_STORAGE;

/**
 * States during data request.
 */
enum NET_dr_packet_type {
	DR_ACK_WAITING = 0,		/**< Waiting for ACK after DATA REQUEST sending. */
	DR_DATA_WAITING = 1,  /**< Waiting for DATA after ACK receiving. */
	DR_GO_SLEEP = 2,			/**< Receipt of GO SLEEP packet after DATA REQUEST sending. */
	DR_DATA_RECEIVED = 3	/**< Receipt of DATA after DATA REQUEST sending. */
};

/*
 * Supporting functions.
 */
extern bool zero_address (uint8_t* addr);
extern bool array_cmp (uint8_t* array_a, uint8_t* array_b);
extern void array_copy (uint8_t* src, uint8_t* dst, uint8_t size);
extern void save_configuration (uint8_t * buf, uint8_t len);
extern void load_configuration (uint8_t * buf, uint8_t len);

/**
 * Sends packet.
 * @param msg_type					Message type on network layer.
 * @param tocoord 					Destination coordinator ID.
 * @param toed 							Destination end device ID.
 * @param payload 					Payload.
 * @param len 							Payload length.
 * @param transfer_type 		Transfer type on link layer.
 * @param msg_type_ext 			Message type on network layer (valid if msg_type is set to F (hexa)).
 * @return Returns false, if packet is not successfully sent, true otherwise.
 */
bool send (uint8_t msg_type, uint8_t tocoord, uint8_t* toed,
					 uint8_t* payload, uint8_t len, uint8_t transfer_type, uint8_t msg_type_ext)
{
	D_NET printf("send()\n");
	uint8_t tmp[MAX_NET_PAYLOAD_SIZE];
	uint8_t index = 0;
	// network header
	tmp[index++] = (msg_type << 4) | ((tocoord >> 2) & 0x0f);
	tmp[index++] = ((tocoord << 6) & 0xc0) | (GLOBAL_STORAGE.parent_cid & 0x3f);
	tmp[index++] = toed[0];
	tmp[index++] = toed[1];
	tmp[index++] = toed[2];
	tmp[index++] = toed[3];
	tmp[index++] = GLOBAL_STORAGE.edid[0];
	tmp[index++] = GLOBAL_STORAGE.edid[1];
	tmp[index++] = GLOBAL_STORAGE.edid[2];
	tmp[index++] = GLOBAL_STORAGE.edid[3];
	if(msg_type == PT_NETWORK_EXTENDED){
		tmp[index++] = msg_type_ext;
	}

	for (uint8_t i = 0; i < len && i < MAX_LINK_PAYLOAD_SIZE; i++) {
		tmp[index++] = payload[i];
	}

	if(msg_type_ext == PT_DATA_MOVE_REQUEST) {
		// MOVE REQUEST message
		LINK_send_broadcast(tmp, index);
		D_NET printf("PT_DATA_MOVE_REQUEST!\n");
		return true;
	}
	return LINK_send_ed (tmp, index, transfer_type);
}

/**
 * Checks if message type is MOVE RESPONSE.
 * @param msg_type	Message type on network layer.
 * @return Returns true if packet is MOVE RESPONSE, false otherwise.
 */
bool NET_is_move_response(uint8_t msg_type)
{
	if(msg_type == PT_DATA_MOVE_RESPONSE)
		return true;
	return false;
}

/**
 * Broadcasts packet.
 * @param msg_type 			Message type on network layer.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
bool NET_send_broadcast (uint8_t msg_type, uint8_t * payload, uint8_t len)
{
	if(msg_type == PT_DATA_MOVE_REQUEST) {
		// MOVE REQUEST message uses extended byte
		send (PT_NETWORK_EXTENDED, NET_COORD_ALL, NET_ED_ALL, payload, len, LINK_DATA_BROADCAST, msg_type);
	}
	send (msg_type, NET_COORD_ALL, NET_ED_ALL, payload, len, LINK_DATA_BROADCAST, NOT_EXTENDED);
	return true;
}

/**
 * Sends MOVE REQUEST packet in case of parent loss.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void NET_send_move_request(uint8_t* payload, uint8_t len)
{
	// timeout for MOVE RESPONSE message receiving
	NET_STORAGE.move_timeout = MAX_MOVE_DELAY;
	NET_STORAGE.waiting_move_response = true;
	NET_send_broadcast(PT_DATA_MOVE_REQUEST, payload, len);
}

/**
 * Sets received network ID, parent ID and coordinator ID.
 * @param data 					Data.
 * @param len 					Data length.
 * @return Returns false if data length is too short or JOIN RESPONSE message
 * is not addressed to this device, true otherwise.
 */
bool LINK_join_response_received (uint8_t * data, uint8_t len)
{
	if (len < 15) {
		// JOIN RESPONSE is too short
		return false;
	}
	if (!array_cmp (GLOBAL_STORAGE.edid, data + 2)) {
		// message is not for this device
		return false;
	}
	for (uint8_t i = NET_HEADER_SIZE; i < 14; i++)
		GLOBAL_STORAGE.nid[i-NET_HEADER_SIZE] = data[i];
	GLOBAL_STORAGE.parent_cid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	D_NET printf
		("LINK_join_response_received(): NID %02x %02x %02x %02x, PARENT CID %02x\n",
		 GLOBAL_STORAGE.nid[0], GLOBAL_STORAGE.nid[1],
		 GLOBAL_STORAGE.nid[2], GLOBAL_STORAGE.nid[3], GLOBAL_STORAGE.parent_cid);

	// save new configuration to EEPROM
	uint8_t cfg[5];
	cfg[0] = GLOBAL_STORAGE.nid[0];
	cfg[1] = GLOBAL_STORAGE.nid[1];
	cfg[2] = GLOBAL_STORAGE.nid[2];
	cfg[3] = GLOBAL_STORAGE.nid[3];
	cfg[4] = GLOBAL_STORAGE.parent_cid;
	save_configuration (cfg, 5);
	return true;
}

/**
 * Sets newly received parent ID.
 * @param parent 			Parent ID.
 */
void LINK_move_response_received (uint8_t parent)
{
	GLOBAL_STORAGE.parent_cid = LINK_cid_mask (parent);
	D_NET printf ("Moved to %d\n", GLOBAL_STORAGE.parent_cid);
}

/**
 * Checks if network is being reinitialized.
 * @return Returns true if network is being reinitialized, false otherwise.
 */
bool network_is_rebuilding()
{
	if(NET_STORAGE.waiting_move_response)
		return true;
	return false;
}

/**
 * Reinitializes network.
 */
void LINK_error_handler_ed ()
{
	D_NET printf ("ED - error during transmitting.\n");
	if(!NET_STORAGE.waiting_move_response) {
		// check if network reinitialization has not been already initiated
		NET_STORAGE.waiting_move_response = true;
		fitp_send_move_request();
	}
}

/**
 * Notifies successful four-way handshake.
 */
void LINK_notify_send_done ()
{
	NET_notify_send_done ();
}

/**
 * Processes received packet for end device.
 * @param data 			Data.
 * @param len			 	Data length.
 * @return Returns false if device is waiting for new parent or packet length
 *				 is too short, true otherwise.
 */
bool LINK_process_packet (uint8_t* data, uint8_t len)
{
	D_NET printf("LINK_process_packet()\n");
	// device finds its new parent (it is waiting for MOVE RESPONSE message)
	if(NET_STORAGE.waiting_move_response && (((data[0] >> 4) != PT_NETWORK_EXTENDED)
	|| data[10] != PT_DATA_MOVE_RESPONSE))
		return false;
	if (len < NET_HEADER_SIZE) {
		D_NET printf ("Packet is too short!\n");
		return false;
	}
	uint8_t type;
	if((data[0] >> 4) == PT_NETWORK_EXTENDED)
		type = data[10];
	else
		type = data[0] >> 4;

	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	uint8_t scid = data[1] & 0x3f;
	uint8_t dedid[EDID_LENGTH];
	uint8_t sedid[EDID_LENGTH];
	uint8_t net_payload[MAX_NET_PAYLOAD_SIZE];

	array_copy (data + 2, dedid, EDID_LENGTH);
	array_copy (data + 6, sedid, EDID_LENGTH);
	D_NET printf("LINK_process_packet():\n");
	D_NET printf ("type %02x, dcid %02x, scid %02x\n", type, dcid, scid);
	D_NET
		printf
		("sedid %02x %02x %02x %02x, dedid %02x %02x %02x %02x\n",
		 sedid[0], sedid[1], sedid[2], sedid[3], dedid[0], dedid[1], dedid[2], dedid[3]);
	// prepared for some special reaction in case of BROADCAST message receipt
	if(dcid == NET_COORD_ALL || array_cmp(dedid, NET_ED_ALL)) {
		D_NET printf("BROADCAST\n");
	}

	switch (type) {
		case PT_DATA_ACK_DR_WAIT:
			NET_STORAGE.dr_state = DR_DATA_WAITING;
			break;
		case PT_DATA_ACK_DR_SLEEP:
			NET_STORAGE.dr_state = DR_GO_SLEEP;
			break;
		case PT_DATA:
			if (GLOBAL_STORAGE.sleepy_device && NET_STORAGE.dr_state == DR_DATA_WAITING) {
				NET_STORAGE.dr_state = DR_DATA_RECEIVED;
			}
			NET_received (scid, sedid, data + LINK_HEADER_SIZE, len - LINK_HEADER_SIZE);
			break;
		case PT_DATA_MOVE_RESPONSE:
			LINK_move_response_received(dcid);
			NET_STORAGE.waiting_move_response = false;
			break;
		default:
			break;
	}
	return true;
}

void NET_init (struct PHY_init_t *phy_params, struct LINK_init_t *link_params)
{
	D_NET printf("NET_init\n");
	LINK_init (phy_params, link_params);
	uint8_t cfg[5] = {0};
	load_configuration (cfg, 5);
	if (cfg[0] == 0 && cfg[1] == 0 && cfg[2] == 0 && cfg[3] == 0) {
		GLOBAL_STORAGE.nid[0] = 1;
	}
	else {
		GLOBAL_STORAGE.nid[0] = cfg[0];
		GLOBAL_STORAGE.nid[1] = cfg[1];
		GLOBAL_STORAGE.nid[2] = cfg[2];
		GLOBAL_STORAGE.nid[3] = cfg[3];
		GLOBAL_STORAGE.parent_cid = cfg[4];
	}
	NET_STORAGE.waiting_move_response = false;
	NET_STORAGE.move_timeout = 0;
	D_NET printf("%02x %02x %02x %02x %02x\n", GLOBAL_STORAGE.nid[0], GLOBAL_STORAGE.nid[1],
				GLOBAL_STORAGE.nid[2], GLOBAL_STORAGE.nid[3], GLOBAL_STORAGE.parent_cid);
}

bool NET_joined ()
{
	if (GLOBAL_STORAGE.waiting_join_response)
		return false;
	return !zero_address (GLOBAL_STORAGE.nid);
}

bool NET_send (uint8_t tocoord, uint8_t * toed, uint8_t * payload, uint8_t len)
{
	uint8_t i;

	if(network_is_rebuilding() || array_cmp(toed, GLOBAL_STORAGE.edid)) {
		// no packets are sent during network reinitialization
		// the device cannot send packets itself
		D_NET printf("Cant send the packet myself\n");
		return false;
	}

	if (GLOBAL_STORAGE.sleepy_device) {
		// sleepy ED
		NET_STORAGE.dr_state = DR_ACK_WAITING;
		// ED sends request for DATA
		send (PT_DATA_DR, tocoord, toed, payload, len, LINK_DATA_HS4, NOT_EXTENDED);
		for (i = 0; i < MAX_DR_ACK_DELAY && NET_STORAGE.dr_state == DR_ACK_WAITING; i++) {
			// waiting for ACK
			delay_ms (10);
		}
		if (NET_STORAGE.dr_state == DR_ACK_WAITING) {
			// ACK was not received
			D_NET printf ("DR_ACK_WAITING timeout\n");
			return false;
		}
		if (NET_STORAGE.dr_state == DR_GO_SLEEP) {
			// ED will be in sleep mode
			D_NET printf ("DR_GO_SLEEP received\n");
			return true;
		}
		if (NET_STORAGE.dr_state == DR_DATA_RECEIVED) {
			// DATA are received
			D_NET printf ("sleepy message received\n");
			return true;
		}
		NET_STORAGE.dr_state = DR_DATA_WAITING;
		for (i = 0; i < MAX_DR_DATA_DELAY && NET_STORAGE.dr_state == DR_DATA_WAITING; i++) {
			// waiting for DATA
			delay_ms (10);
		}
		if (NET_STORAGE.dr_state == DR_DATA_RECEIVED) {
			// DATA are received
			D_NET printf ("sleepy message received\n");
			return true;
		}
		else {
			// DATA were not received
			D_NET printf ("sleepy message timeout\n");
			return false;
		}
	}
	else {
		// not sleepy ED
		return send (PT_DATA, tocoord, toed, payload, len, LINK_DATA_HS4, NOT_EXTENDED);
	}
}

bool NET_join ()
{
	uint8_t tmp[NET_HEADER_SIZE];
	uint8_t index = 0;
	if (GLOBAL_STORAGE.waiting_join_response) {
		// next JOIN REQUEST can be sent after finishing previous joining process
		return false;
	}
	tmp[index++] = (PT_DATA_JOIN_REQUEST << 4) & 0xf0;
	// sleepy or ready device
	if (GLOBAL_STORAGE.sleepy_device) {
		tmp[index++] = SLEEPY_ED;
	}
	else {
		tmp[index++] = READY_ED;
	}
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = GLOBAL_STORAGE.edid[0];
	tmp[index++] = GLOBAL_STORAGE.edid[1];
	tmp[index++] = GLOBAL_STORAGE.edid[2];
	tmp[index++] = GLOBAL_STORAGE.edid[3];
	GLOBAL_STORAGE.waiting_join_response = true;
	if (LINK_send_join_request (tmp, index)) {
		D_NET printf ("NET_join(): ACK JOIN REQUEST received\n");
		for (uint8_t i = 0; i < MAX_JOIN_DELAY; i++) {
			// waiting for JOIN RESPONSE packet
			delay_ms (100);
			if (!GLOBAL_STORAGE.waiting_join_response) {
				break;
			}
		}
	}
	if (GLOBAL_STORAGE.waiting_join_response) {
		// JOIN RESPONSE packet was not received
		D_NET printf ("NET_join(): timeout\n");
		GLOBAL_STORAGE.waiting_join_response = false;
		return false;
	}
	D_NET printf ("NET_join(): success\n");
	return true;
}

/**
 *  Checks if device movement is successful.
 */
void LINK_timer_counter()
{
	if(NET_STORAGE.waiting_move_response) {
		NET_STORAGE.move_timeout--;
		if(NET_STORAGE.move_timeout == 0) {
			D_NET printf("Device movement failed!\n");
			//NET_STORAGE.waiting_move_response = 0;
			fitp_send_move_request();
		}
	}
}
