/**
* @file net.c
*/

#include "net.h"
#include "net_common.h"
#include "log.h"

/*! maximum payload length of ROUTING DATA message */
/*! if the length is greater than 40 B, packet is segmented */
#define MAX_ROUTING_DATA 40
/*! maximum delay for MOVE REPONSE message */
/*! if 2 second delay if required, then MAX_MOVE_DELAY is: */
/*! MAX_MOVE_DELAY = required_delay [ms] / 50 [ms] */
#define MAX_MOVE_DELAY 40
/*! maximum delay for JOIN REPONSE message */
/*! if 2 second delay if required, then MAX_JOIN_DELAY is: */
/*! MAX_JOIN_DELAY = required_delay [ms] / 50 [ms] */
#define MAX_JOIN_DELAY 40

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
	NET_current_processing_packet_t processing_packet;	/**< Structure for currently processed packet. */
	bool waiting_move_response;													/**< Flag if network is being reinitialized. */
	uint8_t move_timeout;																/**< Timeout for MOVE RESPONSE message (containing new parent ID). */
} NET_STORAGE;

/*
 * Supporting functions.
 */
extern void delay_ms (uint16_t maximum);
extern bool zero_address (uint8_t * addr);
extern void array_copy (uint8_t * src, uint8_t * dst, uint8_t size);
extern bool array_cmp (uint8_t * array_a, uint8_t * array_b);
extern void fitp_joining_enable(uint8_t timeout);
extern void fitp_joining_disable();
extern void save_configuration (uint8_t * buf, uint8_t len);
extern void load_configuration (uint8_t * buf, uint8_t len);

uint8_t get_next_coord (uint8_t destination_cid);

/**
 * Sends packet.
 * @param msg_type					Message type on network layer.
 * @param tocoord 					Destination coordinator ID.
 * @param toed 							Destination end device ID.
 * @param payload 					Payload.
 * @param len 							Payload length.
 * @param transfer_type 		Transfer type on link layer.
 * @param msg_type_ext 			Message type on network layer (valid if msg_type is set to F (hexa)).
 * @return Returns false, if next coordinator on the route towards the destination
 * 				 is not found or packet is not successfully sent, true otherwise.
 */
bool send (uint8_t msg_type, uint8_t tocoord, uint8_t* toed,
					 uint8_t* payload, uint8_t len, uint8_t transfer_type, uint8_t msg_type_ext)
{
	D_NET printf("send()\n");
	uint8_t tmp[MAX_NET_PAYLOAD_SIZE];
	uint8_t index = 0;
	uint8_t address_coord;
	// network header
	tmp[index++] = (msg_type << 4) | ((tocoord >> 2) & 0x0f);
	tmp[index++] = ((tocoord << 6) & 0xc0) | (GLOBAL_STORAGE.cid & 0x3f);
	tmp[index++] = toed[0];
	tmp[index++] = toed[1];
	tmp[index++] = toed[2];
	tmp[index++] = toed[3];
	tmp[index++] = GLOBAL_STORAGE.edid[0];
	tmp[index++] = GLOBAL_STORAGE.edid[1];
	tmp[index++] = GLOBAL_STORAGE.edid[2];
	tmp[index++] = GLOBAL_STORAGE.edid[3];
	if (msg_type == PT_NETWORK_EXTENDED)
		tmp[index++] = msg_type_ext;

	for (uint8_t i = 0; i < len && index < MAX_NET_PAYLOAD_SIZE; i++)
		tmp[index++] = payload[i];

	if (msg_type_ext == PT_DATA_MOVE_REQUEST) {
		// MOVE REQUEST message
		LINK_send_broadcast(tmp, index);
		return true;
	}
	/*for(uint8_t i = 0; i < index; i++) {
	   printf("%02x ", tmp[i]);
	  }
	  printf("\n"); */
	if (msg_type == PT_NETWORK_ROUTING_DATA) {
		// ROUTING DATA message
		D_NET printf("send ROUTING DATA\n");
	}
	// search next COORD address
	address_coord = get_next_coord (tocoord);
	D_NET printf ("next COORD: %d\n", address_coord);
	// no next COORD address was searched
	if(address_coord == INVALID_CID)
		return false;
	return LINK_send_coord (false, &address_coord, tmp, index, transfer_type);
}

bool NET_is_routing_data_message(uint8_t msg_type)
{
    if(((msg_type >> 4) & 0x0f) == PT_NETWORK_ROUTING_DATA)
    	return true;
    return false;
}

void NET_send_broadcast (uint8_t msg_type, uint8_t* payload, uint8_t len)
{
	if (msg_type == PT_DATA_MOVE_REQUEST) {
		// MOVE REQUEST message uses extended byte
		send (PT_NETWORK_EXTENDED, NET_COORD_ALL, NET_ED_ALL, payload, len, LINK_DATA_BROADCAST, msg_type);
	}
	send (msg_type, NET_COORD_ALL, NET_ED_ALL, payload, len, LINK_DATA_BROADCAST, NOT_EXTENDED);
}

void NET_send_move_request(uint8_t* payload, uint8_t len)
{
	// timeout for MOVE RESPONSE message receiving
	NET_STORAGE.move_timeout = MAX_MOVE_DELAY;
	NET_STORAGE.waiting_move_response = true;
	NET_send_broadcast(PT_DATA_MOVE_REQUEST, payload, len);
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
 * @param data 			Data.
 * @param len 			Data length.
 */
void LINK_error_handler_ed (uint8_t* data, uint8_t len)
{
	D_NET printf ("ED - error during transmitting!\n");
	/*for(uint8_t i = 0; i < len; i++)
		printf("%02x ", data[i]);
	printf("\n");*/
	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	// if packet is sent down to network, MOVE REQUEST packet is not sent
	if (get_next_coord(dcid) != GLOBAL_STORAGE.parent_cid) {
		return;
	}
	if (!NET_STORAGE.waiting_move_response) {
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
	D_NET printf("LINK_notify_send_done()\n");
	NET_notify_send_done();
}

/**
 * Enables pair mode.
 * @param timeout	Duration of pair mode.
 */
void NET_joining_enable (uint8_t timeout)
{
	fitp_joining_enable(timeout);
	D_NET printf("NET_joining_enable()");
}

/**
 * Disables pair mode.
 */
void NET_joining_disable ()
{
	fitp_joining_disable();
	D_NET printf("NET_joining_disable()");
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
 * Disables pair mode and checks if device movement is successful.
 */
void LINK_timer_counter()
{
		if (GLOBAL_STORAGE.pair_mode) {
			GLOBAL_STORAGE.pair_mode_timeout--;
			if(GLOBAL_STORAGE.pair_mode_timeout == 0)
				NET_joining_disable();
		}
		if (NET_STORAGE.waiting_move_response) {
			NET_STORAGE.move_timeout--;
			if (NET_STORAGE.move_timeout == 0) {
				printf("Device movement failed!\n");
				//NET_STORAGE.waiting_move_response = 0;
				fitp_send_move_request();
			}
		}
}

/**
 * Routes MOVE REQUEST message as MOVE REQUEST ROUTE message.
 * @param data			Data.
 * @param len				Data length.
 * @return Returns true if MOVE REQUEST ROUTE packet is successfully sent,
 *				 false otherwise.
 */
bool move_request_received(uint8_t* data, uint8_t len)
{
	// MOVE REQUEST is too short
	if (len < 12) {
		return false;
	}
	uint8_t address_coord = LINK_cid_mask(get_next_coord(0));
	if(address_coord == INVALID_CID)
		return false;
	uint8_t signal_strength = LINK_get_measured_noise();
	// MOVE REQUEST ROUTE length on network layer is 12 bytes
	uint8_t tmp[12];
	uint8_t index = 0;
	tmp[index++] = (data[0] & 0xf0) | 0x00;
	tmp[index++] = 0x00 | (GLOBAL_STORAGE.cid & 0x3f);
	for(uint8_t i = 2; i < len - 2; i++)
		tmp[index++] = data[i];
	tmp[index++] = PT_DATA_MOVE_REQUEST_ROUTE;
	tmp[index++] = signal_strength;
	//delay_ms(25);
	return LINK_send_coord(false, &address_coord, tmp, index, LINK_DATA_HS4);
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
	// device is waiting for new parent (it is waiting for MOVE RESPONSE message)
	if(NET_STORAGE.waiting_move_response && ((((data[0] & 0xf0) >> 4) != PT_NETWORK_EXTENDED)
	|| data[10] != PT_DATA_MOVE_RESPONSE))
		return false;
	if (len < NET_HEADER_SIZE) {
		D_NET printf ("Packet is too short!\n");
		return false;
	}

	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	uint8_t dedid[EDID_LENGTH];
	array_copy(data + 2, dedid, EDID_LENGTH);

	if(((data[0] & 0xf0) >> 4) == PT_NETWORK_EXTENDED)
		NET_STORAGE.processing_packet.type = data[10];
	else
		NET_STORAGE.processing_packet.type = data[0] >> 4;
	NET_STORAGE.processing_packet.scid = data[1] & 0x3f;
	for (uint8_t i = 0; i < EDID_LENGTH; i++)
		NET_STORAGE.processing_packet.sedid[i] = data[i + 6];
	uint8_t i = 0;
	for (; i < (len - NET_HEADER_SIZE) && i < MAX_NET_PAYLOAD_SIZE; i++)
		NET_STORAGE.processing_packet.payload[i] = data[i + NET_HEADER_SIZE];
	NET_STORAGE.processing_packet.len = i;
	if(NET_STORAGE.processing_packet.type == PT_DATA_PAIR_MODE_ENABLED)
		NET_joining_enable(data[11]);
	if(NET_STORAGE.processing_packet.type == PT_DATA_MOVE_REQUEST){
		move_request_received(data, len);
		D_NET printf("MOVE REQUEST ROUTE\n");
	}
	if(NET_STORAGE.processing_packet.type == PT_DATA_MOVE_RESPONSE){
		D_NET printf("MOVE RESPONSE\n");
		LINK_move_response_received(dcid);
		NET_STORAGE.waiting_move_response = false;
	}
	if(NET_STORAGE.processing_packet.type == PT_DATA) {
		D_NET printf("DATA\n");
		NET_received(NET_STORAGE.processing_packet.scid, NET_STORAGE.processing_packet.sedid,
			NET_STORAGE.processing_packet.payload, NET_STORAGE.processing_packet.len);
	}
	if(dcid == NET_COORD_ALL || array_cmp(dedid, NET_ED_ALL)) {
		D_NET printf("BROADCAST\n");
	}
	return true;
}

/**
 * Sets received network ID, parent ID and coordinator ID.
 * @param data 					Data.
 * @param len 					Data length.
 * @return Returns false if data length is too short or JOIN RESPONSE message
 * is not addressed to this device, true otherwise.
 */
bool LINK_join_response_received (uint8_t* data, uint8_t len)
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
	GLOBAL_STORAGE.cid = data[14];
	D_NET printf
		("LINK_join_response_received(): NID %02x %02x %02x %02x, PARENT CID %02x, CID %02x\n",
		 GLOBAL_STORAGE.nid[0], GLOBAL_STORAGE.nid[1],
		 GLOBAL_STORAGE.nid[2], GLOBAL_STORAGE.nid[3],
		 GLOBAL_STORAGE.parent_cid, GLOBAL_STORAGE.cid);
	return true;
}

/**
 * Routes JOIN REQUEST message as JOIN REQUEST ROUTE message.
 * @param RSSI 					Received signal strength.
 * @param edid		 			Source end device ID.
 * @param data 					Data.
 * @param len 					Data length.
 * @return Returns false if packet length is too short, parent CID is not
 *				 found or packet is not successfully sent, true otherwise.
 */
bool LINK_join_request_received (uint8_t RSSI, uint8_t * edid, uint8_t * data, uint8_t len)
{
	D_NET printf("LINK_join_request_received()\n");
	if (len < NET_HEADER_SIZE)
		return false;
		// JOIN REQUEST ROUTE length on network layer is 12 bytes
	uint8_t tmp[12];
	uint8_t index = 0;
	tmp[index++] = (PT_DATA_JOIN_REQUEST_ROUTE << 4) | 0;;
	tmp[index++] = 0 | (GLOBAL_STORAGE.cid & 0x3f);
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = 0;
	tmp[index++] = edid[0];
	tmp[index++] = edid[1];
	tmp[index++] = edid[2];
	tmp[index++] = edid[3];
	// the device type
	tmp[index++] = data[1];
	tmp[index++] = RSSI;
	D_NET printf("RSSI: %02x\n", tmp[index-1]);
	uint8_t address_coord = get_next_coord (0);
	if(address_coord == INVALID_CID)
		return false;
	return LINK_send_coord (false, &address_coord, tmp, index, LINK_DATA_WITHOUT_ACK);
}

/**
 * Sets newly received parent ID and updates routing tree.
 * @param parent 			Parent ID.
 */
void LINK_move_response_received (uint8_t parent)
{
	GLOBAL_STORAGE.parent_cid = LINK_cid_mask (parent);
	GLOBAL_STORAGE.routing_tree[GLOBAL_STORAGE.cid] = GLOBAL_STORAGE.parent_cid;
	D_NET printf ("Moved to %d\n", GLOBAL_STORAGE.parent_cid);
}

/**
 * Reinitializes network.
 * @param data 			Data.
 * @param len 			Data length.
 */
void LINK_error_handler_coord (uint8_t* data, uint8_t len)
{
	D_NET printf ("COORD - error during transmitting!\n");
	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	if(get_next_coord(dcid) != GLOBAL_STORAGE.parent_cid) {
		// if packet is sent down to network, MOVE REQUEST packet is not sent
		return;
	}
	if(!NET_STORAGE.waiting_move_response) {
		// check if network reinitialization has not been already initiated
		NET_STORAGE.waiting_move_response = true;
		fitp_send_move_request();
	}
}

/**
 * Searches next coordinator ID on the packet route to destination coordinator.
 * @param dst_cid 	Destination coordinator ID.
 * @return Returns next coordinator ID.
 */
uint8_t get_next_coord (uint8_t dst_cid)
{
	uint8_t address;
	uint8_t previous_address;

	if(dst_cid == 0) {
		// packet is routed towards PAN coordinator, send packet to parent
		return GLOBAL_STORAGE.parent_cid;
	}
	address = dst_cid;
	while (1) {
		if (address == GLOBAL_STORAGE.cid) {
			address = previous_address;
			return address;
		}
		previous_address = address;
		address = GLOBAL_STORAGE.routing_tree[address];
		if (address == 0xff) {
			// the same level
			return GLOBAL_STORAGE.parent_cid;
		}
	}
	// to avoid warning
	return address;
}

/**
 * Sends JOIN RESPONSE message.
 * @param data		 			Data.
 * @param len 					Data length.
 */
void send_join_response (uint8_t* data, uint8_t len)
{
	// JOIN RESPONSE length on network layer is 15 bytes
	uint8_t tmp[15];
	uint8_t index = 0;

	tmp[index++] = ((PT_DATA_JOIN_RESPONSE << 4) & 0xf0) | (data[0] & 0x0f);
	for (uint8_t i = 1; i < len; i++) {
		tmp[index++] = data[i];
	}
	LINK_send_join_response (tmp + 2, tmp, index);
}

/**
 * Sends MOVE RESPONSE message.
 * @param data		 			Data.
 * @param len 					Data length.
 */
void send_move_response(uint8_t* data, uint8_t len)
{
	// MOVE RESPONSE length on network layer is 12 bytes
	uint8_t tmp[12];
  uint8_t index = 0;
	for (uint8_t i = 0; i < len - 2; i++)
		tmp[index++] = data[i];
	tmp[index++] = PT_DATA_MOVE_RESPONSE;
	tmp[index++] = FITP_MOVE_RESPONSE;
	LINK_send_coord(true, tmp + 2, tmp, index, LINK_DATA_WITHOUT_ACK);
}

/**
 * Processes received packet for COORD.
 * @param data		 			Data.
 * @param len 					Data length.
 */
void local_process_packet (uint8_t* data, uint8_t len)
{
	uint8_t type = data[0] >> 4;
	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	uint8_t scid = data[1] & 0x3f;
	uint8_t dedid[EDID_LENGTH];
	uint8_t sedid[EDID_LENGTH];
	uint8_t payload[MAX_NET_PAYLOAD_SIZE];
	uint8_t payload_len = len - NET_HEADER_SIZE;
	D_NET printf ("local_process_packet(): type %02x dcid %02x scid %02x\n", type, dcid, scid);

	array_copy (data + 2, dedid, EDID_LENGTH);
	array_copy (data + 6, sedid, EDID_LENGTH);
	array_copy (data + NET_HEADER_SIZE, payload, payload_len);

	if(type == PT_DATA) {
		NET_received(scid, sedid, payload, payload_len);
	}
}

/**
 * Checks if received packet is for direct descendant.
 * @param toed	Destination end device ID.
 * @return Returns true if packet is for direct descendant, false otherwise.
 */
bool is_for_my_child(uint8_t* toed)
{
	if (array_cmp(GLOBAL_STORAGE.edid, toed) || zero_address(toed))
		return false;
	return true;
}

/**
 * Processes received packet if it is addressed to this coordinator or its direct
 * descendants. Otherwise, it routes the received packet towards destination.
 * @param data		 			Data.
 * @param len 					Data length.
 * @param transfer_type Transfer type on link layer.
 * @return Returns false if packet is too short, network reinitialization is being performed,
 *  			 packet is not successfully sent or parent ID is not found, true otherwise.
 */
bool LINK_route (uint8_t * data, uint8_t len, uint8_t transfer_type)
{
	D_NET printf ("LINK_route()\n");
	if (len < NET_HEADER_SIZE)
		return false;
	if(network_is_rebuilding() && !NET_is_move_response(data[10]))
		return false;

	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	uint8_t type = (data[0] & 0xf0) >> 4;
	if (dcid == GLOBAL_STORAGE.cid) {
		// packet is for this coordinator
		if (is_for_my_child(data + 2)) {
			D_NET printf("for my child\n");
			if(type == PT_NETWORK_EXTENDED && data[10] == PT_DATA_MOVE_RESPONSE_ROUTE) {
				send_move_response(data, len);
				D_NET printf("MOVE RESPONSE\n");
			 }
			else if (type == PT_DATA_JOIN_RESPONSE_ROUTE) {
				D_NET printf("JOIN REPONSE\n");
				send_join_response (data, len);
			}
			else {
				return LINK_send_coord(true, data + 2, data, len, transfer_type);
			}
		}
		else {
			D_NET printf("For me\n");
			local_process_packet (data, len);
		}
		return true;
	}
	else {
		// packet is not for this coordinator, route it
		uint8_t address_coord = LINK_cid_mask (get_next_coord (dcid));
		if(address_coord == INVALID_CID)
			return false;
		return LINK_send_coord(false, &address_coord, data, len, transfer_type);
	}
}

void NET_init (struct PHY_init_t *phy_params, struct LINK_init_t *link_params)
{
	D_NET printf("NET_init\n");
	LINK_init(phy_params, link_params);
	GLOBAL_STORAGE.routing_enabled = true;
	NET_STORAGE.move_timeout = 0;

	uint8_t cfg[5] = {0};

	// load_configuration(cfg, 5);
	if (cfg[0] != 0xff && cfg[1] != 0xff && cfg[3] != 0xff && cfg[4] != 0xff) {
		GLOBAL_STORAGE.nid[0] = cfg[0];
		GLOBAL_STORAGE.nid[1] = cfg[1];
		GLOBAL_STORAGE.nid[2] = cfg[2];
		GLOBAL_STORAGE.nid[3] = cfg[3];
		GLOBAL_STORAGE.parent_cid = cfg[4];
	}
	else {
		GLOBAL_STORAGE.nid[0] = 0;
		GLOBAL_STORAGE.nid[1] = 0;
		GLOBAL_STORAGE.nid[2] = 0;
		GLOBAL_STORAGE.nid[3] = 0;
		GLOBAL_STORAGE.parent_cid = 0;
	}
	NET_STORAGE.waiting_move_response = false;
	for(uint8_t i = 0; i < MAX_COORD; i++)
		GLOBAL_STORAGE.routing_tree[i] = INVALID_CID;
}

bool NET_joined ()
{
	if (GLOBAL_STORAGE.waiting_join_response)
		return false;
	return !zero_address (GLOBAL_STORAGE.nid);
}

bool NET_send (uint8_t tocoord, uint8_t* toed, uint8_t* payload, uint8_t len)
{
	D_NET printf("NET_send()\n");

	if(network_is_rebuilding() || tocoord == GLOBAL_STORAGE.cid || array_cmp(toed, GLOBAL_STORAGE.edid)){
		// no packets are sent during network reinitialization
		// device cannot send packets itself
		D_NET printf("Cant send packet myself\n");
		return false;
	}
	return send (PT_DATA, tocoord, toed, payload, len, LINK_DATA_HS4, NOT_EXTENDED);
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
	// coordinator device
	tmp[index++] = COORD;
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
				delay_ms (50);
				if (!GLOBAL_STORAGE.waiting_join_response)
							break;
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
 * Checks if coordinator 1 is in routing subtree of coordinator 2.
 * @param cid_1	Coordinator 1 ID.
 * @param cid_2	Coordinator 2 ID.
 * @return Returns true if coordinator 1 is routing subtree of coordinator 2, false otherwise.
 */
bool is_in_subtree (uint8_t cid_1, uint8_t cid_2)
{
	int i = 0;
	while (i < MAX_COORD) {
		if (cid_1 == cid_2) {
			return true;
		}
		cid_1 = GLOBAL_STORAGE.routing_tree[cid_1];
		i++;
	}
	return false;
}

/**
 * Sends a routing table.
 * @param tocoord				Destination coordinator ID.
 * @param toed					Destination end device ID.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void send_routing_table (uint8_t tocoord, uint8_t* toed, uint8_t* payload, uint8_t len)
{
	uint8_t config_packet = 0;
	uint8_t packet_count = 0;
	// number of packets
	if (len % MAX_ROUTING_DATA != 0)
		packet_count++;
	packet_count += len / MAX_ROUTING_DATA;
	config_packet = packet_count << 4;

	int payload_index = 0;
	uint8_t data[MAX_ROUTING_DATA + 1];
	for (uint8_t j = 0; MAX_ROUTING_DATA * j < len; j++) {
		// order of current packet
		config_packet++;
		data[0] = config_packet;
		uint8_t i;
		for (i = 1; i <= MAX_ROUTING_DATA && payload_index < len; i += 2) {
			// fill routing table for COORD
			if (payload_index % 2 == 0 && is_in_subtree (payload[payload_index], tocoord)) {
				data[i] = payload[payload_index++];
				data[i + 1] = payload[payload_index++];
				continue;
			}
			payload_index += 2;
			i -= 2;
		}
		send (PT_NETWORK_ROUTING_DATA, tocoord, toed, data, i, LINK_DATA_WITHOUT_ACK, NOT_EXTENDED);
	}
}

void NET_process_routing_table (uint8_t* payload, uint8_t len)
{
	// 1 + 2 * 64 - INFO BYTE + CID and PARENT CID (2) * maximum number of COORD (64)
	uint8_t r_table[129];
	int k = 0;

	// count of packets
	int count = payload[0] >> 4;
	// order of current packet
	int act_packet = payload[0] & 0x0f;

	D_NET printf("ROUTING TREE\n");
	// save routing data to routing tree
	for (uint8_t i = 1; i < len; i += 2) {
		GLOBAL_STORAGE.routing_tree[payload[i]] = payload[i + 1];
		D_NET printf("CID: %02x\n", payload[i]);
		D_NET printf("PARENT CID: %02x\n", payload[i + 1]);
	}
	// no more descendants, do not send routing table
	if(len == 3)
		return;

	// fill routing table for direct descendants
	uint8_t tmp;
	for(uint8_t i = 0; i < MAX_COORD; i++)
	{
		// an empty record, ignore it
		if(GLOBAL_STORAGE.routing_tree[i] == INVALID_CID) {
				continue;
		}
		// device is a direct descendant of this COORD
		if(GLOBAL_STORAGE.routing_tree[i] == GLOBAL_STORAGE.cid) {
			r_table[k] = i;
			r_table[k+1] = GLOBAL_STORAGE.cid;
			k = k + 2;
		}
		else {
			tmp = GLOBAL_STORAGE.routing_tree[i];
			// device is not a descendant of this COORD, its parent is PAN
			if(GLOBAL_STORAGE.routing_tree[GLOBAL_STORAGE.routing_tree[i]] == 0)
					continue;
			// device can be an indirect descendant of this COORD
			for(int j = i; j >= 0; j--) {
				if(GLOBAL_STORAGE.routing_tree[j] == GLOBAL_STORAGE.cid) {
						r_table[k] = i;
						r_table[k+1] = tmp;
						k = k + 2;
				}
			}
		}
	}
	// send routing table to direct descendants
	for (int i = 0; i < MAX_COORD; i++) {
		// device is not my direct descendant
		if (GLOBAL_STORAGE.routing_tree[i] != GLOBAL_STORAGE.cid)
				continue;
		// record for PAN is at the index 0
		if(i == 0)
				return;

		uint8_t zeros[] = {0x00, 0x00, 0x00, 0x00};
		send_routing_table (i, zeros, r_table, k);
	}
}

/**
 * Checks if pair mode is enabled.
 * @return Returns true if pair mode is set, false otherwise.
 */
bool NET_is_set_pair_mode ()
{
	if(GLOBAL_STORAGE.pair_mode)
		return true;
	return false;
}
