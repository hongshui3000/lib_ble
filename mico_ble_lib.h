/**
 ******************************************************************************
 * @file    lib_ble.h
 * @author  Jian Zhang
 * @version V1.0.5
 * @date    9-March-2017
 * @brief   RFCOMM Server Sample application header file
 ******************************************************************************
 *
 *  The MIT License
 *  Copyright (c) 2014 MXCHIP Inc.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is furnished
 *  to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 *  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ******************************************************************************
 **/

#pragma once

#include "mico.h"
#include "mico_platform.h"
#include "mico_config.h"

#include "mico_bt_cfg.h"
#include "mico_bt_dev.h"
#include "mico_bt_stack.h"


#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * Constants
 ****************************************************************************/

/*****************************************************************************
 * Types
 *****************************************************************************/

#define BLE_STATE_PERIPHERAL_ADVERTISING 1
#define BLE_STATE_PERIPHERAL_CONNECTED	 2
#define BLE_STATE_CENTRAL_SCANNING 		 3
#define BLE_STATE_CENTRAL_CONNECTING	 4
#define BLE_STATE_CENTRAL_CONNECTED 	 5
#define BLE_STATE_IDLE					 6
typedef uint8_t mico_ble_state_t;

/* Bluetooth event type */
typedef enum {
    BLE_EVT_INIT,
    BLE_EVT_PERIPHREAL_ADV_START,
    BLE_EVT_PERIPHERAL_ADV_STOP,
    BLE_EVT_PERIPHERAL_CONNECTED,
    BLE_EVT_PERIPHERAL_DISCONNECTED,
    BLE_EVT_DATA,
    BLE_EVT_CENTRAL_SCAN_START,
    BLE_EVT_CENTRAL_SCAN_STOP,
    BLE_EVT_CENTRAL_REPORT,
    BLE_EVT_CENTRAL_CONNECTING,
    BLE_EVT_CENTRAL_CONNECTED,
    BLE_EVT_CENTRAL_DISCONNECTED,
} mico_ble_event_t;

/* Bluetooth event callback parameters. */
typedef struct {

    /* Common Information */
    mico_bt_device_address_t bd_addr;

    /* Determinate by Event Type */
    union {
        /* valid if BLE_EVT_INIT */
        struct {
            mico_bt_result_t status;
        } init;

        /* valid if BLE_EVT_PERIPHERAL_CONNECTED or BLE_EVT_CENTRAL_CONNECTED */
        struct {
            uint16_t handle;
        } conn;

        /* valid if BLE_EVT_PERIPHERAL_DISCONNECTED or BLE_EVT_CENTRAL_DISCONNECTED */
        struct {
            uint16_t handle;
        } disconn;

        /* valid if BLE_EVT_DATA */
        struct {
            uint8_t *p_data;
            uint16_t length;
        } data;

        /* valid if BLE_EVT_CENTRAL_REPORT */
        struct { 
            char   name[31];
            int8_t rssi;
        } report;
    } u;
} mico_ble_evt_params_t;

/* Bluetooth event handler in user layer application */
typedef OSStatus (*mico_ble_evt_cback_t)(mico_ble_event_t event, const mico_ble_evt_params_t *p_params);


/*****************************************************************************
 * Globals
 *****************************************************************************/


/*****************************************************************************
 * Function Prototypes
 *****************************************************************************/

/* RFCOMM API */

/**
 *  mico_bluetooth_init
 *
 *  Initialize Bluetooth Sub-system.
 *
 * @param [in] name      : a c-style string name for BT Controller.
 * 
 * @param [in] whitelist_name: a c-style strin name for BT Controller whitelist.
 * 
 * @param [in] whitelist_uuid: a pointer of UUID for BT Controller whitelist.
 *
 * @param [in] cback        : An call back handler for the bluetooth
 *             for bluetooth sub-system.
 *
 * @return #mico_bt_result_t
 *
 */
mico_bt_result_t mico_ble_init(const char *name, 
                               const char *whitelist_name, 
                               mico_bool_t is_central, 
                               mico_ble_evt_cback_t cback);

/**
 *  bluetooth_send_data
 *
 *  Trigger an action for sending data over RFCOMM Channel.
 *
 * @param None
 *
 * @return None
 */
mico_bt_result_t mico_ble_get_dev_address(mico_bt_device_address_t bdaddr);

/**
 * Set local BT Device Name.
 *
 * @param name
 *          An new name for BT Controller.
 *
 * @return MICO_BT_PENDING if successfully.
 */
mico_bt_result_t mico_ble_set_device_name(const char *name);

/**
 * Get local BT Device Name
 *
 * @return A c-style string of local device name.
 */
const char *mico_ble_get_device_name(void);

/**
 * Set passkey for BT Security process.
 *
 * @param key
 *      a c-style string of the passkey.
 *
 * @return
 *      MICO_BT_SUCCESS if successfully.
 */
mico_bt_result_t mico_ble_set_device_whitelist_name(const char *name);

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
const char *mico_ble_get_device_whitelist_name(void);

/**
 * Set passkey for BT Security process.
 *
 * @param key
 *      a c-style string of the passkey.
 *
 * @return
 *      MICO_BT_SUCCESS if successfully.
 */
// mico_bt_result_t mico_ble_set_device_whitelist_uuid(const mico_bt_uuid_t *uuid);

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
// const mico_bt_uuid_t *mico_ble_get_device_whitelist_uuid(void);

/**
 *
 * @param start
 * @return
 */
mico_bt_result_t mico_ble_start_device_scan(void);

/**
 * Start or stop a device discoverable procedure.
 *
 * @param start
 *      A boolean value of start or stop.
 *
 * @return
 *      MICO_BT_SUCCESS if succesfully.
 */
mico_bt_result_t mico_ble_start_device_discovery(void);
/**
 *
 * @param bdaddr
 * @return
 */
mico_bt_result_t mico_ble_connect(mico_bt_device_address_t bdaddr);

/**
 *
 * @param connect_handle
 * @return
 */
mico_bt_result_t mico_ble_disconnect(uint16_t connect_handle);

/**
 * Get local BT RF State.
 *
 * @return
 *      a value of local BT RF State. See details @mico_ble_state_t
 */
mico_ble_state_t mico_ble_get_device_state(void);

/**
 * Send a packet synchronously over BT RFCOMM Channel.
 *
 * @param p_data
 *          A pointer of packet.
 *
 * @param length
 *          the size of packet.
 *
 * @param timeout_ms
 *          Timeout of synchronously.
 *
 * @return
 *      MICO_BT_SUCCESS  -- sending completily.
 *      MICO_BT_NO_RESOURCES -- No resource for malloc()
 *      MICO_BT_ILLEGAL_ACTION -- Illegal action (RFCOMM Channel is not opened).
 *      MICO_BT_TIMEOUT -- Timeout
 */
mico_bt_result_t mico_ble_send_data(const uint8_t *p_data, uint32_t length, uint32_t timeout_ms);

#define BDADDR_NTOA_SIZE 18
uint8_t *bdaddr_aton(const char *addr, uint8_t *out_addr);
char *bdaddr_ntoa(const uint8_t *addr, char *addr_str);

#ifdef __cplusplus
}
#endif
