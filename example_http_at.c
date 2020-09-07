#ifdef __EXAMPLE_HTTP__
#include "custom_feature_def.h"
#include "ql_type.h"
#include "ql_stdlib.h"
#include "ql_trace.h"
#include "ql_timer.h"
#include "ql_uart.h"
#include "ql_error.h"
#include "ql_gprs.h"
#include "ql_fs.h"
#include "ril.h"
#include "ril_network.h"

#define DEBUG_ENABLE 1
#if DEBUG_ENABLE > 0
#define DEBUG_PORT  UART_PORT1
#define DBG_BUF_LEN   512
static char DBG_BUFFER[DBG_BUF_LEN];
#define APP_DEBUG(FORMAT,...) {\
    Ql_memset(DBG_BUFFER, 0, DBG_BUF_LEN);\
    Ql_sprintf(DBG_BUFFER,FORMAT,##__VA_ARGS__); \
    if (UART_PORT2 == (DEBUG_PORT)) \
    {\
        Ql_Debug_Trace(DBG_BUFFER);\
    } else {\
        Ql_UART_Write((Enum_SerialPort)(DEBUG_PORT), (u8*)(DBG_BUFFER), Ql_strlen((const char *)(DBG_BUFFER)));\
    }\
}
#else
#define APP_DEBUG(FORMAT,...) 
#endif

s32 Default_atRsp_callback(char* line, u32 len, void* userdata)
{
    APP_DEBUG(line);
if (Ql_RIL_FindLine(line, len, "OK"))// find <CR><LF>OK<CR><LF>, <CR>OK<CR>，<LF>OK<LF>
{
return RIL_ATRSP_SUCCESS;
}
else if (Ql_RIL_FindLine(line, len, "ERROR") // find <CR><LF>ERROR<CR><LF>,
<CR>ERROR<CR>，<LF>ERROR<LF>
|| Ql_RIL_FindString(line, len, "+CME ERROR:")//fail
|| Ql_RIL_FindString(line, len, "+CMS ERROR:"))//fail
{
return RIL_ATRSP_FAILED;
}
//needs code to be able to wriite data apart from commands eg(POST data, server url, etc.)
return RIL_ATRSP_CONTINUE; //continue to wait
}

void proc_main_task(s32 taskId)

 ST_MSG msg;
 s32 ret; 
    
    // Register & open UART port
    Ql_UART_Register(UART_PORT1, Callback_UART_Hdlr, NULL);
    Ql_UART_Open(UART_PORT1, 115200, FC_NONE);

    Ql_UART_Register(UART_PORT2, Callback_UART_Hdlr, NULL);
    Ql_UART_Open(UART_PORT2, 115200, FC_NONE);

    APP_DEBUG("\r\nOpenCPU: example for HTTP programming\r\n");
    while (1)
    {
        Ql_OS_GetMessage(&msg);
        switch(msg.message)
        {
            case MSG_ID_RIL_READY:
                APP_DEBUG("<-- RIL is ready -->\r\n");
                Ql_RIL_Initialize();
            case MSG_ID_URC_INDICATION:
                switch (msg.param1)
                {
                case URC_SYS_INIT_STATE_IND:
                    APP_DEBUG("<-- Sys Init Status %d -->\r\n", msg.param2);
                    break;
                case URC_CFUN_STATE_IND:
                    APP_DEBUG("<-- CFUN Status:%d -->\r\n", msg.param2);
                    break;
                case URC_SIM_CARD_STATE_IND:
                    SIM_Card_State_Ind(msg.param2);
                    break;
                case URC_GSM_NW_STATE_IND:
                    APP_DEBUG("<-- GSM Network Status:%d -->\r\n", msg.param2);
                    break;
                case URC_GPRS_NW_STATE_IND:
                    APP_DEBUG("<-- GPRS Network Status:%d -->\r\n", msg.param2);
                    if (NW_STAT_REGISTERED == msg.param2 || NW_STAT_REGISTERED_ROAMING == msg.param2)
                    {
                        // GPRS is ready.
                        // Now, you can start to program http
                        ret = Ql_RIL_SendATCmd("AT", ql_strlen("AT"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT", cause:%d\r\n, ret);
                            return FALSE;
                        }
                         ret = Ql_RIL_SendATCmd("AT+QISEND=0", ql_strlen("AT+QISEND = 0"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QISEND=0", cause:%d\r\n, ret);
                            return FALSE;
                        }
                         ret = Ql_RIL_SendATCmd("AT+CREG?", ql_strlen("AT+CREG?"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+CREG?", cause:%d\r\n, ret);
                            return FALSE;
                        }
                         ret = Ql_RIL_SendATCmd("AT+CGATT=1", ql_strlen("AT+CGATT=1"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+CGATT=1", cause:%d\r\n, ret);
                            return FALSE;
                        }
                                          
                        ret = Ql_RIL_SendATCmd("AT+QICSGP=1,\"airtelgprs.com\"", ql_strlen("AT+QICSGP=1,\"airtelgprs.com\""),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QICSGP=1,\"airtelgprs.com\"", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+CSQ", ql_strlen("AT+CSQ"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+CSQ", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QIFGCNT=0", ql_strlen("AT+QIFGCNT=0"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QIFGCNT=0", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QIREGAPP", ql_strlen("AT+QIREGAPP"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QIREGAPP", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QIACT", ql_strlen("AT+QIACT"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QIACT", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QHTTPURL = 38,20", ql_strlen("AT+QHTTPURL = 38,20"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QHTTPURL = 38,20", cause:%d\r\n, ret);
                            return FALSE;
                        }
                          ret = Ql_RIL_SendATCmd("AT+QHTTPGET=20", ql_strlen("AT+QHTTPGET=20"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QHTTPGET=20", cause:%d\r\n, ret);
                            return FALSE;
                        }
                         ret = Ql_RIL_SendATCmd("AT+QHTTPREAD", ql_strlen("AT+QHTTPREAD"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QHTTPREAD", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QIDEACT", ql_strlen("AT+QIDEACT"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QIDEACT", cause:%d\r\n, ret);
                            return FALSE;
                        }
                        ret = Ql_RIL_SendATCmd("AT+QIPOWD=1", ql_strlen("AT+QIPOWD=1"),Default_atRsp_callback,NULL,0);
                        if (RIL_AT_SUCCESS != ret)
                        {
                            APP_DEBUG("Fail to send AT+QIPOWD=1", cause:%d\r\n, ret);
                            return FALSE;
                        }



                    }
                    break;
                }
                break;
            default:
                break;
        }
    }
}

