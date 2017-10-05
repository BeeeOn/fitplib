/**
* @file global.h
*/
#ifndef GLOBAL_STORAGE_H
#define GLOBAL_STORAGE_H

/*! maximum number of coordinators in a network */
#define MAX_COORD 64
/*! length of end device ID */
#define EDID_LENGTH 4
/*! invalid coordinator ID */
#define INVALID_CID 0xff

#include "include.h"

// for xc8
#ifndef X86

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	uint8_t routing_tree[MAX_COORD];	/**< Routing tree (only for coordinator and PAN coordinator). */
	uint8_t nid[4];										/**< Network ID. */
	uint8_t cid;											/**< Coordinator ID. */
	bool waiting_join_response;				/**< Flag if device is waiting for JOIN RESPONSE message (only for coordinator and end device). */
	bool routing_enabled;							/**< Flag if routing is enabled (only for coordinator and PAN coordinator). */
	bool pair_mode;										/**< Flag if pair mode is enabled (only for coordinator and PAN coordinator). */
	uint8_t pair_mode_timeout;				/**< Pair mode duration (in seconds) (only for coordinator). */
	uint8_t parent_cid;								/**< Parent ID. */
	uint8_t edid[EDID_LENGTH];				/**< End device ID. */
};

#else
// push current alignment to stack
#pragma pack(push)
// set alignment to 1 byte boundary
#pragma pack(1)

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	uint8_t routing_tree[MAX_COORD];
	uint8_t nid[4];
	uint8_t cid;
	bool waiting_join_response;
	bool routing_enabled;
	bool pair_mode;
	uint8_t pair_mode_timeout;
	uint8_t parent_cid;
	uint8_t edid[EDID_LENGTH];

	// for x86, for simulator
	uint8_t id;
	uint8_t channel;
	uint8_t pid[4];

	uint8_t tocoord;
	uint8_t toed[EDID_LENGTH];
	uint8_t data_len;
	uint8_t data[MAX_PHY_PAYLOAD_SIZE];
	uint8_t rssi;
	uint8_t bitrate;
	uint8_t band;
};

#endif

extern struct GLOBAL_storage_t GLOBAL_STORAGE;

#endif
