/*****************************************************************************
 *
 * Filename:
 * ---------
 *   example_sms.c 
 *
 * Project:
 * --------
 *   OpenCPU
 *
 * Description:
 * ------------
 *   This example demonstrates how to program SMS function in OpenCPU.
 *   To program SMS, the program has to wait for the URC message "SYS_STATE_SMSOK", which
 *   means SMS initialization finishes.
 *
 *   All debug information will be output through DEBUG port.
 *
 *
 * Usage:
 * ------
 *   Compile & Run:
 *
 *     Set "C_PREDEF=-D __EXAMPLE_SMS__" in gcc_makefile file. And compile the 
 *     app using "make clean/new".
 *     Download image bin to module to run.
 *
 * History:
 * --------
 *  2015/06/02   Vicent GAO    Add example of read/send con-sms by ROTVG00006-P06 
 ****************************************************************************/
#ifdef __EXAMPLE_SMS__
#include "ril.h"//Required for RIL feature
#include "ril_util.h"// Common usefule functions
#include "ril_sms.h" //Sms related functions
#include "ril_telephony.h"// Phone related functions
#include "ril_system.h" // Functions for system definitions
#include "ql_stdlib.h" //Contains functions aking to those in standard c library stlib.h 
#include "ql_error.h" // Return Values for API functionsare here 
#include "ql_trace.h" //To print Log Data
#include "ql_uart.h" //UART accessing functions
#include "ql_system.h" // Message looping structure in the code
#include "ql_memory.h" //Memory related Functions
#include "ql_timer.h" //Timer Related Functions

#if (defined(__OCPU_RIL_SUPPORT__) && defined(__OCPU_RIL_SMS_SUPPORT__))

//This code defines the APP_DEBUG function which shall be used
// to print log info to the debug port(UART PORT 1) 
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


/***********************************************************************
 * MACRO CONSTANT DEFINITIONS
************************************************************************/

#define CON_SMS_BUF_MAX_CNT   (1) //maximum number of buffers
#define CON_SMS_SEG_MAX_CHAR  (160) //maximum characters in a segment of a buffer
#define CON_SMS_SEG_MAX_BYTE  (4 * CON_SMS_SEG_MAX_CHAR) // maximumnumber of bytes; 4 bytes per character
#define CON_SMS_MAX_SEG       (7) //Maximum number of segments in a buffer

/***********************************************************************
 * STRUCT TYPE DEFINITIONS
************************************************************************/
typedef struct
{
    u8 aData[CON_SMS_SEG_MAX_BYTE]; // Stores Data
    u16 uLen; //Stores Data Length
} ConSMSSegStruct; // This structure stores a segment

typedef struct
{
    u16 uMsgRef;  // Reference of the message
    u8 uMsgTot; // Total number of segments

    ConSMSSegStruct asSeg[CON_SMS_MAX_SEG]; //Array of Segments
    bool abSegValid[CON_SMS_MAX_SEG]; // Stores if segment has valid message(TRUE = valid message)
} ConSMSStruct; //This structure stores a concatenated SMS

/***********************************************************************
 * FUNCTION DECLARATIONS
************************************************************************/
static bool ConSMSBuf_IsIntact(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon);
static bool ConSMSBuf_AddSeg(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon,u8 *pData,u16 uLen);
static s8 ConSMSBuf_GetIndex(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,ST_RIL_SMS_Con *pCon);
static bool ConSMSBuf_ResetCtx(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx);

/***********************************************************************
 * GLOBAL DATA DEFINITIONS
************************************************************************/
//Global Object
ConSMSStruct g_asConSMSBuf[CON_SMS_BUF_MAX_CNT];

/***********************************************************************
 * MACRO FUNCTION DEFINITIONS
************************************************************************/


/*****************************************************************************
 * FUNCTION
 *  ConSMSBuf_GetIndex
 *
 * DESCRIPTION
 *  This function is used to get available index in <pCSBuf>
 *  
 * PARAMETERS
 *  <pCSBuf>     The SMS index in storage,it starts from 1
 *  <uCSMaxCnt>  TRUE: The module should reply a SMS to the sender; FALSE: The module only read this SMS.
 *  <pCon>       The pointer of 'ST_RIL_SMS_Con' data
 *
 * RETURNS
 *  -1:   FAIL! Can not get available index
 *  OTHER VALUES: SUCCESS.
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/
//Gets the index(of the Concatenated SMS Buffer)that the current SMS is a part of.
//Return the Concatenated SMS index or an unused index
static s8 ConSMSBuf_GetIndex(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,ST_RIL_SMS_Con *pCon)
{
	u8 uIdx = 0;
	//Checking whether Buffer is null, or MaxCount is 0, Or the concatenated SMS is null
    if(    (NULL == pCSBuf) || (0 == uCSMaxCnt) 
        || (NULL == pCon)
      )
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,pCon:%x\r\n",pCSBuf,uCSMaxCnt,pCon);
        return -1;
    }

    //Checking if total messages is greater than allowed limit
    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return -1;
    }
     //Return index if both reference nunmber and total number of messages are same
	for(uIdx = 0; uIdx < uCSMaxCnt; uIdx++) 
	{
        if(    (pCon->msgRef == pCSBuf[uIdx].uMsgRef)
            && (pCon->msgTot == pCSBuf[uIdx].uMsgTot)
          )
        {
            return uIdx;
        }
	}

    //Return the first unused record
	for (uIdx = 0; uIdx < uCSMaxCnt; uIdx++)
	{
		if (0 == pCSBuf[uIdx].uMsgTot)  
		{
            pCSBuf[uIdx].uMsgTot = pCon->msgTot;
            pCSBuf[uIdx].uMsgRef = pCon->msgRef;
            
			return uIdx;
		}
	}

    APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! No avail index in ConSMSBuf,uCSMaxCnt:%d\r\n",uCSMaxCnt);
    
	return -1;
}

/*****************************************************************************
 * FUNCTION
 *  ConSMSBuf_AddSeg
 *
 * DESCRIPTION
 *  This function is used to add segment in <pCSBuf>
 *  
 * PARAMETERS
 *  <pCSBuf>     The SMS index in storage,it starts from 1
 *  <uCSMaxCnt>  TRUE: The module should reply a SMS to the sender; FALSE: The module only read this SMS.
 *  <uIdx>       Index of <pCSBuf> which will be stored
 *  <pCon>       The pointer of 'ST_RIL_SMS_Con' data
 *  <pData>      The pointer of CON-SMS-SEG data
 *  <uLen>       The length of CON-SMS-SEG data
 *
 * RETURNS
 *  FALSE:   FAIL!
 *  TRUE: SUCCESS.
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/

//This functions adds a new segment to the Buffer Index Obtained
static bool ConSMSBuf_AddSeg(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon,u8 *pData,u16 uLen)
{
    u8 uSeg = 1;
    //Checks wherher Buffer is null, MAxCount is 0, Index is greater than Maxcount, Message given or the data in it is null
    // or if the data given exceeds max length 
    if(    (NULL == pCSBuf) || (0 == uCSMaxCnt) 
        || (uIdx >= uCSMaxCnt)
        || (NULL == pCon)
        || (NULL == pData)
        || (uLen > (CON_SMS_SEG_MAX_CHAR * 4))
      )
    {
        APP_DEBUG("Enter ConSMSBuf_AddSeg,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d,pCon:%x,pData:%x,uLen:%d\r\n",pCSBuf,uCSMaxCnt,uIdx,pCon,pData,uLen);
        return FALSE;
    }

    //Checks if total umber of messages is greater than the max seg allowed
    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return FALSE;
    }

    //Assigning Segment to the Buffer Index
    uSeg = pCon->msgSeg;

    //Declaring the segment as having valid message
    pCSBuf[uIdx].abSegValid[uSeg-1] = TRUE;
    
    //Copying Data
    Ql_memcpy(pCSBuf[uIdx].asSeg[uSeg-1].aData,pData,uLen);
    
    //Assigning data length
    pCSBuf[uIdx].asSeg[uSeg-1].uLen = uLen;
    
	return TRUE;
}

/*****************************************************************************
 * FUNCTION
 *  ConSMSBuf_IsIntact
 *
 * DESCRIPTION
 *  This function is used to check the CON-SMS is intact or not
 *  
 * PARAMETERS
 *  <pCSBuf>     The SMS index in storage,it starts from 1
 *  <uCSMaxCnt>  TRUE: The module should reply a SMS to the sender; FALSE: The module only read this SMS.
 *  <uIdx>       Index of <pCSBuf> which will be stored
 *  <pCon>       The pointer of 'ST_RIL_SMS_Con' data
 *
 * RETURNS
 *  FALSE:   FAIL!
 *  TRUE: SUCCESS.
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/

// This function checks if the Concatenated SMS is intact
static bool ConSMSBuf_IsIntact(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon)
{
    u8 uSeg = 1;
	// Invalid Parameter checking as in the previous functions
    if(    (NULL == pCSBuf) 
        || (0 == uCSMaxCnt) 
        || (uIdx >= uCSMaxCnt)
        || (NULL == pCon)
      )
    {
        APP_DEBUG("Enter ConSMSBuf_IsIntact,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d,pCon:%x\r\n",pCSBuf,uCSMaxCnt,uIdx,pCon);
        return FALSE;
    }

    //Total messages checking as in previous functions
    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return FALSE;
    }
        
	//Checks if all the segments of the message have valid message segments
    for (uSeg = 1; uSeg <= (pCon->msgTot); uSeg++)
	{
        if(FALSE == pCSBuf[uIdx].abSegValid[uSeg-1])
        {
            APP_DEBUG("Enter ConSMSBuf_IsIntact,FAIL! uSeg:%d has not received!\r\n",uSeg);
            return FALSE;
        }
	}
    
    return TRUE;
}

/*****************************************************************************
 * FUNCTION
 *  ConSMSBuf_ResetCtx
 *
 * DESCRIPTION
 *  This function is used to reset ConSMSBuf context
 *  
 * PARAMETERS
 *  <pCSBuf>     The SMS index in storage,it starts from 1
 *  <uCSMaxCnt>  TRUE: The module should reply a SMS to the sender; FALSE: The module only read this SMS.
 *  <uIdx>       Index of <pCSBuf> which will be stored
 *
 * RETURNS
 *  FALSE:   FAIL!
 *  TRUE: SUCCESS.
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/

//This function Resets the Buffer Index to default 
static bool ConSMSBuf_ResetCtx(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx)
{
    //Checking if any parameter is invalid as in the previous functions
    if(    (NULL == pCSBuf) || (0 == uCSMaxCnt) 
        || (uIdx >= uCSMaxCnt)
      )
    {
        APP_DEBUG("Enter ConSMSBuf_ResetCtx,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d\r\n",pCSBuf,uCSMaxCnt,uIdx);
        return FALSE;
    }
    
    //Default reset
    Ql_memset(&pCSBuf[uIdx],0x00,sizeof(ConSMSStruct));

    //TODO: Add special reset here
    
    return TRUE;
}

/*****************************************************************************
 * FUNCTION
 *  SMS_Initialize
 *
 * DESCRIPTION
 *  Initialize SMS environment.
 *  
 * PARAMETERS
 *  VOID
 *
 * RETURNS
 *  TRUE:  This function works SUCCESS.
 *  FALSE: This function works FAIL!
 *****************************************************************************/
static bool SMS_Initialize(void)
{
    s32 iResult = 0;
    u8  nCurrStorage = 0;
    u32 nUsed = 0;
    u32 nTotal = 0;
    
    // Set SMS storage:
    // By default, short message is stored into SIM card. You can change the storage to ME if needed, or
    // you can do it again to make sure the short message storage is SIM card.
    #if 1
    {
        
        //Set storage for SMS
        iResult = RIL_SMS_SetStorage(RIL_SMS_STORAGE_TYPE_SM,&nUsed,&nTotal);
        if (RIL_ATRSP_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to set SMS storage, cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Set SMS storage to SM, nUsed:%u,nTotal:%u -->\r\n", nUsed, nTotal);

        //Get the SMS storage created
        iResult = RIL_SMS_GetStorage(&nCurrStorage, &nUsed ,&nTotal);
        if(RIL_ATRSP_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to get SMS storage, cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Check SMS storage: curMem=%d, used=%d, total=%d -->\r\n", nCurrStorage, nUsed, nTotal);
    }
    #endif

    // Enable new short message indication
    // By default, the auto-indication for new short message is enalbed. You can do it again to 
    // make sure that the option is open.
    #if 1
    {
        iResult = Ql_RIL_SendATCmd("AT+CNMI=2,1",Ql_strlen("AT+CNMI=2,1"),NULL,NULL,0);
        if (RIL_AT_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to send \"AT+CNMI=2,1\", cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Enable new SMS indication -->\r\n");
    }
    #endif

    // Delete all existed short messages (if needed)
    iResult = RIL_SMS_DeleteSMS(0, RIL_SMS_DEL_ALL_MSG);
    if (iResult != RIL_AT_SUCCESS)
    {
        APP_DEBUG("Fail to delete all messages, iResult=%d,cause:%d\r\n", iResult, Ql_RIL_AT_GetErrCode());
        return FALSE;
    }
    APP_DEBUG("Delete all existed messages\r\n");
    
    return TRUE;
}
/*
void SMS_TextMode_Read(u32 nIndex)
{
    s32 iResult;
    ST_RIL_SMS_TextInfo *pTextInfo = NULL;
    ST_RIL_SMS_DeliverParam *pDeliverTextInfo = NULL;
    ST_RIL_SMS_SubmitParam *pSubmitTextInfo = NULL;
    LIB_SMS_CharSetEnum eCharSet = LIB_SMS_CHARSET_GSM;
    
    pTextInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_TextInfo));
    if (NULL == pTextInfo)
    {
        return;
    }        

    Ql_memset(pTextInfo,0x00,sizeof(ST_RIL_SMS_TextInfo));
    iResult = RIL_SMS_ReadSMS_Text(nIndex, eCharSet, pTextInfo);
    if (iResult != RIL_AT_SUCCESS)
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("< Fail to read PDU SMS, cause:%d >\r\n", iResult);
        return;
    }        
    if (RIL_SMS_STATUS_TYPE_INVALID == (pTextInfo->status))
    {
        APP_DEBUG("<-- SMS[index=%d] doesn't exist -->\r\n", nIndex);
        return;
    }

    // Resolve the read short message
    if (LIB_SMS_PDU_TYPE_DELIVER == (pTextInfo->type))
    {
        pDeliverTextInfo = &((pTextInfo->param).deliverParam);
        APP_DEBUG("<-- Read short message (index:%u) with charset %d -->\r\n", nIndex, eCharSet);

        if(FALSE == pDeliverTextInfo->conPres) //Normal SMS
        {
            APP_DEBUG(
                "short message info: \r\n\tstatus:%u \r\n\ttype:%u \r\n\talpha:%u \r\n\tsca:%s \r\n\toa:%s \r\n\tscts:%s \r\n\tdata length:%u\r\ncp:0,cy:0,cr:0,ct:0,cs:0\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length)
           );
        }
        else
        {
            APP_DEBUG(
                "short message info: \r\n\tstatus:%u \r\n\ttype:%u \r\n\talpha:%u \r\n\tsca:%s \r\n\toa:%s \r\n\tscts:%s \r\n\tdata length:%u\r\ncp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length),
                    pDeliverTextInfo->con.msgType,
                    pDeliverTextInfo->con.msgRef,
                    pDeliverTextInfo->con.msgTot,
                    pDeliverTextInfo->con.msgSeg
           );
        }
        
        APP_DEBUG("\r\n\tmessage content:");
        APP_DEBUG("%s\r\n",(pDeliverTextInfo->data));
        APP_DEBUG("\r\n");
    }
    else if (LIB_SMS_PDU_TYPE_SUBMIT == (pTextInfo->type))
    {// short messages in sent-list of drafts-list
    } else {
        APP_DEBUG("<-- Unkown short message type! type:%d -->\r\n", (pTextInfo->type));
    }
    Ql_MEM_Free(pTextInfo);
}
*/


void SMS_TextMode_Send(void)
{
    s32 iResult;
    u32 nMsgRef;
    char strPhNum[] = "+919448630236\0";
    char strTextMsg[] = "Hello this is Himanshu, the following are test messages\0";
    char strConMsgSeg1[] = "Segment 1\0";
    char strConMsgSeg2[] = "Segment 2 \0";
    char strConMsgSeg3[] = "Segment 3\0";
    char strConMsgSeg4[] = "Segment 4\0";
    
    ST_RIL_SMS_SendExt sExt;

    //Initialize
    Ql_memset(&sExt,0x00,sizeof(sExt));

    APP_DEBUG("< Send Normal Text SMS begin... >\r\n");
    
    //Send a normal text
    iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, strTextMsg, Ql_strlen(strTextMsg), &nMsgRef);
    if (iResult != RIL_AT_SUCCESS)
    {   
        APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
        return;
    }
    APP_DEBUG("< Send Text SMS successfully, MsgRef:%u >\r\n", nMsgRef);

    APP_DEBUG("< Send English Concatenate Text SMS begin... >\r\n");

    //The 1st segment of CON-SMS
    sExt.conPres = TRUE;
    sExt.con.msgType = 0xFF; //If it's 0xFF: use default CON-SMS-TYPE
    sExt.con.msgRef = 52; //Different CON-SMS shall have different <msgRef>
    sExt.con.msgTot = 4;//Total Number of messages
    sExt.con.msgSeg = 1;
    
    iResult = RIL_SMS_SendSMS_Text_Ext(strPhNum,Ql_strlen(strPhNum),LIB_SMS_CHARSET_GSM,strConMsgSeg1,Ql_strlen(strConMsgSeg1),&nMsgRef,&sExt);
    if (iResult != RIL_AT_SUCCESS)
    {   
        APP_DEBUG( 
            "< Fail to send Text SMS, cause:%d,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
            iResult,
            sExt.con.msgType,
            sExt.con.msgRef,
            sExt.con.msgTot,
            sExt.con.msgSeg
        );
        
        return;
    }
    APP_DEBUG(
        "< Send Text SMS successfully, MsgRef:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
        nMsgRef,
        sExt.con.msgType,
        sExt.con.msgRef,
        sExt.con.msgTot,
        sExt.con.msgSeg
    ); 

    //The 2st segment of CON-SMS
    sExt.con.msgSeg = 2;
    iResult = RIL_SMS_SendSMS_Text_Ext(strPhNum,Ql_strlen(strPhNum),LIB_SMS_CHARSET_GSM,strConMsgSeg2,Ql_strlen(strConMsgSeg2),&nMsgRef,&sExt);
    if (iResult != RIL_AT_SUCCESS)
    {   
        APP_DEBUG(
            "< Fail to send Text SMS, cause:%d,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
            iResult,
            sExt.con.msgType,
            sExt.con.msgRef,
            sExt.con.msgTot,
            sExt.con.msgSeg
        );
        
        return;
    }
    APP_DEBUG(
        "< Send Text SMS successfully, MsgRef:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
        nMsgRef,
        sExt.con.msgType,
        sExt.con.msgRef,
        sExt.con.msgTot,
        sExt.con.msgSeg
    ); 

    //The 3st segment of CON-SMS
    sExt.con.msgSeg = 3;
    iResult = RIL_SMS_SendSMS_Text_Ext(strPhNum,Ql_strlen(strPhNum),LIB_SMS_CHARSET_GSM,strConMsgSeg3,Ql_strlen(strConMsgSeg3),&nMsgRef,&sExt);
    if (iResult != RIL_AT_SUCCESS)
    {   
        APP_DEBUG(
            "< Fail to send Text SMS, cause:%d,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
            iResult,
            sExt.con.msgType,
            sExt.con.msgRef,
            sExt.con.msgTot,
            sExt.con.msgSeg
        );
        
        return;
    }
    APP_DEBUG(
        "< Send Text SMS successfully, MsgRef:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
        nMsgRef,
        sExt.con.msgType,
        sExt.con.msgRef,
        sExt.con.msgTot,
        sExt.con.msgSeg
    ); 

    //The 4st segment of CON-SMS
    sExt.con.msgSeg = 4;
    iResult = RIL_SMS_SendSMS_Text_Ext(strPhNum,Ql_strlen(strPhNum),LIB_SMS_CHARSET_GSM,strConMsgSeg4,Ql_strlen(strConMsgSeg4),&nMsgRef,&sExt);
    if (iResult != RIL_AT_SUCCESS)
    {   
        APP_DEBUG( 
            "< Fail to send Text SMS, cause:%d,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
            iResult,
            sExt.con.msgType,
            sExt.con.msgRef,
            sExt.con.msgTot,
            sExt.con.msgSeg
        );
        
        return;
    }
    APP_DEBUG(
        "< Send Text SMS successfully, MsgRef:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d >\r\n", 
        nMsgRef,
        sExt.con.msgType,
        sExt.con.msgRef,
        sExt.con.msgTot,
        sExt.con.msgSeg
    ); 

   
    
}
/*
void SMS_PDUMode_Read(u32 nIndex)
{
    s32 iResult;
    ST_RIL_SMS_PDUInfo *pPDUInfo = NULL;

    pPDUInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_PDUInfo));
    if (NULL == pPDUInfo)
    {
        return;
    }
    
    iResult = RIL_SMS_ReadSMS_PDU(nIndex, pPDUInfo);
    if (RIL_AT_SUCCESS != iResult)
    {
        Ql_MEM_Free(pPDUInfo);
        APP_DEBUG("< Fail to read PDU SMS, cause:%d >\r\n", iResult);
        return;
    }

    do
    {
        if (RIL_SMS_STATUS_TYPE_INVALID == (pPDUInfo->status))
        {
            APP_DEBUG("<-- SMS[index=%d] doesn't exist -->\r\n", nIndex);
            break;
        }

        APP_DEBUG("<-- Send Text SMS[index=%d] successfully -->\r\n", nIndex);
        APP_DEBUG("status:%u,data length:%u\r\n", (pPDUInfo->status), (pPDUInfo->length));
        APP_DEBUG("data = %s\r\n",(pPDUInfo->data));
    } while(0);
    
    Ql_MEM_Free(pPDUInfo);
}

void SMS_PDUMode_Send(void)
{
    s32 iResult;
    u32 nMsgRef;
    char pduStr[] = "0011000D91683156911591F60000B00331D90C";
    iResult = RIL_SMS_SendSMS_PDU(pduStr, Ql_strlen(pduStr), &nMsgRef);
    if (RIL_AT_SUCCESS != iResult)
    {
        APP_DEBUG("< Fail to send PDU SMS, cause:%d >\r\n", iResult);
        return;
    }
    APP_DEBUG("< Send PDU SMS successfully, MsgRef:%u >\r\n", nMsgRef);

}
*/
/*****************************************************************************
 * FUNCTION
 *  Hdlr_RecvNewSMS
 *
 * DESCRIPTION
 *  The handler function of new received SMS.
 *  
 * PARAMETERS
 *  <nIndex>     The SMS index in storage,it starts from 1
 *  <bAutoReply> TRUE: The module should reply a SMS to the sender; 
 *               FALSE: The module only read this SMS.
 *
 * RETURNS
 *  VOID
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/
static void Hdlr_RecvNewSMS(u32 nIndex, bool bAutoReply)
{
    s32 iResult = 0; // To capture results of function calls
    u32 uMsgRef = 0; //Reference to reply message
    ST_RIL_SMS_TextInfo *pTextInfo = NULL; // Textinfo Object
    ST_RIL_SMS_DeliverParam *pDeliverTextInfo = NULL; //Deliver Parameter Object
    char aPhNum[RIL_SMS_PHONE_NUMBER_MAX_LEN] = {0,}; //Phone number
    const char aReplyCon[] = {"Module has received SMS."}; //This is the auto reply message that you eant to send
    bool bResult = FALSE;
    //Dynamic Memory allocation to pTextInfo, checking if memory is allocated and setting the memory to blank
    pTextInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_TextInfo));
    if (NULL == pTextInfo)
    {
        APP_DEBUG("%s/%d:Ql_MEM_Alloc FAIL! size:%u\r\n", sizeof(ST_RIL_SMS_TextInfo), __func__, __LINE__);
        return;
    }
    Ql_memset(pTextInfo, 0x00, sizeof(ST_RIL_SMS_TextInfo));
    //Reading the message received
    iResult = RIL_SMS_ReadSMS_Text(nIndex, LIB_SMS_CHARSET_GSM, pTextInfo);
    if (iResult != RIL_AT_SUCCESS)
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("Fail to read text SMS[%d], cause:%d\r\n", nIndex, iResult);
        return;
    }        
    //Checking if received SMS is valid
    if ((LIB_SMS_PDU_TYPE_DELIVER != (pTextInfo->type)) || (RIL_SMS_STATUS_TYPE_INVALID == (pTextInfo->status)))
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("WARNING: NOT a new received SMS.\r\n");    
        return;
    }
    
    pDeliverTextInfo = &((pTextInfo->param).deliverParam);    

    //If a concatenated SMS is refceived then the statements in the if block will execute
    if(TRUE == pDeliverTextInfo->conPres)  
    {
        s8 iBufIdx = 0; //To store buffer index
        u8 uSeg = 0; //To store segment numebr
        u16 uConLen = 0; // To store the length of the concatenated SMS

        //Getting an abvailable index in the buffer 
        iBufIdx = ConSMSBuf_GetIndex(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,&(pDeliverTextInfo->con));
        if(-1 == iBufIdx)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_GetIndex FAIL! Show this CON-SMS-SEG directly!\r\n");

            APP_DEBUG(
                "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length),
                    pDeliverTextInfo->con.msgType,
                    pDeliverTextInfo->con.msgRef,
                    pDeliverTextInfo->con.msgTot,
                    pDeliverTextInfo->con.msgSeg
            );
            APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));

            Ql_MEM_Free(pTextInfo);
        
            return;
        }

        //Adding a Segment to the Index gotten
        bResult = ConSMSBuf_AddSeg(
                    g_asConSMSBuf,
                    CON_SMS_BUF_MAX_CNT,
                    iBufIdx,
                    &(pDeliverTextInfo->con),
                    (pDeliverTextInfo->data),
                    (pDeliverTextInfo->length)
        );
        if(FALSE == bResult)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_AddSeg FAIL! Show this CON-SMS-SEG directly!\r\n");

            APP_DEBUG(
                "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                (pTextInfo->status),
                (pTextInfo->type),
                (pDeliverTextInfo->alpha),
                (pTextInfo->sca),
                (pDeliverTextInfo->oa),
                (pDeliverTextInfo->scts),
                (pDeliverTextInfo->length),
                pDeliverTextInfo->con.msgType,
                pDeliverTextInfo->con.msgRef,
                pDeliverTextInfo->con.msgTot,
                pDeliverTextInfo->con.msgSeg
            );
            APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));

            Ql_MEM_Free(pTextInfo);
        
            return;
        }

        //Checking if the buffer index's segments are intact after adding the segment
        bResult = ConSMSBuf_IsIntact(
                    g_asConSMSBuf,
                    CON_SMS_BUF_MAX_CNT,
                    iBufIdx,
                    &(pDeliverTextInfo->con)
        );
        if(FALSE == bResult)
        {
            APP_DEBUG(
                "Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_IsIntact FAIL! Waiting. cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                pDeliverTextInfo->con.msgType,
                pDeliverTextInfo->con.msgRef,
                pDeliverTextInfo->con.msgTot,
                pDeliverTextInfo->con.msgSeg
            );
            //Freeing the memory
            Ql_MEM_Free(pTextInfo);

            return;
        }

        //Show the CON-SMS
        APP_DEBUG(
            "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s",
            (pTextInfo->status),
            (pTextInfo->type),
            (pDeliverTextInfo->alpha),
            (pTextInfo->sca),
            (pDeliverTextInfo->oa),
            (pDeliverTextInfo->scts)
        );
        
        uConLen = 0;
        for(uSeg = 1; uSeg <= pDeliverTextInfo->con.msgTot; uSeg++)
        {
            uConLen += g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].uLen;
        }

        APP_DEBUG(",data length:%u",uConLen);
        APP_DEBUG("\r\n"); //Print CR LF 
        //The for loop iterates over all the segments of the index to display them
        for(uSeg = 1; uSeg <= pDeliverTextInfo->con.msgTot; uSeg++)
        {
            APP_DEBUG("data = %s ,len = %d",
                g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].aData,
                g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].uLen
            );
        }

        APP_DEBUG("\r\n"); //Print CR LF

        //Reset CON-SMS context
        bResult = ConSMSBuf_ResetCtx(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,iBufIdx);
        if(FALSE == bResult)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_ResetCtx FAIL! iBufIdx:%d\r\n",iBufIdx);
        }

        //Freeing the memory
        Ql_MEM_Free(pTextInfo);
        
        return;
    }
    
    //These lines will execute if the sms is not a concatenated sms
    APP_DEBUG("<-- RIL_SMS_ReadSMS_Text OK. eCharSet:LIB_SMS_CHARSET_GSM,nIndex:%u -->\r\n",nIndex);
    //Displaying the SMS parameters
    APP_DEBUG("status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u\r\n",
        pTextInfo->status,
        pTextInfo->type,
        pDeliverTextInfo->alpha,
        pTextInfo->sca,
        pDeliverTextInfo->oa,
        pDeliverTextInfo->scts,
        pDeliverTextInfo->length);
    //Displaying SMS Data
    APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));
    
    //Copying phone number
    Ql_strcpy(aPhNum, pDeliverTextInfo->oa);
    Ql_MEM_Free(pTextInfo);
    
    //If autoreply is enabled, then the statements in the if statement will execute
    if (bAutoReply)
    {
        // Put the nimber of the carrier, as it should not message back to an automated message by carrier
        if (!Ql_strstr(aPhNum, "10086"))  
        {
            APP_DEBUG("<-- Replying SMS... -->\r\n");
            //Sending SMS
            iResult = RIL_SMS_SendSMS_Text(aPhNum, Ql_strlen(aPhNum),LIB_SMS_CHARSET_GSM,
            (u8*)aReplyCon,Ql_strlen(aReplyCon),&uMsgRef);
            if (iResult != RIL_AT_SUCCESS)
            {
                APP_DEBUG("RIL_SMS_SendSMS_Text FAIL! iResult:%u\r\n",iResult);
                return;
            }
            APP_DEBUG("<-- RIL_SMS_SendTextSMS OK. uMsgRef:%d -->\r\n", uMsgRef);
        }
    }
    return;
}



static void InitSerialPort(void)
{
    s32 iResult = 0;

    //Register & Open UART port
    iResult = Ql_UART_Register(UART_PORT1, CallBack_UART_Hdlr, NULL);
    if (iResult != QL_RET_OK)
    {
        Ql_Debug_Trace("Fail to register UART port[%d]:%d\r\n",UART_PORT1);
    }
    
    iResult = Ql_UART_Open(UART_PORT1, 115200, FC_NONE);
    if (iResult != QL_RET_OK)
    {
        Ql_Debug_Trace("Fail to open UART port[%d], baud rate:115200, FC_NONE\r\n", UART_PORT1);
    }    
}


/*****************************************************************************
 * FUNCTION
 *  proc_main_task
 *
 * DESCRIPTION
 *  Entry function of this example.
 *  
 * PARAMETERS
 *  <iTaskID>  Task ID
 *
 * RETURNS
 *  VOID
 *
 * NOTE
 *  1. This is the entrance to application
 *****************************************************************************/
void proc_main_task(s32 iTaskID)
{
    s32 iResult = 0; //
    ST_MSG taskMsg; //To store messages from the core

    //Register & open UART port
    InitSerialPort();
    
    
    APP_DEBUG("OpenCPU: SMS Example\r\n");

    // START MESSAGE LOOP OF THIS TASK
    while (TRUE) 
    {
        s32 i = 0;
        //Setting memory of a message type and getting message from core   
        Ql_memset(&taskMsg, 0x0, sizeof(ST_MSG));
        Ql_OS_GetMessage(&taskMsg);
        switch (taskMsg.message)
        {
        case MSG_ID_RIL_READY:
            {
                //If RIL is ready then
                APP_DEBUG("<-- RIL is ready -->\r\n");
                Ql_RIL_Initialize(); // MUST call this function, contains important AT commands to start with

                //Reset the indices of the entire buffer
                for(i = 0; i < CON_SMS_BUF_MAX_CNT; i++)
                {
                
                    ConSMSBuf_ResetCtx(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,i);
                }
                
                break;
            }
        //If a URC(showing status of various Radio related sectors) message is reccceived    
        case MSG_ID_URC_INDICATION:
            switch (taskMsg.param1)
            {
            //SMS Module Status Indication    
            case URC_SYS_INIT_STATE_IND:
                {
                    APP_DEBUG("<-- Sys Init Status %d -->\r\n", taskMsg.param2);
                    if (SYS_STATE_SMSOK == taskMsg.param2)
                    {
                        APP_DEBUG("\r\n<-- SMS module is ready -->\r\n");
                        APP_DEBUG("\r\n<-- Initialize SMS-related options -->\r\n");
                        //If SMS module is ready, initialize sms functions
                        iResult = SMS_Initialize();         
                        if (!iResult)
                        {
                            APP_DEBUG("Fail to initialize SMS\r\n");
                        }
                        //Send the messages to be sent
                        SMS_TextMode_Send();
                    }
                    break;
                }
            //Sim Card Status messages
            case URC_SIM_CARD_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- SIM Card Status:%d -->\r\n", taskMsg.param2);
                }
                break;

            //GSM Network Status message    
            case URC_GSM_NW_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- GSM Network Status:%d -->\r\n", taskMsg.param2);
                    break;
                }
            //GPRS Network Status message
            case URC_GPRS_NW_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- GPRS Network Status:%d -->\r\n", taskMsg.param2);
                    break;
                }
            //Calling Functionality Status Messages
            case URC_CFUN_STATE_IND:
                {

                    APP_DEBUG("\r\n<-- CFUN Status:%d -->\r\n", taskMsg.param2);
                    break;
                }
            //Incoming Call Indication
            case URC_COMING_CALL_IND:
                {
                    ST_ComingCall* pComingCall = (ST_ComingCall*)(taskMsg.param2);
                    APP_DEBUG("\r\n<-- Coming call, number:%s, type:%d -->\r\n", pComingCall->phoneNumber, pComingCall->type);
                    break;
               }
            //New SMS Indiaction. Recall that we had initialised it in SMS_Initialize()
            case URC_NEW_SMS_IND:
                {
                    APP_DEBUG("\r\n<-- New SMS Arrives: index=%d\r\n", taskMsg.param2);

                    //Call the New Message Handling Function
                    Hdlr_RecvNewSMS((taskMsg.param2), FALSE);
                    break;
                }
            //Module Voltage Indication Message
            case URC_MODULE_VOLTAGE_IND:
                {
                    APP_DEBUG("\r\n<-- VBatt Voltage Ind: type=%d\r\n", taskMsg.param2);
                    break;
                }

            default:
                break;
            }
            break;

        default:
            break;
        }
    }
}

#endif  // __OCPU_RIL_SUPPORT__ && __OCPU_RIL_SMS_SUPPORT__
#endif  // __EXAMPLE_SMS__

