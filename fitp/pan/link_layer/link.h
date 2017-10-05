/**
* @file link.h
*/

#ifndef LINK_PAN_H
#define LINK_PAN_H

#include <stdint.h>
#include <stdbool.h>
#include "../phy_layer/phy.h"
#include "fitp/pan/debug.h"

/*! size of link header */
#define LINK_HEADER_SIZE 10
/*! maximum size of link payload */
/*! MAX_LINK_PAYLOAD_SIZE = 63 - 10 = 53 */
#define MAX_LINK_PAYLOAD_SIZE ( MAX_PHY_PAYLOAD_SIZE - LINK_HEADER_SIZE )

/*! data transfer using four-way handshake */
#define LINK_DATA_HS4        	    0x00
/*! data transfer without waiting for ACK */
#define LINK_DATA_WITHOUT_ACK			0x01
/*! data transfer using broadcating */
#define LINK_DATA_BROADCAST     	0x02
/*! JOIN REQUEST message */
#define LINK_DATA_JOIN_REQUEST		0x03
/*! JOIN RESPONSE message */
#define LINK_DATA_JOIN_RESPONSE		0x04
/*! ACK JOIN message */
#define LINK_ACK_JOIN_REQUEST			0x05

/**
 * Structure for link layer (accessible for user).
 */
struct LINK_init_t {
	uint8_t tx_max_retries;	/**< Maximum number of packet retransmissions. */
};

/**
 * Initializes link layer and ensures initialization of physical layer.
 * @param phy_params 		Parameters of physical layer.
 * @param link_params 	Parameters of link layer.
 */
void LINK_init (struct PHY_init_t* phy_params, LINK_init_t* link_params);

/**
 * Gets address of coordinator (CID).
 * @param byte Byte containing coordinator ID.
 * @return Returns coordinator ID (6 bits).
 */
uint8_t LINK_cid_mask (uint8_t address);

extern void LINK_process_packet (uint8_t * payload, uint8_t len);

extern bool LINK_join_request_received (uint8_t signal_strenght,
																				uint8_t * payload, uint8_t len);

/**
 * Sends JOIN RESPONSE packet.
 * @param edid 					Destination EDID.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
void LINK_send_join_response (uint8_t * to, uint8_t * data, uint8_t len);

/**
* Sends packet.
* @param to_ed 				Flag if destination device is end device.
* @param address 			Destination address.
* @param payload 			Payload.
* @param len 					Payload length.
* @param transfer_type Transfer type on link layer.
* @return Returns false if TX buffer is full, true otherwise.
*/
bool LINK_send_coord (bool to_ed, uint8_t * address, uint8_t * payload,
											uint8_t len, uint8_t transfer_type);

extern bool LINK_route (uint8_t * payload, uint8_t len, uint8_t transfer_type);

extern void LINK_error_handler_coord ();

/**
 * Broadcasts packet.
 * @param payload 			Payload.
 * @param len 					Payload length.
 */
bool LINK_send_broadcast (uint8_t * payload, uint8_t payload_len);

/**
* Gets RSSI value.
* @return Returns RSSI value.
*/
uint8_t LINK_get_measured_noise();

extern void LINK_notify_send_done ();

extern void LINK_save_msg_info (uint8_t* data, uint8_t len);

uint8_t LINK_get_measured_noise();

#endif
