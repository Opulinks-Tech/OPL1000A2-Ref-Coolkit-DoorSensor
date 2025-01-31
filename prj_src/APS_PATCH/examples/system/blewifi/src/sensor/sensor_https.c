/******************************************************************************
*  Copyright 2017 - 2019, Opulinks Technology Ltd.
*  ----------------------------------------------------------------------------
*  Statement:
*  ----------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of Opulinks Technology Ltd. (C) 2019
******************************************************************************/

#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/errno.h"

#include "blewifi_ctrl.h"
#include "blewifi_data.h"
#include "blewifi_common.h"
#include "blewifi_configuration.h"
#include "blewifi_wifi_api.h"
#include "driver_netlink.h"
#include "sensor_https.h"
#include "sensor_data.h"
#include "sensor_common.h"
#include "sensor_battery.h"
#include "wifi_api.h"
#include "ftoa_util.h"
#include "etharp.h"


#include "controller_wifi.h"

#define TCP_LOCAL_PORT_RANGE_START        0xc000
#define TCP_LOCAL_PORT_RANGE_END          0xffff
#define TCP_ENSURE_LOCAL_PORT_RANGE(port) ((u16_t)(((port) & ~TCP_LOCAL_PORT_RANGE_START) + TCP_LOCAL_PORT_RANGE_START))

#define HDR_HOST_DIR         " HTTP/1.1"
#define HDR_HOST             "Host:"

#define HDR_CT               "Content-Type:application/json"
#define HDR_AUTH             "Authorization:Sign "
#define HDR_CONTENT_LENGTH   "content-length: "

#define CALC_AUTH_KEY_FORMAT "{\"apikey\":\"%s\",\"deviceid\":\"%s\",\"d_seq\":\"%u\"}"
#define POST_DATA_DEVICE_ID_FORMAT "{\"deviceid\":\"%s\",\"d_seq\":\"%u\",\"params\":{"
#define POST_DATA_SWITCH_FORMAT "\"switch\":\"%s\","
#define POST_DATA_BATTERY_FORMAT "\"battery\":%s,"
#define POST_DATA_FWVERSION_FORMAT "\"fwVersion\":\"%s\","
#define POST_DATA_TYPE_FORMAT "\"type\":%d,"
#define POST_DATA_CHIP_FORMAT "\"chipID\":\"%02x%02x%02x%02x%02x%02x\","
#define POST_DATA_MACADDRESS_FORMAT "\"mac\":\"%02x%02x%02x%02x%02x%02x\","
#define POST_DATA_RSSI_FORMAT "\"rssi\":%d}}"
#define OTA_DATA_URL "%s?deviceid=%s&ts=%u&sign=%s"
#define SHA256_FOR_OTA_FORMAT "%s%u%s"
#define CKS_FW_VERSION_FORMAT "%d.%d.%03d"

#define POST_DATA_INFO1_FORMAT "\"deviceid\":\"%s\",\"time\":\"%u\",\"switch\":\"%s\",\"battery\":%s,"   //Goter
#define POST_DATA_INFO2_FORMAT "\"fwVersion\":\"%s\",\"type\":\"%d\",\"rssi\":%d\""

#define BODY_FORMAT   "POST %s %s\r\n%s%s\r\n%s\r\n%s%s\r\n%s%d\r\n\r\n%s"

#define RSSI_SHINFT         22

#define MAX_TYPE1_2_3_COUNT 6

int g_nType1_2_3_Retry_counter = 0;
int g_nDoType1_2_3_Retry_Flag = 0;

const char ssl_client_ca_crt[] =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/\r\n"
    "MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\r\n"
    "DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow\r\n"
    "PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD\r\n"
    "Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\r\n"
    "AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O\r\n"
    "rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq\r\n"
    "OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b\r\n"
    "xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw\r\n"
    "7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD\r\n"
    "aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV\r\n"
    "HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG\r\n"
    "SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69\r\n"
    "ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr\r\n"
    "AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz\r\n"
    "R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5\r\n"
    "JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo\r\n"
    "Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ\r\n"
    "-----END CERTIFICATE-----\r\n";

#define DEBUG_LEVEL 5
#define type        1

unsigned char g_ubSendBuf[BUFFER_SIZE] = {0};
char OTA_FULL_URL[256] = {0};

unsigned char ubHttpsReadBuf[BUFFER_SIZE] = {0};

extern osTimerId    g_tAppCtrlHttpPostTimer;
extern osTimerId    g_tAppCtrlHourlyHttpPostRetryTimer;
extern osTimerId    g_tAppCtrlType1_2_3_HttpPostRetryTimer;

extern osTimerId    g_tAppPostFailLedTimeOutTimerId;

extern T_MwFim_GP12_HttpPostContent g_tHttpPostContent;
extern T_MwFim_GP12_HttpHostInfo g_tHostInfo;

float g_fBatteryVoltage = 0;

int g_nHrlyPostRetry = 0;

#define POST_FAILED_RETRY    3
#define MAX_HOUR_RETRY_POST  6

static void my_debug( void *ctx, int level,
                      const char *file, int line,
                      const char *str )
{
    const char *p, *basename;

    /* Extract basename from file */
    for( p = basename = file; *p != '\0'; p++ )
        if( *p == '/' || *p == '\\' )
            basename = p + 1;

    printf("%s:%04d: |%d| %s", basename, line, level, str );
}

static const int ciphersuite_preference[] =
{
    /* All AES-256 ephemeral suites */
    MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256,
    MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256,
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
    0
};


int Sensor_Updata_Post_Content(HttpPostData_t *);
int Sensor_Sha256_Value(HttpPostData_t *, uint8_t *);
int Sensor_Sha256_Value_OTA(HttpPostData_t *,  uint8_t *);


static int Sensor_Https_Init(mbedtls_net_context *server_fd,
                           mbedtls_ssl_context *ssl,
                           mbedtls_ssl_config *conf,
                           mbedtls_x509_crt *cacert,
                           mbedtls_ctr_drbg_context *ctr_drbg,
                           mbedtls_entropy_context *entropy)
{
      int ret = 0;
      const char *pers = "ssl_client";

    printf("SSL client starts\n");

    /*
     * 0. Initialize the RNG and the session data
     */
    mbedtls_ssl_setup_preference_ciphersuites(ciphersuite_preference);
    mbedtls_net_init( server_fd );
    mbedtls_ssl_init( ssl );
    mbedtls_ssl_config_init( conf );
    mbedtls_x509_crt_init( cacert );
    mbedtls_ctr_drbg_init( ctr_drbg );

    //printf("Seeding the random number generator...");

    mbedtls_entropy_init( entropy );
    //printf("entropy_inited");
    if ((ret = mbedtls_ctr_drbg_seed( ctr_drbg, mbedtls_entropy_func, entropy,
                               (const unsigned char *)pers, strlen(pers))) != 0)
    {
        printf("failed\n  ! mbedtls_ctr_drbg_seed returned -0x%x\n", -ret );
        return false;
    }

    /*
     * 0. Initialize certificates
     */
    //printf("Loading the CA root certificate ...");

    ret = mbedtls_x509_crt_parse(cacert, (const unsigned char *) ssl_client_ca_crt,
                          sizeof(ssl_client_ca_crt));
    if (ret < 0)
    {
        printf("mbedtls_x509_crt_parse returned -0x%x\n\n", -ret );
        return false;
    }

    printf("ok (%d skipped)\n", ret);

    return true;
}

/*
 * Set the socket blocking or non-blocking
 */
static int mbedtls_net_ex_set_block(mbedtls_net_context *ctx)
{
    return ( fcntl( ctx->fd, F_SETFL, fcntl( ctx->fd, F_GETFL, 0 ) & ~O_NONBLOCK ) );
}

static int mbedtls_net_ex_set_nonblock(mbedtls_net_context *ctx)
{
    return ( fcntl( ctx->fd, F_SETFL, fcntl( ctx->fd, F_GETFL, 0 ) | O_NONBLOCK ) );
}

/*
 * Initiate a TCP connection with host:port and the given protocol
 */
static int mbedtls_net_connect_ex( mbedtls_net_context *ctx, const char *host,
                         const char *port, int proto )
{
    int ret;
    struct addrinfo hints, *addr_list, *cur;
    struct sockaddr_in local_addr = {0};
    struct netif *iface = netif_find("st1");

    /* Do name resolution with both IPv6 and IPv4 */
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = proto == MBEDTLS_NET_PROTO_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = proto == MBEDTLS_NET_PROTO_UDP ? IPPROTO_UDP : IPPROTO_TCP;

    if( getaddrinfo( host, port, &hints, &addr_list ) != 0 ) {
        return( MBEDTLS_ERR_NET_UNKNOWN_HOST );
    }

    /* Try the sockaddrs until a connection succeeds */
    ret = MBEDTLS_ERR_NET_UNKNOWN_HOST;
    for( cur = addr_list; cur != NULL; cur = cur->ai_next )
    {
        ctx->fd = (int) socket( cur->ai_family, cur->ai_socktype,
                            cur->ai_protocol );

        if( ctx->fd < 0 )
        {
            ret = MBEDTLS_ERR_NET_SOCKET_FAILED;
            continue;
        }

        srand(reg_read(0x40003044));

        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(TCP_ENSURE_LOCAL_PORT_RANGE((u32_t)rand()));
        local_addr.sin_addr.s_addr = iface->ip_addr.u_addr.ip4.addr;
        if (bind(ctx->fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) != 0) {
            printf("bind failed\n");
        }

        printf("local_addr.sin_port=%d\n", htons(local_addr.sin_port));

        mbedtls_net_ex_set_nonblock(ctx);

        if ( connect( ctx->fd, cur->ai_addr, cur->ai_addrlen ) == 0 ) {
            ret = 0;
            mbedtls_net_ex_set_block(ctx);
            break;
        }
        else
        {
            if (errno == EINPROGRESS)
            {
                fd_set rfds, wfds;
                struct timeval tv;

                FD_ZERO(&rfds);
                FD_ZERO(&wfds);
                FD_SET(ctx->fd, &rfds);
                FD_SET(ctx->fd, &wfds);

                tv.tv_sec = (SOCKET_CONNECT_TIMEOUT / 1000);
                tv.tv_usec = (SOCKET_CONNECT_TIMEOUT % 1000) * 1000;

                int selres = select(ctx->fd + 1, &rfds, &wfds, NULL, &tv);

                if (selres > 0) {
                    if (FD_ISSET(ctx->fd, &wfds)) {
                        ret = 0;
                        mbedtls_net_ex_set_block(ctx);
                        break;
                    }
                }
            }
        }

        close( ctx->fd );
        ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    freeaddrinfo( addr_list );

    return (ret);
}

static int Sensor_Https_Establish(mbedtls_net_context *server_fd,
                           mbedtls_ssl_context *ssl,
                           mbedtls_ssl_config *conf,
                           mbedtls_x509_crt *cacert,
                           mbedtls_ctr_drbg_context *ctr_drbg
)
{
    int ret = 0;
    uint32_t flags = 0;
    const char *PosStart = NULL;
    const char *PosResult = NULL;
    const char *NeedleStart = ":";
    int TotalLen = 0;
    char URL[128] = {0};
    char Port[8] = {0};

    PosStart = g_tHostInfo.ubaHostInfoURL;
    TotalLen = strlen(g_tHostInfo.ubaHostInfoURL);

    if ((PosStart=strstr(PosStart,NeedleStart)) != NULL ) {
        PosResult = PosStart;

        strncpy (URL, g_tHostInfo.ubaHostInfoURL, (PosResult - g_tHostInfo.ubaHostInfoURL) );
        strncpy (Port, PosResult + strlen("/"), TotalLen - (PosResult - g_tHostInfo.ubaHostInfoURL + strlen("/")) );
    }
    else
    {
        strcpy (URL, g_tHostInfo.ubaHostInfoURL);
        strcpy (Port, "80");

    }

    /*
     * 1. Start the connection
     */
    printf("Connecting to tcp/%s:%s...\n\n", URL, Port);

    if ((ret = mbedtls_net_connect_ex(server_fd, URL,
                                   Port, MBEDTLS_NET_PROTO_TCP)) != 0)
    {
        printf("mbedtls_net_connect_ex returned -0x%x\n\n", -ret);
        return false;
    }

    /*
     * 2. Setup stuff
     */
    //printf("Setting up the SSL/TLS structure...\n\n");

    if ((ret = mbedtls_ssl_config_defaults(conf,
                    MBEDTLS_SSL_IS_CLIENT,
                    MBEDTLS_SSL_TRANSPORT_STREAM,
                    MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
    {
        printf("mbedtls_ssl_config_defaults returned -0x%x\n\n", -ret);
        return false;
    }

    /* OPTIONAL is not optimal for security,
     * but makes interop easier in this simplified example */
    mbedtls_ssl_conf_read_timeout(conf, SSL_HANDSHAKE_TIMEOUT);
    printf("[SSL][HANDSHAKE]conf.read_timeout=%d\n", conf->read_timeout);

    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(conf, cacert, NULL );
    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, ctr_drbg);
    mbedtls_ssl_conf_dbg(conf, my_debug, stdout);

    if ((ret = mbedtls_ssl_setup(ssl, conf)) != 0)
    {
        printf("mbedtls_ssl_setup returned -0x%x\n\n", -ret);
        return false;
    }

    if ((ret = mbedtls_ssl_set_hostname(ssl, URL)) != 0)
    {
        printf("mbedtls_ssl_set_hostname returned -0x%x\n\n", -ret);
        return false;
    }

    mbedtls_ssl_set_bio(ssl, server_fd, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    /*
     * 4. Handshake
     */
    printf("Performing the SSL/TLS handshake...\n\n");

    while ((ret = mbedtls_ssl_handshake(ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            printf("mbedtls_ssl_handshake returned -0x%x\n\n", -ret);
            return false;
        }
    }

    mbedtls_ssl_conf_read_timeout(conf, SSL_SOCKET_TIMEOUT);
    printf("[SSL][READ]conf.read_timeout=%d\n", conf->read_timeout);

    /*
     * 5. Verify the server certificate
     */
    //printf("Verifying peer X.509 certificate...\n\n");

    /* In real life, we probably want to bail out when ret != 0 */
    if ((flags = mbedtls_ssl_get_verify_result(ssl)) != 0)
    {
        //memset(buf, 0, sizeof(buf));
        printf("Failed to verify peer certificate! Flags = %d\n\n", flags);
        //mbedtls_x509_crt_verify_info((char*)&buf[0], sizeof(buf), "  ! ", flags);
        //printf("verification info: %s\n\n", buf);
    }
    else
    {
        printf("Certificate verified.\n\n");
    }

    //printf("Cipher suite is %s", mbedtls_ssl_get_ciphersuite(ssl));

    return true;
}

static int Sensor_Https_Write(unsigned char *post_data, int len, mbedtls_ssl_context *ssl)
{
    int ret = SENSOR_DATA_FAIL;
    /*
     * 3. Write the GET request
     */
    printf("Writing HTTP request\n\n");

    if ((ret = mbedtls_ssl_write(ssl, post_data, len)) <= 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_TIMEOUT)
        {
            printf(" failed\n  ! mbedtls_ssl_write returned -0x%x\n\n", -ret);
            ret = SENSOR_DATA_SOCKET_FAIL;
        }
        goto exit;
    }

    len = ret;
    printf("%d bytes written\n\n", len);
    // tracer_drct_printf("%d bytes written\n\n%s", len, (char *) post_data);
    ret = SENSOR_DATA_OK;

exit:
    return ret;
}



#include "blewifi_http_ota.h"

static int Sensor_Https_Parsing(unsigned char *haystack)
{
    int length;
    char URL[128] = {0};
    const char *PosStart = (const char *)haystack;
    const char *PosEnd;
    const char *NeedleStart = "downloadUrl";
    const char *NeedleEnd = "\",\"";


    if ((PosStart=strstr(PosStart,NeedleStart)) != NULL)
    {
        // Assign String to another
        PosEnd = PosStart;
        // Search the match string
        if ((PosEnd=strstr(PosEnd, NeedleEnd)) != NULL)
        {
            // Calcuate the length
            length = PosEnd - PosStart;

            memset(URL, '\0', sizeof(URL));
            // Copy string to destination string
            strncpy(URL, (PosStart + (strlen(NeedleStart) + strlen(NeedleEnd))), (length - (strlen(NeedleStart) + strlen(NeedleEnd))));


            uint8_t ubaSHA256CalcStrBuf[SCRT_SHA_256_OUTPUT_LEN];

            HttpPostData_t PostContentData;
            PostContentData.TimeStamp =  BleWifi_SntpGetRawData();

            if (SENSOR_DATA_FAIL == Sensor_Sha256_Value_OTA(&PostContentData, ubaSHA256CalcStrBuf))
            {
                printf("\n SENSOR_DATA_FAIL \n");
                return SENSOR_DATA_FAIL;
            }

            int iOffset = 0;
            char baSha256Buf[68] = {0};

            for(int i = 0; i < SCRT_SHA_256_OUTPUT_LEN; i++)
            {
                iOffset += snprintf(baSha256Buf + iOffset, sizeof(baSha256Buf), "%02x", ubaSHA256CalcStrBuf[i]);
            }

            memset(OTA_FULL_URL,0x00, sizeof(OTA_FULL_URL));
            sprintf(OTA_FULL_URL, OTA_DATA_URL, URL, g_tHttpPostContent.ubaDeviceId, PostContentData.TimeStamp, baSha256Buf);

            BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_OTA_MODE, true);
       }
    }

    return SENSOR_DATA_OK;
}

static int Sensor_Https_Parsing_ErrorCode(unsigned char *haystack)
{
    int length;
    char u8ErrorCode[16] = {0};
    const char *PosStart = (const char *)haystack;
    const char *PosEnd;
    const char *NeedleStart = "error";
    const char *NeedleEnd = ",\"";
    const char *NeedleStr = "\":";


    if ((PosStart=strstr(PosStart,NeedleStart)) != NULL)
    {
        // Assign String to another
        PosEnd = PosStart;
        // Search the match string
        if ((PosEnd=strstr(PosEnd, NeedleEnd)) != NULL)
        {
            // Calcuate the length
            length = PosEnd - PosStart;

            memset(u8ErrorCode, '\0', sizeof(u8ErrorCode));
            // Copy string to destination string
            strncpy(u8ErrorCode, (PosStart + (strlen(NeedleStart) + strlen(NeedleStr))), (length - (strlen(NeedleStart) + strlen(NeedleStr))));

            printf("Error Code: %s\r\n", u8ErrorCode);
        }
    }

    return SENSOR_DATA_OK;
}

static int Sensor_Https_Read(mbedtls_ssl_context *ssl)
{
    int ret = 0, len = 0;
    int ReturnValue = SENSOR_DATA_FAIL;

    /*
     * 7. Read the HTTP response
     */
    printf("Reading HTTP response\r\n");
    do
    {
        len = BUFFER_SIZE - 1;
        memset(ubHttpsReadBuf, 0, BUFFER_SIZE);
        ret = mbedtls_ssl_read(ssl, ubHttpsReadBuf, len);

        if (ret < 0)
        {
            if (ret != MBEDTLS_ERR_SSL_TIMEOUT)
            {
                ReturnValue = SENSOR_DATA_SOCKET_FAIL;
            }
            printf( "mbedtls_ssl_read returned -0x%x\r\n", -ret);
            break;
        }

        if (ret == 0)
        {
            ReturnValue = SENSOR_DATA_SOCKET_FAIL;
            printf( "connection closed\r\n");
            break;
        }

        printf("\n\n%d bytes read\n\n", ret);
        // tracer_drct_printf("\n\n%d bytes read\n\n%s", ret, (char *) ubHttpsReadBuf);
        if (ret != len)
        {
            ReturnValue = SENSOR_DATA_OK;
            break;
        }

    }while(1);

    if (ReturnValue == SENSOR_DATA_OK)
    {
        Sensor_Https_Parsing_ErrorCode(ubHttpsReadBuf);
        Sensor_Https_Parsing(ubHttpsReadBuf);
    }

    return ReturnValue;
}

static int Sensor_Https_Destroy(mbedtls_ssl_context *ssl)
{
    int ret = 0;
    printf("\nSSL client ends\n");

    ret = mbedtls_ssl_close_notify( ssl );

    return ret;
}

#if 1   // Terence, implement new retry flow
static int Sensor_Https_Post(unsigned char *post_data, int len, int isDestroy_if_post_fail)
#else
static int Sensor_Https_Post(unsigned char *post_data, int len)
#endif
{
    int ReturnValue = SENSOR_DATA_FAIL;

    static uint8_t u8Establish = 0;
    static mbedtls_net_context server_fd;
    static mbedtls_ssl_context ssl;
    static mbedtls_ssl_config conf;
    static mbedtls_x509_crt cacert;
    static mbedtls_ctr_drbg_context ctr_drbg;
    static mbedtls_entropy_context entropy;
    static uint32_t u32PostSuccLastTickMsec = 0;
    static int32_t  s32PostSuccLastNumOverFlow = 0;
    uint32_t u32CurTickMsec;
    int32_t  s32CurNumOverFlow;

    if(u8Establish == 1)
    {
        if (true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_CHANGE_HTTPURL))
        {
            printf(" Change URL so need to re-establiash socket...\n");

            BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_CHANGE_HTTPURL, false);
            u8Establish = 0;
        }
#if 1   // if idle over 3 min from last post success, destroy connect and re-create it.
        else
        {
            osKernelSysTickEx( &u32CurTickMsec, &s32CurNumOverFlow);
            printf("current : tick = [%d], overflow = [%d]\r\n", u32CurTickMsec, s32CurNumOverFlow);

            if ( (s32CurNumOverFlow - s32PostSuccLastNumOverFlow) >= 2 )
            {
                printf("L%d : Idle timeout so need to re-establiash socket...\n", __LINE__);
                u8Establish = 0;
            }
            else if ( (s32CurNumOverFlow - s32PostSuccLastNumOverFlow) == 1 )
            {
                if ( u32CurTickMsec >= u32PostSuccLastTickMsec )
                {
                    printf("L%d : Idle timeout so need to re-establiash socket...\n", __LINE__);
                    u8Establish = 0;
                }
                else if ( ((0xFFFFFFFF - u32PostSuccLastTickMsec) + u32CurTickMsec) >= HTTPS_IDLE_TIMEOUT )
                {
                    printf("L%d : Idle timeout (%d) so need to re-establiash socket...\n", __LINE__, u32CurTickMsec + 0xFFFFFFFF - u32PostSuccLastTickMsec);
                    u8Establish = 0;
                }
            }
            else if ( (s32CurNumOverFlow - s32PostSuccLastNumOverFlow) == 0 && (u32CurTickMsec - u32PostSuccLastTickMsec) >= HTTPS_IDLE_TIMEOUT )
            {
                printf("L%d : Idle timeout (%d) so need to re-establiash socket...\n", __LINE__, u32CurTickMsec - u32PostSuccLastTickMsec);
                u8Establish = 0;
            }
        }
#endif
        if ( u8Establish == 0 )
        {
            /* Destroy */
            if (0 != ((Sensor_Https_Destroy(&ssl))))
            {
                printf("Destroy Failure\n\n");
            }

            mbedtls_net_free( &server_fd );
            mbedtls_x509_crt_free( &cacert );
            mbedtls_ssl_free( &ssl );
            mbedtls_ssl_config_free( &conf );
            mbedtls_ctr_drbg_free( &ctr_drbg );
            mbedtls_entropy_free( &entropy );
        }
    }

    if (0 == u8Establish )
    {
        /* Init */
        if (true != (Sensor_Https_Init(&server_fd, &ssl, &conf, &cacert, &ctr_drbg, &entropy)))
        {
            printf("Sensor_Https_Init Failure\r\n");
#if 1   // Terence, implement new retry flow
            ReturnValue = SENSOR_DATA_HTTPS_INIT_FAIL;
#endif
            goto exit;
        }

        /* Establish */
        if (true != (Sensor_Https_Establish(&server_fd, &ssl, &conf, &cacert, &ctr_drbg)))
        {
            printf("Sensor_Https_Establish Failure\r\n");
#if 1   // Terence, implement new retry flow
            ReturnValue = SENSOR_DATA_HTTPS_ESTABLISH_FAIL;
#endif
            goto exit;
        }

        u8Establish = 1;
    }

    /* Write */
    ReturnValue = Sensor_Https_Write(post_data, len, &ssl);
    if (SENSOR_DATA_OK != ReturnValue)
    {
        printf("Writing HTTP request Failure \n\n");
#if 1   // Terence, implement new retry flow
        if ( isDestroy_if_post_fail == 1 || ReturnValue == SENSOR_DATA_SOCKET_FAIL )
        {
            u8Establish = 0;
        }
#else
        u8Establish = 0;
#endif
    }
    else
    {
        /* Read */
        ReturnValue = Sensor_Https_Read(&ssl);
        if (SENSOR_DATA_OK != ReturnValue)
        {
            printf("Reading HTTP response Failure \n\n");
#if 1   // Terence, implement new retry flow
            if ( isDestroy_if_post_fail == 1 || ReturnValue == SENSOR_DATA_SOCKET_FAIL )
            {
                u8Establish = 0;
            }
#else
            u8Establish = 0;
#endif
        }
        else
        {
#if 1   // if idle over 3 min from last post success, destroy connect and re-create it.
            osKernelSysTickEx( &u32PostSuccLastTickMsec, &s32PostSuccLastNumOverFlow);
            printf("post data ok : tick = [%d], overflow = [%d]\r\n", u32PostSuccLastTickMsec, s32PostSuccLastNumOverFlow);
#endif
            ReturnValue = SENSOR_DATA_OK;
        }
    }

    if (0 == u8Establish )
    {
        /* Destroy */
        if (0 != ((Sensor_Https_Destroy(&ssl))))
        {
            printf("Destroy Failure\n\n");
        }
    }

exit:

    if (0 == u8Establish)
    {
        mbedtls_net_free( &server_fd );
        mbedtls_x509_crt_free( &cacert );
        mbedtls_ssl_free( &ssl );
        mbedtls_ssl_config_free( &conf );
        mbedtls_ctr_drbg_free( &ctr_drbg );
        mbedtls_entropy_free( &entropy );
    }

    return ReturnValue;
}

void UpdateBatteryContent(void)
{
    int i = 0;
    float fVBatPercentage = 0;

    for (i = 0 ;i < SENSOR_MOVING_AVERAGE_COUNT ;i++)
    {
        fVBatPercentage = Sensor_Auxadc_VBat_Get();
    }
    g_fBatteryVoltage = fVBatPercentage;
}

int UpdatePostContent(HttpPostData_t *data)
{
    float fVBatPercentage = 0;

    fVBatPercentage = g_fBatteryVoltage;

    if(SENSOR_DATA_OK == Sensor_Data_Pop(&data->DoorStatus, &data->ContentType, &data->TimeStamp))
    {
        Sensor_Data_ReadIdxUpdate();

        /* Battery State */
        //ftoa(fVBatPercentage, data->Battery, 3);
        sprintf(data->Battery , "%.3f" , fVBatPercentage);

        /* FW Version */

        uint16_t uwProjectId;
        uint16_t uwChipId;
        uint16_t uwFirmwareId;

        ota_get_version(&uwProjectId, &uwChipId, &uwFirmwareId);

        memset(data->FwVersion, 0x00, sizeof(data->FwVersion));

        sprintf(data->FwVersion, CKS_FW_VERSION_FORMAT, uwProjectId, uwChipId, uwFirmwareId);

        /* WiFi MAC */
        wifi_config_get_mac_address(WIFI_MODE_STA, data->ubaMacAddr);

        /* WiFi RSSI */
        printf(" Original RSSI is %d \n", wpa_driver_netlink_get_rssi());
        data->rssi = wpa_driver_netlink_get_rssi() - RSSI_SHINFT;

        return SENSOR_DATA_OK;
    }

    return SENSOR_DATA_FAIL;
}

int Sensor_Updata_Post_Content(HttpPostData_t *PostContentData)
{
    /* Updata Post Content */
    memset(PostContentData, '0', sizeof(HttpPostData_t));

    if (SENSOR_DATA_FAIL == UpdatePostContent(PostContentData))
        return -1;

    return 0;
}

int Sensor_Sha256_Value_OTA(HttpPostData_t *PostContentData, uint8_t ubaSHA256CalcStrBuf[])
{
    int len = 0;
    unsigned char uwSHA256_Buff[BUFFER_SIZE_128] = {0};

    /* Combine https Post key */
    memset(uwSHA256_Buff, 0, BUFFER_SIZE_128);
    len = sprintf((char *)uwSHA256_Buff, SHA256_FOR_OTA_FORMAT, g_tHttpPostContent.ubaDeviceId, PostContentData->TimeStamp, g_tHttpPostContent.ubaApiKey);

    Sensor_SHA256_Calc(ubaSHA256CalcStrBuf, len, uwSHA256_Buff);

    return SENSOR_DATA_OK;

}

int Sensor_Sha256_Value(HttpPostData_t *PostContentData, uint8_t ubaSHA256CalcStrBuf[])
{
    int len = 0;
    unsigned char uwSHA256_Buff[BUFFER_SIZE_128] = {0};

    /* Combine https Post key */
    memset(uwSHA256_Buff, 0, BUFFER_SIZE_128);
    len = sprintf((char *)uwSHA256_Buff, CALC_AUTH_KEY_FORMAT, g_tHttpPostContent.ubaApiKey, g_tHttpPostContent.ubaDeviceId, PostContentData->TimeStamp);

    Sensor_SHA256_Calc(ubaSHA256CalcStrBuf, len, uwSHA256_Buff);

    return SENSOR_DATA_OK;

}

int Sensor_Https_Post_Content(HttpPostData_t *PostContentData, uint8_t ubaSHA256CalcStrBuf[])
{
    char content_buf[BUFFER_SIZE] = {0};
    char baSha256Buf[68] = {0};
    uint8_t ubaWiFiMacAddr[6];

    int i = 0, len = 0;
    int iOffset = 0;


    sscanf(g_tHttpPostContent.ubaChipId, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &ubaWiFiMacAddr[0], &ubaWiFiMacAddr[1], &ubaWiFiMacAddr[2], &ubaWiFiMacAddr[3], &ubaWiFiMacAddr[4], &ubaWiFiMacAddr[5]);

    /* Combine https Post Content */
	memset(content_buf, 0, BUFFER_SIZE);
    snprintf (content_buf, BUFFER_SIZE, POST_DATA_DEVICE_ID_FORMAT
                                        POST_DATA_SWITCH_FORMAT
                                        POST_DATA_BATTERY_FORMAT
                                        POST_DATA_FWVERSION_FORMAT
                                        POST_DATA_TYPE_FORMAT
                                        POST_DATA_CHIP_FORMAT
                                        POST_DATA_MACADDRESS_FORMAT
                                        POST_DATA_RSSI_FORMAT
                                      , g_tHttpPostContent.ubaDeviceId
                                      , PostContentData->TimeStamp
                                      // Door Status - Open   - switch on  - type = 2
                                      // Door Status - Close  - switch off - type = 3
                                      , PostContentData->DoorStatus ? "off": "on"
                                      , PostContentData->Battery
                                      , PostContentData->FwVersion
                                      // Door Status - Open   - switch on  - type = 2
                                      // Door Status - Close  - switch off - type = 3
                                      , PostContentData->ContentType
                                      , ubaWiFiMacAddr[0], ubaWiFiMacAddr[1], ubaWiFiMacAddr[2], ubaWiFiMacAddr[3], ubaWiFiMacAddr[4], ubaWiFiMacAddr[5]
                                      , PostContentData->ubaMacAddr[0], PostContentData->ubaMacAddr[1], PostContentData->ubaMacAddr[2], PostContentData->ubaMacAddr[3], PostContentData->ubaMacAddr[4], PostContentData->ubaMacAddr[5]
                                      , PostContentData->rssi
                                      );

    /* Combine https Post the content of authorization */
	for(i = 0; i < SCRT_SHA_256_OUTPUT_LEN; i++)
	{
		iOffset += snprintf(baSha256Buf + iOffset, sizeof(baSha256Buf), "%02x", ubaSHA256CalcStrBuf[i]);
	}

    len = snprintf((char *)g_ubSendBuf, BUFFER_SIZE, BODY_FORMAT ,
                                         g_tHostInfo.ubaHostInfoDIR,
                                         HDR_HOST_DIR,
                                         HDR_HOST,
                                         g_tHostInfo.ubaHostInfoURL,
                                         HDR_CT,
                                         HDR_AUTH,
                                         (baSha256Buf),
                                         HDR_CONTENT_LENGTH,
                                         strlen(content_buf),
                                         content_buf);

    printf(POST_DATA_INFO1_FORMAT
            , g_tHttpPostContent.ubaDeviceId
            , PostContentData->TimeStamp    //Goter
            , PostContentData->DoorStatus ? "off": "on"
            , PostContentData->Battery
        );

    printf(POST_DATA_INFO2_FORMAT"\r\n"
        , PostContentData->FwVersion
        , PostContentData->ContentType
        , PostContentData->rssi
        );

    return len;
}

int Sensor_Https_Post_On_Line(void)
{
    int len = 0;
    int PostResult = 0;
    int Count = 0;
    HttpPostData_t PostContentData;
    uint8_t ubaSHA256CalcStrBuf[SCRT_SHA_256_OUTPUT_LEN];


    if (SENSOR_DATA_OK == Sensor_Data_CheckEmpty())
        return SENSOR_DATA_OK;

    while (1)
    {
        if(true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_OTA_MODE))
        {
             osDelay(3000);
             continue;
        }

        // If this statement is true, it means ring buffer is empty.
        if (-1 == Sensor_Updata_Post_Content(&PostContentData))
        {
            return SENSOR_DATA_OK;
        }

        if (SENSOR_DATA_FAIL == Sensor_Sha256_Value(&PostContentData, ubaSHA256CalcStrBuf))
            return SENSOR_DATA_FAIL;

        len = Sensor_Https_Post_Content(&PostContentData, ubaSHA256CalcStrBuf);
        if (len == 0)
            return SENSOR_DATA_FAIL;

        Count = 0;
        BleWifi_Wifi_SetDTIM(0);


        lwip_one_shot_arp_enable();//Goter


        // Disable WIFI PS POLL
        //CtrlWifi_PsStateForce(STA_PS_AWAKE_MODE, 0);

        do
        {
#if 1   // Terence, implement new retry flow
            if (Count < (POST_FAILED_RETRY-1))
            {
                PostResult = Sensor_Https_Post(g_ubSendBuf, len, 0);
            }
            else
            {
                PostResult = Sensor_Https_Post(g_ubSendBuf, len, 1);
            }
#else
            PostResult = Sensor_Https_Post(g_ubSendBuf, len);
#endif
            Count++;

#if 1   // Terence, implement new retry flow
            if (SENSOR_DATA_HTTPS_INIT_FAIL == PostResult || SENSOR_DATA_HTTPS_ESTABLISH_FAIL == PostResult)
            {
                printf("\n\n Connect server fail...\n\n");
                break;
            }
#endif

            if (SENSOR_DATA_FAIL == PostResult)
                osDelay(100);

            // If device doesn't get IP, then break (no retry any more).
            if (false == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_GOT_IP))
            {
                printf("IP is gone , will not post count = %d\n",Count);
                break;
            }
        } while((SENSOR_DATA_OK != PostResult) && (Count < POST_FAILED_RETRY)); //Goter

        /*++++++++++++++++++++++ Goter ++++++++++++++++++++++++++++*/

        if (SENSOR_DATA_FAIL == PostResult)
        {

        }
        else if(SENSOR_DATA_OK == PostResult)
        {
            osTimerStop(g_tAppPostFailLedTimeOutTimerId);
            BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_OFFLINE, false);
            BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_NOT_CNT_SRV, false);

            BleWifi_Ctrl_LedStatusChange();

            g_nHrlyPostRetry = 0;
            g_nType1_2_3_Retry_counter = 0;

        }
        /*---------------------- Goter -----------------------------*/

        // Update Battery voltage for post data
        UpdateBatteryContent();

        BleWifi_Wifi_SetDTIM(BleWifi_Ctrl_DtimTimeGet());
        // Enable WIFI PS POLL
        //CtrlWifi_PsStateForce(STA_PS_NONE, 0);

        if(true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_OTA_MODE))
        {
            printf("Will Do OTA .....\n");
            printf("\n OTA_FULL_URL = %s\n",OTA_FULL_URL);
            BleWifi_Wifi_OtaTrigReq((uint8_t *)OTA_FULL_URL);
        }

        if (SENSOR_DATA_OK != PostResult)
        {/*++++++++++++++++++++++ Goter ++++++++++++++++++++++++++++*/
            // keep alive is fail, retry it after POST_DATA_TIME_RETRY
            if (PostContentData.ContentType == TIMER_POST)
            {
                if (true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_GOT_IP))
                {
                    if(g_nHrlyPostRetry == 0)
                    {
                        osTimerStop(g_tAppPostFailLedTimeOutTimerId);
                        osTimerStart(g_tAppPostFailLedTimeOutTimerId , BLEWIFI_POST_FAIL_LED_MAX);
                        BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_OFFLINE, true);
                        BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_NOT_CNT_SRV, false);
                        BleWifi_Ctrl_LedStatusChange();
                    }
                }

                if(Sensor_Data_CheckEmpty() == SENSOR_DATA_OK)
                {
                    //g_nHrlyPostRetry++;
                    g_nHrlyPostRetry += Count;
                    printf("Hrly post failed , retry count is %d .....\n",g_nHrlyPostRetry);

                    if( g_nHrlyPostRetry < MAX_HOUR_RETRY_POST)
                    {
                        osTimerStop(g_tAppCtrlHourlyHttpPostRetryTimer);
                        osTimerStart(g_tAppCtrlHourlyHttpPostRetryTimer, POST_DATA_TIME_RETRY);
                    }
                    else
                    {
                        /* When do wifi scan, set wifi auto connect is true */
                        //BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_OFFLINE, false);
                        //BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_NOT_CNT_SRV, false);
                        //BleWifi_Ctrl_LedStatusChange();

                        printf("offline, retry count over\n");
                        g_nHrlyPostRetry = 0;
                    }
                }
            }
            else
            {
                if (true == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_GOT_IP))
                {
                    if(g_nType1_2_3_Retry_counter == 0)
                    {
                        osTimerStop(g_tAppPostFailLedTimeOutTimerId);
                        osTimerStart(g_tAppPostFailLedTimeOutTimerId , BLEWIFI_POST_FAIL_LED_MAX);
                        BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_NOT_CNT_SRV, true);
                        BleWifi_Ctrl_LedStatusChange();
                    }
                }

                if(Sensor_Data_CheckEmpty() == SENSOR_DATA_OK)
                {
                    //g_nType1_2_3_Retry_counter++;
                    g_nType1_2_3_Retry_counter += Count;
                    printf("Type %d  post failed , retry count is %d .....\n",PostContentData.ContentType, g_nType1_2_3_Retry_counter);

                    if(g_nType1_2_3_Retry_counter < MAX_TYPE1_2_3_COUNT)
                    {
                        osTimerStop(g_tAppCtrlType1_2_3_HttpPostRetryTimer);
                        osTimerStart(g_tAppCtrlType1_2_3_HttpPostRetryTimer, POST_DATA_TIME_RETRY);
                    }
                    else
                    {
                        //BleWifi_Ctrl_EventStatusSet(BLEWIFI_CTRL_EVENT_BIT_NOT_CNT_SRV, false);
                        //BleWifi_Ctrl_LedStatusChange();

                        printf("Type %d  post failed , retry count over\n",PostContentData.ContentType);
                        g_nType1_2_3_Retry_counter = 0;
                    }
                }

            }

            /*---------------------- Goter -----------------------------*/
            //Sensor_Data_ResetBuffer();
            // If wifi disconnect, then jump out while-loop.
            if (false == BleWifi_Ctrl_EventStatusGet(BLEWIFI_CTRL_EVENT_BIT_GOT_IP))
                return SENSOR_DATA_FAIL;
        }
        osDelay(10);
    }
}

