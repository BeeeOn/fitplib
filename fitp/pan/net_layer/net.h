/**
* @file net.h
*/

#ifndef NET_LAYER_H
#define NET_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include "pan/global_storage/global.h"
#include "pan/link_layer/link.h"
#include "pan/debug.h"

/*! size of network header */
#define NET_HEADER_SIZE 10
/*! maximum size of network payload */
/*! MAX_NET_PAYLOAD_SIZE = 53 - 10 = 43 */
#define MAX_NET_PAYLOAD_SIZE ( MAX_LINK_PAYLOAD_SIZE - NET_HEADER_SIZE )

/**
 * Structure for received JOIN and MOVE REQUEST (ROUTE) messages.
 */
typedef struct {
	uint8_t edid[EDID_LENGTH];	/**< End device ID (joining or moved device). */
	uint8_t scid;								/**< Coordinator ID (potential parent). */
	uint8_t cid;								/**< Coordinator ID (assigned to joining device). */
	uint8_t device_type;				/**< Device type (coordinator, sleepy end device, ready end device). */
	uint8_t RSSI;								/**< Received signal strength. */
	uint8_t valid;							/**< Flag if record is valid. */
	int time;										/**< JOIN/MOVE REQUEST (ROUTE) message arrival time. */
	bool accepted;
} NET_join_move_info_t;

/**
 * Initializes network layer and ensures initialization of link and
 * physical layer.
 * @param phy_params 		Parameters of physical layer.
 * @param link_params 	Parameters of link layer.
 */
void NET_init (struct PHY_init_t *phy_params, struct LINK_init_t *link_params);

/**
 * Sends packet.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 					Destination end device ID.
 * @param payload		 		Payload.
 * @param len 					Payload length.
 * @return Returns true if packet is successfully sent, false otherwise.
 */
bool NET_send (uint8_t tocoord, uint8_t * toed, uint8_t * payload, uint8_t len);

/**
 * Broadcasts packet.
 * @param msg_type 			Message type on network layer.
 * @param msg_type_ext	essage type on network layer (valid if msg_type is set to F (hexa)).
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
bool NET_send_broadcast (uint8_t msg_type, uint8_t msg_type_ext, uint8_t * data, uint8_t len);

/**
 * Removes device from network.
 * @param edid  End device ID.
 * @return Returns true if device is successfully removed from network.
 */
bool NET_unpair (uint8_t edid[EDID_LENGTH]);

/**
 * Enables pair mode.
 */
void NET_joining_enable ();

/**
 * Disables pair mode.
 */
void NET_joining_disable ();

/**
 * Checks if pair mode is enabled.
 * @return Returns true if pair mode is set, false otherwise.
 */
bool NET_is_set_pair_mode ();

/**
 * Checks if end device is joined the network.
 * @return Returns true if end device is joined the network, false otherwise.
 */
bool NET_joined ();

extern void NET_received (const uint8_t from_cid,
														 const uint8_t from_edid[EDID_LENGTH], const uint8_t * data,
														 const uint8_t len);

extern bool NET_accept_device (uint8_t parent_cid);


extern void LINK_timer_counter();

/**
 * Waits for timeout for joining process, then sends JOIN RESPONSE (ROUTE) packet.
 */
void NET_joining();

/**
 * Waits for timeout for network reinitialization, then sends MOVE RESPONSE (ROUTE) packet.
 */
void NET_moving();

extern void fitp_send_move_response(uint8_t tocoord, uint8_t* toed);

extern void fitp_send_move_response_route(uint8_t tocoord, uint8_t* toed);

/**
 * Sends MOVE RESPONSE packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 					Destination end device ID.
 */
void NET_send_move_response(uint8_t* payload, uint8_t len, uint8_t tocoord, uint8_t* toed);

/**
 * Sends MOVE RESPONSE ROUTE packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 * @param tocoord 			Destination coordinator ID.
 * @param toed 					Destination end device ID.
 */
void NET_send_move_response_route(uint8_t* packet, uint8_t len, uint8_t tocoord, uint8_t* toed);

extern void NET_notify_send_done();

void NET_set_pair_mode_timeout(uint8_t timeout);

void NET_save_msg_info(uint8_t msg_type, uint8_t device_type, uint8_t* sedid, uint8_t* data, uint8_t len);

bool load_device_table();

uint8_t NET_get_measured_noise();

#endif
