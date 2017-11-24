#include <string.h>
#include <stdio.h>
#include <mico_bt_types.h>

#include "mico.h"
#include "mico_bt.h"
#include "mico_bt_cfg.h"
#include "mico_bt_smart_interface.h"
#include "mico_bt_smartbridge.h"
#include "mico_bt_smartbridge_gatt.h"
#include "mico_bt_peripheral.h"
#include "sdpdefs.h"

#include "StringUtils.h"
#include "LinkListUtils.h"

#include "mico_ble_lib.h"

#define mico_ble_log(M, ...) custom_log("BLE", M, ##__VA_ARGS__)

#define MANUFACTURE "MXCHIP"
#define MODEL       "123456"
#define SYSTEM_ID   "001122"

#define BLUETOOTH_PRINT_SERVICE_UUID    0x18F0
#define BLUETOOTH_PRINT_CHAR_CMD_UUID   0x2AF1

enum {
    HDLS_DEV_INFO = 0x01,
    HDLC_DEV_INFO_MFR_NAME,
    HDLC_DEV_INFO_MFR_NAME_VALUE,
    HDLC_DEV_INFO_MODEL_NUM,
    HDLC_DEV_INFO_MODEL_NUM_VALUE,
    HDLC_DEV_INFO_SYSTEM_ID,
    HDLC_DEV_INFO_SYSTEM_ID_VALUE,
};

typedef struct {
    mico_bool_t          m_is_central;
    mico_bool_t          m_is_initialized;
    mico_ble_evt_cback_t m_cback;

    const char          *m_wl_name;
    const mico_bt_uuid_t*m_wl_uuid;
} mico_ble_context_t;

static mico_bool_t mico_ble_check_uuid(const mico_bt_uuid_t *uuid);

static mico_ble_context_t g_ble_context;

/*---------------------------------------------------------------------------------------------
 * Central local resource
 * 
 */
static mico_bt_smartbridge_socket_t g_central_socket;
static mico_worker_thread_t         g_central_worker_thread;
static mico_bt_uuid_t               g_central_whitelist_uuid = {
    .len = LEN_UUID_16,
    .uu.uuid16 = BLUETOOTH_PRINT_SERVICE_UUID,
};

static const mico_bt_smart_security_settings_t g_central_security_settings = {
    .timeout_second = 15,
    .io_capabilities = BT_SMART_IO_DISPLAY_ONLY,
    .authentication_requirements = BT_SMART_AUTH_REQ_NONE,
    .oob_authentication = BT_SMART_OOB_AUTH_NONE,
    .max_encryption_key_size = 16,
    .master_key_distribution = BT_SMART_DISTRIBUTE_ENCRYPTION_AND_SIGN_KEYS,
    .slave_key_distribution = BT_SMART_DISTRIBUTE_ALL_KEYS,
};

static const mico_bt_smart_connection_settings_t g_central_connection_settings = {
    .timeout_second = 10,
    .filter_policy = FILTER_POLICY_NONE,
    .interval_min = MICO_BT_CFG_DEFAULT_CONN_MIN_INTERVAL,
    .interval_max = MICO_BT_CFG_DEFAULT_CONN_MAX_INTERVAL,
    .latency  = MICO_BT_CFG_DEFAULT_CONN_LATENCY,
    .supervision_timeout = MICO_BT_CFG_DEFAULT_CONN_SUPERVISION_TIMEOUT,
    .ce_length_min = 0,
    .ce_length_max = 0,
    .attribute_protocol_timeout_ms = 1000,
};

/*--------------------------------------------------------------------------------------------
 * Peripheral local resource
 * 
 */
static mico_bt_peripheral_socket_t g_peripheral_socket;

static const mico_bt_smart_advertising_settings_t g_peripheral_advertising_settings = {
    .type = BT_SMART_UNDIRECTED_ADVERTISING,
    .use_high_duty = MICO_TRUE,
    .high_duty_interval = MICO_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MIN_INTERVAL,
    .high_duty_duration = 30,
    .low_duty_interval = MICO_BT_CFG_DEFAULT_LOW_DUTY_ADV_MIN_INTERVAL,
    .low_duty_duration = 0,
};

static const uint8_t g_peripheral_gatt_database[] = {
    /* Declare Data Service */
    PRIMARY_SERVICE_UUID16(HDLS_DEV_INFO, UUID_SERVCLASS_DEVICE_INFO),
        /* Handle 0x43: characteristic Manufacture Name */
        CHARACTERISTIC_UUID16(HDLC_DEV_INFO_MFR_NAME, HDLC_DEV_INFO_MFR_NAME_VALUE,
                               GATT_UUID_MANU_NAME, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE),
        /* Handle 0x50: characteristic Model number */
        CHARACTERISTIC_UUID16(HDLC_DEV_INFO_MODEL_NUM, HDLC_DEV_INFO_MODEL_NUM_VALUE,
                               GATT_UUID_MODEL_NUMBER_STR, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE),
        /* Handle 0x52: characteristic System ID */
        CHARACTERISTIC_UUID16(HDLC_DEV_INFO_SYSTEM_ID, HDLC_DEV_INFO_SYSTEM_ID_VALUE,
                               GATT_UUID_SYSTEM_ID, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE),
};

/*---------------------------------------------------------------------------------------------
 * Peripheral function definition 
 */

static void mico_ble_peripheral_create_attribute_db(void)
{
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_MFR_NAME, strlen((char *)MANUFACTURE), (uint8_t *)MANUFACTURE, NULL);
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_MODEL_NUM_VALUE, strlen((char *)MODEL), (uint8_t *)MODEL, NULL);
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_SYSTEM_ID_VALUE, strlen((char *)SYSTEM_ID), (uint8_t *)SYSTEM_ID, NULL);
}

static void mico_ble_peripheral_set_advertisement_data(void)
{

}

static OSStatus mico_ble_peripheral_connect_handler(mico_bt_peripheral_socket_t *socket)
{

}

static OSStatus mico_ble_peripheral_disconnect_handler(mico_bt_peripheral_socket_t *socket)
{

}

static mico_bt_result_t mico_ble_peripheral_device_init(void)
{
    OSStatus err = mico_bt_peripheral_init(&g_peripheral_socket, 
                                           &g_central_security_settings, 
                                           mico_ble_peripheral_connect_handler,
                                           mico_ble_peripheral_disconnect_handler,
                                           NULL);
    require_noerr(err, exit);

    /* Build BT Stack layer GATT database */
    err = mico_bt_gatt_db_init(g_peripheral_gatt_database, sizeof(g_peripheral_gatt_database));
    require_noerr(err, exit);

    /* Build BT Application layer GATT database */
    mico_ble_peripheral_create_attribute_db();

    /* Set advertisement parameters and data payload */
    mico_ble_peripheral_set_advertisement_data();

exit:
    return err;
}

/*----------------------------------------------------------------------------------------------
 * Central function definition 
 */

static OSStatus mico_ble_central_scan_complete_handler(void *arg)
{

}

static OSStatus mico_ble_central_scan_result_handler(const mico_bt_smart_advertising_report_t *scan_result)
{

}

static OSStatus mico_ble_central_connect_handler(void *arg)
{

}

static OSStatus mico_ble_central_disconnection_handler(mico_bt_smartbridge_socket_t *socket)
{

}

static mico_bt_result_t mico_ble_central_device_init(const char *whitelist_name, const mico_bt_uuid_t *whitelist_uuid)
{
    OSStatus err = mico_bt_smartbridge_init(1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_enable_attribute_cache(1, &g_central_whitelist_uuid, 1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_create_socket(&g_central_socket);
    require_noerr(err, exit);

    err = mico_rtos_create_worker_thread(&g_central_worker_thread, MICO_APPLICATION_PRIORITY, 2048, 1);
    require_noerr(err, exit);

exit:
    return err;
}

/**
 *  mico_bluetooth_init
 *
 *  Initialize Bluetooth Sub-system.
 *
 * @param [in] context      : An pointer of The Application Context
 *             Structure.
 * @param [in] mico_context : An pointer of the MiCO Context structure.
 *
 * @param [in] cback        : An call back handler for the bluetooth
 *             for bluetooth sub-system.
 *
 * @return #mico_bt_result_t
 *
 */
mico_bt_result_t mico_ble_init(const char *device_name, const char *whitelist_name, const mico_bt_uuid_t *whitelist_uuid, mico_ble_evt_cback_t cback) 
{
    mico_bt_result_t err;
    
    if (!device_name || !cback) {
        return MICO_BT_ERROR;
    }

    if (g_ble_context.m_is_initialized) {
        return MICO_BT_SUCCESS;
    }

    memset(&g_ble_context, 0, sizeof(g_ble_context));

    err = mico_bt_init(MICO_BT_HCI_MODE, device_name, 1, 1);
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializing MiCO Bluetooth Framework");

    err = mico_ble_central_device_init(whitelist_name, whitelist_uuid);
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializing MiCO Bluetooth Central Role");

    err = mico_ble_peripheral_device_init();
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializaing MiCO Bluetooth Peripheral Role");

    /* BT Mode to Peripheral */
    err = mico_ble_set_device_discovery(MICO_TRUE);
    require_string(err == MICO_BT_SUCCESS, exit, "Error setting device to discoverable");
    g_ble_context.m_is_central = MICO_FALSE;
    g_ble_context.m_cback = cback;
    g_ble_context.m_is_initialized = MICO_TRUE;

    if (whitelist_name && strlen(whitelist_name)) {
        g_ble_context.m_wl_name = whitelist_name;
    }
    if (whitelist_uuid && mico_ble_check_uuid(whitelist_uuid)) {
        g_ble_context.m_wl_uuid = whitelist_uuid;
    }

exit:
    return err;
}

/**
 *  mico_bluetooth_start_procedure
 *
 *  Start or stop the Data Mode.
 *
 * @param [in] start: True is starting, else stopping
 *
 * @return #mico_bt_result_t
 */
mico_bt_result_t mico_ble_start_procedure(mico_bool_t start)
{
    return MICO_BT_SUCCESS;
}

/**
 *  bluetooth_send_data
 *
 *  Trigger an action for sending data over RFCOMM Channel.
 *
 * @param None
 *
 * @return None
 */
mico_bt_result_t mico_ble_get_dev_address(mico_bt_device_address_t bdaddr)
{
    return MICO_BT_SUCCESS;
}

/**
 * Set local BT Device Name.
 *
 * @param name
 *          An new name for BT Controller.
 *
 * @return MICO_BT_PENDING if successfully.
 */
mico_bt_result_t mico_ble_set_device_name(const char *name)
{
    return MICO_BT_SUCCESS;
}

/**
 * Get local BT Device Name
 *
 * @return A c-style string of local device name.
 */
const char *mico_ble_get_device_name(void)
{
    return NULL;
}

/**
 * Set passkey for BT Security process.
 *
 * @param key
 *      a c-style string of the passkey.
 *
 * @return
 *      MICO_BT_SUCCESS if successfully.
 */
mico_bt_result_t mico_ble_set_device_whitelist_name(const char *name)
{
    return MICO_BT_SUCCESS;
}

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
const char *mico_ble_get_device_whitelist_name(void)
{
    return g_ble_context.m_wl_name;
}

/**
 * Set passkey for BT Security process.
 *
 * @param key
 *      a c-style string of the passkey.
 *
 * @return
 *      MICO_BT_SUCCESS if successfully.
 */
mico_bt_result_t mico_ble_set_device_whitelist_uuid(const mico_bt_uuid_t *uuid)
{
    return MICO_BT_SUCCESS;
}

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
const mico_bt_uuid_t *mico_ble_get_device_whitelist_uuid(void)
{
    return g_ble_context.m_wl_uuid;
}

/**
 *
 * @param start
 * @return
 */
mico_bt_result_t mico_ble_set_device_scan(mico_bool_t start)
{
    return MICO_BT_SUCCESS;
}

/**
 * Start or stop a device discoverable procedure.
 *
 * @param start
 *      A boolean value of start or stop.
 *
 * @return
 *      MICO_BT_SUCCESS if succesfully.
 */
mico_bt_result_t mico_ble_set_device_discovery(mico_bool_t start)
{
    return MICO_BT_SUCCESS;
}

/**
 *
 * @param bdaddr
 * @return
 */
mico_bt_result_t mico_ble_connect(mico_bt_device_address_t bdaddr)
{
    return MICO_BT_SUCCESS;
}

/**
 *
 * @param connect_handle
 * @return
 */
mico_bt_result_t mico_ble_disconnect(uint16_t connect_handle)
{
    return MICO_BT_SUCCESS;
}

/**
 * Get local BT RF State.
 *
 * @return
 *      a value of local BT RF State. See details @mico_ble_state_t
 */
mico_ble_state_t mico_ble_get_device_state(void)
{
    return BLE_STATE_CONNECTED;
}

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
mico_bt_result_t mico_ble_send_data(const uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
    return MICO_BT_SUCCESS;
}

static mico_bool_t mico_ble_check_uuid(const mico_bt_uuid_t *uuid)
{
    if (!uuid) {
        return MICO_FALSE;
    }

    if (uuid->len == LEN_UUID_16) {
        return (uuid->uu.uuid16 > (uint16_t)0 && uuid->uu.uuid16 < (uint16_t)(-1));
    } else if (uuid->len == LEN_UUID_32) {
        return (uuid->uu.uuid32 > (uint32_t)0 && uuid->uu.uuid32 < (uint32_t)(-1));
    } else if (uuid->len == LEN_UUID_128) {
        uint8_t *p = uuid->uu.uuid128;
        while (p < &uuid->uu.uuid128[LEN_UUID_128] && *p++ == 0);
        return (p != &uuid->uu.uuid128[LEN_UUID_128]);
    } else {
        return MICO_FALSE;
    }
}
