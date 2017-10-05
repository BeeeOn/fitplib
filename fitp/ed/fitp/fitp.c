/**
* @file fitp.c
*/

#include "phy.h"
#include "constants.h"
#include "link.h"
#include "net.h"
#include "fitp.h"
#include "include.h"

extern void NET_notify_send_done();
extern void fitp_notify_send_done();

/**
 * Ensures initialization of network, link and physical layer.
 * @param phy_params		Parameters of physical layer.
 * @param link_params		Parameters of link layer.
 */
void fitp_init (struct PHY_init_t * phy_params, struct LINK_init_t * link_params)
{
	NET_init (phy_params, link_params);
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
	return NET_send (tocoord, FITP_DIRECT_COORD, data, len);
}

/**
 * Sends JOIN REQUEST message.
 * @return Returns true if end device is successfully joined a network, false otherwise.
 */
bool fitp_join ()
{
	return NET_join ();
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
 * Processes received data.
 * @param from_cid 		Source coordinator ID.
 * @param from_edid 	Source end device ID.
 * @param data 				Received data.
 * @param len 				Received data length.
 */
void NET_received (const uint8_t from_cid, const uint8_t from_edid[4],
										 const uint8_t* data, const uint8_t len)
{
	 fitp_received (from_cid, from_edid, data, len);
}

/**
 * Sends MOVE REQUEST message.
 */
void fitp_send_move_request()
{
	uint8_t packet[1];
	packet[0] = FITP_MOVE_REQUEST;
	NET_send_move_request(packet, 1);
}

/**
 * Notifies successful four-way handshake.
 */
void NET_notify_send_done()
{
	fitp_notify_send_done();
}
