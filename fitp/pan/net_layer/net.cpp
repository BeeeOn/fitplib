/**
* @file net.cc
*/

#include "pan/net_layer/net.h"
#include "common/net_layer/net_common.h"
#include "common/log/log.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>

using namespace std;

/*! maximum payload length of ROUTING DATA message */
/*! if the length is greater than 40 B, packet is segmented */
#define MAX_ROUTING_DATA 40
/*! maximum number of SLEEPY messages */
#define MAX_SLEEPY_MESSAGES 10
/*! maximum number of JOIN messages */
#define MAX_JOIN_MESSAGES 5
/*! maximum number of MOVE messages */
#define MAX_MOVE_MESSAGES 7
#define MAX_MESSAGES 10
/*! delay before sending of required data to end device (SLEEPY message) */
#define ACK_DATA_DELAY 200
/*! maximum time for MOVE REQUEST (ROUTE) message collecting */
/*! if 3 second delay if required, then MAX_MOVE_DELAY is: */
/*! MAX_MOVE_DELAY = required_delay [ms] / 50 [ms] */
#define MAX_MOVE_DELAY 60
/*! maximum time for JOIN REQUEST (ROUTE) message collecting */
/*! if 3 second delay if required, then MAX_JOIN_DELAY is: */
/*! MAX_JOIN_DELAY = required_delay [ms] / 50 [ms] */
//#define MAX_JOIN_DELAY 3
/*! pair mode duration (in seconds) */
//#define PAIR_MODE_TIMEOUT 30
/*! maximum value of 8b counter */
#define MAX_CNT_VALUE 255
/*! limit value of 8b counter (for joining process), if value is greater than limit, counter overflows ()*/
#define JOIN_CNT_OVERFLOW_VALUE (MAX_CNT_VALUE - MAX_JOIN_DELAY)
/*! limit value of 8b counter (for network reinitialization), if value is greater than limit, counter overflows ()*/
#define MOVE_CNT_OVERFLOW_VALUE (MAX_CNT_VALUE - MAX_MOVE_DELAY)

/**
 * Structure for currently processed packet.
 */
typedef struct {
	uint8_t type;														/**< Message type. */
	uint8_t sedid[EDID_LENGTH];							/**< Source end device ID. */
	//uint8_t payload[MAX_NET_PAYLOAD_SIZE];	/**< Payload. */
	//uint8_t len;														/**< Payload length. */
	int device;
	bool empty;
} NET_received_packets_t;

/**
 * Structure for SLEEPY messages.
 */
typedef struct {
	uint8_t toed[EDID_LENGTH];							/**< Destination end device ID. */
	uint8_t payload[MAX_NET_PAYLOAD_SIZE];	/**< Data. */
	uint8_t len;														/**< Data length. */
	bool valid;															/**< Flag if record is valid. */
} NET_sleepy_messages_t;

/**
 * Structure for network layer.
 */
struct NET_storage_t {
	NET_received_packets_t received_packets[MAX_MESSAGES];					/**< Structure for currently processed packet. */
	NET_sleepy_messages_t sleepy_messages[MAX_SLEEPY_MESSAGES];	/**< Structure for SLEEPY messages. */
	NET_join_move_info_t join_info[MAX_JOIN_MESSAGES];					/**< Structure for JOIN REQUEST (ROUTE) messages. */
	NET_join_move_info_t move_info[MAX_MOVE_MESSAGES];					/**< Structure for MOVE REQUEST (ROUTE) messages. */
	uint8_t timer_counter;																			/**< Timer controlling JOIN RESPONSE (ROUTE) and MOVE RESPONSE (ROUTE) sending. */
	uint16_t pair_mode_timeout;
	uint16_t join_cnt_overflow_value;
} NET_STORAGE;

/*
 * Supporting functions.
 */
extern bool zero_address (uint8_t * addr);
extern bool array_cmp (uint8_t * array_a, uint8_t * array_b);
extern void array_copy (uint8_t * src, uint8_t * dst, uint8_t size);
extern void delay_ms (uint16_t t);
extern uint8_t fitp_find_parent(NET_join_move_info_t* join_info, uint8_t* edid, uint8_t max_messages);
extern void save_configuration (uint8_t * buf, uint8_t len);
extern void load_configuration (uint8_t * buf, uint8_t len);

uint8_t get_next_coord (uint8_t destination_cid);
bool is_for_my_child(uint8_t* edid);
bool change_ed_parent(uint8_t* edid, uint8_t parent);
void load_routing_table();
uint8_t get_parent_cid (uint8_t* edid);
bool is_coord_device (uint8_t * edid, uint8_t cid);
bool NET_remove (uint8_t* edid);

// for idication that counter overflowed than timeout for sending of
// JOIN RESPONSE (ROUTE) or MOVE RESPONSE (ROUTE) message expired
bool overflow_joining = false;
bool overflow_moving = false;

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
	D_NET printf("=== send()\n");
	uint8_t tmp[MAX_NET_PAYLOAD_SIZE];
	uint8_t index = 0;
	uint8_t address_coord;

	if (!is_coord_device(toed, tocoord) && tocoord != NET_COORD_ALL && !array_cmp(toed, NET_ED_ALL)) {
		// packet is not for coordinator or it is not a broadcast packet
		// find parent of destination end device
		tocoord = get_parent_cid(toed);
		D_NET printf("Dst COORD: %02x", tocoord);
	}
	D_NET printf("After 1. if\n");
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
	if(msg_type == PT_NETWORK_EXTENDED)
		tmp[index++] = msg_type_ext;
	if(msg_type_ext == PT_DATA_PAIR_MODE_ENABLED)
		tmp[index++] = NET_STORAGE.pair_mode_timeout;

	for (uint8_t i = 0; i < len && index < MAX_NET_PAYLOAD_SIZE; i++) {
		tmp[index++] = payload[i];
	}

	if(msg_type == PT_NETWORK_ROUTING_DATA) {
		D_NET printf("ROUTING DATA sent!\n");
		address_coord = get_next_coord (tocoord);
		if(address_coord == INVALID_CID)
			return false;
		return LINK_send_coord(false, &address_coord, tmp, index, transfer_type);
	}
	else if (msg_type_ext == PT_DATA_PAIR_MODE_ENABLED) {
		D_NET printf("PT_DATA_PAIR_MODE_ENABLED\n");
		LINK_send_broadcast(tmp, index);
	}
	else if ((!zero_address(toed)) && (is_for_my_child(toed))
					&& msg_type_ext != PT_DATA_JOIN_RESPONSE_ROUTE
					&& msg_type_ext != PT_DATA_MOVE_RESPONSE_ROUTE) {
		D_NET printf("Message for PAN child (ED)!");
		return LINK_send_coord(true, toed, tmp, index, transfer_type);
	}
	else {
		D_NET printf("Message for COORD\n");
		address_coord = get_next_coord (tocoord);
		if(address_coord == INVALID_CID)
			return false;
		return LINK_send_coord(false, &address_coord, tmp, index, transfer_type);
	}

	return false;
}

/**
 * Broadcasts packet.
 * @param msg_type 			Message type on network layer.
 * @param msg_type_ext	Message type on network layer (valid if msg_type is set to F (hexa)).
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
bool NET_send_broadcast (uint8_t msg_type, uint8_t msg_type_ext, uint8_t* payload, uint8_t len)
{
	send (msg_type, NET_COORD_ALL, NET_ED_ALL, payload, len, LINK_DATA_BROADCAST, msg_type_ext);
	return true;
}

/**
 * Sends MOVE RESPONSE packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 					Destination end device ID.
 */
void NET_send_move_response(uint8_t* payload, uint8_t len, uint8_t tocoord, uint8_t* toed)
{
	change_ed_parent(toed, tocoord);
	send (PT_NETWORK_EXTENDED, tocoord, toed, payload, len, LINK_DATA_WITHOUT_ACK, PT_DATA_MOVE_RESPONSE);
	//delay_ms(50);
	load_routing_table();
}

/**
 * Sends MOVE RESPONSE ROUTE packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 					Destination end device ID.
 */
void NET_send_move_response_route(uint8_t* payload, uint8_t len, uint8_t tocoord, uint8_t* toed)
{
	D_NET printf("NET_send_move_response_route()\n");
	change_ed_parent(toed, tocoord);

	send (PT_NETWORK_EXTENDED, tocoord, toed, payload, len, LINK_DATA_HS4, PT_DATA_MOVE_RESPONSE_ROUTE);
	//delay_ms(50);
	load_routing_table();
}

// ===== BEGIN: DEVICE TABLE SUPPORT FUNCTIONS  =====
/**
 * Prints device table.
 */
void print_device_table ()
{
	D_NET printf ("\nEDID\tCID    PARENT    SLEEPY    COORD\n");
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid) {
			D_NET printf ("%02x %02x %02x %02x %02d\t%02d\t%d\t%d\n",
							GLOBAL_STORAGE.devices[i].edid[0],
							GLOBAL_STORAGE.devices[i].edid[1],
							GLOBAL_STORAGE.devices[i].edid[2],
							GLOBAL_STORAGE.devices[i].edid[3],
							GLOBAL_STORAGE.devices[i].cid, GLOBAL_STORAGE.devices[i].parent_cid,
							GLOBAL_STORAGE.devices[i].sleepy, GLOBAL_STORAGE.devices[i].coord);
		}
	}
	D_NET printf ("---------------------------------\n");
}

/*
 * Checks if device is in device table.
 * @param edid	End device ID.
 * @return Returns true if device is in device table, false otherwise.
 */
bool is_my_device (uint8_t* edid)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid
				&& array_cmp (GLOBAL_STORAGE.devices[i].edid, edid))
			return true;
	}
	return false;
}

/*
 * Checks if device is direct descendant of PAN.
 * @param edid	End device ID.
 * @return Returns true if device is direct descendant of PAN, false otherwise.
 */
bool is_for_my_child(uint8_t* edid)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid
			&& array_cmp (GLOBAL_STORAGE.devices[i].edid, edid) && GLOBAL_STORAGE.devices[i].parent_cid == 0x00)
				return true;
	}
	return false;
}

/*
 * Checks if end device is sleepy.
 * @param edid	End device ID.
 * @return Returns true if end device is sleepy, false otherwise.
 */
bool is_sleepy_device (uint8_t* edid)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				array_cmp (GLOBAL_STORAGE.devices[i].edid, edid) &&
				GLOBAL_STORAGE.devices[i].sleepy)
			return true;
	}
	return false;
}

/*
 * Checks if device is COORD.
 * @param edid	End device ID.
 * @param cid		Coordinator ID.
 * @return Returns true if device is coordinator, false otherwise.
 */
bool is_coord_device (uint8_t * edid, uint8_t cid)
{
	if(cid == 0 && !zero_address(edid))
		return false;
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				(array_cmp (GLOBAL_STORAGE.devices[i].edid, edid) || (GLOBAL_STORAGE.devices[i].cid == cid)) &&
				GLOBAL_STORAGE.devices[i].coord){
			return true;
		}
	}
	return false;
}

/*
 * Changes device parent.
 * @param edid			End device ID.
 * @param parent		New parent of end device.
 * @return Returns true if parent is successfully changed, false otherwise.
 */
bool change_ed_parent(uint8_t* edid, uint8_t parent)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
			array_cmp (GLOBAL_STORAGE.devices[i].edid, edid)){
				GLOBAL_STORAGE.devices[i].parent_cid = parent;
				D_NET printf("parent changed\n");
				return true;
			}
	}
	return false;
}

/*
 * Searches coordinator ID of coordinator or parent ID of end device.
 * @param edid			End device ID.
 * @return Returns coordinator ID or parent ID of device, INVALID_CID (0xff) in case of failure.
 */
uint8_t get_cid (uint8_t * edid)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				array_cmp (GLOBAL_STORAGE.devices[i].edid, edid)
				&& GLOBAL_STORAGE.devices[i].coord)
			return GLOBAL_STORAGE.devices[i].cid;
		else if (GLOBAL_STORAGE.devices[i].valid &&
				array_cmp (GLOBAL_STORAGE.devices[i].edid, edid)
				&& !GLOBAL_STORAGE.devices[i].coord)
			return GLOBAL_STORAGE.devices[i].parent_cid;
	}
	return INVALID_CID;
}

/*
 * Searches parent ID of device.
 * @param edid			End device ID.
 * @return Returns parent ID of device, INVALID_CID (0xff) in case of failure.
 */
uint8_t get_parent_cid (uint8_t* edid)
{
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				array_cmp (GLOBAL_STORAGE.devices[i].edid, edid))
				return GLOBAL_STORAGE.devices[i].parent_cid;
	}
	return INVALID_CID;
}

/*
 * Adds a new device to device table.
 * @param edid				End device ID.
 * @param cid					Coordinator ID.
 * @param parent_cid	Parent ID of the device.
 * @param sleepy			True if device is sleepy, false otherwise.
 * @param coord				True if device is coordinator, false otherwise.
 * @return Returns true if device is successfully added to device table, false otherwise.
 */
bool add_device (uint8_t* edid, uint8_t cid, uint8_t parent_cid, bool sleepy, bool coord)
{
	if (is_my_device (edid))
		return false;
	for (uint8_t i = 0; i < MAX_DEVICES; i++) {
		if (GLOBAL_STORAGE.devices[i].valid == false) {
			array_copy (edid, GLOBAL_STORAGE.devices[i].edid, EDID_LENGTH);
			GLOBAL_STORAGE.devices[i].cid = cid;
			GLOBAL_STORAGE.devices[i].parent_cid = parent_cid;
			GLOBAL_STORAGE.devices[i].sleepy = sleepy;
			GLOBAL_STORAGE.devices[i].valid = true;
			GLOBAL_STORAGE.devices[i].coord = coord;
			return true;
		}
	}
	return false;
}

/*
 * Removes a joined device from device table.
 * @param edid				End device ID.
 * @return Returns true if device is successfully removed from device table, false otherwise.
 */
bool remove_device (uint8_t* edid) {
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				array_cmp (GLOBAL_STORAGE.devices[i].edid, edid)) {
			GLOBAL_STORAGE.devices[i].valid = false;
			return true;
		}
	}
	return false;
}

/*
 * Saves device table to a file specified by string in device_table_path variable
 * of GLOBAL STORAGE structure.
 * @return Returns true if device table is successfully saved to the file, false otherwise.
 */
bool save_device_table (void)
{
	ofstream fs (GLOBAL_STORAGE.device_table_path);
	if (!fs)
		return false;
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		// EDID | PARRENT_CID | CID | SLEEPY | COORD
		// COORD => COORD = 0; ED = 1
		if (GLOBAL_STORAGE.devices[i].valid) {
			fs << setfill ('0') << hex
				<< setw (2) << unsigned (GLOBAL_STORAGE.devices[i].edid[0]) << " "
				<< setw (2) << unsigned (GLOBAL_STORAGE.devices[i].edid[1]) << " "
				<< setw (2) << unsigned (GLOBAL_STORAGE.devices[i].edid[2]) << " "
				<< setw (2) << unsigned (GLOBAL_STORAGE.devices[i].edid[3]) << " "
				<< "| " << setfill ('0') << setw (2)
			<< unsigned (GLOBAL_STORAGE.devices[i].parent_cid) << " | "
				<< unsigned (GLOBAL_STORAGE.devices[i].cid) << " | "
				<< GLOBAL_STORAGE.devices[i].sleepy << " | "
				<< GLOBAL_STORAGE.devices[i].coord << endl;
		}
	}
	fs.close ();
	if (!fs)
		return false;
	return true;
}

/*
 * Loads device table from a file specified by string in device_table_path variable
 * of GLOBAL STORAGE structure.
 * @return Returns true if device table is successfully loaded from file, false otherwise.
 */
bool load_device_table (void)
{
	int i = 0;
	int edid[EDID_LENGTH];
	int parent_cid;
	int cid;
	char delimiter;
	int sleepy;
	int coord;
	ifstream fs (GLOBAL_STORAGE.device_table_path);
	string line;

	if (!fs)
		return false;
	while (getline (fs, line)) {
		istringstream iss (line);
		if (!(iss >> hex >> edid[0] >> edid[1] >> edid[2] >> edid[3] >>
					delimiter >> parent_cid >> delimiter >> cid >> delimiter >> sleepy
					>> delimiter >> coord))
			continue;
		GLOBAL_STORAGE.devices[i].valid = true;
		(sleepy) ? GLOBAL_STORAGE.devices[i].sleepy =
			true : GLOBAL_STORAGE.devices[i].sleepy = false;
		GLOBAL_STORAGE.devices[i].edid[0] = edid[0];
		GLOBAL_STORAGE.devices[i].edid[1] = edid[1];
		GLOBAL_STORAGE.devices[i].edid[2] = edid[2];
		GLOBAL_STORAGE.devices[i].edid[3] = edid[3];
		GLOBAL_STORAGE.devices[i].parent_cid = parent_cid;
		GLOBAL_STORAGE.devices[i].cid = cid;
		GLOBAL_STORAGE.devices[i].coord = coord;
		i++;
	}
	fs.close ();
	if (!fs)
		return false;
	return true;
}
// ===== END: DEVICE TABLE SUPPORT FUNCTIONS  =====

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
		if (cid_1 == cid_2)
			return true;
		cid_1 = GLOBAL_STORAGE.routing_tree[cid_1];
		i++;
	}
	return false;
}

/**
 * Sends a routing table.
 * @param tocoord				Destination coordinator ID.
 * @param toed					Destination end device ID.
 * @params payload 			Payload.
 * @params len 					Payload length.
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
			// remove PAN record
			if (data[i - 2] == 0 && data[i - 1] == 0) {
				i -= 2;
				break;
			}
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

/*
 * Loads routing table.
 */
void load_routing_table ()
{
	// 1 + 2 * 64 - INFO BYTE + CID and PARENT CID (2) * maximum number of COORD (64)
	uint8_t r_table[129];
	int k = 0;

	// print devices saved in fitprotocold.devices file and edit routing table for next devices
	/*cout << "\nROUTING TABLE:\n";
	cout << "EDID | PCID | CID | SLEEPY | IS_COORD" << endl;
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid) {
			for (uint8_t j = 0; j < EDID_LENGTH; j++)
				cout << setfill ('0') << setw (2) << hex << unsigned (GLOBAL_STORAGE.
																															devices[i].edid[j]) << " ";
			cout << unsigned (GLOBAL_STORAGE.devices[i].parent_cid) << " ";
			cout << unsigned (GLOBAL_STORAGE.devices[i].cid) << " ";
			cout << unsigned (GLOBAL_STORAGE.devices[i].sleepy) << " ";
			cout << unsigned (GLOBAL_STORAGE.devices[i].coord) << " ";
			cout << endl;
		}
	}*/
	// parent of PAN is always set to 0
	GLOBAL_STORAGE.routing_tree[0] = 0x00;
	for (uint8_t i = 1; i < MAX_COORD; i++) {
		GLOBAL_STORAGE.routing_tree[i] = INVALID_CID;
	}
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid && GLOBAL_STORAGE.devices[i].coord == 1) {
			GLOBAL_STORAGE.routing_tree[GLOBAL_STORAGE.devices[i].cid] = GLOBAL_STORAGE.devices[i].parent_cid;
			r_table[k] = unsigned (GLOBAL_STORAGE.devices[i].cid);
			r_table[k + 1] = unsigned (GLOBAL_STORAGE.devices[i].parent_cid);
			k += 2;
		}
	}
	/*for (uint8_t i = 0; i < MAX_COORD; i++) {
			cout << unsigned (i) << " : " << unsigned (GLOBAL_STORAGE.routing_tree[i]) << endl;
	}*/

	// send routing table to other devices
	for (uint8_t i = 0; i < MAX_COORD; i++) {
		if (GLOBAL_STORAGE.devices[i].valid &&
				GLOBAL_STORAGE.devices[i].parent_cid == 0 && GLOBAL_STORAGE.devices[i].cid != 0) {
			cout << "ROUTING TREE to COORD: " << setfill ('0') << setw (2) <<
				hex << unsigned (GLOBAL_STORAGE.devices[i].cid) << "\n";
			uint8_t toed[EDID_LENGTH];
			toed[0] = GLOBAL_STORAGE.devices[i].edid[0];
			toed[1] = GLOBAL_STORAGE.devices[i].edid[1];
			toed[2] = GLOBAL_STORAGE.devices[i].edid[2];
			toed[3] = GLOBAL_STORAGE.devices[i].edid[3];

			send_routing_table (GLOBAL_STORAGE.devices[i].cid, toed, r_table, k);
			//delay_ms(100);
		}
	}
}

// ===== BEGIN: SLEEPY MESSAGES TABLE SUPPORT FUNCTIONS =====
/*
 * Prints sleepy message table.
 */
void print_sleepy_message_table ()
{
	D_NET printf ("SLEEPY MESSAGE TABLE\n");
	D_NET printf ("\nEDID\tpayload\n");
	for (uint8_t i = 0; i < MAX_SLEEPY_MESSAGES; i++) {
		if (NET_STORAGE.sleepy_messages[i].valid) {
			D_NET printf ("%02x %02x %02x %02x\t",
							NET_STORAGE.sleepy_messages[i].toed[0],
							NET_STORAGE.sleepy_messages[i].toed[1],
							NET_STORAGE.sleepy_messages[i].toed[2],
							NET_STORAGE.sleepy_messages[i].toed[3]
				);
			for (uint8_t j = 0; j < NET_STORAGE.sleepy_messages[i].len; j++) {
				D_NET printf ("%02x ", NET_STORAGE.sleepy_messages[i].payload[j]);
			}
			D_NET printf ("\n");
		}
	}
	D_NET printf ("---------------------------------\n");
}

/*
 * Pushes message for sleepy end dvice, only one message is allowed for end device at the same time.
 * @param toed 				Destination end device ID.
 * @param payload 		Payload.
 * @param len	 				Payload length.
 * @return Returns true if insertion of message is successfull, false otherwise.
 */
bool push_sleepy_message (uint8_t * toed, uint8_t * payload, uint8_t len)
{
	uint8_t i;
	// overwrite current message for ED if exists
	for (i = 0; i < MAX_SLEEPY_MESSAGES; i++) {
		if (NET_STORAGE.sleepy_messages[i].valid == true &&
				array_cmp (NET_STORAGE.sleepy_messages[i].toed, toed) == true) {
			for (uint8_t j = 0; j < len; j++) {
				NET_STORAGE.sleepy_messages[i].payload[j] = payload[j];
			}
			NET_STORAGE.sleepy_messages[i].len = len;
			return true;
		}
	}
	// push a new message for ED and save EDID
	for (i = 0; i < MAX_SLEEPY_MESSAGES; i++) {
		if (NET_STORAGE.sleepy_messages[i].valid == false) {
			uint8_t j;
			for (j = 0; j < len; j++) {
				NET_STORAGE.sleepy_messages[i].payload[j] = payload[j];
			}
			for (j = 0; j < EDID_LENGTH; j++) {
				NET_STORAGE.sleepy_messages[i].toed[j] = toed[j];
			}
			NET_STORAGE.sleepy_messages[i].len = len;
			NET_STORAGE.sleepy_messages[i].valid = true;
			return true;
		}
	}
	return false;
}

/*
 * Searches stored SLEEPY message for end device.
 * @param edid 				End device ID.
 * @return Returns pointer to sleepy message record if it exists, NULL otherwise.
 */
NET_sleepy_messages_t* get_sleepy_message (uint8_t* edid)
{
	for (uint8_t i = 0; i < MAX_SLEEPY_MESSAGES; i++) {
		if (NET_STORAGE.sleepy_messages[i].valid == true &&
				array_cmp (NET_STORAGE.sleepy_messages[i].toed, edid) == true) {
			D_NET printf ("get_sleepy_message(): found\n");
			return &NET_STORAGE.sleepy_messages[i];
		}
	}
	return NULL;
}
// ===== END: SLEEPY MESSAGES TABLE SUPPORT FUNCTIONS =====

/*
 * Saves information about device which sent JOIN REQUEST message.
 * @param edid 							Source end device ID.
 * @param cid 							Source coordinator ID.
 * @param RSSI						 	Received signal strength.
 * @param device_type 			Device type.
 * @return Returns true if information about device is saved successfully, false otherwise.
 */
bool save_join_message(uint8_t *edid, uint8_t cid, uint8_t RSSI, uint8_t device_type)
{
	for (uint8_t i = 0; i < MAX_JOIN_MESSAGES; i++) {
		if (NET_STORAGE.join_info[i].valid == false) {
				array_copy(edid, NET_STORAGE.join_info[i].edid, EDID_LENGTH);
				NET_STORAGE.join_info[i].scid = cid;
				NET_STORAGE.join_info[i].RSSI = RSSI;
				NET_STORAGE.join_info[i].device_type = device_type;
				NET_STORAGE.join_info[i].valid = true;
				NET_STORAGE.join_info[i].time = NET_STORAGE.timer_counter;
				D_NET printf("NET_STORAGE.join_info[i].scid: %02x RSSI: %d\n", NET_STORAGE.join_info[i].scid, NET_STORAGE.join_info[i].RSSI);
				return true;
		}
	}
	return false;
}

/*
 * Saves information about device which sent MOVE REQUEST message.
 * @param message_type 			Message type.
 * @param edid 							Source end device ID.
 * @param cid 							Source coordinator ID.
 * @param RSSI						 	Received signal strength.
 * @return Returns true if information about device is successfully saved, false otherwise.
 */
bool save_move_message(uint8_t message_type, uint8_t *edid, uint8_t cid, uint8_t RSSI)
{
	if (message_type == PT_DATA_MOVE_REQUEST_ROUTE) {
		D_NET printf("MOVE REQUEST ROUTE %02x %02x %02x %02x CID: %02x RSSI: %d\n", edid[0], edid[1], edid[2], edid[3], cid, RSSI);
		for (uint8_t i = 0; i < MAX_MOVE_MESSAGES; i++) {
			if (NET_STORAGE.move_info[i].valid && (NET_STORAGE.move_info[i].scid == cid) && (array_cmp(NET_STORAGE.move_info[i].edid, edid))) {
				D_NET printf("maybe it will be updated ROUTE\n");
				if (NET_STORAGE.move_info[i].RSSI < RSSI) {
					NET_STORAGE.move_info[i].RSSI = RSSI;
					NET_STORAGE.move_info[i].time = NET_STORAGE.timer_counter;
					D_NET printf("record actualized\n");
				}
				return true;
			}
		}
		for (uint8_t i = 0; i < MAX_MOVE_MESSAGES; i++) {
			if (!NET_STORAGE.move_info[i].valid) {
				D_NET printf("save MOVE REQUEST ROUTE\n");
				array_copy(edid, NET_STORAGE.move_info[i].edid, EDID_LENGTH);
				NET_STORAGE.move_info[i].scid = cid;
				NET_STORAGE.move_info[i].RSSI = RSSI;
				NET_STORAGE.move_info[i].time = NET_STORAGE.timer_counter;
				NET_STORAGE.move_info[i].valid = true;
				return true;
			}
		}
		// no more space in move_info structure
		return false;
	}
	if(message_type == PT_DATA_MOVE_REQUEST) {
			D_NET printf("MOVE REQUEST %02x %02x %02x %02x\n", edid[0], edid[1], edid[2], edid[3]);
			for(uint8_t i = 0; i < MAX_JOIN_MESSAGES; i++) {
				if(NET_STORAGE.move_info[i].valid && (array_cmp(NET_STORAGE.move_info[i].edid, edid))) {
					NET_STORAGE.move_info[i].valid = true;
					D_NET printf("Maybe it will be updated.\n");
					if(NET_STORAGE.move_info[i].RSSI < RSSI) {
						NET_STORAGE.move_info[i].RSSI = RSSI;
						NET_STORAGE.move_info[i].time = NET_STORAGE.timer_counter;
						D_NET printf("Record actualized.\n");
					}
					return true;
				}
			}
			for (uint8_t i = 0; i < MAX_MOVE_MESSAGES; i++) {
				if (!NET_STORAGE.move_info[i].valid) {
					D_NET printf("save MOVE REQUEST\n");
					array_copy(edid, NET_STORAGE.move_info[i].edid, EDID_LENGTH);
					NET_STORAGE.move_info[i].scid = 0x00;
					NET_STORAGE.move_info[i].RSSI = LINK_get_measured_noise();
					D_NET printf("RSSI: %d\n", NET_STORAGE.move_info[i].RSSI);
					NET_STORAGE.move_info[i].time = NET_STORAGE.timer_counter;
					NET_STORAGE.move_info[i].valid = true;
					return true;
				}
			}
			// no more space in move_info structure
			return false;
	}

	return false;
}

/**
 * Stores JOIN REQUEST (ROUTE) packet.
 * @param RSSI 					Received signal strength.
 * @param data 					Data.
 * @param len 					Data length.
 * @return Returns false if packet length is too short or packet cannot be
 * 				 stored, true otherwise.
 */
bool LINK_join_request_received (uint8_t RSSI, uint8_t* data, uint8_t len)
{
	if (len < NET_HEADER_SIZE)
		return false;

	uint8_t type = (data[0] >> 4);
	uint8_t scid;

	if (type == PT_DATA_JOIN_REQUEST) {
		D_NET printf ("PT_DATA_JOIN_REQUEST from %02x %02x %02x %02x\n",
									data[6], data[7], data[8], data[9]);
		// direct JOIN REQUEST message, CID of ED is always set to 0
		scid = 0x00;
		if(!save_join_message(data + 6, scid, RSSI, data[1])){
			D_NET printf("JOIN REQUEST was not saved!\n");
			return false;
		}
	}
	else if(type == PT_DATA_JOIN_REQUEST_ROUTE) {
		// indirect JOIN REQUEST message, get CID of device that route this message
		scid = data[1] & 0x3f;
		if(!save_join_message(data + 6, scid, RSSI, data[10])) {
			D_NET printf("JOIN REQUEST was not saved!\n");
			return false;
		}
	}
	else {
		D_NET printf ("Invalid JOIN REQUEST!\n");
		return false;
	}
	return true;
}

/**
 * Searches a free coordinator ID.
 * @return Returns a free coordinator ID or INVALID_CID (0xff) in case of failure.
 */
uint8_t find_free_cid()
{
	bool free_cid;
	for(uint8_t i = 1; i < MAX_COORD; i++)
	{
		free_cid = true;
		for(uint8_t j = 1; j < MAX_COORD; j++)
		{
			if(GLOBAL_STORAGE.devices[j].valid && GLOBAL_STORAGE.devices[j].cid == i)
			{
				D_NET printf("CID: %d is not free!\n", i);
				free_cid = false;
				break;
			}
		}
		if(free_cid)
			return i;
	}
	return INVALID_CID;
}

/**
 * Sends JOIN RESPONSE ROUTE message.
 * @param tocoord 		Dstination coordinator ID.
 * @param toed 				Destination end device ID.
 * @param cid 				Coordinator ID assigned to a new device.
 * @param device_type Device type.
 */
void send_join_response_route (uint8_t tocoord, uint8_t* toed, uint8_t cid)
{
	// JOIN RESPONSE ROUTE length on network layer is 15 bytes
	uint8_t tmp[15];
	uint8_t index = 0;
	uint8_t address_coord = get_next_coord (tocoord);
	if(address_coord == INVALID_CID)
		return;
	tmp[index++] = (PT_DATA_JOIN_RESPONSE_ROUTE << 4) | ((tocoord >> 2) & 0x0f);;
	tmp[index++] = ((tocoord << 6) & 0xc0) | (GLOBAL_STORAGE.cid & 0x3f);
	// address of source ED
	tmp[index++] = toed[0];
	tmp[index++] = toed[1];
	tmp[index++] = toed[2];
	tmp[index++] = toed[3];
	// address of PAN
	tmp[index++] = GLOBAL_STORAGE.edid[0];
	tmp[index++] = GLOBAL_STORAGE.edid[1];
	tmp[index++] = GLOBAL_STORAGE.edid[2];
	tmp[index++] = GLOBAL_STORAGE.edid[3];
	// payload with NID, CID information
	tmp[index++] = GLOBAL_STORAGE.nid[0];
	tmp[index++] = GLOBAL_STORAGE.nid[1];
	tmp[index++] = GLOBAL_STORAGE.nid[2];
	tmp[index++] = GLOBAL_STORAGE.nid[3];
	tmp[index++] = cid;

	LINK_send_coord(false, &address_coord, tmp, index, LINK_DATA_WITHOUT_ACK);
}

/**
 * Notifies an unsuccessful data transmission.
 */
void LINK_error_handler_coord ()
{
	D_NET printf ("COORD - error during transmitting\n");
}

/**
 * Searches next coordinator ID on the packet route to destination coordinator.
 * The searching runs from dst to PAN.
 * WARNING: Do not set dst_cid to zero!
 * @param dst_cid 	Destination CID.
 * @return Returns next coordinator ID.
 */

uint8_t get_next_coord (uint8_t dst_cid)
{
	uint8_t address;
	uint8_t previous_address;
	address = dst_cid;
	while (1) {
		if (address == GLOBAL_STORAGE.cid) {
			address = previous_address;
			D_NET printf("Next COORD: %d\n", address);
			return address;
		}
		previous_address = address;
		address = GLOBAL_STORAGE.routing_tree[address];
	}
}

/**
 * Sends JOIN RESPONSE message.
 * @param toed		 			Destination end device ID.
 * @param cid 					Assigned coordinator ID to a new device.
 * @param device_type 	Device type.
 */
void send_join_response (uint8_t* toed, uint8_t cid)
{
	// JOIN RESPONSE length on network layer is 15 bytes
	uint8_t tmp[15];
	uint8_t index = 0;

	tmp[index++] = (PT_DATA_JOIN_RESPONSE << 4) & 0xf0;
	tmp[index++] = GLOBAL_STORAGE.cid;
	tmp[index++] = toed[0];
	tmp[index++] = toed[1];
	tmp[index++] = toed[2];
	tmp[index++] = toed[3];
	tmp[index++] = GLOBAL_STORAGE.edid[0];
	tmp[index++] = GLOBAL_STORAGE.edid[1];
	tmp[index++] = GLOBAL_STORAGE.edid[2];
	tmp[index++] = GLOBAL_STORAGE.edid[3];
	tmp[index++] = GLOBAL_STORAGE.nid[0];
	tmp[index++] = GLOBAL_STORAGE.nid[1];
	tmp[index++] = GLOBAL_STORAGE.nid[2];
	tmp[index++] = GLOBAL_STORAGE.nid[3];
	tmp[index++] = cid;

	LINK_send_join_response (toed, tmp, index);
}

/*
 * Processes packet.
 * @param data		 			Data.
 * @param len 					Data length.
 * @return Returns false if JOIN REQUEST ROUTE is received and PAIR MODE
	* 			 is not set, packet length is too short or JOIN REQUEST ROUTE cannot be
  * 			 stored, true otherwise.
 */
bool local_process_packet (uint8_t * data, uint8_t len)
{
	D_NET printf ("local_process_packet()\n");
	uint8_t type = data[0] >> 4;
	uint8_t dcid = ((data[0] << 2) & 0x3c) | ((data[1] >> 6) & 0x03);
	uint8_t scid = data[1] & 0x3f;
	uint8_t dedid[EDID_LENGTH];
	uint8_t sedid[EDID_LENGTH];
	NET_sleepy_messages_t *p_sleepy_message;
	uint8_t payload[MAX_NET_PAYLOAD_SIZE];
	uint8_t payload_len = len - NET_HEADER_SIZE;

	D_NET printf ("local_process_packet(): type %02x dcid %02x scid %02x\n",
								type, dcid, scid);
	array_copy (data + 2, dedid, EDID_LENGTH);
	array_copy (data + 6, sedid, EDID_LENGTH);
	array_copy (data + NET_HEADER_SIZE, payload, payload_len);
	D_NET
		printf
		("local_process_packet(): sedid %02x %02x %02x %02x dedid %02x %02x %02x %02x\n",
		 sedid[0], sedid[1], sedid[2], sedid[3], dedid[0], dedid[1], dedid[2], dedid[3]);

	if(dcid == NET_COORD_ALL || array_cmp(dedid, NET_ED_ALL)) {
		D_NET printf("BROADCAST!\n");
	}
	if (type == PT_DATA_JOIN_REQUEST_ROUTE) {
		if(!NET_is_set_pair_mode())
			return false;
		return LINK_join_request_received(data[11], data, len);
	}

	// DATA REQUEST packet (ED requests DATA from PAN)
	if (type == PT_DATA_DR) {
		if ((p_sleepy_message = get_sleepy_message (sedid)) != NULL) {
			// send first message and then invalidate this record
			p_sleepy_message->valid = false;
			send (PT_DATA_ACK_DR_WAIT, scid, sedid, NULL, 0, LINK_DATA_WITHOUT_ACK, NOT_EXTENDED);
			//delay_ms (ACK_DATA_DELAY);
			send (PT_DATA, scid, sedid, p_sleepy_message->payload,
						p_sleepy_message->len, LINK_DATA_HS4, NOT_EXTENDED);
		}
		else {
			send (PT_DATA_ACK_DR_SLEEP, scid, sedid, NULL, 0, LINK_DATA_WITHOUT_ACK, NOT_EXTENDED);
		}
		NET_received (scid, sedid, payload, payload_len);
	}
	else if (type == PT_DATA) {
		NET_received (scid, sedid, payload, payload_len);
	}
	return true;
}

/**
 * Processes received packet if it is addressed to this coordinator.
 * Otherwise, it routes the received packet towards destination.
 * @param data		 			Data.
 * @param len 					Data length.
 * @param transfer_type Transfer type on link layer.
 * @return Returns false if packet is too short, network reinitialization is being performed,
 *  			 packet is not successfully sent or parent ID is not found, true otherwise.
 */
bool LINK_route (uint8_t* data, uint8_t len, uint8_t transfer_type)
{
	D_NET printf ("LINK_route()\n");
	if (len < NET_HEADER_SIZE)
		return false;
	// due to MOVE REQUEST message after unpairing of device
	if(!is_my_device(data + 6) && ((data[0] & 0xf0) >> 4) != PT_DATA_JOIN_REQUEST_ROUTE) {
		D_NET printf("Not my device!\n");
		return false;
	}
	uint8_t dcid = ((data[0] << 2) & 0x3C) | ((data[1] >> 6) & 0x03);
	if ((dcid == GLOBAL_STORAGE.cid && (zero_address(data + 2) || array_cmp(data + 2, GLOBAL_STORAGE.edid) || array_cmp(data + 2, NET_ED_ALL))) || transfer_type == LINK_DATA_BROADCAST) {
		if((data[10] & 0xf0) == PT_DATA_MOVE_REQUEST || (data[10] & 0xf0) == PT_DATA_MOVE_REQUEST_ROUTE) {
			save_move_message(data[10] & 0xf0, data + 6, data[1] & 0x3f, data[11]);
		}
		// packet is for PAN or its descendant
		local_process_packet (data, len);
		return true;
	}
	// packet is for another device
	else {
		if (((data[0] & 0xf0) >> 4) == PT_DATA) {
				// packet is for COORD by default
				uint8_t address_coord = dcid;
				if(!zero_address(data + 2)) {
					// packet is for ED, find its parent
					address_coord = get_parent_cid(data + 2);
					if(address_coord == INVALID_CID)
						return false;
					if(address_coord == 0) {
						// ED is direct descendant of PAN
						return LINK_send_coord(true, data + 2, data, len, LINK_DATA_HS4);
					}
					else {
						// ED is not direct descendant of PAN, it is necessary to change dcid
						// dcid has to be parent of ED
						data[0] = (data[0] & 0xf0) | (address_coord >> 4);
						data[1] = (data[1] & 0x3f) | (address_coord << 6);
					}
				}
				// packet is for COORD or for ED that is not a direct descendant of PAN
				address_coord = LINK_cid_mask (get_next_coord (address_coord));
				if(address_coord == INVALID_CID)
					return false;
				return LINK_send_coord(false, &address_coord, data, len, LINK_DATA_HS4);
		}
	}
	return true;
}

/**
 * Initializes network layer and ensures initialization of link and
 * physical layer.
 * @param phy_params 		Parameters of physical layer.
 * @param link_params 	Parameters of link layer.
 */
void NET_init (struct PHY_init_t *phy_params, struct LINK_init_t *link_params)
{
	LINK_init(phy_params, link_params);
	GLOBAL_STORAGE.device_table_path = "/tmp/fitprotocold.devices";
	GLOBAL_STORAGE.routing_enabled = true;
	GLOBAL_STORAGE.pair_mode = false;
	GLOBAL_STORAGE.nid[0] = 0xa1;
	GLOBAL_STORAGE.nid[1] = 0x00;
	GLOBAL_STORAGE.nid[2] = 0x00;
	GLOBAL_STORAGE.nid[3] = 0x03;

	for (int i = 0; i < MAX_SLEEPY_MESSAGES; i++) {
		NET_STORAGE.sleepy_messages[i].valid = false;
	}

	for (int i = 0; i < MAX_JOIN_MESSAGES; i++) {
		NET_STORAGE.join_info[i].valid = false;
	}

	for (int i = 0; i < MAX_MOVE_MESSAGES; i++) {
		NET_STORAGE.move_info[i].valid = false;
	}

	for (int i = 0; i < MAX_COORD; i++) {
		GLOBAL_STORAGE.devices[i].valid = false;
	}

	for (int i = 0; i < MAX_MESSAGES; i++) {
		NET_STORAGE.received_packets[i].empty = true;
	}

	if (!load_device_table ()) {
		D_NET cout << "NET_init(): cannot load device table from "
			<< GLOBAL_STORAGE.device_table_path << endl;
	}

	load_routing_table ();
}

void NET_set_pair_mode_timeout(uint8_t timeout)
{
	NET_STORAGE.pair_mode_timeout = (timeout * 1000)/50;
	NET_STORAGE.join_cnt_overflow_value = MAX_CNT_VALUE - NET_STORAGE.pair_mode_timeout;
}

/**
 * Waits for timeout for joining process, then sends JOIN RESPONSE (ROUTE) packet.
 */
void NET_joining()
{
	for(int i = 0; i < MAX_JOIN_MESSAGES; i++){
		if(NET_STORAGE.join_info[i].valid) {
			if(NET_STORAGE.timer_counter == 0 && NET_STORAGE.join_info[i].time > NET_STORAGE.join_cnt_overflow_value)
			{
				overflow_joining = true;
				D_NET printf("overflow during joining\n");
			}
			if((NET_STORAGE.join_info[i].time <= NET_STORAGE.join_cnt_overflow_value && (abs(NET_STORAGE.timer_counter - NET_STORAGE.join_info[i].time) > NET_STORAGE.pair_mode_timeout)) || (overflow_joining && NET_STORAGE.timer_counter > (NET_STORAGE.pair_mode_timeout - (MAX_CNT_VALUE - NET_STORAGE.join_info[i].time))))
			{
				D_NET printf("send JOIN RESPONSE\n");
				uint8_t new_parent = fitp_find_parent(NET_STORAGE.join_info, NET_STORAGE.join_info[i].edid, MAX_JOIN_MESSAGES);
				if(new_parent == INVALID_CID)
					return;
				D_NET printf("New parent: %d\n",  NET_STORAGE.join_info[new_parent].scid);
				D_NET printf("DEDID: %02x %02x %02x %02x\n", NET_STORAGE.join_info[i].edid[0], NET_STORAGE.join_info[i].edid[1], NET_STORAGE.join_info[i].edid[2], NET_STORAGE.join_info[i].edid[3]);
				//if(NET_accept_device(NET_STORAGE.join_info[new_parent].scid)) {
					D_NET printf("really send JOIN RESPONSE\n");
					// set CID of a COORD
					if (NET_STORAGE.join_info[i].device_type == 0xcc) {
						NET_STORAGE.join_info[i].cid = find_free_cid();
						D_NET printf("CID: %d\n", NET_STORAGE.join_info[i].cid);
						if(NET_STORAGE.join_info[i].cid == INVALID_CID)
							D_NET printf("No free CID!\n");
					}
					// CID of ED is always set to 0
					else {
						NET_STORAGE.join_info[i].cid = 0;
					}
					add_device (NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid, NET_STORAGE.join_info[new_parent].scid, NET_STORAGE.join_info[i].device_type == SLEEPY_ED, NET_STORAGE.join_info[i].device_type == COORD);
					/*if(result == 0)
						return;*/
					if(NET_STORAGE.join_info[new_parent].scid == 0)
						send_join_response (NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid);
					else
						send_join_response_route (NET_STORAGE.join_info[new_parent].scid, NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid);
					print_device_table();
					if (!save_device_table ()) {
						D_NET cout << "NET_joining(): cannot save device: "
							<< GLOBAL_STORAGE.device_table_path << endl;
						load_routing_table ();
					}
				//}
				uint8_t edid_tmp[EDID_LENGTH];
				array_copy(NET_STORAGE.join_info[i].edid, edid_tmp, EDID_LENGTH);
				for (int i = 0; i < MAX_JOIN_MESSAGES; i++){
					if(NET_STORAGE.join_info[i].valid && array_cmp(NET_STORAGE.join_info[i].edid, edid_tmp)){
						D_NET printf("EDID: %02x %02x %02x %02x\n", NET_STORAGE.join_info[i].edid[0], NET_STORAGE.join_info[i].edid[1], NET_STORAGE.join_info[i].edid[2], NET_STORAGE.join_info[i].edid[3]);
						D_NET printf("DELETE record!\n");
						NET_STORAGE.join_info[i].valid = false;
						overflow_joining = false;
					}
				}
				return;
			}
		}
	}
}

/**
 * Waits for timeout for joining process, then sends JOIN RESPONSE (ROUTE) packet.
 */
void NET_accepted_device(uint8_t edid[4])
{
	//uint8_t zeros[] = {0x00, 0x00, 0x00, 0x00};
	//uint8_t result;
	for (int i = 0; i < MAX_JOIN_MESSAGES; i++) {
		if (/*NET_STORAGE.join_info[i].valid && */array_cmp(edid, NET_STORAGE.join_info[i].edid)) {
			/*if(NET_STORAGE.timer_counter == 0 && NET_STORAGE.join_info[i].time > NET_STORAGE.join_cnt_overflow_value)
			{
				overflow_joining = true;
				D_NET printf("overflow during joining\n");
			}*/
			/*if((NET_STORAGE.join_info[i].time <= NET_STORAGE.join_cnt_overflow_value && (abs(NET_STORAGE.timer_counter - NET_STORAGE.join_info[i].time) > NET_STORAGE.pair_mode_timeout)) || (overflow_joining && NET_STORAGE.timer_counter > (NET_STORAGE.pair_mode_timeout - (MAX_CNT_VALUE - NET_STORAGE.join_info[i].time))))
			{
				D_NET printf("send JOIN RESPONSE\n");*/
			uint8_t new_parent = fitp_find_parent(NET_STORAGE.join_info, NET_STORAGE.join_info[i].edid, MAX_JOIN_MESSAGES);
			if (new_parent == INVALID_CID)
				return;
			//D_NET printf("New parent: %d\n",  NET_STORAGE.join_info[new_parent].scid);
			//D_NET printf("DEDID: %02x %02x %02x %02x\n", NET_STORAGE.join_info[i].edid[0], NET_STORAGE.join_info[i].edid[1], NET_STORAGE.join_info[i].edid[2], NET_STORAGE.join_info[i].edid[3]);
			// TODO: o prijmu zarizeni rozhoduje server?
			//if(NET_accept_device(NET_STORAGE.join_info[new_parent].scid)) {
			//D_NET printf("really send JOIN RESPONSE\n");
			// set CID of a COORD
			if (NET_STORAGE.join_info[i].device_type == 0xcc) {
				NET_STORAGE.join_info[i].cid = find_free_cid();
				//D_NET printf("CID: %d\n", NET_STORAGE.join_info[i].cid);
				if (NET_STORAGE.join_info[i].cid == INVALID_CID) {
					D_NET printf("No free CID!\n");
					//}
					// CID of ED is always set to 0
				}
			}
			else {
				NET_STORAGE.join_info[i].cid = 0;
			}
			add_device(NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid, NET_STORAGE.join_info[new_parent].scid,
				NET_STORAGE.join_info[i].device_type == SLEEPY_ED, NET_STORAGE.join_info[i].device_type == COORD);
			/*if(result == 0)
				return;*/
			if (NET_STORAGE.join_info[new_parent].scid == 0)
				send_join_response(NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid);
			else
				send_join_response_route(NET_STORAGE.join_info[new_parent].scid, NET_STORAGE.join_info[i].edid, NET_STORAGE.join_info[i].cid);
			print_device_table();
			if (!save_device_table()) {
				D_NET
				cout << "NET_joining(): cannot save device: "
					 << GLOBAL_STORAGE.device_table_path << endl;
				load_routing_table();
			}
			uint8_t edid_tmp[EDID_LENGTH];
			array_copy(NET_STORAGE.join_info[i].edid, edid_tmp, EDID_LENGTH);
			for (int i = 0; i < MAX_JOIN_MESSAGES; i++) {
				if (NET_STORAGE.join_info[i].valid && array_cmp(NET_STORAGE.join_info[i].edid, edid_tmp)) {
					D_NET printf("EDID: %02x %02x %02x %02x\n", NET_STORAGE.join_info[i].edid[0], NET_STORAGE.join_info[i].edid[1], NET_STORAGE.join_info[i].edid[2], NET_STORAGE.join_info[i].edid[3]);
					D_NET printf("DELETE record!\n");
					NET_STORAGE.join_info[i].valid = false;
					overflow_joining = false;
				}
			}
			return;
		}
	}
}

/**
 * Waits for timeout for network reinitialization, then sends MOVE RESPONSE (ROUTE) packet.
 */
void NET_moving()
{
	for(int i = 0; i < MAX_MOVE_MESSAGES; i++){
		if(NET_STORAGE.move_info[i].valid){
			if(NET_STORAGE.timer_counter == 0 && NET_STORAGE.move_info[i].time > MOVE_CNT_OVERFLOW_VALUE)
				overflow_moving = true;
			if((NET_STORAGE.move_info[i].time <= MOVE_CNT_OVERFLOW_VALUE && (abs(NET_STORAGE.timer_counter - NET_STORAGE.move_info[i].time) > MAX_MOVE_DELAY)) ||
			(overflow_moving && NET_STORAGE.timer_counter > (MAX_MOVE_DELAY - (MAX_CNT_VALUE - NET_STORAGE.move_info[i].time)))) {
				uint8_t new_parent = fitp_find_parent(NET_STORAGE.move_info, NET_STORAGE.move_info[i].edid, MAX_MOVE_MESSAGES);
				D_NET printf("New parent: %d\n",  NET_STORAGE.move_info[new_parent].scid);
				if(new_parent == INVALID_CID)
					return;
				D_NET printf("DEDID: %02x %02x %02x %02x\n", NET_STORAGE.move_info[i].edid[0], NET_STORAGE.move_info[i].edid[1], NET_STORAGE.move_info[i].edid[2], NET_STORAGE.move_info[i].edid[3]);
				if(NET_STORAGE.move_info[new_parent].scid != 0)
					fitp_send_move_response_route(NET_STORAGE.move_info[new_parent].scid, NET_STORAGE.move_info[i].edid);
				else
					fitp_send_move_response(NET_STORAGE.move_info[new_parent].scid, NET_STORAGE.move_info[i].edid);
				uint8_t edid_tmp[EDID_LENGTH];
				array_copy(NET_STORAGE.move_info[i].edid, edid_tmp, EDID_LENGTH);
				for (int i = 0; i < MAX_JOIN_MESSAGES; i++){
					if(NET_STORAGE.move_info[i].valid && array_cmp(NET_STORAGE.move_info[i].edid, edid_tmp)){
						D_NET printf("EDID: %02x %02x %02x %02x\n", NET_STORAGE.move_info[i].edid[0], NET_STORAGE.move_info[i].edid[1], NET_STORAGE.move_info[i].edid[2], NET_STORAGE.move_info[i].edid[3]);
						D_NET printf("DELETE record!\n");
						NET_STORAGE.move_info[i].valid = false;
						overflow_moving = false;
					}
				}
				// actualize routing tree
				for (uint8_t i = 0; i < MAX_COORD; i++) {
					if (GLOBAL_STORAGE.devices[i].valid && GLOBAL_STORAGE.devices[i].coord == 1) {
						GLOBAL_STORAGE.routing_tree[GLOBAL_STORAGE.devices[i].cid] = GLOBAL_STORAGE.devices[i].parent_cid;
					}
				}
				return;
			}
		}
	}
}

/**
 * Checks if end device is joined the network.
 * @return Returns true if end device is joined the network, false otherwise.
 */
bool NET_joined ()
{
	return !zero_address (GLOBAL_STORAGE.nid);
}

/**
 * Sends packet.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 				  Destination end device ID.
 * @param payload		   	Payload.
 * @param len 					Payload length.
 * @return Returns true if packet is successfully sent, false otherwise.
 */
bool NET_send (uint8_t tocoord, uint8_t * toed, uint8_t * payload, uint8_t len)
{
	D_NET printf("NET_send()\n");
	D_NET print_device_table ();
	D_NET print_sleepy_message_table ();

	if(tocoord == GLOBAL_STORAGE.cid && (zero_address(toed) || array_cmp(toed, GLOBAL_STORAGE.edid))) {
		// device cannot send packets itself
		D_NET printf("Cant send packet myself!\n");
		return false;
	}

	if (!is_my_device (toed)) {
		D_NET
			printf
			("ED %02x %02x %02x %02x is not in device table!\n",
			 toed[0], toed[1], toed[2], toed[3]);
		return false;
	}
	// send data to COORD
	if (is_coord_device (toed, tocoord) || !is_sleepy_device (toed)) {
		D_NET printf("to COORD");
		return send (PT_DATA, tocoord, toed, payload, len, LINK_DATA_HS4, NOT_EXTENDED);
	}
	if (is_sleepy_device (toed)) {
		D_NET printf ("to sleepy device %02x %02x %02x %02x\n",
									toed[0], toed[1], toed[2], toed[3]);
		return push_sleepy_message (toed, payload, len);
	}

	return false;
}

/*
 * Removes device from network.
 * @param edid  End device ID.
 * @return Returns true if device is successfully removed from network.
 */
bool NET_unpair(uint8_t* edid)
{
	print_device_table();
	if (remove_device (edid)) {
		save_device_table ();
		load_routing_table ();
		print_device_table();
		return true;
	}
	return false;
}

/**
 * Notifies successful four-way handshake.
 */
void LINK_notify_send_done ()
{
	NET_notify_send_done();
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

/*
 * Increments counter.
 */
void LINK_timer_counter()
{
    NET_STORAGE.timer_counter++;
}

void LINK_save_msg_info(uint8_t* data, uint8_t len)
{
	NET_save_msg_info((data[0] & 0xf0) >> 4, data[1], data + 6, data + 10, len - 10);
	/*for (uint8_t i = 0; i > MAX_MESSAGES; i++) {
		if (NET_STORAGE.received_packets[i].empty) {
			NET_STORAGE.received_packets[i].empty = false;
			NET_STORAGE.received_packets[i].type = (data[0] & 0xf0) >> 4;
			NET_STORAGE.received_packets[i].device = data[1];
			array_copy(data + 6, NET_STORAGE.received_packets[i].sedid, 4);
			printf("Type: %02x DEVICE: %02x  EDID: %02x %02x %02x", NET_STORAGE.received_packets[i].type, NET_STORAGE.received_packets[i].device,
				NET_STORAGE.received_packets[i].sedid[0], NET_STORAGE.received_packets[i].sedid[1], NET_STORAGE.received_packets[i].sedid[2], NET_STORAGE.received_packets[i].sedid[3]);
			break;
		}
	}*/
}

uint8_t NET_get_measured_noise()
{
	return LINK_get_measured_noise();
}
