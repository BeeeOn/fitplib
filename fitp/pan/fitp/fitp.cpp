/**
* @file fitp.cpp
*/
#include <fitp.h>
#include <iostream>
#include <pan/global_storage/global.h>
#include "pan/net_layer/net.h"
#include "pan/global_storage/global.h"
#include "fitp.h"

std::deque<struct fitp_received_messages_t> received_messages;
std::mutex received_messages_mutex;
std::condition_variable condition_variable_received_messages;

/**
 * Ensures initialization of network, link and physical layer.
 * @param phy_params		Parameters of physical layer.
 * @param link_params		Parameters of link layer.
 */
void fitp_init (struct PHY_init_t* phy_params, struct LINK_init_t* link_params)
{
	NET_init(phy_params, link_params);
}

void fitp_deinit ()
{
	NET_stop();
}

std::string fitp_version()
{
	return GIT_ID;
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
	std::unique_lock<std::mutex> lk(received_messages_mutex);
	struct fitp_received_messages_t tmp_received_message;
	if (msg_type == FITP_JOIN_REQUEST)
		tmp_received_message.msg_type = FITP_JOIN_REQUEST;
	if (msg_type == FITP_DATA)
		tmp_received_message.msg_type = FITP_DATA;
	if (msg_type == FITP_DATA_DR)
		tmp_received_message.msg_type = FITP_DATA_DR;

	tmp_received_message.device_type = device_type;

	for (uint8_t k = 0; k < EDID_LENGTH; k++)
		tmp_received_message.sedid[k] = sedid[k];

	for (uint8_t l = 0; l < len; l++)
		tmp_received_message.data[l] = data[l];

	tmp_received_message.len = len;
	received_messages.push_back(tmp_received_message);
	condition_variable_received_messages.notify_all();
}


/**
 * Processes received data.
 * @param data 	Data sent from end device.
 */
void fitp_received_data(std::vector<uint8_t> &data)
{
	struct fitp_received_messages_t tmp_received_message;
	std::unique_lock<std::mutex> lk(received_messages_mutex);
	if (received_messages.empty()) {
		condition_variable_received_messages.wait_for(lk, std::chrono::seconds(5));
	}
	else {
		tmp_received_message = received_messages.front();
		received_messages.pop_front();

		data.push_back(tmp_received_message.msg_type);
		data.push_back(tmp_received_message.device_type);
		data.push_back(tmp_received_message.sedid[0]);
		data.push_back(tmp_received_message.sedid[1]);
		data.push_back(tmp_received_message.sedid[2]);
		data.push_back(tmp_received_message.sedid[3]);

		for (uint8_t k = 0; k < tmp_received_message.len; k++)
			data.push_back(tmp_received_message.data[k]);
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
	fitp_joining_enable(timeout);
}

/**
 * Reacts to accept command sent from server.
 * @param edid	Destination end device ID.
 */
void fitp_accepted_device(std::vector<uint8_t> edid)
{
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
	uint8_t id[EDID_LENGTH];
	for (int i = EDID_LENGTH - 1; i >= 0; i--) {
		id[i] = edid & 0xff;
		edid >>= 8;
	}
	return NET_unpair(id);
}

uint64_t convert_array_to_number(uint8_t edid[4])
{
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
				//printf("COORD was inserted\n");
			}
			else {
				device_info.emplace(edid_number, END_DEVICE);
				//printf("ED was inserted\n");
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

bool isDataMessage(const std::vector <uint8_t> &data)
{
	return (data.at(0) == FITP_DATA || data.at(0) == FITP_DATA_DR);
}

bool isJoinMessage(const std::vector <uint8_t> &data)
{
	return data.at(0) == FITP_JOIN_REQUEST;
}

void fitp_set_nid(uint32_t nid)
{
	GLOBAL_STORAGE.nid[3] = (nid >> 24) & 0xFF;
	GLOBAL_STORAGE.nid[2] = (nid >> 16) & 0xFF;
	GLOBAL_STORAGE.nid[1] = (nid >> 8) & 0xFF;
	GLOBAL_STORAGE.nid[0] = nid & 0xFF;
}
