/**
 ******************************************************************************
 * @file    at_cmd_ble_command.c
 * @author
 * @version V1.0.0
 * @date
 * @brief
 ******************************************************************************
 *
 *  The MIT License
 *  Copyright (c) 2016 MXCHIP Inc.
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
 */

#include "mico.h"
#include "StringUtils.h"

#include "at_cmd.h"
#include "at_cmd_driver.h"

#include "mico_ble_lib.h"

#define BT_MAGIC_NUMBER         0x342b123e
#define BT_DEVICE_NAME_LEN      32

/* Log api */
#define at_ble_log(fmt, ...) at_log("ble", fmt, ##__VA_ARGS__)

/* Bluetooth Non-volatile Configuration Structure */
#pragma pack(1)
typedef struct {
    uint32_t    magic_number;
    mico_bool_t is_at_mode;
    mico_bool_t is_enable_event;
    char        device_name[BT_DEVICE_NAME_LEN];    // c-style string
    char        whitelist_name[BT_DEVICE_NAME_LEN];     // c-style string
    mico_bt_uuid_t whitelist_uuid;
} at_cmd_ble_config_t;
#pragma pack()

/* Local context */
typedef struct {
    /* Configuration */
    at_cmd_config_t          config_handle;
    at_cmd_ble_config_t     *p_config;
} at_cmd_ble_context_t;

static OSStatus ble_event_handle(mico_ble_event_t  event, const mico_ble_evt_params_t  *params);

static mico_bt_result_t ble_default_config(at_cmd_ble_config_t *config);
static void ble_set_device_name(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_get_device_name(at_cmd_driver_t *driver);
static void ble_get_device_addr(at_cmd_driver_t *driver);
static void ble_send_rawdata(at_cmd_driver_t *driver);
static void ble_send_data_packet(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_set_scan_mode(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_set_advertisement_mode(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_gap_connect(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_gap_disconnect(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_get_state(at_cmd_driver_t *driver);
static void ble_set_event_mask(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_get_event_mask(at_cmd_driver_t *driver);
static void ble_get_whitelist_name(at_cmd_driver_t *driver);
static void ble_set_whitelist_name(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_set_whitelist_uuid(at_cmd_driver_t *driver, at_cmd_para_t *para);
static void ble_get_whitelist_uuid(at_cmd_driver_t *driver);

static at_cmd_ble_context_t g_ble_context;

static const struct at_cmd_command g_ble_cmds[] = {
        /* Common */
        { "AT+LENAME",      ble_get_device_name,    ble_set_device_name,            NULL,                       NULL },
        { "AT+LEMAC",       ble_get_device_addr,    NULL,                           NULL,                       NULL },
        { "AT+LEEVENT",     NULL,                   ble_set_event_mask,             ble_get_event_mask,         NULL },
        { "AT+LESTATE",     NULL,                   NULL,                           ble_get_state,              NULL },
        { "AT+LESENDRAW",   NULL,                   NULL,                           NULL,                       ble_send_rawdata },
        { "AT+LESEND",      NULL,                   ble_send_data_packet,           NULL,                       NULL },
        { "AT+LEDISCONN",   NULL,                   ble_gap_disconnect,             NULL,                       NULL },

        /* BLE Central */
        { "AT+LEWLNAME",    ble_get_whitelist_name, ble_set_whitelist_name,         NULL,                       NULL },
        { "AT+LEWLUUID",    ble_get_whitelist_uuid, ble_set_whitelist_uuid,         NULL,                       NULL },
        { "AT+LESCAN",      NULL,                   ble_set_scan_mode,              NULL,                       NULL },
        { "AT+LECONN",      NULL,                   ble_gap_connect,                NULL,                       NULL },

        /* BLE Peripheral */
        { "AT+LEADV",       NULL,                   ble_set_advertisement_mode,     NULL,                       NULL },
};


OSStatus at_cmd_register_ble_component(void)
{
    OSStatus err = kNoErr;
    mico_bt_result_t result;

    /*
     * Load user configuration data from local NVRAM.
     */

    at_cmd_register_config(&g_ble_context.config_handle, sizeof(at_cmd_ble_config_t));
    g_ble_context.p_config = (at_cmd_ble_config_t *)at_cmd_config_data_read(&g_ble_context.config_handle);

    if (g_ble_context.p_config->magic_number != BT_MAGIC_NUMBER) {
        ble_default_config(g_ble_context.p_config);
        at_cmd_config_data_write();
    }

    /*
     * Initialize Bluetooth Low Energy Component
     */

    result = mico_ble_init(g_ble_context.p_config->device_name,
                           g_ble_context.p_config->whitelist_name,
                           &g_ble_context.p_config->whitelist_uuid,
                           ble_event_handle);

    if (result == MICO_BT_SUCCESS) {
        /* Register BLE Commands. */
        err = at_cmd_register_commands(g_ble_cmds, sizeof(g_ble_cmds) / sizeof(g_ble_cmds[0]));
        require_noerr_string(err, exit, "Registering AT Command for BLE failed");

        /* Launch application mode. */
        if (!g_ble_context.p_config->is_at_mode) {
            err = uart_driver_struct_get()->ioctl(AT_SET_AT_COMMAND, "AT+LESENDRAW");
        }
    } else if (result == MICO_BT_PENDING) {
        /* Process in @ble_event_handle */
    } else {
        at_ble_log("Initialising BLE Library failed");
        err = result;
    }

exit:
    return err;
}


static OSStatus ble_event_handle(mico_ble_event_t  event, const mico_ble_evt_params_t  *params)
{
    OSStatus err = kNoErr;
    char response[50] = {0};

    switch (event) {
        case BLE_EVT_INIT:
            if (params->u.init.status == MICO_BT_SUCCESS) {
                /* Register BTE RFCOMM Commands. */
                err = at_cmd_register_commands(g_ble_cmds, sizeof(g_ble_cmds) / sizeof(g_ble_cmds[0]));
                require_noerr_string(err, exit, "Registering AT Command for BLE failed");

                at_ble_log("ble central/peripheral init");

                /* Launch application mode. */
                if (!g_ble_context.p_config->is_at_mode) {
                    err = uart_driver_struct_get()->ioctl(AT_SET_AT_COMMAND, "AT+LESENDRAW");
                }

                /* Start to main process */
                mico_ble_start_procedure(MICO_TRUE);
            }

            /* +LEEVENT:INIT,ON */
            if (g_ble_context.p_config->is_enable_event) {
                if (params->u.init.status == MICO_BT_SUCCESS) {
                    sprintf(response, "%s+LEEVENT:INIT,%s%s", AT_PROMPT, "ON", AT_PROMPT);
                } else {
                    sprintf(response, "%s+LEEVENT:INIT,%s%s", AT_PROMPT, "OFF", AT_PROMPT);
                }
                uart_driver_struct_get()->write((uint8_t *)response, strlen(response));
            }
            break;
        case BLE_EVT_CONNECTED:
            at_ble_log("A remote device is connected");
            if (g_ble_context.p_config->is_enable_event) {
                /* +LEEVENT:CONNECTION,ON */
                sprintf(response, "%s+LEEVENT:CONNECTION,%s%s", AT_PROMPT, "ON", AT_PROMPT);
                uart_driver_struct_get()->write((uint8_t *)response, strlen(response));
            }
            break;
        case BLE_EVT_DISCONNECTED:
            at_ble_log("The remote device is disconnected");
            if (g_ble_context.p_config->is_enable_event) {
                /* +LEEVENT:CONNECTION,OFF */
                sprintf(response, "%s+LEEVENT:CONNECTION,%s%s", AT_PROMPT, "OFF", AT_PROMPT);
                uart_driver_struct_get()->write((uint8_t *)response, strlen(response));
            }
            break;
        case BLE_EVT_DATA:
            // at_ble_log("Remote data: len = %d", params->u.data.length);
            if (mico_ble_get_device_state() == BLE_STATE_CONNECTED) {
                if (g_ble_context.p_config->is_at_mode && g_ble_context.p_config->is_enable_event) {
                    /* +LEEVENT:DATA,<length>,xxxx */
                    sprintf(response, "%s+LEEVENT,DATA:%d,", AT_PROMPT, params->u.data.length);
                    uart_driver_struct_get()->write((uint8_t *) response, strlen(response));
                }
                if (uart_driver_struct_get()->write(params->u.data.p_data, params->u.data.length) != kNoErr) {
                    at_ble_log("Send data over UART failed");
                }
            }
            break;
        case BLE_EVT_STATE:
            at_ble_log("State Changed: state = %d", params->u.state.value);
            break;
        case BLE_EVT_REPORT:
            at_ble_log("An new device: %s [%d]", params->u.report.addr, params->u.report.rssi);
            break;
        default:
            at_ble_log("Unhandled event");
            break;
    }

exit:
    return err;
}

/**
 * AT+LENAME=<xxx>
 * OK
 */
static void ble_set_device_name(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char response[50];

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    char *name = at_cmd_parse_get_string(para->para, 1);
    if (strlen(name) > BT_DEVICE_NAME_LEN - 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    if (mico_ble_set_device_name(name) != MICO_BT_PENDING) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        strcpy(g_ble_context.p_config->device_name, name);
        at_cmd_config_data_write();

        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LENAME=?
 * +LENAME:<name>
 * OK
 */
static void ble_get_device_name(at_cmd_driver_t *driver)
{
    char response[50];

    sprintf(response, "%s+LENAME:%s%s", AT_PROMPT, mico_ble_get_device_name(), AT_RESPONSE_OK);
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEMAC=?
 * +LEMAC:<xx:xx:xx:xx:xx:xx>
 * OK
 * @param driver
 */
static void ble_get_device_addr(at_cmd_driver_t *driver)
{
    char response[50];
    mico_bt_device_address_t addr = {0};

    mico_ble_get_dev_address(addr);
    sprintf(response, "%s+LEMAC:%s%s", AT_PROMPT,
            DataToHexStringWithColons((uint8_t *)addr, 6),
            AT_RESPONSE_OK);
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEPASSKEY=<xxx>
 * OK
 */
// static void ble_set_passkey(at_cmd_driver_t *driver, at_cmd_para_t *para)
// {
//     char response[50];

//     if (para->para_num != 1) {
//         sprintf(response, "%s", AT_RESPONSE_ERR);
//         goto exit;
//     }

//     char *passkey = at_cmd_parse_get_string(para->para, 1);
//     if (strlen(passkey) != BT_DEVICE_PASSKEY_LEN - 1) {
//         sprintf(response, "%s", AT_RESPONSE_ERR);
//         goto exit;
//     }

//     if (mico_bt_rfcomm_set_device_passkey(passkey) != MICO_BT_SUCCESS) {
//         sprintf(response, "%s", AT_RESPONSE_ERR);
//     } else {
//         strcpy(g_ble_context.p_config->passkey, passkey);
//         at_cmd_config_data_write();

//         sprintf(response, "%s", AT_RESPONSE_OK);
//     }

// exit:
//     driver->write((uint8_t *)response, strlen(response));
// }

/**
 * AT+LEPASSKEY=?
 * +LEPASSKEY:<passkey>
 * OK
 */
// static void ble_get_passkey(at_cmd_driver_t *driver)
// {
//     char response[50];

//     sprintf(response, "%s+LEPASSKEY:%s%s",
//             AT_PROMPT,
//             mico_bt_rfcomm_get_device_passkey(),
//             AT_RESPONSE_OK);
//     driver->write((uint8_t *)response, strlen(response));
// }

/**
 * AT+LEEVENT=<ON/OFF>
 * OK
 */
static void ble_set_event_mask(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char response[50];

    if (para->para_num != 1) {
        goto err_exit;
    }

    char *enable = at_cmd_parse_get_string(para->para, 1);
    if (strcmp(enable, "ON") == 0 && g_ble_context.p_config->is_enable_event != MICO_TRUE) {
        g_ble_context.p_config->is_enable_event = MICO_TRUE;
    } else if (strcmp(enable, "OFF") == 0 && g_ble_context.p_config->is_enable_event != MICO_FALSE) {
        g_ble_context.p_config->is_enable_event = MICO_FALSE;
    } else {
        goto err_exit;
    }

    at_cmd_config_data_write();
    sprintf(response, "%s", AT_RESPONSE_OK);
    goto exit;

err_exit:
    sprintf(response, "%s", AT_RESPONSE_ERR);

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEEVENT?
 * +LEEVENT:<ON/OFF>
 * OK
 */
static void ble_get_event_mask(at_cmd_driver_t *driver)
{
    char response[50];

    sprintf(response, "%s+LEEVENT:%s%s",
            AT_PROMPT,
            g_ble_context.p_config->is_enable_event ? "ON" : "OFF",
            AT_RESPONSE_OK);
    driver->write((uint8_t *)response, strlen(response));
}

static void ble_send_rawdata(at_cmd_driver_t *driver)
{
    char     response[50];
    uint8_t *msg = NULL;
    uint32_t len, timeout;

    driver->ioctl(AT_GET_ROW_DATA_READ_LENGTH, &len);
    driver->ioctl(AT_GET_ROW_DATA_READ_TIMEOUT, &timeout);
    require_string(len != 0 && timeout != 0, err_exit, "Invalid UART Format Parameters");

    msg = (uint8_t *)malloc(len);
    require_string(msg != NULL, err_exit, "Malloc failed");

    /* Set discoverable */
    mico_ble_set_device_discovery(MICO_TRUE);

    /* Response to user. */
    sprintf(response, "%s", AT_RESPONSE_OK);
    driver->write((uint8_t *)response, strlen(response));

    /* Not AT Mode */
    g_ble_context.p_config->is_at_mode = MICO_FALSE;
    at_cmd_config_data_write();

    while (MICO_TRUE) {
        uint32_t real_len = at_cmd_driver_read(driver, msg, len, timeout);
        if (real_len == 0) continue;

        /* Check "+++" and response to user */
        if (real_len == 3 && memcmp("+++", msg, 3) == 0) {
            goto succ_exit;
        }

        /* Send data */
        while (mico_ble_send_data(msg, real_len, 1000) == MICO_BT_TIMEOUT) {
            /* Re-send the packet if it returned TIMEOUT. */
        }
    }

err_exit:
    sprintf(response, "%s", AT_RESPONSE_ERR);
    goto exit;

succ_exit:
    g_ble_context.p_config->is_at_mode = MICO_TRUE;
    at_cmd_config_data_write();
    sprintf(response, "%s", AT_RESPONSE_OK);

exit:
    driver->write((uint8_t *)response, strlen(response));
    if (msg) free(msg);
    at_ble_log("%s: send raw data exit", __FUNCTION__);
}

/**
 * AT+LESEND=<length>
 * >
 * <xxxxxxxx>
 * OK
 */
static void ble_send_data_packet(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char             response[50];
    uint8_t         *msg = NULL;
    uint32_t         timeout;

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    int len = at_cmd_parse_get_digital(para->para, 1);
    require(len > 0 && len < 256, exit);

    msg = (uint8_t *)malloc((size_t)len);
    require_string(msg != NULL, exit, "No resource for malloc");

    sprintf(response, "%s", AT_RESPONSE_SEND);
    driver->write((uint8_t *)response, strlen(response));

    driver->ioctl(AT_GET_CMD_READ_TIMEOUT, &timeout);
    uint32_t real_len = at_cmd_driver_read(driver, msg, (uint32_t)len, timeout);
    if (real_len > 0 && mico_ble_send_data(msg, real_len, 500) == MICO_BT_SUCCESS) {
        sprintf(response, "%s", AT_RESPONSE_OK);
    } else {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    }

exit:
    if (msg) free(msg);
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LESCAN=<ON/OFF>
 * OK or ERR
 */
static void ble_set_scan_mode(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    mico_bool_t start;
    char response[50], *params;

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    params = at_cmd_parse_get_string(para->para, 1);
    if (strcmp(params, "ON") == 0) {
        start = MICO_TRUE;
    } else if (strcmp(params, "OFF") == 0) {
        start = MICO_FALSE;
    } else {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    if (MICO_BT_SUCCESS != mico_ble_set_device_scan(start)) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEADV=<ON/OFF>
 * OK or ERR
 */
static void ble_set_advertisement_mode(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    mico_bool_t  start;
    char response[50], *params;

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    params = at_cmd_parse_get_string(para->para, 1);
    if (strcmp(params, "ON") == 0) {
        start = MICO_TRUE;
    } else if (strcmp(params, "OFF") == 0) {
        start = MICO_FALSE;
    } else {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    if (MICO_BT_SUCCESS != mico_ble_set_device_discovery(start)) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LECONN=<addr>
 * OK or ERR
 * +LECONN:<YES/NO>
 *
 * @param driver
 * @param para
 */
static void ble_gap_connect(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char response[50], *params;
    mico_bt_device_address_t addr = {0};

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    params = at_cmd_parse_get_string(para->para, 1);
    bdaddr_aton(params, addr);

    if (MICO_BT_SUCCESS != mico_ble_connect(addr)) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEDISCONN=<handle>
 * OK or ERR
 * +LEDISCONN:<YES/NO>
 *
 * @param driver
 * @param para
 */
static void ble_gap_disconnect(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char response[50];
    uint16_t handle;

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    handle = (uint16_t)at_cmd_parse_get_digital(para->para, 1);
    require(handle > 0x0000 && handle < 0xffff, exit);

    if (MICO_BT_SUCCESS != mico_ble_disconnect(handle)) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LESTATE?
 *
 * +LESTATE:<state>
 * OK
 */
static void ble_get_state(at_cmd_driver_t *driver)
{
    char response[50];
    int  idx = 0;

    switch (mico_ble_get_device_state()) {
        case BLE_STATE_CONNECTED:
            idx = sprintf(response, "%s+LESTATE:CONNECTED", AT_PROMPT);
            break;
        case BLE_STATE_DISCONNECTED:
            idx = sprintf(response, "%s+LESTATE:DISCONNECTED", AT_PROMPT);
            break;
        case BLE_STATE_CONNECTING:
            idx = sprintf(response, "%s+LESTATE:CONNECTING", AT_PROMPT);
            break;
        case BLE_STATE_DISCOVERABLE:
            idx = sprintf(response, "%s+LESTATE:DISCOVERABLE", AT_PROMPT);
            break;
        case BLE_STATE_INQUIRING:
            idx = sprintf(response, "%s+LESTATE:INQUIRING", AT_PROMPT);
            break;
        default:
            break;
    }

    if (idx == 0) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        sprintf(response + idx, "%s", AT_RESPONSE_OK);
    }
    driver->write((uint8_t *)response, strlen(response));
}

/**
 * AT+LEWLNAME=?
 * 
 * +LEWLNAME:<name>
 * OK
 */
static void ble_get_whitelist_name(at_cmd_driver_t *driver)
{
    char response[50];

    sprintf(response, "%s+LEWLNAME:%s%s",
            AT_PROMPT,
            mico_ble_get_device_whitelist_name(),
            AT_RESPONSE_OK);
    driver->write((uint8_t *) response, strlen(response));
}

/**
 * AT+LEWLNAME=<name>
 * 
 * OK or ERR
 */
static void ble_set_whitelist_name(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    char response[50];

    if (para->para_num != 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    char *name = at_cmd_parse_get_string(para->para, 1);
    if (strlen(name) > BT_DEVICE_NAME_LEN - 1) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
        goto exit;
    }

    if (mico_ble_set_device_whitelist_name(name) != MICO_BT_SUCCESS) {
        sprintf(response, "%s", AT_RESPONSE_ERR);
    } else {
        strcpy(g_ble_context.p_config->whitelist_name, name);
        at_cmd_config_data_write();

        sprintf(response, "%s", AT_RESPONSE_OK);
    }

exit:
    driver->write((uint8_t *) response, strlen(response));
}

/**
 * AT+LEWLUUID=?
 * 
 * +LEWLUUID:<UUID>
 * OK
 */
static void ble_get_whitelist_uuid(at_cmd_driver_t *driver)
{
    UNUSED_PARAMETER(driver);
//    char response[50];
//
//    sprintf(response, "%s+LEWLUUID:%s%s",
//            AT_PROMPT,
//            mico_ble_get_device_whitelist_name(),
//            AT_RESPONSE_OK);
//    driver->write((uint8_t *) response, strlen(response));
}

/**
 * AT+LEWLUUID=<UUID>
 * 
 * OK or ERR
 */
static void ble_set_whitelist_uuid(at_cmd_driver_t *driver, at_cmd_para_t *para)
{
    UNUSED_PARAMETER(driver);
    UNUSED_PARAMETER(para);

//    char response[50];
//
//    if (para->para_num != 1) {
//        sprintf(response, "%s", AT_RESPONSE_ERR);
//        goto exit;
//    }
//
//    char *name = at_cmd_parse_get_string(para->para, 1);
//    if (strlen(name) > BT_DEVICE_NAME_LEN - 1) {
//        sprintf(response, "%s", AT_RESPONSE_ERR);
//        goto exit;
//    }
//
//    if (mico_ble_set_device_whitelist_name(name) != MICO_BT_SUCCESS) {
//        sprintf(response, "%s", AT_RESPONSE_ERR);
//    } else {
//        strcpy(g_ble_context.p_config->whitelist_name, name);
//        at_cmd_config_data_write();
//
//        sprintf(response, "%s", AT_RESPONSE_OK);
//    }
//
//exit:
//    driver->write((uint8_t *) response, strlen(response));
}


/**
 *
 * @param config
 * @return
 */
static mico_bt_result_t ble_default_config(at_cmd_ble_config_t *config)
{
    config->magic_number = BT_MAGIC_NUMBER;

    memset(config->device_name, 0, BT_DEVICE_NAME_LEN);
    strcpy(config->device_name, "MXCHIP_BT123456");

    memset(config->whitelist_name, 0, BT_DEVICE_NAME_LEN);
    memset(&config->whitelist_uuid, 0, sizeof(mico_bt_uuid_t));

    config->is_at_mode = MICO_TRUE; /* AT Command Mode for default configuration */
    config->is_enable_event = MICO_TRUE;
    return MICO_BT_SUCCESS;
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

