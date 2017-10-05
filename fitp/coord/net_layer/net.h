/**
* @file net.h
*/

#ifndef NET_LAYER_H
#define NET_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "global.h"
#include "link.h"

/*! size of network header */
#define NET_HEADER_SIZE 10
/*! maximum size of network payload */
/*! MAX_NET_PAYLOAD_SIZE = 53 - 10 = 43 */
#define MAX_NET_PAYLOAD_SIZE ( MAX_LINK_PAYLOAD_SIZE - NET_HEADER_SIZE )

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
 * @param toed 				Destination end device ID.
 * @param payload		 	Payload.
 * @param len 					Payload length.
 * @return Returns true if packet is successfully sent, false otherwise.
 */
bool NET_send (uint8_t tocoord, uint8_t * toed, uint8_t * payload, uint8_t len);

/**
 * Broadcasts packet.
 * @param msg_type 			Message type on network layer.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void NET_send_broadcast (uint8_t msg_type, uint8_t * payload, uint8_t len);

/**
 * Sends JOIN REQUEST message.
 * @return Returns true if JOIN RESPONSE packet is received, false otherwise.
 */
bool NET_join ();

/**
 * Checks if end device is joined the network.
 * @return Returns true if end device is joined the network, false otherwise.
 */
bool NET_joined ();

/**
 * Processes received data.
 * @param from_cid 		Source coordinator ID.
 * @param from_edid 	Source end device ID.
 * @param data 				Received data.
 * @param len 				Received data length.
 */
void NET_received (const uint8_t from_cid,
														const uint8_t from_edid[EDID_LENGTH], const uint8_t * data,
														const uint8_t len);

/**
 * Processes a routing table and sends it to descendants of this coordinator.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void NET_process_routing_table (uint8_t * payload, uint8_t len);

/**
 * Checks if pair mode is enabled.
 * @return Returns true if pair mode is enabled, false otherwise.
 */
bool NET_is_set_pair_mode();

/**
 * Checks if received packet includes routing table (RT).
 * @param msg_type				Message type on network layer.
 * @return Returns true if packet contains RT (PT_NETWORK_ROUTING_DATA message type), false otherwise.
 */
bool NET_is_routing_data_message(uint8_t msg_type);

/**
 * Sends MOVE REQUEST packet in case of parent loss.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void NET_send_move_request(uint8_t* payload, uint8_t len);

/**
 * Notifies successful four-way handshake.
 */
void NET_notify_send_done();

#endif
