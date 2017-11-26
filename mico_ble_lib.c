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
#include "statemachine.h"

#include "mico_ble_lib.h"

#define mico_ble_log(M, ...) custom_log("BLE", M, ##__VA_ARGS__)

#define APP_MANUFACTURE "MXCHIP"
#define APP_MODEL       "123456"
#define APP_SYSTEM_ID   "001122"

#define BLUETOOTH_PRINT_SERVICE_UUID    0x18F0
#define BLUETOOTH_PRINT_CHAR_CMD_UUID   0x2AF1

/* The handle of Custom GATT Service attribute.  */
enum {
    HDLS_DEV_INFO = 0x01,
    HDLC_DEV_INFO_MFR_NAME,
    HDLC_DEV_INFO_MFR_NAME_VALUE,
    HDLC_DEV_INFO_MODEL_NUM,
    HDLC_DEV_INFO_MODEL_NUM_VALUE,
    HDLC_DEV_INFO_SYSTEM_ID,
    HDLC_DEV_INFO_SYSTEM_ID_VALUE,
};

/* Application event type */
#define BLE_SM_EVT_PERIPHERAL_ADV_STOPED			1
#define BLE_SM_EVT_PERIPHERAL_CONNECTION_FAIL		2
#define BLE_SM_EVT_PERIPHERAL_DISCONNECTED		    3
#define BLE_SM_EVT_PERIPHERAL_CONNECTED			    4
#define BLE_SM_EVT_PERIPHERAL_LEADV_CMD			    5
#define BLE_SM_EVT_CENTRAL_LESCAN_CMD				6
#define BLE_SM_EVT_CENTRAL_LECONN_CMD				7
#define BLE_SM_EVT_CENTRAL_CONNECTED				8
#define BLE_SM_EVT_CENTRAL_CONNECTION_FAIL		    9
#define BLE_SM_EVT_CENTRAL_DISCONNECTED			    10
#define BLE_SM_EVT_CENTRAL_SCANNED				    11

static const char *g_stateNameTab[] = {
        "",
        "BLE_STATE_PERIPHERAL_ADVERTISING",
        "BLE_STATE_PERIPHERAL_CONNECTED",
        "BLE_STATE_CENTRAL_SCANNING",
        "BLE_STATE_CENTRAL_CONNECTING",
        "BLE_STATE_CENTRAL_CONNECTED",
        "BLE_STATE_IDLE"
};

static const char *g_eventTypeNameTabl[] = {
        "",
        "BLE_SM_EVT_PERIPHERAL_ADV_STOPED",
        "BLE_SM_EVT_PERIPHERAL_CONNECTION_FAIL",
        "BLE_SM_EVT_PERIPHERAL_DISCONNECTED",
        "BLE_SM_EVT_PERIPHERAL_CONNECTED",
        "BLE_SM_EVT_PERIPHERAL_LEADV_CMD",
        "BLE_SM_EVT_CENTRAL_LESCAN_CMD",
        "BLE_SM_EVT_CENTRAL_LECONN_CMD",
        "BLE_SM_EVT_CENTRAL_CONNECTED",
        "BLE_SM_EVT_CENTRAL_CONNECTION_FAIL",
        "BLE_SM_EVT_CENTRAL_DISCONNECTED",
        "BLE_SM_EVT_CENTRAL_SCANNED"
};

typedef struct {
    StateMachine         m_sm;
    SmRule               m_rules[18];
    mico_bool_t          m_is_central;
    mico_bool_t          m_is_initialized;
    mico_ble_evt_cback_t m_cback;

    const char          *m_wl_name;
    const mico_bt_uuid_t*m_wl_uuid;

    mico_worker_thread_t m_worker_thread;
    mico_bt_smartbridge_socket_t m_central_socket;
    mico_bt_peripheral_socket_t  m_peripheral_socket;
} mico_ble_context_t;

static mico_bool_t mico_ble_check_uuid(const mico_bt_uuid_t *uuid);
static mico_bool_t mico_ble_post_evt(mico_ble_event_t evt, mico_ble_evt_params_t *parms);
static mico_bt_result_t mico_ble_set_device_discovery(mico_bool_t start);
static mico_bt_result_t mico_ble_set_device_scan(mico_bool_t start);

static mico_ble_context_t g_ble_context;

/*---------------------------------------------------------------------------------------------
 * Central local resource
 * 
 */
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

/* SmartBridge auto scan settings */
static const mico_bt_smart_scan_settings_t g_central_scan_settings = {
   .type              = BT_SMART_PASSIVE_SCAN,
   .filter_policy     = FILTER_POLICY_NONE,
   .filter_duplicates = DUPLICATES_FILTER_ENABLED,
   .interval          = 512,
   .window            = 48,
   .duration_second   = 10,
};

/*--------------------------------------------------------------------------------------------
 * Peripheral local resource
 * 
 */
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
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_MFR_NAME, strlen((char *)APP_MANUFACTURE), (uint8_t *)APP_MANUFACTURE, NULL);
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_MODEL_NUM_VALUE, strlen((char *)APP_MODEL), (uint8_t *)APP_MODEL, NULL);
    mico_bt_peripheral_ext_attribute_add(HDLC_DEV_INFO_SYSTEM_ID_VALUE, strlen((char *)APP_SYSTEM_ID), (uint8_t *)APP_SYSTEM_ID, NULL);
}

static mico_bt_result_t mico_ble_peripheral_set_advertisement_data(void)
{
    OSStatus err = kNoErr;
    mico_bt_ble_advert_data_t adv_data;

    // mico_bt_ble_128service_t adver_services_128 = {
    //     .list_cmpl = false,
    //     .uuid128 = {UUID_HELLO_SERVICE},
    // };

    adv_data.flag = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
    // adv_data.p_services_128b = &adver_services_128;
    
    err = mico_bt_ble_set_advertisement_data(BTM_BLE_ADVERT_BIT_DEV_NAME 
                                            //  | BTM_BLE_ADVERT_BIT_SERVICE_128
                                             | BTM_BLE_ADVERT_BIT_FLAGS, 
                                             &adv_data);
    require_noerr_string(err, exit, "Set Advertisement Data failed");

    err = mico_bt_ble_set_scan_response_data(BTM_BLE_ADVERT_BIT_DEV_NAME, &adv_data);
    require_noerr_string(err, exit, "Set Advertisement ScanRsp Data failed");

exit:
    return err;
}

static OSStatus mico_ble_peripheral_advertisement_complete_handler(void *arg)
{
    UNUSED_PARAMETER(arg);

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_ADVERTISING)) {
        return mico_ble_set_device_discovery(MICO_TRUE);
    }
    return kNoErr;
}

static OSStatus mico_ble_peripheral_connect_handler(mico_bt_peripheral_socket_t *socket)
{
    mico_ble_log("Connection up [peripheral]");

    mico_bt_peripheral_stop_advertisements();

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_ADVERTISING)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_CONNECTED);
    }

    return kNoErr;
}

static OSStatus mico_ble_peripheral_disconnect_handler(mico_bt_peripheral_socket_t *socket)
{
    mico_ble_log("Connection down [periphreal]");

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_CONNECTED)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_DISCONNECTED);
    }

    return kNoErr;
}

static mico_bt_result_t mico_ble_peripheral_device_init(void)
{
    OSStatus err = mico_bt_peripheral_init(&g_ble_context.m_peripheral_socket, 
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

/* State translate action. */
static mico_bool_t app_peripheral_start_advertising(void *context)
{
    /* Start advertising proceudre */
    mico_bt_result_t err = mico_ble_set_device_discovery(MICO_TRUE);
    if (err != MICO_BT_SUCCESS) {
        return MICO_FALSE;
    }
    
    /* notify user latyer */
    mico_ble_post_evt(BLE_EVT_PERIPHREAL_ADV_START, NULL);

    return MICO_TRUE;
}

static mico_bool_t app_peripheral_connected(void *context)
{
    mico_ble_evt_params_t evt_params;

    /* 发送LEADV=OFF消息 */
    mico_ble_post_evt(BLE_EVT_PERIPHERAL_ADV_STOP, NULL);
    
    /* 发送LECONN=SLAVE,ON消息 */
    // memcpy(evt_params.bd_addr, )
    mico_ble_post_evt(BLE_EVT_PERIPHERAL_CONNECTED, &evt_params);
    
    return TRUE;
}

static mico_bool_t app_peripheral_disconnected(void *context)
{
    /* 发送LECONN=SLAVE,OFF消息 */
    mico_ble_evt_params_t evt_params;

    mico_ble_post_evt(BLE_EVT_PERIPHREAL_DISCONNECTED, &evt_params);
    return TRUE;
}

/*----------------------------------------------------------------------------------------------
 * Central function definition 
 */

static OSStatus mico_ble_central_scan_complete_handler(void *arg)
{
    return kNoErr;
}

static OSStatus mico_ble_central_scan_result_handler(const mico_bt_smart_advertising_report_t *scan_result)
{
    return kNoErr;
}

static OSStatus mico_ble_central_connect_handler(void *arg)
{
    return kNoErr;
}

static OSStatus mico_ble_central_disconnection_handler(mico_bt_smartbridge_socket_t *socket)
{
    return kNoErr;
}

static mico_bt_result_t mico_ble_central_device_init(const char *whitelist_name, const mico_bt_uuid_t *whitelist_uuid)
{
    OSStatus err = mico_bt_smartbridge_init(1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_enable_attribute_cache(1, &g_central_whitelist_uuid, 1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_create_socket(&g_ble_context.m_central_socket);
    require_noerr(err, exit);

    err = mico_rtos_create_worker_thread(&g_ble_context.m_worker_thread, MICO_APPLICATION_PRIORITY, 2048, 10);
    require_noerr(err, exit);

exit:
    return err;
}

static mico_bool_t app_central_start_scanning(void *context)
{
    /* 如果没有SCAN，则开启SCAN */
    
    /* 发送LESCAN=ON消息 */
    
    return TRUE;
}

static mico_bool_t app_central_scanning_stoped(void *context)
{
    /* 发送LESCAN=OFF消息 */
    return TRUE;
}

static mico_bool_t app_central_connected(void *context)
{
    /* 发送LECONN=CENTRAL,ON消息 */
    return TRUE;
}

static mico_bool_t app_central_disconnected(void *context)
{
    /* 发送LECONN=CENTRAL,OFF消息 */
    return TRUE;
}

static void mico_ble_state_machine_init(StateMachine *sm, uint8_t init_state)
{
    /* Initialize StateMachine */
    SmInitParms smParms = {
        .rules = g_ble_context.m_rules,
        .maxRules = sizeof(g_ble_context.m_rules)/sizeof(g_ble_context.m_rules[0]),
        .context = NULL,
        .initState = init_state,
    };

    SM_Init(sm, &smParms);

#if XA_DECODER == MICO_TRUE
    SM_EnableDecode(sm, MICO_TRUE, "BLE", g_stateNameTab, g_eventTypeNameTabl);
#endif 

    SM_OnEvent(sm, BLE_STATE_PERIPHERAL_ADVERTISING, BLE_SM_EVT_PERIPHERAL_CONNECTION_FAIL, BLE_STATE_PERIPHERAL_ADVERTISING, NULL);
    SM_OnEvent(sm, BLE_STATE_PERIPHERAL_ADVERTISING, BLE_SM_EVT_PERIPHERAL_CONNECTED, BLE_STATE_PERIPHERAL_CONNECTED, NULL);
    
    SM_OnEvent(sm, BLE_STATE_PERIPHERAL_CONNECTED, BLE_SM_EVT_PERIPHERAL_DISCONNECTED, BLE_STATE_PERIPHERAL_ADVERTISING, app_peripheral_start_advertising);
    SM_OnEnter(sm, BLE_STATE_PERIPHERAL_CONNECTED, app_peripheral_connected);
    SM_OnExit(sm, BLE_STATE_PERIPHERAL_CONNECTED, app_peripheral_disconnected);
    
    SM_OnEvent(sm, BLE_STATE_CENTRAL_SCANNING, BLE_SM_EVT_CENTRAL_SCANNED, BLE_STATE_IDLE, NULL);
    SM_OnEvent(sm, BLE_STATE_CENTRAL_SCANNING, BLE_SM_EVT_PERIPHERAL_LEADV_CMD, BLE_STATE_PERIPHERAL_ADVERTISING, app_peripheral_start_advertising);
    SM_OnEnter(sm, BLE_STATE_CENTRAL_SCANNING, app_central_start_scanning);
    SM_OnExit(sm, BLE_STATE_CENTRAL_SCANNING, app_central_scanning_stoped);
    
    SM_OnEvent(sm, BLE_STATE_CENTRAL_CONNECTING, BLE_SM_EVT_CENTRAL_CONNECTION_FAIL, BLE_STATE_IDLE, NULL);
    SM_OnEvent(sm, BLE_STATE_CENTRAL_CONNECTING, BLE_SM_EVT_CENTRAL_CONNECTED, BLE_STATE_CENTRAL_CONNECTED, NULL);
    
    SM_OnEvent(sm, BLE_STATE_CENTRAL_CONNECTED, BLE_SM_EVT_CENTRAL_DISCONNECTED, BLE_STATE_IDLE, NULL);
    SM_OnEnter(sm, BLE_STATE_CENTRAL_CONNECTED, app_central_connected);
    SM_OnExit(sm, BLE_STATE_CENTRAL_CONNECTED, app_central_disconnected);
    
    SM_OnEvent(sm, BLE_STATE_IDLE, BLE_SM_EVT_CENTRAL_LESCAN_CMD, BLE_STATE_CENTRAL_SCANNING, NULL);
    SM_OnEvent(sm, BLE_STATE_IDLE, BLE_SM_EVT_CENTRAL_LECONN_CMD, BLE_STATE_CENTRAL_CONNECTING, NULL);
    SM_OnEvent(sm, BLE_STATE_IDLE, BLE_SM_EVT_PERIPHERAL_LEADV_CMD, BLE_STATE_PERIPHERAL_ADVERTISING, app_peripheral_start_advertising);
    
    SM_Finalize(sm);
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
mico_bt_result_t mico_ble_init(const char *device_name, 
                               const char *whitelist_name, 
                               const mico_bt_uuid_t *whitelist_uuid, 
                               mico_bool_t is_central, 
                               mico_ble_evt_cback_t cback) 
{
    mico_bt_result_t        err;
    uint8_t                 init_state;

    /* Check */
    if (!device_name || !cback) {
        return MICO_BT_ERROR;
    }

    if (g_ble_context.m_is_initialized) {
        return MICO_BT_SUCCESS;
    }

    memset(&g_ble_context, 0, sizeof(g_ble_context));

    /* Initialize Bluetooth Stack & GAP Role. */
    err = mico_bt_init(MICO_BT_HCI_MODE, device_name, 1, 1);
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializing MiCO Bluetooth Framework");

    err = mico_ble_central_device_init(whitelist_name, whitelist_uuid);
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializing MiCO Bluetooth Central Role");

    err = mico_ble_peripheral_device_init();
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializaing MiCO Bluetooth Peripheral Role");

    /* BT Mode to Peripheral */
    if (is_central) {
        init_state = BLE_STATE_CENTRAL_SCANNING;
        err = mico_ble_set_device_scan(MICO_TRUE);
        require_string(err == MICO_BT_SUCCESS, exit, "Error setting device to scanning");
    } else {
        init_state = BLE_STATE_PERIPHERAL_ADVERTISING;
        err = mico_ble_set_device_discovery(MICO_TRUE);
        require_string(err == MICO_BT_SUCCESS, exit, "Error setting device to discoverable");
    }

    /* Initialize local storage information */
    g_ble_context.m_is_central = is_central;
    g_ble_context.m_cback = cback;
    g_ble_context.m_is_initialized = MICO_TRUE;
    if (whitelist_name && strlen(whitelist_name)) {
        g_ble_context.m_wl_name = whitelist_name;
    }
    if (mico_ble_check_uuid(whitelist_uuid)) {
        g_ble_context.m_wl_uuid = whitelist_uuid;
    }

    /* Initialize StateMachine */
    mico_ble_state_machine_init(&g_ble_context.m_sm, init_state);

    /* Post first event to user. */
    if (init_state == BLE_STATE_CENTRAL_SCANNING) {
        mico_ble_post_evt(BLE_EVT_CENTRAL_SCAN_START, NULL);
    } else {
        mico_ble_post_evt(BLE_EVT_PERIPHREAL_ADV_START, NULL);
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
// mico_bt_result_t mico_ble_start_procedure(mico_bool_t start)
// {
//     return MICO_BT_SUCCESS;
// }

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
static mico_bt_result_t mico_ble_set_device_scan(mico_bool_t start)
{
    OSStatus err;
    if (start) {
        err = mico_bt_smartbridge_start_scan(&g_central_scan_settings,
                                             mico_ble_central_scan_complete_handler,
                                             mico_ble_central_scan_result_handler);
    } else {
        err = mico_bt_smartbridge_stop_scan();
    }
    return (mico_bt_result_t)err;
}

mico_bt_result_t mico_ble_start_device_scan(void)
{
    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_IDLE)
        || SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_ADVERTISING)) {

        mico_ble_set_device_discovery(MICO_FALSE);

        mico_bt_result_t ret = mico_ble_set_device_scan(MICO_TRUE);
        if (ret == MICO_BT_SUCCESS) {
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_LESCAN_CMD);
        }
        return ret;
    }
    return MICO_BT_BADOPTION;
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
static mico_bt_result_t mico_ble_set_device_discovery(mico_bool_t start)
{
    OSStatus err;
    if (start) {
        err = mico_bt_peripheral_start_advertisements((mico_bt_smart_advertising_settings_t *)&g_peripheral_advertising_settings,
                                                      mico_ble_peripheral_advertisement_complete_handler);
    } else {
        err = mico_bt_peripheral_stop_advertisements();
    }
    return (mico_bt_result_t)err;
}

mico_bt_result_t mico_ble_start_device_discovery(void)
{
    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_IDLE)
        || SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_SCANNING)) {

        /* Stop scanning */
        if (mico_bt_smartbridge_is_scanning()) {
            mico_bt_smartbridge_stop_scan();
        }

        mico_bt_result_t ret = mico_ble_set_device_discovery(MICO_TRUE);
        if (ret == MICO_BT_SUCCESS) {
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_LEADV_CMD);
        }
        return ret;
    }
    return MICO_BT_BADOPTION;
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
    return (mico_ble_state_t)SM_GetState(&g_ble_context.m_sm);
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

/**
 * Check if UUID is valid.
 * 
 * @param uuid
 *          A pointer of UUID.
 * 
 * @return 
 *      MICO_FALSE -- invalid
 *      MICO_TRUE  -- valid 
 */
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
        const uint8_t *p = uuid->uu.uuid128;
        while (p < &uuid->uu.uuid128[LEN_UUID_128] && *p++ == 0);
        return (p != &uuid->uu.uuid128[LEN_UUID_128]);
    } else {
        return MICO_FALSE;
    }
}

/* Handle an event for POST EVENT To User Layer. */
static OSStatus ble_post_evt_handler(void *arg)
{
    mico_ble_event_t evt;
    mico_ble_evt_params_t *parms =
        (mico_ble_evt_params_t *)((uint8_t *)arg + sizeof(mico_ble_event_t));

    memcpy(&evt, arg, sizeof(mico_ble_event_t));
    
    if (g_ble_context.m_cback) {
        g_ble_context.m_cback(evt, parms);
    }

    if (evt == BLE_EVT_DATA) {
        free(parms->u.data.p_data);
    }
    free(arg);

    /* Restore current RFCOMM Channel.  */
    // if (evt == BT_RFCOMM_EVT_DATA && g_ble_context.m_is_recv_data_pending) {
    //     g_ble_context.m_is_recv_data_pending = MICO_FALSE;
    //     mico_bt_rfcomm_flow_control(g_ble_context.m_connection_handle, MICO_TRUE);
    // }

    return kNoErr;
}

/* Post event to user layer. */
static mico_bool_t mico_ble_post_evt(mico_ble_event_t evt, mico_ble_evt_params_t *parms)
{
    uint8_t                     *arg = NULL;
    mico_ble_evt_params_t       *p = NULL;
    
    if (g_ble_context.m_cback) {

        /* Package an event parameters packet. */
        arg = malloc(sizeof(mico_ble_event_t) + sizeof(mico_ble_evt_params_t));
        if (!arg) {
            mico_ble_log("%s: malloc failed", __FUNCTION__);
            return MICO_FALSE;
        }
        memcpy(arg, &evt, sizeof(mico_ble_event_t));
        if (parms) {
            memcpy(arg + sizeof(mico_ble_event_t), parms, sizeof(mico_ble_evt_params_t));
        } else {
            memset(arg + sizeof(mico_ble_event_t), 0, sizeof(mico_ble_evt_params_t));
        }

        /* Otherwise */
        if (evt == BLE_EVT_DATA && parms) {
            p = (mico_ble_evt_params_t *)(arg + sizeof(mico_ble_event_t));
            p->u.data.p_data = malloc(parms->u.data.length);
            if (!p->u.data.p_data) {
                mico_ble_log("%s: malloc failed", __FUNCTION__);
                free(arg);
                return MICO_FALSE;
            }
            memcpy(p->u.data.p_data, parms->u.data.p_data, parms->u.data.length);
        } 

        /* Post */
        if (kNoErr != mico_rtos_send_asynchronous_event(&g_ble_context.m_worker_thread,
                                                        ble_post_evt_handler, 
                                                        arg)) {
            /* Pending current RFCOMM Channel. */
            // mico_bt_rfcomm_flow_control(g_ble_context.m_connection_handle, MICO_FALSE);
            // g_ble_context.m_is_recv_data_pending = MICO_TRUE;

            mico_ble_log("%s: send asyn event failed", __FUNCTION__);
            if (p) free(p->u.data.p_data);
            free(arg);
            return MICO_FALSE;
        }
    }

    return MICO_TRUE;
}
