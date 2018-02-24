/**
* @file fitp.h
*/

//#ifndef FITP_LAYER_H
//#define FITP_LAYER_H
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string>
#include <map>
#include <vector>
#include "fitp/common/phy_layer/phy.h"
#include "pan/link_layer/link.h"
//#include "pan/net_layer/net.h"
//#include "net_common.h"
#include <unistd.h>
#include <mutex>
#include <deque>
#include <condition_variable>

/*! end device ID in case of addressing using coordinator ID */
#define FITP_DIRECT_COORD (uint8_t*)"\x00\x00\x00\x00"
//uint8_t FITP_ED_ALL[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
/*! broadcast address */
#define FITP_COORD_ALL 0x3F
/*! MOVE REQUEST message */
#define FITP_MOVE_REQUEST	0x00
/*! MOVE RESPONSE message */
#define FITP_MOVE_RESPONSE	0x01
/*! MOVE RESPONSE RESPONSE message */
#define FITP_MOVE_RESPONSE_ROUTE	0x02
#define MAX_DATA_LENGTH 32
#define MAX_MESSAGES 10

enum fitp_packet_type {
	FITP_DATA = 0x00,
	FITP_DATA_DR = 0x01,
	FITP_JOIN_REQUEST = 0x03
};

struct fitp_received_messages_t {
	fitp_packet_type msg_type;
	uint8_t data[MAX_DATA_LENGTH];
	uint8_t len;
	uint8_t sedid[4];
	uint8_t device_type;
};

enum DeviceType {
	NONE,
	END_DEVICE,
	COORDINATOR,
};



extern bool array_cmp (uint8_t* array1, uint8_t* array2);

/**
 * Ensures initialization of network, link and physical layer.
 * @param phy_params		Parameters of physical layer.
 * @param link_params		Parameters of link layer.
 */
void fitp_init (struct PHY_init_t *phy_params, struct LINK_init_t *link_params);

/**
 * Returns a protocol version.
 */
std::string fitp_version();

/**
 * Sends data.
 * @param tocoord		Destination coordinator ID.
 * @param toed 			Destination end device ID.
 * @param data 			Data.
 * @param len 			Data length.
 * @return Returns true if data sending is successful, false otherwise.
 */
bool fitp_send (uint8_t tocoord, uint8_t * toed, uint8_t * data, uint8_t len);

/**
 * Checks if end device has been already joined a network.
 * @return Returns true if end device has been already joined a network, false otherwise.
 */
bool fitp_joined ();

extern void fitp_received (const uint8_t from_cid,
														 const uint8_t from_edid[4], const uint8_t * data,
														 const uint8_t len);

extern bool NET_accept_device (uint8_t parent_cid);

extern void fitp_notify_send_done();

/**
 * Enables pair mode.
 * @param timeout	Duration of pair mode (in seconds).
 */
void fitp_joining_enable(uint8_t timeout);

/**
 * Disables pair mode.
 */
void fitp_joining_disable();

/**
 * Removes device from network.
 * @param  edid End device ID.
 * @return Returns true if device is successfully removed from network.
 */
bool fitp_unpair(uint32_t edid);

/**
 * Processes received data.
 * @param data 	Data sent from end device.
 */
void NET_received_data(uint8_t* data);

/**
 * Reacts to listen command sent from server.
 * @param timeout	Duration of pair mode (in seconds).
 */
void fitp_listen(int timeout);

/**
 * Reacts to accept command sent from server.
 * @param edid	Destination end device ID.
 */
void NET_accepted_device(uint8_t* edid);

/**
 * Reacts to unpair command sent from server.
 * @param edid	Destination end device ID.
 * @return Return true, if end device is unpaired successfully, false otherwise.
 */
bool NET_unpair(uint8_t* edid);

/**
 * Processes received data.
 * @param data 	Data sent from end device.
 */
void fitp_received_data(std::vector<uint8_t> &data);

/**
 * Reacts to accept command sent from server.
 * @param edid	Destination end device ID.
 */
void fitp_accepted_device(std::vector<uint8_t> edid);

/**
 * Checks if message type is DATA or DATA_DR.
 * @param data	Data containing message type.
 * @return Return true, if message type is DATA or DATA_DR, false otherwise.
 */
bool isDataMessage(const std::vector <uint8_t> &data);

/**
 * Checks if message type is JOIN_REQUEST.
 * @param data	Data containing message type.
 * @return Return true, if message type is JOIN_REQUEST, false otherwise.
 */
bool isJoinMessage(const std::vector <uint8_t> &data);

std::map<uint64_t, DeviceType> fitp_device_list();
void print_device_table();
bool save_device_table();
bool add_device (uint8_t* edid, uint8_t cid, uint8_t parent_cid, bool sleepy, bool coord);
bool fitp_is_coord(uint8_t * edid, uint8_t cid);
void fitp_set_config_path(const std::string &configPath);

double fitp_get_measured_noise();

void fitp_set_nid(uint32_t nid);

//#endif
