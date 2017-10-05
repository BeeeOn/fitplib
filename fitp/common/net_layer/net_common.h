/**
* @file net_common.h
*/

#ifndef COMMON_NET_LAYER_H
#define COMMON_NET_LAYER_H

/*! broadcast address (end device) */
uint8_t NET_ED_ALL[4] = {0xff, 0xff, 0xff, 0xff};
/*! broadcast address (coordinator) */
#define NET_COORD_ALL 0x3f
/*! unused extended message type */
#define NOT_EXTENDED 0x00
/*! device type - coordinator */
#define COORD 0xcc
/*! device type - sleepy devoce */
#define SLEEPY_ED 0xff
/*! device type - ready device */
#define READY_ED 0x00

/**
 * Packet types on network layer.
 */
enum packet_type_e {
	PT_DATA = 0x00,													/**< DATA. */
	PT_DATA_DR = 0x01,											/**< DATA request. */
	PT_DATA_JOIN_REQUEST = 0x03,						/**< JOIN REQUEST. */
	PT_DATA_ACK_DR_WAIT = 0x05,							/**< ACK of DATA request. */
	PT_DATA_ACK_DR_SLEEP = 0x06,						/**< NACK of DATA request. */
	PT_DATA_JOIN_RESPONSE = 0x07,						/**< JOIN RESPONSE. */
	PT_DATA_UNJOIN = 0x08,									/**< UNJOIN. */
	PT_DATA_JOIN_REQUEST_ROUTE = 0x09,			/**< JOIN REQUEST ROUTE. */
	PT_DATA_JOIN_RESPONSE_ROUTE = 0x0C,			/**< JOIN RESPONSE ROUTE. */
	PT_NETWORK_ROUTING_DATA = 0x0D,					/**< ROUTING DATA. */
	// it will be used for ACK message
	// on network layer
	//PT_NETWORK_ACK = 0x0E,
	PT_NETWORK_EXTENDED = 0x0F,						/*! EXTENDED. */
	PT_DATA_PAIR_MODE_ENABLED = 0x10,			/*! PAIR MODE ENABLED. */
	PT_DATA_MOVE_REQUEST = 0x30,					/*! MOVE REQUEST. */
	PT_DATA_MOVE_RESPONSE = 0x40,					/*! MOVE RESPONSE. */
	PT_DATA_MOVE_REQUEST_ROUTE = 0x50,		/*! MOVE REQUEST ROUTE. */
	PT_DATA_MOVE_RESPONSE_ROUTE = 0x60		/*! MOVE RESPONSE ROUTE. */
};

#endif
