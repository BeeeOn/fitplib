/**
* @file fitp.c
*/

#include "net.h"
#include "fitp.h"

/**
 * Ensures initialization of network, link and physical layer.
 * @param phy_params		Parameters of physical layer.
 * @param link_params		Parameters of link layer.
 */
void fitp_init (struct PHY_init_t* phy_params, struct LINK_init_t* link_params)
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
	if(tocoord != 0) {
		// packet is for COORD, destination EDID is filled with zeros
		return NET_send(tocoord, FITP_DIRECT_COORD, data, len);
	}
	else {
		// packet is for ED, destination COORD has been already set to zero
		return NET_send(tocoord, toed, data, len);
	}
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
 * Enables pair mode and sets timeout.
 * @param timeout	Duration of pair mode (in seconds).
 */
void fitp_joining_enable (uint8_t timeout)
{
	GLOBAL_STORAGE.pair_mode = true;
	// pair_mode_timeout is decreased every 50 ms
	// 1s = 1000 ms -> 1000/50 = 20
	// 20 is number of pair_mode_timeout decrements per second
	GLOBAL_STORAGE.pair_mode_timeout = 20 * timeout;
	D_G printf("fitp_joining_enable()\n");
}

/**
 * Disables pair mode.
 */
void fitp_joining_disable ()
{
	GLOBAL_STORAGE.pair_mode = false;
	D_G printf("fitp_joining_disable()\n");
}

/**
 * Notifies successful four-way handshake.
 */
void NET_notify_send_done ()
{
	fitp_notify_send_done ();
}
