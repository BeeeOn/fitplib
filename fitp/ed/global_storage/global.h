/**
* @file fitp.h
*/
#ifndef GLOBAL_STORAGE_H
#define GLOBAL_STORAGE_H

#include "include.h"

/*! maximum number of coordinators in a network */
#define MAX_COORD 64
/*! length of end device ID */
#define EDID_LENGTH 4

// for xc8
#ifndef X86

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	bool waiting_join_response;				/**< Flag if device is waiting for JOIN RESPONSE message (only for coordinator and end device). */
	bool sleepy_device;								/**< Flag if end device is sleepy (only for end device). */
	uint8_t nid[4];										/**< Network ID. */
	uint8_t cid;											/**< Coordinator ID. */
	uint8_t parent_cid;								/**< Parent ID. */
	uint8_t edid[EDID_LENGTH];				/**< End device ID. */
	uint16_t refresh_time;						/**< Refresh time (only for end device). */
};

// for x86
#else	/*  */

// push current alignment to stack
#pragma pack(push)
// set alignment to 1 byte boundary
#pragma pack(1)

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	bool waiting_join_response;
	bool sleepy_device;
	uint8_t nid[4];
	uint8_t cid;
	uint8_t parent_cid;
	uint8_t edid[EDID_LENGTH];
	uint16_t refresh_time;
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

#endif /*  */
extern struct GLOBAL_storage_t GLOBAL_STORAGE;
#endif /*  */
