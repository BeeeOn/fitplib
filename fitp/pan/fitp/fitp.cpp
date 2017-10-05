/**
* @file fitp.cpp
*/
#include <fitp.h>
#include <iostream>
#include <pan/global_storage/global.h>
#include "pan/net_layer/net.h"
#include "pan/global_storage/global.h"
#include "fitp.h"

/**
 * Ensures initialization of network, link and physical layer.
 * @param phy_params		Parameters of physical layer.
 * @param link_params		Parameters of link layer.
 */
void fitp_init (struct PHY_init_t* phy_params, struct LINK_init_t* link_params)
{
	NET_init(phy_params, link_params);
}

/**
 * Sends data.
 * @param tocoord		Destination coordinator ID.
 * @param toed 			Destination end device ID.
 * @param data 			Data.
 * @param len 			Data length.
 * @return Returns true if data sending is successful, false otherwise.
 */
bool fitp_send (uint8_t tocoord, uint8_t* toed, uint8_t* data, uint8_t len)
{
	// TODO: FITP_ED_ALL has to be defined in fitp.h, but some error occured!
	if (tocoord == FITP_COORD_ALL /*|| array_cmp(toed, FITP_ED_ALL)*/) {
		// TODO: To be deleted, include a .h file!
		return NET_send_broadcast(0, 0, data, len);
	}
	else if (tocoord != 0) {
		// packet is for COORD, destination EDID is filled with zeros
		return NET_send (tocoord, FITP_DIRECT_COORD, data, len);
	}
	else {
		// packet is for ED, destination COORD has been already set to zero
		return NET_send (tocoord, toed, data, len);
	}
}

/**
 * Checks if end device has been already joined a network.
 * @return Returns true if end device has been already joined a network, false otherwise.
 */
bool fitp_joined ()
{
	return NET_joined ();
}

/**
 * Searches new parent of moved device.
 * @param msg_info			Information about devices attempting to join a network or about moved devices.
 * @param edid					End device ID of device attempting to join a network or of moved device.
 * @param max_messages	Maximum number of JOIN or MOVE messages.
 * @return Returns new parent of device.
 */
uint8_t fitp_find_parent(NET_join_move_info_t* msg_info, uint8_t* edid, uint8_t max_messages)
{
	uint8_t i;
	uint8_t index = 0;
	uint8_t max;
	uint8_t parent = INVALID_CID;

	for (i = 0; i < max_messages; i++) {
		if (msg_info[i].valid && array_cmp(msg_info[i].edid, edid)) {
			// some record for the ED, store RSSI, record index and device
			// with this RSSI
			max = msg_info[i].RSSI;
			index = i;
			parent = i;
			break;
		}
	}

	for (i = index; i < max_messages; i++) {
		if (msg_info[i].valid && array_cmp(msg_info[i].edid, edid) &&
		max < msg_info[i].RSSI) {
			// actualize RSSI and store device with this RSSI
			max = msg_info[i].RSSI;
			parent = i;
		}
	}
	return parent;
}

/**
 * Sends MOVE RESPONSE message.
 * @param tocoord			Destination coordinator ID (set to 0).
 * @param toed				Destination end device ID.
 */
void fitp_send_move_response(uint8_t tocoord, uint8_t* toed)
{
	uint8_t packet[1];
	packet[0] = FITP_MOVE_RESPONSE;
	NET_send_move_response(packet, 1, tocoord, toed);
}

/**
 * Sends MOVE RESPONSE ROUTE message.
 * @param tocoord			Destination coordinator ID (a new parent of ED).
 * @param toed				Destination end device ID.
 */
void fitp_send_move_response_route(uint8_t tocoord, uint8_t* toed)
{
	uint8_t packet[1];
	packet[0] = FITP_MOVE_RESPONSE_ROUTE;
	NET_send_move_response_route(packet, 1, tocoord, toed);
}

/**
 * Notifies successful four-way handshake.
 */
void NET_notify_send_done()
{
	//fitp_notify_send_done();
}

/**
 * Processes received data.
 * @param from_cid 		Source coordinator ID.
 * @param from_edid 	Source end device ID.
 * @param data 				Received data.
 * @param len 				Received data length.
 */
void NET_received (const uint8_t, const uint8_t [EDID_LENGTH],
										  const uint8_t*, const uint8_t)
{
	 //fitp_received (from_cid, from_edid, data, len);
}

void NET_save_msg_info(uint8_t msg_type, uint8_t device_type, uint8_t* sedid, uint8_t* data, uint8_t len)
{
	printf("NET_save_msg_info() 1\n");
	for (uint8_t i = 0; i < MAX_MESSAGES; i++) {
		if (received_messages[i].empty) {
			received_messages[i].empty = false;
			if (msg_type == FITP_JOIN_REQUEST)
				received_messages[i].msg_type = FITP_JOIN_REQUEST;
			if (msg_type == FITP_DATA)
				received_messages[i].msg_type = FITP_DATA;
			received_messages[i].device_type = device_type;
			for (uint8_t k = 0; k < EDID_LENGTH; k++) {
				received_messages[i].sedid[k] = sedid[k];
			}
			for (uint8_t l = 0; l < len; l++)
				received_messages[i].data[l] = data[l];
			received_messages[i].len = len;
			printf("Type: %02x DEVICE: %02x  EDID: %02x %02x %02x %02x\n", received_messages[i].msg_type, received_messages[i].device_type,
				received_messages[i].sedid[0], received_messages[i].sedid[1], received_messages[i].sedid[2], received_messages[i].sedid[3]);
			printf("DATA in data: ");
			for (uint8_t m = 0; m < len; m++)
				printf("%02x ", data[m]);
			printf("DATA in received_messages: ");
			for (uint8_t m = 0; m < len; m++)
				printf("%02x ", received_messages[i].data[m]);
			break;
		}
	}
}


/**
 * Processes received data.
 * @param data 	Data sent from end device.
 */
void fitp_received_data(std::vector<uint8_t> &data)
{
	uint8_t i = 0;
	for (; i < MAX_MESSAGES; i++) {
		if (!received_messages[i].empty) {
			printf("packet was received\n");
			received_messages[i].empty = true;
			data.push_back(received_messages[i].msg_type);
			data.push_back(received_messages[i].device_type);
			data.push_back(received_messages[i].sedid[0]);
			data.push_back(received_messages[i].sedid[1]);
			data.push_back(received_messages[i].sedid[2]);
			data.push_back(received_messages[i].sedid[3]);
			for (uint8_t k = 0; k < received_messages[i].len; k++)
				data.push_back(received_messages[i].data[k]);
		}
	}
}

/**
 * Enables pair mode.
 * @param timeout	Duration of pair mode (in seconds).
 */
void fitp_joining_enable (uint8_t timeout)
{
	//printf("fitp_joining_enable()\n");
	GLOBAL_STORAGE.pair_mode = true;
	// TODO: To be deleted, include a .h file!
	//NET_send_broadcast(PT_NETWORK_EXTENDED, PT_DATA_PAIR_MODE_ENABLED, NULL, 0);
	//NET_send_broadcast(15, 16, NULL, 0);
	NET_set_pair_mode_timeout(timeout);
	D_G printf("fitp_joining_enable()\n");
}

/**
 * Disables pair mode.
 */
void fitp_joining_disable ()
{
//	printf("fitp_joining_disable()\n");
	GLOBAL_STORAGE.pair_mode = false;
//	D_G printf("fitp_joining_disable()\n");
}

/**
 * Reacts to listen command sent from server.
 * @param timeout	Duration of pair mode (in seconds).
 */
void fitp_listen(int timeout)
{
	printf("fitp_listen()\n");
	fitp_joining_enable(timeout);
}

/**
 * Reacts to accept command sent from server.
 * @param edid	Destination end device ID.
 */
void fitp_accepted_device(std::vector<uint8_t> edid)
{
	printf("fitp_accepted_device()\n");
	uint8_t id[EDID_LENGTH];
	for (int i = 0; i < EDID_LENGTH; i++)
		id[i] = edid.at(i);
	sleep(3);
	NET_accepted_device(id);
}

/**
 * Reacts to unpair command sent from server.
 * @param edid	Destination end device ID.
 * @return Return true, if end device is unpaired successfully, false otherwise.
 */
bool fitp_unpair(uint32_t edid)
{
	printf("fitp_unpair()\n");
	uint8_t id[EDID_LENGTH];
	for (int i = EDID_LENGTH - 1; i >= 0; i--) {
		id[i] = edid & 0xff;
		edid >>= 8;
	}
	return NET_unpair(id);
}

uint64_t convert_array_to_number(uint8_t edid[4])
{
	printf("EDID: %02x %02x %02x %02x\n", edid[0], edid[1], edid[2], edid[3]);
	uint64_t edid_number = 0;
	for (uint8_t i = 0; i < EDID_LENGTH - 1; i++)
	{
		edid_number = (edid_number & 0xFFFFFFFFFF00) | edid[i];
		edid_number = edid_number << 8;
	}
	edid_number = (edid_number & 0xFFFFFFFFFF00) | edid[3];
	std::cout << "EDID as number: " << edid_number;
	return edid_number;
}

std::map<uint64_t, DeviceType> fitp_device_list()
{
	printf("fitp_device_list()\n");
	std::map<uint64_t, DeviceType> device_info;
	uint8_t edid[EDID_LENGTH] = {0xed, 0x00, 0x00, 0x02};
	add_device (edid, 1, 0, false, false);
	save_device_table();
	print_device_table();
	for (uint8_t i = 0; i < MAX_DEVICES; i++) {
		if (GLOBAL_STORAGE.devices[i].valid) {
			uint64_t edid_number = convert_array_to_number(GLOBAL_STORAGE.devices[i].edid);
			if (GLOBAL_STORAGE.devices[i].coord) {
				device_info.emplace(edid_number, COORDINATOR);
				printf("COORD was inserted\n");
			}
			else {
				device_info.emplace(edid_number, END_DEVICE);
				printf("ED was inserted\n");
			}
		}
	}
	return device_info;
}

double fitp_get_measured_noise()
{
	uint8_t res = NET_get_measured_noise();
	return double(res);
}

void fitp_set_config_path(const std::string &configPath)
{
	GLOBAL_STORAGE.device_table_path = configPath;
}