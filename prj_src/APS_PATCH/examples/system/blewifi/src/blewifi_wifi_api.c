/******************************************************************************
*  Copyright 2017 - 2018, Opulinks Technology Ltd.
*  ----------------------------------------------------------------------------
*  Statement:
*  ----------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of Opulinks Technology Ltd. (C) 2018
******************************************************************************/

#include "blewifi_wifi_api.h"
#include "blewifi_ctrl.h"
#include "blewifi_data.h"
#include "blewifi_configuration.h"
#include "blewifi_app.h"
#include "blewifi_server_app.h"
#include "blewifi_ble_api.h"

#include "cmsis_os.h"
#include "wifi_api.h"
#include "wifi_nvm.h"
#include "event_loop.h"
#include "lwip_helper.h"
#include "sys_common_api.h"
#ifdef __BLEWIFI_TRANSPARENT__
#include "at_cmd_app_patch.h"
#endif

#include "at_cmd_common.h"
#include "etharp.h"

#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"

extern uint8_t g_ubAppCtrlRequestRetryTimes;
extern uint8_t g_WifiSSID[WIFI_MAX_LENGTH_OF_SSID];


wifi_config_t wifi_config_req_connect;
uint32_t g_ulBleWifi_Wifi_BeaconTime;
extern uint8_t g_wifi_disconnectedDoneForAppDoWIFIScan;    //Goter

extern osTimerId g_tAppWifiConnectTimeOutTimerId;
extern uint8_t g_u8ConnectTimeOutFlag;

extern msg_print_uart1_fp_t msg_print_uart1;
extern unsigned char g_ucSecretKey[SECRETKEY_LEN + 1];

extern uint8_t g_mp_mode_flag;
uint8_t g_IsFindTargetAP = 0; // 1:found the target AP, 0:target AP not found
uint8_t u8MPBlinkingMode = 0;
uint32_t g_u32TickStart = 0, g_u32TickEnd = 0;

#define AUTO_CONNECT_PATCH  // patch wifi_connection_connect_from_ac_index bug

static int BleWifi_Wifi_EventHandler_Start(wifi_event_id_t event_id, void *data, uint16_t length);
static int BleWifi_Wifi_EventHandler_Connected(wifi_event_id_t event_id, void *data, uint16_t length);
static int BleWifi_Wifi_EventHandler_Disconnected(wifi_event_id_t event_id, void *data, uint16_t length);
static int BleWifi_Wifi_EventHandler_ScanComplete(wifi_event_id_t event_id, void *data, uint16_t length);
static int BleWifi_Wifi_EventHandler_GotIp(wifi_event_id_t event_id, void *data, uint16_t length);
static int BleWifi_Wifi_EventHandler_ConnectionFailed(wifi_event_id_t event_id, void *data, uint16_t length);
static T_BleWifi_Wifi_EventHandlerTbl g_tWifiEventHandlerTbl[] =
{
    {WIFI_EVENT_STA_START,              BleWifi_Wifi_EventHandler_Start},
    {WIFI_EVENT_STA_CONNECTED,          BleWifi_Wifi_EventHandler_Connected},
    {WIFI_EVENT_STA_DISCONNECTED,       BleWifi_Wifi_EventHandler_Disconnected},
    {WIFI_EVENT_SCAN_COMPLETE,          BleWifi_Wifi_EventHandler_ScanComplete},
    {WIFI_EVENT_STA_GOT_IP,             BleWifi_Wifi_EventHandler_GotIp},
    {WIFI_EVENT_STA_CONNECTION_FAILED,  BleWifi_Wifi_EventHandler_ConnectionFailed},

    {0xFFFFFFFF,                        NULL}
};

void BleWifi_Wifi_DoScan(wifi_scan_config_t *pstScan_config)
{
    if ( true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_TEST_MODE))
    {
        pstScan_config->ssid = &(g_WifiSSID[0]);
        #ifdef TEST_MODE_DEBUG_ENABLE
        msg_print_uart1("\r\n Test Mode SSID is %s\r\n", scan_config.ssid);
        #endif
    }
    BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_SCANNING, true);

    wifi_scan_start(pstScan_config, NULL);
}

void BleWifi_Wifi_DoConnect(uint8_t *data, int len)
{
    uint8_t ubAPPAutoConnectGetApNum = 0;
    uint32_t i = 0;
    wifi_auto_connect_info_t info = {0};
    uint8_t ubAPPConnect = 1;
    unsigned char ucDecPassword[BLEWIFI_WIFI_MAX_REC_PASSWORD_SIZE + 1] = {0};
    unsigned char iv[IV_SIZE + 1] = {0};
    size_t u16DecPasswordLen = 0;
    uint8_t u8TimeOutSettings = 0;

    g_ubAppCtrlRequestRetryTimes = 0;
    memset(&wifi_config_req_connect , 0 ,sizeof(wifi_config_t));
    memcpy(wifi_config_req_connect.sta_config.bssid, &data[0], WIFI_MAC_ADDRESS_LENGTH);

    if (len >= 9)
    {
        u8TimeOutSettings = data[7];
        if(u8TimeOutSettings <= BLEWIFI_WIFI_DEFAULT_TIMEOUT) //set timeout
        {
            g_ubAppCtrlRequestRetryTimes = BLEWIFI_WIFI_REQ_CONNECT_RETRY_TIMES;
            g_u8ConnectTimeOutFlag = 1;
        }
        else
        {
            osTimerStop(g_tAppWifiConnectTimeOutTimerId);
            osTimerStart(g_tAppWifiConnectTimeOutTimerId, (u8TimeOutSettings - BLEWIFI_WIFI_DEFAULT_TIMEOUT)*1000);
        }

        // Determine the connected column
        if (data[WIFI_MAC_ADDRESS_LENGTH])
        {
            wifi_auto_connect_get_ap_num(&ubAPPAutoConnectGetApNum);
            if (ubAPPAutoConnectGetApNum)
            {
                memset(&info, 0, sizeof(wifi_auto_connect_info_t));
                for (i = 0; i < ubAPPAutoConnectGetApNum; i++)
                {
                    wifi_auto_connect_get_ap_info(i, &info);
                    if(!MemCmp(wifi_config_req_connect.sta_config.bssid, info.bssid, sizeof(info.bssid)))
                    {
                        BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_CONNECTING, true);
                        wifi_connection_connect_from_ac_index(i);
                        ubAPPConnect = 0;
                        return;
                    }
                }
            }

        }

        // Can't find ap in the ap record list
        if (ubAPPConnect)
        {
#if 0
            wifi_config_req_connect.sta_config.password_length = data[7];
            memcpy((char *)wifi_config_req_connect.sta_config.password, &data[8], wifi_config_req_connect.sta_config.password_length);
#endif
            if(data[8] == 0) //password len = 0
            {
                printf("password_length = 0\r\n");
                wifi_config_req_connect.sta_config.password_length = 0;
                memset((char *)wifi_config_req_connect.sta_config.password, 0 , WIFI_LENGTH_PASSPHRASE);
            }
            else
            {
                if(data[8] > BLEWIFI_WIFI_MAX_REC_PASSWORD_SIZE)
                {
                    BLEWIFI_INFO(" \r\n Not do Manually connect %d \r\n ",__LINE__);
                    BleWifi_Ble_SendResponse(BLEWIFI_RSP_CONNECT, BLEWIFI_WIFI_PASSWORD_FAIL);
                    return;
                }
                mbedtls_base64_decode(ucDecPassword , BLEWIFI_WIFI_MAX_REC_PASSWORD_SIZE + 1 , &u16DecPasswordLen , (unsigned char *)&data[9] , data[8]);
                memset(iv, '0' , IV_SIZE); //iv = "0000000000000000"
                BleWifi_CBC_decrypt((void *)ucDecPassword , u16DecPasswordLen , iv , g_ucSecretKey , (void *)wifi_config_req_connect.sta_config.password);
                wifi_config_req_connect.sta_config.password_length = strlen((char *)wifi_config_req_connect.sta_config.password);
                //printf("password = %s\r\n" , wifi_config_req_connect.sta_config.password);
                //printf("password_length = %u\r\n" , wifi_config_req_connect.sta_config.password_length);
            }
            wifi_auto_connect_reset();

            BLEWIFI_INFO("BLEWIFI: Recv Connect Request\r\n");
            BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_CONNECTING, true);
            wifi_set_config(WIFI_MODE_STA, &wifi_config_req_connect);
            wifi_connection_connect(&wifi_config_req_connect);
        }
    }
    else
    {
        BleWifi_Ble_SendResponse(BLEWIFI_RSP_CONNECT, BLEWIFI_WIFI_CONNECTED_FAIL);
    }
}

void BleWifi_Wifi_ManuallyConnectAP(wifi_config_t *pstWifi_config_req_connect)
{
    BLEWIFI_INFO("\r\n %d SSID is %s  Recv SSID Password is %s\r\n",__LINE__, wifi_config_req_connect.sta_config.ssid ,wifi_config_req_connect.sta_config.password);

    wifi_auto_connect_reset();

    g_ubAppCtrlRequestRetryTimes = 0;

    wifi_set_config(WIFI_MODE_STA, pstWifi_config_req_connect);

    BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_CONNECTING, true);

    wifi_connection_connect(pstWifi_config_req_connect);
}


void BleWifi_Wifi_DoDisconnect(void)
{
    wifi_connection_disconnect_ap();
}

static int BleWifi_Wifi_GetManufName(uint8_t *name)
{
    uint16_t ret = false;

    if (name == NULL)
        return ret;

    memset(name, 0, STA_INFO_MAX_MANUF_NAME_SIZE);
    ret = wifi_nvm_sta_info_read(WIFI_NVM_STA_INFO_MANUFACTURE_NAME, STA_INFO_MAX_MANUF_NAME_SIZE, name);

    return ret;
}

static void BleWifi_Wifi_SendDeviceInfo(blewifi_device_info_t *dev_info)
{
    uint8_t *data;
    int data_len;
    uint8_t *pos;

    pos = data = malloc(sizeof(blewifi_scan_info_t));
    if (data == NULL) {
        printf("malloc error\r\n");
        return;
    }

    memcpy(data, dev_info->device_id, WIFI_MAC_ADDRESS_LENGTH);
    pos += 6;

    if (dev_info->name_len > BLEWIFI_MANUFACTURER_NAME_LEN)
        dev_info->name_len = BLEWIFI_MANUFACTURER_NAME_LEN;

    *pos++ = dev_info->name_len;
    memcpy(pos, dev_info->manufacturer_name, dev_info->name_len);
    pos += dev_info->name_len;
    data_len = (pos - data);

    BLEWIFI_DUMP("device info data", data, data_len);

    /* create device info data packet */
    BleWifi_Ble_DataSendEncap(BLEWIFI_RSP_READ_DEVICE_INFO, data, data_len);

    free(data);
}

void BleWifi_Wifi_ReadDeviceInfo(void)
{
    blewifi_device_info_t dev_info = {0};
    char manufacturer_name[33] = {0};

    wifi_config_get_mac_address(WIFI_MODE_STA, &dev_info.device_id[0]);
    BleWifi_Wifi_GetManufName((uint8_t *)&manufacturer_name);

    dev_info.name_len = strlen(manufacturer_name);
    memcpy(dev_info.manufacturer_name, manufacturer_name, dev_info.name_len);
    BleWifi_Wifi_SendDeviceInfo(&dev_info);
}

static int BleWifi_Wifi_SetManufName(uint8_t *name)
{
    uint16_t ret = false;
    uint8_t len;

    if (name == NULL)
        return ret;

    len = strlen((char *)name);
    if (len > STA_INFO_MAX_MANUF_NAME_SIZE)
        len = STA_INFO_MAX_MANUF_NAME_SIZE;

    ret = wifi_nvm_sta_info_write(WIFI_NVM_STA_INFO_MANUFACTURE_NAME, len, name);

    return ret;
}

void BleWifi_Wifi_WriteDeviceInfo(uint8_t *data, int len)
{
    blewifi_device_info_t dev_info;

    memset(&dev_info, 0, sizeof(blewifi_device_info_t));
    memcpy(dev_info.device_id, &data[0], WIFI_MAC_ADDRESS_LENGTH);
    dev_info.name_len = data[6];
    memcpy(dev_info.manufacturer_name, &data[7], dev_info.name_len);

    wifi_config_set_mac_address(WIFI_MODE_STA, dev_info.device_id);
    BleWifi_Wifi_SetManufName(dev_info.manufacturer_name);

    BleWifi_Ble_SendResponse(BLEWIFI_RSP_WRITE_DEVICE_INFO, 0);

    BLEWIFI_INFO("BLEWIFI: Device ID: \""MACSTR"\"\r\n", MAC2STR(dev_info.device_id));
    BLEWIFI_INFO("BLEWIFI: Device Manufacturer: %s",dev_info.manufacturer_name);
}

void BleWifi_Wifi_SendStatusInfo(uint16_t uwType)
{
    uint8_t *pubData, *pubPos;
    uint8_t ubStatus = 0, ubStrLen = 0;
    uint16_t uwDataLen;
    uint8_t ubaIp[4], ubaNetMask[4], ubaGateway[4];

    struct netif *iface = netif_find("st1");
    wifi_scan_info_t tInfo;

    ubaIp[0] = (iface->ip_addr.u_addr.ip4.addr >> 0) & 0xFF;
    ubaIp[1] = (iface->ip_addr.u_addr.ip4.addr >> 8) & 0xFF;
    ubaIp[2] = (iface->ip_addr.u_addr.ip4.addr >> 16) & 0xFF;
    ubaIp[3] = (iface->ip_addr.u_addr.ip4.addr >> 24) & 0xFF;

    ubaNetMask[0] = (iface->netmask.u_addr.ip4.addr >> 0) & 0xFF;
    ubaNetMask[1] = (iface->netmask.u_addr.ip4.addr >> 8) & 0xFF;
    ubaNetMask[2] = (iface->netmask.u_addr.ip4.addr >> 16) & 0xFF;
    ubaNetMask[3] = (iface->netmask.u_addr.ip4.addr >> 24) & 0xFF;

    ubaGateway[0] = (iface->gw.u_addr.ip4.addr >> 0) & 0xFF;
    ubaGateway[1] = (iface->gw.u_addr.ip4.addr >> 8) & 0xFF;
    ubaGateway[2] = (iface->gw.u_addr.ip4.addr >> 16) & 0xFF;
    ubaGateway[3] = (iface->gw.u_addr.ip4.addr >> 24) & 0xFF;

    wifi_sta_get_ap_info(&tInfo);

    pubPos = pubData = malloc(sizeof(blewifi_wifi_status_info_t));
    if (pubData == NULL) {
        printf("malloc error\r\n");
        return;
    }

    ubStrLen = strlen((char *)&tInfo.ssid);

    if (ubStrLen == 0)
    {
        ubStatus = 1; // Return Failure
        if (uwType == BLEWIFI_IND_IP_STATUS_NOTIFY)     // if failure, don't notify the status
        {
            printf("=== SSID length : 0 =====\n");
            //goto release;
        }
    }
    else
    {
        ubStatus = 0; // Return success
    }

    /* Status */
    *pubPos++ = ubStatus;

    /* ssid length */
    *pubPos++ = ubStrLen;

   /* SSID */
    if (ubStrLen != 0)
    {
        memcpy(pubPos, tInfo.ssid, ubStrLen);
        pubPos += ubStrLen;
    }

   /* BSSID */
    memcpy(pubPos, tInfo.bssid, 6);
    pubPos += 6;

    /* IP */
    memcpy(pubPos, (char *)ubaIp, 4);
    pubPos += 4;

    /* MASK */
    memcpy(pubPos,  (char *)ubaNetMask, 4);
    pubPos += 4;

    /* GATEWAY */
    memcpy(pubPos,  (char *)ubaGateway, 4);
    pubPos += 4;

    uwDataLen = (pubPos - pubData);

    BLEWIFI_DUMP("Wi-Fi status info data", pubData, uwDataLen);
    /* create Wi-Fi status info data packet */
    BleWifi_Ble_DataSendEncap(uwType, pubData, uwDataLen);

//release:
    free(pubData);
}

void BleWifi_Wifi_ResetRecord(void)
{
    uint8_t ubResetResult = 0;

    printf("\033[1;32;40m");
    printf("[%s %d] do wifi_auto_connect_reset!!!");
    printf("\033[0m");
    printf("\n");
    ubResetResult = wifi_auto_connect_reset();
    BleWifi_Ble_SendResponse(BLEWIFI_RSP_RESET, ubResetResult);

    wifi_connection_disconnect_ap();
}

void BleWifi_Wifi_MacAddrWrite(uint8_t *data, int len)
{
    uint8_t ubaMacAddr[6];

    // save the mac address into flash
    memcpy(ubaMacAddr, &data[0], 6);
    wifi_config_set_mac_address(WIFI_MODE_STA, ubaMacAddr);
    // apply the mac address from flash
    mac_addr_set_config_source(MAC_IFACE_WIFI_STA, MAC_SOURCE_FROM_FLASH);

    BleWifi_Ble_SendResponse(BLEWIFI_RSP_ENG_WIFI_MAC_WRITE, 0);
}

void BleWifi_Wifi_MacAddrRead(uint8_t *data, int len)
{
    uint8_t ubaMacAddr[6];

    // get the mac address from flash
    wifi_config_get_mac_address(WIFI_MODE_STA, ubaMacAddr);

    BleWifi_Ble_DataSendEncap(BLEWIFI_RSP_ENG_WIFI_MAC_READ, ubaMacAddr, 6);
}

static void BleWifi_Wifi_UpdateAutoConnList(uint16_t apCount, wifi_scan_info_t *ap_list,uint8_t *pu8IsUpdate)
{
    wifi_auto_connect_info_t *info = NULL;
    uint8_t ubAutoCount;
    uint16_t i, j;

    // if the count of auto-connection list is empty, don't update the auto-connect list
    ubAutoCount = BleWifi_Wifi_AutoConnectListNum();
    if (0 == ubAutoCount)
    {
        *pu8IsUpdate = false;
        return;
    }

    // compare and update the auto-connect list
    // 1. prepare the buffer of auto-connect information
    info = (wifi_auto_connect_info_t *)malloc(sizeof(wifi_auto_connect_info_t));
    if (NULL == info)
    {
        printf("malloc fail, prepare is NULL\r\n");
        *pu8IsUpdate = false;
        return;
    }
    // 2. comapre
    for (i=0; i<ubAutoCount; i++)
    {
        // get the auto-connect information
        memset(info, 0, sizeof(wifi_auto_connect_info_t));
        wifi_auto_connect_get_ap_info(i, info);
        for (j=0; j<apCount; j++)
        {
            if (0 == MemCmp(ap_list[j].bssid, info->bssid, sizeof(info->bssid)))
            {
                // if the channel is not the same, update it
                if (ap_list[j].channel != info->ap_channel)
                    wifi_auto_connect_update_ch(i, ap_list[j].channel);
                *pu8IsUpdate = true;
                continue;
            }
        }
    }
    // 3. free the buffer of auto-connect information
    free(info);
}

static void BleWifi_Wifi_SendSingleScanReport(uint16_t apCount, blewifi_scan_info_t *ap_list)
{
    uint8_t *data;
    int data_len;
    uint8_t *pos;
    int malloc_size = sizeof(blewifi_scan_info_t) * apCount;

    pos = data = malloc(malloc_size);
    if (data == NULL) {
        printf("malloc error\r\n");
        return;
    }

    for (int i = 0; i < apCount; ++i)
    {
        uint8_t len = ap_list[i].ssid_length;

        data_len = (pos - data);

        *pos++ = len;
        memcpy(pos, ap_list[i].ssid, len);
        pos += len;
        memcpy(pos, ap_list[i].bssid,6);
        pos += 6;
        *pos++ = ap_list[i].auth_mode;
        *pos++ = ap_list[i].rssi;
#ifdef AUTO_CONNECT_PATCH
        *pos++ = 0;
#else
        *pos++ = ap_list[i].connected;
#endif
    }

    data_len = (pos - data);

    BLEWIFI_DUMP("scan report data", data, data_len);

    /* create scan report data packet */
    BleWifi_Ble_DataSendEncap(BLEWIFI_RSP_SCAN_REPORT, data, data_len);

    free(data);
}

int BleWifi_Wifi_SendScanReport(void)
{
    wifi_scan_info_t *ap_list = NULL;
    blewifi_scan_info_t *blewifi_ap_list = NULL;
    uint16_t apCount = 0;
    int8_t ubAppErr = 0;
    int32_t i = 0, j = 0;
    uint8_t ubAPPAutoConnectGetApNum = 0;
    wifi_auto_connect_info_t *info = NULL;
    uint8_t u8IsUpdate = false;

    wifi_scan_get_ap_num(&apCount);

    if (apCount == 0) {
        printf("No AP found\r\n");
        goto err;
    }
    printf("ap num = %d\n", apCount);
    ap_list = (wifi_scan_info_t *)malloc(sizeof(wifi_scan_info_t) * apCount);

    if (!ap_list) {
        printf("malloc fail, ap_list is NULL\r\n");
        ubAppErr = -1;
        goto err;
    }

    wifi_scan_get_ap_records(&apCount, ap_list);

    BleWifi_Wifi_UpdateAutoConnList(apCount, ap_list,&u8IsUpdate);

    blewifi_ap_list = (blewifi_scan_info_t *)malloc(sizeof(blewifi_scan_info_t) *apCount);
    if (!blewifi_ap_list) {
        printf("malloc fail, blewifi_ap_list is NULL\r\n");
        ubAppErr = -1;
        goto err;
    }

    wifi_auto_connect_get_ap_num(&ubAPPAutoConnectGetApNum);
    if (ubAPPAutoConnectGetApNum)
    {
        info = (wifi_auto_connect_info_t *)malloc(sizeof(wifi_auto_connect_info_t) * ubAPPAutoConnectGetApNum);
        if (!info) {
            printf("malloc fail, info is NULL\r\n");
            ubAppErr = -1;
            goto err;
        }

        memset(info, 0, sizeof(wifi_auto_connect_info_t) * ubAPPAutoConnectGetApNum);

        for (i = 0; i < ubAPPAutoConnectGetApNum; i++)
        {
            wifi_auto_connect_get_ap_info(i, (info+i));
        }
    }

    /* build blewifi ap list */
    for (i = 0; i < apCount; ++i)
    {
        memcpy(blewifi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        memcpy(blewifi_ap_list[i].bssid, ap_list[i].bssid, WIFI_MAC_ADDRESS_LENGTH);
        blewifi_ap_list[i].rssi = ap_list[i].rssi;
        blewifi_ap_list[i].auth_mode = ap_list[i].auth_mode;
        blewifi_ap_list[i].ssid_length = strlen((const char *)ap_list[i].ssid);
        blewifi_ap_list[i].connected = 0;
        for (j = 0; j < ubAPPAutoConnectGetApNum; j++)
        {
            if ((info+j)->ap_channel)
            {
                if(!MemCmp(blewifi_ap_list[i].ssid, (info+j)->ssid, sizeof((info+j)->ssid)) && !MemCmp(blewifi_ap_list[i].bssid, (info+j)->bssid, sizeof((info+j)->bssid)))
                {
                    blewifi_ap_list[i].connected = 1;
                    break;
                }
            }
        }
    }

    /* Send Data to BLE */
    /* Send AP inforamtion individually */
    for (i = 0; i < apCount; ++i)
    {
        if(blewifi_ap_list[i].ssid_length != 0)
        {
            BleWifi_Wifi_SendSingleScanReport(1, &blewifi_ap_list[i]);
            osDelay(100);
        }
    }

err:
    if (ap_list)
        free(ap_list);

    if (blewifi_ap_list)
        free(blewifi_ap_list);

    if (info)
        free(info);

    return ubAppErr;
}

int BleWifi_Wifi_UpdateScanInfoToAutoConnList(uint8_t *pu8IsUpdate)
{
    wifi_scan_info_t *ap_list = NULL;
    uint16_t apCount = 0;
    int8_t ubAppErr = 0;

    wifi_scan_get_ap_num(&apCount);

    if (apCount == 0) {
        printf("No AP found\r\n");
        *pu8IsUpdate = false;
        goto err;
    }
    printf("ap num = %d\n", apCount);
    ap_list = (wifi_scan_info_t *)malloc(sizeof(wifi_scan_info_t) * apCount);

    if (!ap_list) {
        printf("malloc fail, ap_list is NULL\r\n");
        ubAppErr = -1;
        *pu8IsUpdate = false;
        goto err;
    }

    wifi_scan_get_ap_records(&apCount, ap_list);

    BleWifi_Wifi_UpdateAutoConnList(apCount, ap_list, pu8IsUpdate);

err:
    if (ap_list)
        free(ap_list);

    return ubAppErr;
}

uint8_t BleWifi_Wifi_AutoConnectListNum(void)
{
    uint8_t ubApNum = 0;

    wifi_auto_connect_get_saved_ap_num(&ubApNum);
    return ubApNum;
}

void BleWifi_Wifi_DoAutoConnect(void)
{
    BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_CONNECTING, true);
    wifi_auto_connect_start();
}

void BleWifi_Wifi_ReqConnectRetry(void)
{
    BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_WIFI_CONNECTING, true);

    wifi_connection_connect(&wifi_config_req_connect);
}

int BleWifi_Wifi_Rssi(int8_t *rssi)
{
    return wifi_connection_get_rssi(rssi);
}

int BleWifi_Wifi_SetDTIM(uint32_t value)
{
    uint32_t ulDtimInterval;

    // DTIM interval
    ulDtimInterval = (value + (g_ulBleWifi_Wifi_BeaconTime / 2)) / g_ulBleWifi_Wifi_BeaconTime;
    // the max is 8 bits
    if (ulDtimInterval > 255)
        ulDtimInterval = 255;

    /* DTIM: skip n-1 */
    if (ulDtimInterval == 0)
        return wifi_config_set_skip_dtim(0, false);
    else
        return wifi_config_set_skip_dtim(ulDtimInterval - 1, false);
}

void BleWifi_Wifi_UpdateBeaconInfo(void)
{
    wifi_scan_info_t tInfo;

    // get the information of Wifi AP
    wifi_sta_get_ap_info(&tInfo);

    // beacon time (ms)
    g_ulBleWifi_Wifi_BeaconTime = tInfo.beacon_interval * tInfo.dtim_period;

    // error handle
    if (g_ulBleWifi_Wifi_BeaconTime == 0)
        g_ulBleWifi_Wifi_BeaconTime = 100;
}

static int BleWifi_Wifi_EventHandler_Start(wifi_event_id_t event_id, void *data, uint16_t length)
{
    printf("\r\nWi-Fi Start \r\n");

    /* Tcpip stack and net interface initialization,  dhcp client process initialization. */
    lwip_network_init(WIFI_MODE_STA);

    /* DTIM */
    BleWifi_Wifi_SetDTIM(0);

    //lwip_one_shot_arp_enable();   //Terence remove

    BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_INIT_COMPLETE, NULL, 0);

    return 0;
}

static int BleWifi_Wifi_EventHandler_Connected(wifi_event_id_t event_id, void *data, uint16_t length)
{
    uint8_t reason = *((uint8_t*)data);

    g_wifi_disconnectedDoneForAppDoWIFIScan = 1;

    osTimerStop(g_tAppWifiConnectTimeOutTimerId);
    printf("\r\nWi-Fi Connected, reason %d \r\n", reason);
    lwip_net_start(WIFI_MODE_STA);
    BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_CONNECTION_IND, NULL, 0);

    return 0;
}

static int BleWifi_Wifi_EventHandler_Disconnected(wifi_event_id_t event_id, void *data, uint16_t length)
{
    uint8_t reason = *((uint8_t*)data);

    printf("\r\nWi-Fi Disconnected , reason %d\r\n", reason);
    g_wifi_disconnectedDoneForAppDoWIFIScan = 1;   //Goter

    if ( reason == 255 ) {
        /* Reset the beacon time (ms) */
        g_ulBleWifi_Wifi_BeaconTime = 100;

        /* DTIM */
        BleWifi_Wifi_SetDTIM(0);

        BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_RESET_DEFAULT_IND, NULL, 0);
    }
    else {
        BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_DISCONNECTION_IND, NULL, 0);
    }

    return 0;
}

static int BleWifi_Wifi_EventHandler_ScanComplete(wifi_event_id_t event_id, void *data, uint16_t length)
{
    printf("\r\nWi-Fi Scan Done \r\n");

    if (g_mp_mode_flag)
    {
        int i = 0;
        int isMatched = 0;
        wifi_scan_list_t *p_scan_list = NULL;

        wifi_scan_config_t scan_config = {0};
        scan_config.ssid = TEST_MODE_WIFI_DEFAULT_SSID;
        scan_config.scan_type = WIFI_SCAN_TYPE_MIX;
        //scan the specified SSID "8DB0839D"

        p_scan_list = (wifi_scan_list_t *)malloc(sizeof(wifi_scan_list_t));
        if (p_scan_list == NULL)
        {
            return 0;
        }
        MemSet(p_scan_list, 0, sizeof(wifi_scan_list_t));

        /* Get APs list */
        wifi_scan_get_ap_list(p_scan_list);

        /* Search if AP matched */
        for (i=0; i< p_scan_list->num; i++)
        {
            if (memcmp(p_scan_list->ap_record[i].ssid, scan_config.ssid, StrLen(scan_config.ssid)) == 0)
            {
                isMatched = 1;
                break;
            }
        }

        free(p_scan_list);

        if (isMatched)  //find the specified SSID
        {
            g_IsFindTargetAP = 1;
            printf("MP mode start\r\n");

            if(p_scan_list->ap_record[i].rssi <= -60)
            {
                u8MPBlinkingMode = MP_MODE_NO_ROUTER;
            }
            else
            {
                u8MPBlinkingMode = MP_MODE_NO_SERVER;
            }
            BleWifi_Ctrl_LedStatusChange();
        }
        else   //can not find the specified SSID, trigger wifi scan again
        {
            g_IsFindTargetAP = 0;
            g_u32TickEnd = osKernelSysTick();

            if((g_u32TickEnd - g_u32TickStart) < MP_MODE_SCAN_AP_TIMEOUT)
            {
                wifi_scan_start(&scan_config, NULL);
            }
            else
            {
                g_mp_mode_flag = 0;
                g_IsFindTargetAP = 0;
                BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_INIT_COMPLETE, NULL, 0);
            }
        }
    }
    else
    {
        BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_SCAN_DONE_IND, NULL, 0);
    }

    return 0;
}

static int BleWifi_Wifi_EventHandler_GotIp(wifi_event_id_t event_id, void *data, uint16_t length)
{
    printf("\r\nWi-Fi Got IP \r\n");
    BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_GOT_IP_IND, NULL, 0);

    return 0;
}

static int BleWifi_Wifi_EventHandler_ConnectionFailed(wifi_event_id_t event_id, void *data, uint16_t length)
{
    extern uint8_t g_ATWIFICNTRetry;
    uint8_t reason = *((uint8_t*)data);

    printf("\r\nWi-Fi Connected failed, reason %d\r\n", reason);

    if (( true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_TEST_MODE))
        && (true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_AT_WIFI_MODE)))
    {
        if(g_ATWIFICNTRetry < ATWIFI_CNT_FAILED_RETRY)
        {
           // msg_print_uart1("\r\ng_ATWIFICNTRetry %d\r\n", g_ATWIFICNTRetry);
            g_ATWIFICNTRetry++;
            BleWifi_Ctrl_WiFiConnect();
            return 0;
        }
    }

    BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_DISCONNECTION_IND, &reason , 0);

    return 0;
}

// it is used in the Wifi task
int BleWifi_Wifi_EventHandlerCb(wifi_event_id_t event_id, void *data, uint16_t length)
{
    uint32_t i = 0;
    int lRet = 0;

#ifdef __BLEWIFI_TRANSPARENT__
    at_wifi_event_update_status(event_id, data, length);
#endif

    while (g_tWifiEventHandlerTbl[i].ulEventId != 0xFFFFFFFF)
    {
        // match
        if (g_tWifiEventHandlerTbl[i].ulEventId == event_id)
        {
            lRet = g_tWifiEventHandlerTbl[i].fpFunc(event_id, data, length);
            break;
        }

        i++;
    }

    // not match
    if (g_tWifiEventHandlerTbl[i].ulEventId == 0xFFFFFFFF)
    {
        printf("\r\n Unknown Event %d \r\n", event_id);
        lRet = 1;
    }

    return lRet;
}

void BleWifi_Wifi_Init(void)
{
    /* Event Loop Initialization */
    wifi_event_loop_init((wifi_event_cb_t)BleWifi_Wifi_EventHandlerCb);

    /* Initialize wifi stack and register wifi init complete event handler */
    wifi_init(NULL, NULL);

    /* Wi-Fi operation start */
    wifi_start();
    wifi_auto_connect_set_ap_num(1);

    /* Init the beacon time (ms) */
    g_ulBleWifi_Wifi_BeaconTime = 100;
}
