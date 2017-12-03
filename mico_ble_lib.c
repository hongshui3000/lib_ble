#include <string.h>
#include <mico_bt_types.h>

#include "mico.h"
#include "mico_bt.h"
#include "mico_bt_cfg.h"
#include "mico_bt_smartbridge.h"
#include "mico_bt_peripheral.h"
#include "sdpdefs.h"

#include "statemachine.h"

#include "mico_ble_lib.h"

#define mico_ble_log(M, ...) custom_log("BLE", M, ##__VA_ARGS__)

/*-----------------------------------------------------------------------------------------
 * Configuration 
 */
#define BLUETOOTH_PRINT_SERVICE_UUID    0x18F0
#define BLUETOOTH_PRINT_CHAR_CMD_UUID   0x2AF1

/*------------------------------------------------------------------------------------------
 * GATT Service UUID & Handle
 */

/* UUID value of the SPP Service */
#define UUID_SPP_SERVICE                        0x5E, 0x67, 0x21, 0x8A, 0x3f, 0x4b, 0x4D, 0x32, 0x91, 0x36, 0x38, 0xE3, 0xD8, 0xED, 0x63, 0x71
/* UUID value of the SPP Characteristic, Data In */
#define UUID_SPP_SERVICE_CHARACTERISTIC_IN      0x45, 0x39, 0x3E, 0x90, 0x24, 0x1D, 0x21, 0x78, 0x32, 0x70, 0x21, 0x35, 0xB4, 0xBA, 0xAE, 0xE2
/* UUID value of the SPP Characteristic, Data OUT */
#define UUID_SPP_SERVICE_CHARACTERISTIC_OUT     0x32, 0x15, 0x1a, 0x5e, 0x82, 0x2e, 0x12, 0x2a, 0x91, 0x43, 0x27, 0x52, 0xba, 0x1d, 0xf3, 0x30

/* The handle of Custom GATT Service attribute.  */
enum {
    /* GATT Service */
    HDLS_GENERIC_ATTRIBUTE = 0x1,
    HDLC_GENERIC_ATTRIBUTE_SERVICE_CHANGED,
    HDLC_GENERIC_ATTRIBUTE_SERVICE_CHANGED_VALUE,
    /* GAP Service */
    HDLS_GENERIC_ACCESS = 0x14,
    HDLC_GENERIC_ACCESS_DEVICE_NAME,
    HDLC_GENERIC_ACCESS_DEVICE_NAME_VALUE,
    HDLC_GENERIC_ACCESS_APPEARANCE,
    HDLC_GENERIC_ACCESS_APPEARANCE_VALUE,

    /* SPP Service */
    HDLS_SPP= 0x30,
    HDLC_SPP_IN,
    HDLC_SPP_IN_VALUE,
    HDLC_SPP_IN_DESCRIPTION,

    HDLC_SPP_OUT,
    HDLC_SPP_OUT_VALUE,
    HDLC_SPP_OUT_CCC_DESCRIPTION,
    HDLC_SPP_OUT_DESCRIPTION,
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

/*------------------------------------------------------------------------------------------
 * Local defined type 
 */
typedef struct {
    StateMachine         m_sm;
    SmRule               m_rules[20];
    mico_bool_t          m_is_central;
    mico_bool_t          m_is_initialized;
    mico_ble_evt_cback_t m_cback;

    char                *m_wl_name;
    uint16_t             m_central_attr_handle;

    uint16_t             m_spp_out_cccd_value;
    mico_bt_ext_attribute_value_t *m_spp_out_attribute;

    mico_worker_thread_t m_worker_thread;
    mico_worker_thread_t m_evt_worker_thread;
    mico_bt_smartbridge_socket_t m_central_socket;
    mico_bt_peripheral_socket_t  m_peripheral_socket;
} mico_ble_context_t;

/*--------------------------------------------------------------------------------------------
 * Local function prototype
 */

// static mico_bool_t mico_ble_check_uuid(const mico_bt_uuid_t *uuid);
static mico_bool_t mico_ble_post_evt(mico_ble_event_t evt, mico_ble_evt_params_t *parms);
static mico_bt_result_t mico_ble_set_device_discovery(mico_bool_t start);
static mico_bt_result_t mico_ble_set_device_scan(mico_bool_t start);

static mico_bt_gatt_status_t mico_ble_periphreal_spp_data_in_callback(mico_bt_ext_attribute_value_t *attribute, 
                                                                      mico_bt_gatt_request_type_t op);
static mico_bt_gatt_status_t mico_ble_periphreal_spp_cccd_callback(mico_bt_ext_attribute_value_t *attribute, 
                                                                   mico_bt_gatt_request_type_t op);

/*---------------------------------------------------------------------------------------------
 * Central local resource
 * 
 */
static mico_bt_uuid_t g_central_whitelist_serv_uuid = {
    .len = LEN_UUID_16,
    .uu.uuid16 = BLUETOOTH_PRINT_SERVICE_UUID,
};

static mico_bt_uuid_t g_central_whitelist_char_uuid = {
    .len = LEN_UUID_16,
    .uu.uuid16 = BLUETOOTH_PRINT_CHAR_CMD_UUID,
};

static const mico_bt_smart_security_settings_t g_central_security_settings = {
    .timeout_second = 10,
    .io_capabilities = BT_SMART_IO_NO_INPUT_NO_OUTPUT,
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
   .interval          = 128,
   .window            = 64,
   .duration_second   = 5,
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

static const uint8_t g_peripheral_appearance_name[2] = { BIT16_TO_8(APPEARANCE_GENERIC_TAG) };

static const uint8_t g_peripheral_gatt_database[] = {
    /* Declare mandatory GATT service */
    PRIMARY_SERVICE_UUID16(HDLS_GENERIC_ATTRIBUTE, UUID_SERVCLASS_GATT_SERVER),

        CHARACTERISTIC_UUID16(HDLC_GENERIC_ATTRIBUTE_SERVICE_CHANGED,
                              HDLC_GENERIC_ATTRIBUTE_SERVICE_CHANGED_VALUE,
                              GATT_UUID_GATT_SRV_CHGD,
                              LEGATTDB_CHAR_PROP_INDICATE,
                              LEGATTDB_PERM_NONE),

    /* Declare mandatory GAP service. Device Name and Appearance are mandatory
     * characteristics of GAP service                                        
     */
    PRIMARY_SERVICE_UUID16(HDLS_GENERIC_ACCESS, UUID_SERVCLASS_GAP_SERVER),

        /* Declare mandatory GAP service characteristic: Dev Name */
        CHARACTERISTIC_UUID16(HDLC_GENERIC_ACCESS_DEVICE_NAME, HDLC_GENERIC_ACCESS_DEVICE_NAME_VALUE,
                              GATT_UUID_GAP_DEVICE_NAME,
                              LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE),

        /* Declare mandatory GAP service characteristic: Appearance */
        CHARACTERISTIC_UUID16(HDLC_GENERIC_ACCESS_APPEARANCE, HDLC_GENERIC_ACCESS_APPEARANCE_VALUE,
                              GATT_UUID_GAP_ICON,
                              LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE),

    /* Declare SPP Service with 128 byte UUID */
    PRIMARY_SERVICE_UUID128(HDLS_SPP, UUID_SPP_SERVICE),

        /* Declare characteristic used to write spp data to server */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_SPP_IN, 
                                        HDLC_SPP_IN_VALUE,
                                        UUID_SPP_SERVICE_CHARACTERISTIC_IN,
                                        LEGATTDB_CHAR_PROP_WRITE, 
                                        LEGATTDB_PERM_WRITE_CMD | LEGATTDB_PERM_WRITE_REQ),

        CHAR_DESCRIPTOR_UUID16(HDLC_SPP_IN_DESCRIPTION, 
                               GATT_UUID_CHAR_DESCRIPTION,
                               LEGATTDB_PERM_READABLE),

        /* Declare characteristic used to send spp data to client */
        CHARACTERISTIC_UUID128(HDLC_SPP_OUT, 
                               HDLC_SPP_OUT_VALUE,
                               UUID_SPP_SERVICE_CHARACTERISTIC_OUT,
                               LEGATTDB_CHAR_PROP_INDICATE | LEGATTDB_CHAR_PROP_NOTIFY, 
                               LEGATTDB_PERM_NONE),

        CHAR_DESCRIPTOR_UUID16_WRITABLE(HDLC_SPP_OUT_CCC_DESCRIPTION,
                                        GATT_UUID_CHAR_CLIENT_CONFIG,
                                        LEGATTDB_PERM_READABLE | LEGATTDB_PERM_WRITE_CMD | LEGATTDB_PERM_WRITE_REQ),

        CHAR_DESCRIPTOR_UUID16(HDLC_SPP_OUT_DESCRIPTION, 
                               GATT_UUID_CHAR_DESCRIPTION,
                               LEGATTDB_PERM_READABLE),
};

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

static mico_ble_context_t g_ble_context;

/*---------------------------------------------------------------------------------------------
 * Peripheral function definition 
 */

static void mico_ble_peripheral_create_attribute_db(void)
{
    extern mico_bt_cfg_settings_t   mico_bt_cfg_settings;

    /* Create BLE GATT value database */
    // ***** Primary service 'Generic Attribute'
    mico_bt_peripheral_ext_attribute_add(HDLC_GENERIC_ATTRIBUTE_SERVICE_CHANGED_VALUE, 0, NULL, NULL);

    // ***** Primary service 'Generic Access'
    mico_bt_peripheral_ext_attribute_add(HDLC_GENERIC_ACCESS_DEVICE_NAME_VALUE, 
                                         (uint16_t)strlen((char *)mico_bt_cfg_settings.device_name),
                                         mico_bt_cfg_settings.device_name, 
                                         NULL);
    mico_bt_peripheral_ext_attribute_add(HDLC_GENERIC_ACCESS_APPEARANCE_VALUE, 
                                         sizeof(g_peripheral_appearance_name), 
                                         g_peripheral_appearance_name, 
                                         NULL);

    // ***** Primary service 'SPP' (Vender specific)
    mico_bt_peripheral_ext_attribute_add(HDLC_SPP_IN_VALUE, 0, NULL, mico_ble_periphreal_spp_data_in_callback);
    mico_bt_peripheral_ext_attribute_add(HDLC_SPP_IN_DESCRIPTION,
                                         (uint16_t)strlen("SPP Data IN"),
                                         (uint8_t *)"SPP Data IN", NULL);

    g_ble_context.m_spp_out_attribute = mico_bt_peripheral_ext_attribute_add(HDLC_SPP_OUT_VALUE, 0, NULL, NULL );
    mico_bt_peripheral_ext_attribute_add(HDLC_SPP_OUT_CCC_DESCRIPTION, 2,
                                         (uint8_t *)&g_ble_context.m_spp_out_cccd_value,
                                         mico_ble_periphreal_spp_cccd_callback);
    mico_bt_peripheral_ext_attribute_add(HDLC_SPP_OUT_DESCRIPTION,
                                         (uint16_t)strlen("SPP Data OUT"),
                                         (uint8_t *)"SPP Data OUT",
                                         NULL);

    mico_bt_peripheral_ext_attribute_find_by_handle(HDLC_SPP_OUT_VALUE, &g_ble_context.m_spp_out_attribute);
}

static mico_bt_result_t mico_ble_peripheral_set_advertisement_data(void)
{
    OSStatus err;
    mico_bt_ble_advert_data_t adv_data;

     mico_bt_ble_128service_t adver_services_128 = {
         .list_cmpl = false,
         .uuid128 = { UUID_SPP_SERVICE },
     };

    adv_data.flag = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
    adv_data.p_services_128b = &adver_services_128;
    
    err = mico_bt_ble_set_advertisement_data(BTM_BLE_ADVERT_BIT_DEV_NAME 
                                             | BTM_BLE_ADVERT_BIT_SERVICE_128
                                             | BTM_BLE_ADVERT_BIT_FLAGS, 
                                             &adv_data);
    require_noerr_string(err, exit, "Set Advertisement Data failed");

    err = mico_bt_ble_set_scan_response_data(BTM_BLE_ADVERT_BIT_DEV_NAME, &adv_data);
    require_noerr_string(err, exit, "Set Advertisement ScanRsp Data failed");

exit:
    return (mico_bt_result_t)err;
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
    UNUSED_PARAMETER(socket);

    mico_ble_log("Connection up [peripheral]");

    mico_bt_peripheral_stop_advertisements();

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_ADVERTISING)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_CONNECTED);
    }

    return kNoErr;
}

static OSStatus mico_ble_peripheral_disconnect_handler(mico_bt_peripheral_socket_t *socket)
{
    UNUSED_PARAMETER(socket);

    mico_ble_log("Connection down [periphreal]");

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_CONNECTED)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_DISCONNECTED);
    }

    return kNoErr;
}

static mico_bt_gatt_status_t mico_ble_periphreal_spp_data_in_callback(mico_bt_ext_attribute_value_t *attribute, 
                                                                      mico_bt_gatt_request_type_t op)
{
    mico_ble_evt_params_t evt;

    if (op == GATTS_REQ_TYPE_WRITE) {
        evt.u.data.length = attribute->value_length;
        evt.u.data.p_data = attribute->p_value;
        mico_ble_post_evt(BLE_EVT_DATA, &evt);
        return MICO_BT_GATT_SUCCESS;
    } else {
        return MICO_BT_GATT_ERROR;
    }
}

static mico_bt_gatt_status_t mico_ble_periphreal_spp_cccd_callback(mico_bt_ext_attribute_value_t *attribute, 
                                                                   mico_bt_gatt_request_type_t op)
{
    if (op == GATTS_REQ_TYPE_READ) {
        return MICO_BT_GATT_SUCCESS;
    } else if (op == GATTS_REQ_TYPE_WRITE) {
        if (attribute->value_length != 2) {
            return MICO_BT_GATT_INVALID_ATTR_LEN;
        }
        g_ble_context.m_spp_out_cccd_value = attribute->p_value[0] | (attribute->p_value[1] << 8);
        return MICO_BT_GATT_SUCCESS;
    } else {
        return MICO_BT_GATT_ERROR;
    }
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
    return (mico_bt_result_t)err;
}

/* State translate action. */
static mico_bool_t app_peripheral_start_advertising(void *context)
{
    UNUSED_PARAMETER(context);

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

    UNUSED_PARAMETER(context);

    /* 发送LEADV=OFF消息 */
    mico_ble_post_evt(BLE_EVT_PERIPHERAL_ADV_STOP, NULL);
    
    /* 发送LECONN=SLAVE,ON消息 */
    memcpy(evt_params.bd_addr, g_ble_context.m_peripheral_socket.remote_device.address, 6);
    evt_params.u.conn.handle = g_ble_context.m_peripheral_socket.connection_handle;
    mico_ble_post_evt(BLE_EVT_PERIPHERAL_CONNECTED, &evt_params);
    
    return TRUE;
}

static mico_bool_t app_peripheral_disconnected(void *context)
{
    /* 发送LECONN=SLAVE,OFF消息 */
    mico_ble_evt_params_t evt_params;

    UNUSED_PARAMETER(context);

    memcpy(evt_params.bd_addr, g_ble_context.m_peripheral_socket.remote_device.address, 6);
    evt_params.u.disconn.handle = g_ble_context.m_peripheral_socket.connection_handle;
    mico_ble_post_evt(BLE_EVT_PERIPHERAL_DISCONNECTED, &evt_params);
    return TRUE;
}

/*----------------------------------------------------------------------------------------------
 * Central function definition 
 */

static OSStatus mico_ble_central_scan_complete_handler(void *arg)
{
    UNUSED_PARAMETER(arg);

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_SCANNING)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_SCANNED);
    }
    return kNoErr;
}

static OSStatus mico_ble_central_scan_result_handler(const mico_bt_smart_advertising_report_t *scan_result)
{
    char str_addr[BDADDR_NTOA_SIZE] = {0};
    mico_ble_evt_params_t evt_params;

    if (scan_result->signal_strength >= 0) {
        return kUnknownErr;
    }

    if (scan_result->event == BT_SMART_CONNECTABLE_UNDIRECTED_ADVERTISING_EVENT) {

        mico_ble_log("Scan result: %s", bdaddr_ntoa(scan_result->remote_device.address, str_addr));
        memset(&evt_params, 0, sizeof(evt_params));

        if ((!g_ble_context.m_wl_name && strlen(scan_result->remote_device.name) > 0)
            || (g_ble_context.m_wl_name 
                && memcmp(g_ble_context.m_wl_name, scan_result->remote_device.name, strlen(g_ble_context.m_wl_name)) == 0)) {

            memcpy(evt_params.bd_addr, scan_result->remote_device.address, 6);
            strcpy(evt_params.u.report.name, scan_result->remote_device.name);
            evt_params.u.report.rssi = scan_result->signal_strength;
            mico_ble_post_evt(BLE_EVT_CENTRAL_REPORT, &evt_params);
        }
    }
    return kNoErr;
}

static OSStatus mico_ble_central_disconnection_handler(mico_bt_smartbridge_socket_t *socket)
{
    UNUSED_PARAMETER(socket);

    mico_ble_log("smartbridge device disconnected.");

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_CONNECTED)) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_DISCONNECTED);
    }
    return kNoErr;
}

static OSStatus mico_ble_central_connect_handler(void *arg)
{
    OSStatus ret = MICO_BT_BADOPTION;
    mico_bt_smartbridge_socket_status_t status;
    mico_bt_smart_device_t *remote_device = (mico_bt_smart_device_t *)arg;

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_CONNECTING)) {
        mico_bt_smartbridge_get_socket_status(&g_ble_context.m_central_socket, &status);
        if (status == SMARTBRIDGE_SOCKET_DISCONNECTED) {
            if (g_central_security_settings.authentication_requirements != BT_SMART_AUTH_REQ_NONE) {
                if (mico_bt_dev_find_bonded_device((uint8_t *)remote_device->address) == MICO_FALSE) {
                    mico_ble_log("Bond info not found. Initiate pairing request.");
                    mico_bt_smartbridge_enable_pairing(&g_ble_context.m_central_socket, &g_central_security_settings, NULL);
                } else {
                    mico_ble_log("Bond info found. Encrypt use bond info.");
                    mico_bt_smartbridge_set_bond_info(&g_ble_context.m_central_socket, &g_central_security_settings, NULL);
                }
            }

            /* Connecting */
            ret = mico_bt_smartbridge_connect(&g_ble_context.m_central_socket, 
                                              remote_device,
                                              &g_central_connection_settings, 
                                              mico_ble_central_disconnection_handler, 
                                              NULL);
            require_noerr_string(ret, exit, "Connect to the peer device failed.");

            /* Find service */
            uint8_t attribute_buffer[100];
            mico_bt_smart_attribute_t *attribute = (mico_bt_smart_attribute_t *)attribute_buffer;
            ret = mico_bt_smartbridge_get_service_from_attribute_cache_by_uuid(&g_ble_context.m_central_socket, 
                                                                               &g_central_whitelist_serv_uuid, 
                                                                               0x00, 0xffff, attribute, 100);
            require_noerr_action_string(ret, exit, mico_bt_smartbridge_disconnect(&g_ble_context.m_central_socket, MICO_FALSE), 
                                         "The specified GATT Service not found, disconnect.");

            /* Find characteristic, and save characteristic value handle */
            ret = mico_bt_smartbridge_get_characteritics_from_attribute_cache_by_uuid(&g_ble_context.m_central_socket, 
                                                                                       &g_central_whitelist_char_uuid, 
                                                                                       attribute->value.service.start_handle, 
                                                                                       attribute->value.service.end_handle, 
                                                                                       (mico_bt_smart_attribute_t *)attribute_buffer, 
                                                                                       100);
            if (ret != kNoErr) {
                mico_ble_log("The specified characteristic not found, remove cache and disconnect");
                mico_bt_smartbridge_remove_attribute_cache(&g_ble_context.m_central_socket);
                mico_bt_smartbridge_disconnect(&g_ble_context.m_central_socket, MICO_FALSE);
                goto exit;
            }
            g_ble_context.m_central_attr_handle = attribute->value.characteristic.value_handle;
        }
    }

exit:
    if (ret != MICO_BT_SUCCESS) {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_CONNECTION_FAIL);
        /* 发送LECONN=CENTRAL,OFF消息 */
        mico_ble_evt_params_t params;
        memcpy(params.bd_addr, g_ble_context.m_central_socket.remote_device.address, 6);
        mico_ble_post_evt(BLE_EVT_CENTRAL_DISCONNECTED, &params);
    } else {
        SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_CONNECTED);
    }
    if (remote_device) free(remote_device);
    return ret;
}

static mico_bt_result_t mico_ble_central_device_init(void)
{
    OSStatus err = mico_bt_smartbridge_init(1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_enable_attribute_cache(1, &g_central_whitelist_serv_uuid, 1);
    require_noerr(err, exit);

    err = mico_bt_smartbridge_create_socket(&g_ble_context.m_central_socket);
    require_noerr(err, exit);

    err = mico_rtos_create_worker_thread(&g_ble_context.m_evt_worker_thread, MICO_APPLICATION_PRIORITY, 2048, 10);
    require_noerr(err, exit);

    err = mico_rtos_create_worker_thread(&g_ble_context.m_worker_thread, MICO_APPLICATION_PRIORITY, 2048, 10);
    require_noerr_action(err, exit, mico_rtos_delete_worker_thread(&g_ble_context.m_evt_worker_thread));

exit:
    return (mico_bt_result_t)err;
}

static mico_bool_t app_central_start_scanning(void *context)
{
    UNUSED_PARAMETER(context);

    /* 如果没有SCAN，则开启SCAN */
    if (!mico_bt_smartbridge_is_scanning()) {
        mico_ble_set_device_scan(MICO_TRUE);
    }
    
    /* 发送LESCAN=ON消息 */
    mico_ble_post_evt(BLE_EVT_CENTRAL_SCAN_START, NULL);
    return TRUE;
}

static mico_bool_t app_central_scanning_stoped(void *context)
{
    UNUSED_PARAMETER(context);

    /* 发送LESCAN=OFF消息 */
    mico_ble_post_evt(BLE_EVT_CENTRAL_SCAN_STOP, NULL);
    return TRUE;
}

static mico_bool_t app_central_connected(void *context)
{
    mico_ble_evt_params_t params;

    UNUSED_PARAMETER(context);

    /* 发送LECONN=CENTRAL,ON消息 */
    memcpy(params.bd_addr, g_ble_context.m_central_socket.remote_device.address, 6);
    params.u.conn.handle = g_ble_context.m_central_socket.connection_handle;
    mico_ble_post_evt(BLE_EVT_CENTRAL_CONNECTED, &params);
    return TRUE;
}

static mico_bool_t app_central_disconnected(void *context)
{
    mico_ble_evt_params_t params;

    UNUSED_PARAMETER(context);

    /* 发送LECONN=CENTRAL,OFF消息 */
    memcpy(params.bd_addr, g_ble_context.m_central_socket.remote_device.address, 6);
    params.u.disconn.handle = g_ble_context.m_central_socket.connection_handle;
    mico_ble_post_evt(BLE_EVT_CENTRAL_DISCONNECTED, &params);
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
    SM_OnEvent(sm, BLE_STATE_PERIPHERAL_ADVERTISING, BLE_SM_EVT_CENTRAL_LESCAN_CMD, BLE_STATE_CENTRAL_SCANNING, NULL);
    
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
                               const char *wl_name, 
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
    err = (mico_bt_result_t)mico_bt_init(MICO_BT_HCI_MODE, device_name, 1, 1);
    require_string(err == MICO_BT_SUCCESS, exit, "Error initializing MiCO Bluetooth Framework");

    err = mico_ble_central_device_init();
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
    mico_ble_set_device_whitelist_name(wl_name);

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
    mico_bt_dev_read_local_addr(bdaddr);
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
    /* Update BT Controller Name */
    extern mico_bt_result_t BTM_SetLocalDeviceName(char *);
    extern mico_bt_cfg_settings_t mico_bt_cfg_settings;

    mico_bt_cfg_settings.device_name = (uint8_t *)name;
    return BTM_SetLocalDeviceName((char *)mico_bt_cfg_settings.device_name);
}

/**
 * Get local BT Device Name
 *
 * @return A c-style string of local device name.
 */
const char *mico_ble_get_device_name(void)
{
    extern mico_bt_cfg_settings_t mico_bt_cfg_settings;
    return (const char *)mico_bt_cfg_settings.device_name;
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
    mico_bt_result_t ret = MICO_BT_BADARG;

    if (name) {
        if (!g_ble_context.m_wl_name) {
            g_ble_context.m_wl_name = (char *)malloc(31);
            require_action(g_ble_context.m_wl_name, exit, ret = MICO_BT_NO_RESOURCES);
        }
        memset(g_ble_context.m_wl_name, 0, 31);
        strcpy(g_ble_context.m_wl_name, name);
        ret = MICO_BT_SUCCESS;
    }

exit:
    return ret;
}

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
const char *mico_ble_get_device_whitelist_name(void)
{
    return (const char *)g_ble_context.m_wl_name;
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
// mico_bt_result_t mico_ble_set_device_whitelist_uuid(const mico_bt_uuid_t *uuid)
// {
//     mico_bt_result_t ret = MICO_BT_BADARG;

//     if (uuid && mico_ble_check_uuid(uuid)) {
//         if (!g_ble_context.m_wl_serv_uuid) {
//             g_ble_context.m_wl_serv_uuid = (mico_bt_uuid_t *)malloc(sizeof(mico_bt_uuid_t));
//             require_action(g_ble_context.m_wl_serv_uuid, exit, ret = MICO_BT_NO_RESOURCES);
//         }
//         memset(g_ble_context.m_wl_serv_uuid, 0, sizeof(mico_bt_uuid_t));
//         memcpy(g_ble_context.m_wl_serv_uuid, uuid, sizeof(mico_bt_uuid_t));
//     } 

// exit:
//     return ret;
// }

/**
 * Get passkey for BT Security process.
 *
 * @return
 *  A c-style string of local device passkey.
 */
// const mico_bt_uuid_t *mico_ble_get_device_whitelist_uuid(void)
// {
//     return (const mico_bt_uuid_t *)g_ble_context.m_wl_serv_uuid;
// }

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
        if (ret == MICO_BT_PENDING) {
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_LESCAN_CMD);
            ret = MICO_BT_SUCCESS;
        } else {
            mico_ble_set_device_discovery(MICO_TRUE);
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
        } else {
            mico_ble_set_device_scan(MICO_TRUE);
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
    mico_bt_result_t ret = MICO_BT_BADOPTION;
    mico_bt_smartbridge_socket_status_t status;
    mico_bt_smart_device_t *remote_device = NULL;

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_IDLE)) {
        mico_bt_smartbridge_get_socket_status(&g_ble_context.m_central_socket, &status);
        if (status == SMARTBRIDGE_SOCKET_DISCONNECTED) {
            /* Construct mico_bt_smart_device_t */
            remote_device = (mico_bt_smart_device_t *)malloc(sizeof(mico_bt_smart_device_t));
            require_action_string(remote_device != NULL, exit, ret = MICO_BT_NO_RESOURCES, "Malloc failed");
            memcpy(remote_device->address, bdaddr, 6);
            remote_device->address_type = BT_SMART_ADDR_TYPE_PUBLIC;
            /* Connecting... */
            ret = (mico_bt_result_t)mico_rtos_send_asynchronous_event(&g_ble_context.m_worker_thread,
                                                                      mico_ble_central_connect_handler,
                                                                      remote_device);
            require_noerr_string(ret, exit, "Send asynchronous event failed");
            /* Handle Event */
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_LECONN_CMD);
        } 
    }

exit:
    if (ret != MICO_BT_SUCCESS && remote_device) {
        free(remote_device);
    }
    return ret;
}

/**
 *
 * @param connect_handle
 * @return
 */
mico_bt_result_t mico_ble_disconnect(uint16_t connect_handle)
{
    mico_bt_result_t ret = MICO_BT_BADOPTION;

    UNUSED_PARAMETER(connect_handle);

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_CONNECTED)) {
        ret = (mico_bt_result_t)mico_bt_peripheral_disconnect();
        if (ret == MICO_BT_SUCCESS) {
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_PERIPHERAL_DISCONNECTED);
        }
    }

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_CONNECTED)) {
        ret = (mico_bt_result_t)mico_bt_smartbridge_disconnect(&g_ble_context.m_central_socket,
                                                               MICO_FALSE);
        if (ret == MICO_BT_SUCCESS) {
            SM_Handle(&g_ble_context.m_sm, BLE_SM_EVT_CENTRAL_DISCONNECTED);
        }
    }

    return ret;
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
    OSStatus err = kParamErr;
    mico_bt_smart_attribute_t *characteristic_value = NULL;

    UNUSED_PARAMETER(timeout_ms);

    require(p_data != NULL && length > 0 && length < (uint16_t)-1, exit);

    if (SM_InState(&g_ble_context.m_sm, BLE_STATE_CENTRAL_CONNECTED)) {
        err = mico_bt_smart_attribute_create(&characteristic_value, MICO_ATTRIBUTE_TYPE_CHARACTERISTIC_VALUE, (uint16_t)length);
        require_noerr(err, exit);
        err = mico_bt_smartbridge_get_attribute_cache_by_handle(&g_ble_context.m_central_socket, 
                                                                 g_ble_context.m_central_attr_handle, 
                                                                 characteristic_value, 
                                                                 ATTR_CHARACTERISTIC_VALUE_SIZE(20));
        require_noerr_action(err, exit, mico_bt_smart_attribute_delete(characteristic_value));

        const uint32_t attr_cap_size = characteristic_value->value_length;
        uint32_t actual_len = 0;

        while (length > 0 && err == kNoErr) {
            actual_len = MIN(attr_cap_size, length);
            memcpy(characteristic_value->value.value, p_data, actual_len);
            characteristic_value->value_length = actual_len;
            err = (mico_bt_result_t)mico_bt_smartbridge_write_attribute_cache_characteristic_value(&g_ble_context.m_central_socket, 
                                                                                                    characteristic_value);
            p_data += actual_len;
            length -= actual_len;
        }
        mico_bt_smart_attribute_delete(characteristic_value);
    } else if (SM_InState(&g_ble_context.m_sm, BLE_STATE_PERIPHERAL_CONNECTED)) {
        err = mico_bt_peripheral_ext_attribute_value_write(g_ble_context.m_spp_out_attribute, (uint16_t)length, 0, p_data);
        require_noerr(err, exit);
        if (g_ble_context.m_spp_out_cccd_value & GATT_CLIENT_CONFIG_NOTIFICATION) {
            err = mico_bt_peripheral_gatt_notify_attribute_value(&g_ble_context.m_peripheral_socket,
                                                                 g_ble_context.m_spp_out_attribute);
        } else if (g_ble_context.m_spp_out_cccd_value & GATT_CLIENT_CONFIG_INDICATION) {
            err = mico_bt_peripheral_gatt_indicate_attribute_value(&g_ble_context.m_peripheral_socket,
                                                                   g_ble_context.m_spp_out_attribute);
        } else {
            err = MICO_BT_BADOPTION;
        }
    }

exit:
    return (mico_bt_result_t)err;
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
        if (kNoErr != mico_rtos_send_asynchronous_event(&g_ble_context.m_evt_worker_thread,
                                                        ble_post_evt_handler, 
                                                        arg)) {
            mico_ble_log("%s: send asyn event failed", __FUNCTION__);
            if (p) free(p->u.data.p_data);
            free(arg);
            return MICO_FALSE;
        }
    }

    return MICO_TRUE;
}

uint8_t *bdaddr_aton(const char *addr, uint8_t *out_addr)
{
    uint8_t val = 0, i = BD_ADDR_LEN;

    while (*addr) {
        if (*addr >= '0' && *addr <= '9') {
            val = (uint8_t)((val << 4) + *addr - '0');
        } else if (*addr >= 'A' && *addr <= 'F') {
            val = (uint8_t)((val << 4) + *addr - 'A' + 10);
        } else if (*addr >= 'a' && *addr <= 'f') {
            val = (uint8_t)((val << 4) + *addr - 'a' + 10);
        } else {
            out_addr[--i] = val;
        }
        addr++;
    }

    out_addr[--i] = val;
    return out_addr;
}

char *bdaddr_ntoa(const uint8_t *addr, char *addr_str)
{
    char *bp = addr_str;
    uint8_t u, l;
    int8_t  i = BD_ADDR_LEN;

    while (i > 0) {
        u = (uint8_t)(addr[i - 1] / 16);
        l = (uint8_t)(addr[i - 1] % 16);

        if (u < 10) {
            *bp++ = (uint8_t)('0' + u);
        } else {
            *bp++ = (uint8_t)('A' + u - 10);
        }

        if (l < 10) {
            *bp++ = (uint8_t)('0' + l);
        } else {
            *bp++ = (uint8_t)('A' + l - 10);
        }
        *bp++ = ':';
        i--;
    }
    *--bp = 0;
    return addr_str;
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
// static mico_bool_t mico_ble_check_uuid(const mico_bt_uuid_t *uuid)
// {
//     if (!uuid) {
//         return MICO_FALSE;
//     }

//     if (uuid->len == LEN_UUID_16) {
//         return (uuid->uu.uuid16 > (uint16_t)0 && uuid->uu.uuid16 < (uint16_t)(-1));
//     } else if (uuid->len == LEN_UUID_32) {
//         return (uuid->uu.uuid32 > (uint32_t)0 && uuid->uu.uuid32 < (uint32_t)(-1));
//     } else if (uuid->len == LEN_UUID_128) {
//         const uint8_t *p = uuid->uu.uuid128;
//         while (p < &uuid->uu.uuid128[LEN_UUID_128] && *p++ == 0);
//         return (p != &uuid->uu.uuid128[LEN_UUID_128]);
//     } else {
//         return MICO_FALSE;
//     }
// }
