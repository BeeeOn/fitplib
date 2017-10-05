/**
* @file global.h
*/
#ifndef GLOBAL_STORAGE_H
#define GLOBAL_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <string>

/*! maximum number of coordinators in a network */
#define MAX_COORD 64
/*! length of end device ID */
#define EDID_LENGTH 4
/*! maximum number of end devices in a network */
#define MAX_DEVICES 255
/*! invalid coordinator ID */
#define INVALID_CID 0xff

/**
 * Structure for device table.
 */
typedef struct {
	bool coord;									/**< Flag if device is coordinator. */
	bool valid;									/**< Flag if record is valid. */
	bool sleepy;								/**< Flag if device is sleepy. */
	uint8_t edid[EDID_LENGTH];	/**< End device ID. */
	uint8_t cid;								/**< Coordinator ID. */
	uint8_t parent_cid;					/**< Parent ID. */
} device_table_record_t;

// for arm
#ifndef X86

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	bool pair_mode;															/**< Flag if pair mode is enabled (only for coordinator and PAN coordinator). */
	bool routing_enabled;												/**< Flag if routing is enabled (only for coordinator and PAN coordinator). */
	uint8_t routing_tree[MAX_COORD];						/**< Routing tree (only for coordinator and PAN coordinator). */
	uint8_t nid[4];															/**< Network ID. */
	uint8_t cid;																/**< Coordinator ID. */
	uint8_t parent_cid;													/**< Parent ID. */
	uint8_t edid[EDID_LENGTH];									/**< End device ID. */
	device_table_record_t devices[MAX_DEVICES];	/**< Device table (only for PAN coordinator). */
	std::string device_table_path;							/**< Path to device table (only for PAN coordinator). */
};

// for x86
#else

/**
 * Structure for global layer.
 */
struct GLOBAL_storage_t {
	bool pair_mode;
	bool routing_enabled;

	uint8_t routing_tree[MAX_COORD];
	uint8_t nid[4];
	uint8_t cid;

	uint8_t parent_cid;
	uint8_t edid[EDID_LENGTH];

	device_table_record_t devices[MAX_DEVICES];
	 std::string device_table_path;

	// for x86, for simulator
	uint8_t pid[4];
	uint8_t channel;

	uint8_t tocoord;
	uint8_t toed[EDID_LENGTH];
	uint8_t data_len;
	uint8_t data[100];
	uint8_t rssi;
	uint8_t bitrate;
	uint8_t band;
};
#endif

extern struct GLOBAL_storage_t GLOBAL_STORAGE;

#endif
