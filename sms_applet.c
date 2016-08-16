#include <stdio.h>
#include <stdlib.h>
#include <duster_applets.h>
#include "psm_wrapper.h"
#include "Dialer_Task.h"
#include "ci_api_types.h"
#include "telmsg.h"
#include "../../apx_ui/cgi/inc/cgi.h"
#include <sms_applet.h>
#include "phonebook_applet.h"


#define UDH_length  6
#define Msg_Numer  1
#define TEL_AT_SMS_MAX_LEN  Msg_Numer*640+80
#define MAX_SINGLE_SMS_LEN  140

#define PSM1_max 200
#define PSM2_max 400
#define PSM3_max 600
#define PSM4_max 800
#define PSM5_max 1000

#define longSMS_enable

#define SMS_TIMEOUT 200*300	/*180 seconds*/

extern GlobalParameter_Duster2Dialer gUI2DialerPara;
extern GlobalParameter_Dialer2Duster gDialer2UIPara;
extern OSMsgQRef 	gsATPMsgQ[];
extern UINT32  		gMem1MsgNum;
extern UINT32  		gMem1TotalNum;
extern UINT8 		SMSReadyFlag;
extern UINT8 		DCSMSGCLASS_0_Flag;
extern UINT8   		gSmsFormatMode;
OSASemaRef			WebSMSSema = 0;
char				fd, rd;
static char			LocalSMSIndex[SupportSMSNum+1]= {0};
static char 		DraftSMSIndex[MaxDraftSMSNum+1]= {0};
static char 		SentSMSIndex[MaxSentSMSNum+1]= {0};

OSMsgQRef 			RcvSMSMsgQ;
OSMsgQRef 			RcvSMSATMsgQ;
static UINT32 		gRcvSMSTaskStack[DIALER_TASK_STACK_SIZE>>2] = {0xffffffff};
static OSTaskRef	gRcvSMSTask = NULL;

unsigned int		totalLocalSMSNum=0;
unsigned int 		totalLocalNum_LongSMS=0;//long sms counted as 1 sms
unsigned int 		totalSimSmsNum=0;
unsigned int 		totalSmsNum=0;
unsigned int 		totalSMSNum_LongSMS=0;
unsigned int 		SMSPageNum=1;
unsigned int 		totalLocalSentNum=0;
unsigned int		totalLocalSentNum_LongSMS = 0;
unsigned int 		totalSimSentSmsNum=0;
unsigned int 		totalSimRcvSmsNum = 0;
unsigned int		totalLocalDraftNum=0;
unsigned int 		totalSimDraftSmsNum=0;

OSMsgQRef 			gWebSMSMSGQ = NULL;
unsigned int 		UnreadSMSNum = 0;
unsigned int 		sendSMSflag;
extern char 		SMSsrcAddr[ CI_MAX_ADDRESS_LENGTH + 1] ;
extern char 		IMSI[16];
extern char 		DM_ADDR[14];
extern char 		DEFAULT_SMSC_ADDR[14];
extern OSMsgQRef 	gSendATMsgQ;

UINT8 				sim_is_ready=0;

UINT8 				DM_first_flag=0;
UINT8 				recvDMReply_flag=0;

extern int 			gCurrRegStatus;
static UINT8 		referenceNum=1;
static UINT8		writeSMSIndex = 0;
unsigned int		 Local_Index=1;
extern psm_handle_t *g_handle[TOTAL_PSMFILE_NUM];
extern unsigned long PSM_FILE_SIZE;
OSAFlagRef			 SMS_FlgRef = NULL;
char 				 csca_resp_str[DIALER_MSG_SIZE] = {'\0'};
static UINT8		 save_location = 1;
extern 	OSASemaRef   PhoneBookSema;
static 	int			 new_sms_num = 0;
static	int			 new_sms_num_led = 0;
extern BOOL PlatformIsTPMifi(void);

/*sms memeory full percent
when over or below , send AT+CMEMFULL*/
static int sms_full_per = 0;//10;   //modified 10 to 0 by notion ggj 20160407 for sms full 
static int sms_full_num = 0;

//added by nt yecong 20160201
UINT8 ntIsMMS = 0;

UINT32 message_time_index = 0; //added by notion ggj 20160324

UINT8 get_save_location()
{
	return save_location;
}
/*Insert_Delete Lcok */
OSASemaRef			SMSInsertDeleteSema = 0;
void SMS_DeleteInsert_lock(void)
{
	OSA_STATUS status;

	status = OSASemaphoreAcquire(SMSInsertDeleteSema, OS_SUSPEND);
	ASSERT(status == OS_SUCCESS);
}

void SMS_DeleteInsert_unlock(void)
{
	OSA_STATUS status;
	status = OSASemaphoreRelease(SMSInsertDeleteSema);
	ASSERT(status == OS_SUCCESS);
}

s_MrvFormvars 		   *SimSMS_list_start = NULL;
s_MrvFormvars 		   *SimSMS_list_last  = NULL;

s_MrvFormvars 	       *LocalSMS_list_start = NULL;
s_MrvFormvars           *LocalSMS_list_last  = NULL;

s_MrvFormvars 		   *SimSendSMS_list_start = NULL;
s_MrvFormvars 	       *SimSendSMS_list_last  = NULL;

s_MrvFormvars 		   *LocalSendSMS_list_start = NULL;
s_MrvFormvars 		   *LocalSendSMS_list_last  = NULL;

s_MrvFormvars 		   *SimDraftSMS_list_start = NULL;
s_MrvFormvars 	       *SimDraftSMS_list_last  = NULL;

s_MrvFormvars 		   *LocalDraftSMS_list_start = NULL;
s_MrvFormvars		   *LocalDraftSMS_list_last  = NULL;


/*added by shoujunl 131010*/
/**************************************************
*Local SMS Init : 0  begin init
*			    1  Finish init
**************************************************/
static int local_sms_init = -1;
void SMS_Set_LocalSMS_Init(int num)
{
	local_sms_init = num;
}
int SMS_Get_LocalSMS_Init()
{
	return local_sms_init;
}

extern UINT8 		libReadSmsAddress( UINT8 *pPdu, UINT8 *pIdx, UINT8 *pAddr );
extern void			libReadSmsProtocolId( UINT8 *pPdu, UINT8 *pIdx, UINT32 *protocolId );
extern void 		libReadSmsDcs( UINT8 *pPdu, UINT8 *pIdx, DCS *dDCS);
extern void 		libReadSmsTimeStamp( UINT8 *pPdu, UINT8 *pIdx, TIMESTAMP *tm);
extern UINT16 		libDecodeGsm7BitData( UINT8 *pInBuf, UINT16 inLen, UINT8 *pOutBuf , UINT8 Udhl,	UINT8 GMS7TableType,	UINT8 extensionType);

extern char 		ConvertASCToChar(char* str);
extern char 		ConvertStrToChar(char* str);
UINT16 				libEncodeGsm7BitData( UINT8 *pInBuf, UINT16 inLen, char *pOutBuf ,UINT8 Udhl);
extern	char		*MrvCgiUnescapeSpecialChars(char *str);
extern int	 		PB_init(void);
extern UINT8 		DustPoolArray[DUST_POOL_SIZE];
static int 			sms_over_cs = 0;
extern UINT32 		ConvertStrToInteger(char* str);
void slist_insert_firstNode(s_MrvFormvars **item, s_MrvFormvars **start, s_MrvFormvars **last)
{
	if(!*start)
	{
		(*item)->next=NULL;
		*start=*item;
		*last=*item;
		return;
	}
	(*item)->next=*start;
	*start=*item;
}


/*****************************************************************
   function description: falimilar with function MrvStrnReplace
                                but do not malloc new memory
                     input : src--source string
                                replace char 'delim' with char 'with'
******************************************************************/
void str_replaceChar(char *src, const char delim, const char with)
{
	char *tmpChar =NULL;
	tmpChar = src;
	while (*tmpChar)
	{
		if ( *tmpChar == delim)
		{
			//Duster_module_Printf(1,"%s,meet delim=%02x",__FUNCTION__,delim);
			*tmpChar = with;
		}
		tmpChar++;
	}

}
UINT8 CHAR_TO_2CHAR(UINT8 *pInBuf, UINT16 inLen, UINT8 *pOutBuf)
{
	UINT16 inIdx,outIdx;
	UINT8 bits;
	inIdx=0;
	outIdx=0;
	bits=0;
	UINT8 tempchar[2]= {'\0'};
	int tempvalue;
	UINT8 temp;

	while(inIdx<inLen)
	{
		temp=0xF0&pInBuf[inIdx];
		tempvalue=(temp>>4)&0x0F;
		//  CPUartLogPrintf("tempvalue F0 index%d=%d",inIdx,tempvalue);
		itoa(tempvalue,tempchar,16);
		pOutBuf[outIdx++]=tempchar[0];
		// CPUartLogPrintf("pOutBuf F0 inIdx%d=%c",inIdx,tempchar[0]);
		tempvalue=0x0F&pInBuf[inIdx];
		// CPUartLogPrintf("tempvalue 0F index%d=%d",inIdx,tempvalue);
		itoa(tempvalue,tempchar,16);
		pOutBuf[outIdx++]=tempchar[0];
		// CPUartLogPrintf("pOutBuf F0 inIdx%d=%c",inIdx,tempchar[0]);
		inIdx++;
	}
	return outIdx;

}

int SMS_get_id(char *in_buf,	char *outbuf)
{
	char  i = 0;
	UINT8 num_begin = 0;

	while(*in_buf)
	{
		if(num_begin && !isdigit(*in_buf))
		{
			return 0;
		}
		if(isdigit(*in_buf))
		{
			if(num_begin == 0)
			{
				num_begin = 1;
			}
			outbuf[i++] = *in_buf;
		}
		in_buf++;
	}
	return 1;
}


UINT8 CPMS_GET_CALLBACK(UINT8 OK_flag,char* resp_str ,UINT8 cmd_type)
{

	OS_STATUS   os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s ok_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag	=	CPMS_GET_CMD;
	else
		setflag	=	CMS_ERROR;

	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;

}
UINT8 LKSMSSTA_SET_CALLBACK(UINT8 OK_flag,char* resp_str ,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag = LKSMSSTA_SET_CMD;
	else
		setflag = LKSMSSTA_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;
}
/*Used in duster_init-->SMS_readSimList*/
UINT8 Init_LKSMSSTA_SET_CALLBACK(UINT8 OK_flag,char* resp_str ,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag = INIT_LKSMSSTA_SET;
	else
		setflag = INIT_LKSMSSTA_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;
}
/*Used in RcvSMSTask-->SMS_readSimList*/
UINT8 RcvSMS_LKSMSSTA_SET_CALLBACK(UINT8 OK_flag,char* resp_str ,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag = RCVSMS_LKSMSSTA_SET;
	else
		setflag = RCVSMS_LKSMSSTA_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;
}


UINT8 CNMI_SET_CALLBACK(UINT8 OK_flag,char*resp_str,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag=CNMI_SET_CMD;
	else
		setflag=CMS_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return 1;
}

UINT8 CMGF_SET_CALLBACK(UINT8 OK_flag,char*resp_str,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag=CMGF_SET_CMD;
	else
		setflag=CMS_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;
}
UINT8 CMGR_SET_CALLBACK(UINT8 OK_flag,char* resp_str,UINT8 cmd_type)//cmd_type acts as index
{

	OSA_STATUS 	os_status;
	UINT32		setflag,k,dataLength,smsc_length;
	char 		*p=NULL,*tp=NULL,*parsebuf=NULL;
	char		tmp[6]= {0},SMS_index[12]= {0},*sms_cbuf = 	NULL;
	SMS_Node	*tempSMS = NULL;
	s_MrvFormvars	*tempData = NULL;
	char 		SimSmsStatus;

	sms_cbuf = sms_malloc(162);
	if(!sms_cbuf)
	{
		Duster_module_Printf(1,"%s, sms_cbuf malloc failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	Duster_module_Printf(1,"enter %s OK_flag is %d,cmd_type is %d",__FUNCTION__,OK_flag,cmd_type);
	if(gSmsFormatMode == 0)
	{
		if(OK_flag&&(cmd_type!=255))
		{
			Duster_module_Printf(3,"%s,resp_str is %s",__FUNCTION__,resp_str);

			if((p = strstr(resp_str,"+CMGR:")) != NULL)
			{
				tempSMS = sms_malloc(sizeof(SMS_Node));
				DUSTER_ASSERT(tempSMS);
				memset(tempSMS,	'\0',	sizeof(SMS_Node));
				tempSMS->location	=	0;
				tempSMS->psm_location	=	cmd_type;
				tempSMS->LSMSinfo = NULL;
				tempSMS->srcNum	=	NULL;

				p = p+7;
				tp = strtok(p, ",");

				SimSmsStatus = ConvertStrToChar(tp);
				Duster_module_Printf(3,"SimSmsStatus =%u",  SimSmsStatus);

				p=p+3;
				memcpy(tmp, p, 5);

				for(k=0; k<5; k++)
				{
					if(tmp[k] == '\r')
					{
						tmp[k]='\0';
						break;
					}
				}
				dataLength = ConvertStrToInteger(tmp);
				Duster_module_Printf(3,"dataLength is %u",dataLength);
				p+=(k+2);

				memset(tmp,'\0',5);
				memcpy(tmp, p, 2);
				smsc_length = ConvertASCToChar(tmp);
				memset(SMS_index,0,12);
				sprintf(SMS_index,"SRCV%d",cmd_type);

				parsebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(parsebuf == NULL)
				{
					duster_Printf("%s: there is no enough memory",__FUNCTION__);
					if(sms_cbuf)
						sms_free(sms_cbuf);
					if(tempSMS)
					{
						sms_free(tempSMS);
						tempSMS = NULL;
					}
					sms_cbuf = NULL;
					return 0;
				}
				memset(sms_cbuf, '\0', 162);
				convertStrtoArray((p+(smsc_length<<1)+2), sms_cbuf, 162);

				memset(parsebuf, '\0', TEL_AT_SMS_MAX_LEN);
				parseSMS(cmd_type, sms_cbuf, parsebuf, dataLength, SimSmsStatus,0,tempSMS);

				psm_set_wrapper(PSM_MOD_SMS,NULL, SMS_index, parsebuf);
				sms_free(parsebuf);
				parsebuf = NULL;

				tempData = sms_malloc(sizeof(s_MrvFormvars));
				DUSTER_ASSERT(tempData);
				memset(tempData,	'\0',	sizeof(tempData));
				tempData->value =(char *)tempSMS;

#ifndef longSMS_enable
				MrvSlistAdd(tempData,&SimSMS_list_start,&SimSMS_list_last);
#endif

#ifdef longSMS_enable
				SMS_insert_Node(&SimSMS_list_start,&SimSMS_list_last,&tempData,1);
#endif
			}
			setflag =CMGR_SET_CMD;
		}
		else if(OK_flag&&(cmd_type==255))
		{
			Duster_module_Printf(1,"%s,read sim sms",__FUNCTION__);
			setflag =CMGR_SET_CMD;
		}
		else
			setflag =CMGR_ERROR;
	}
	else
		setflag =CMGR_ERROR;

	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	if(sms_cbuf)
		sms_free(sms_cbuf);
	return 1;
}
UINT8 CMGD_SET_CALLBACK(UINT8 OK_flag,char* resp_str,UINT8 cmd_type)
{

	OSA_STATUS os_status;
	UINT32     setflag;

	Duster_module_Printf(1,"enter %s,OK_falg is %d",__FUNCTION__,OK_flag);

	if(OK_flag)
		setflag = CMGD_SET_CMD;
	else
		setflag = CMS_ERROR;


	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;

}
UINT8 CMGS_SET_CALLBACK(UINT8 OK_flag,char* resp_str,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32     setflag;

	Duster_module_Printf(1,"enter %s,OK_falg is %d",__FUNCTION__,OK_flag);

	if(cmd_type!=255)
	{

		if(OK_flag)
			setflag = CMGS_SET_CMD;
		else
			setflag = CMGS_ERROR;


		os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
		DUSTER_ASSERT( os_status == OS_SUCCESS );
	}

	return 1;

}
UINT8 CSCA_GET_CALLBACK(UINT8 OK_flag,char* resp_str,UINT8 cmd_type)
{

	OSA_STATUS	 os_status;
	UINT32		 setflag;

	Duster_module_Printf(1,"enter %s,OK_falg is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
	{
		memset(csca_resp_str,'\0',DIALER_MSG_SIZE);
		memcpy(csca_resp_str,resp_str,strlen(resp_str));
		setflag=CSCA_GET_CMD;
	}
	else
		setflag=CMS_ERROR;

	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;

}
UINT8 CSCA_SET_CALLBACK(UINT8 OK_flag,char* resp_str,UINT8 cmd_type)
{

	OSA_STATUS  os_status;
	UINT32      setflag;

	Duster_module_Printf(1,"enter %s,OK_falg is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
	{
		setflag=CSCA_SET_CMD;
	}
	else
		setflag=CMS_ERROR;

	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;

}
UINT8 CMGW_SET_CALLBACK(UINT8 ok_flag,char* resp_str ,UINT8 cmd_type)
{
	OS_STATUS   os_status;
	UINT32      setflag;
	char       *p1=NULL,tmp[10];
	UINT16      index=0;

	Duster_module_Printf(1,"enter %s ok_flag is %d,cmd_type is %d",__FUNCTION__,ok_flag,cmd_type);

	Duster_module_Printf(3,"%s,resp_str is %s",__FUNCTION__,resp_str);
	if(ok_flag)
	{
		p1=strchr(resp_str,':');
		if(p1)
		{
			memset(tmp,'\0',10);
			SMS_get_id(p1,	tmp);
			index=ConvertStrToInteger(tmp);
			Duster_module_Printf(3,"%s tmp is %s,index is %d",__FUNCTION__,tmp,index);
			writeSMSIndex=index;
			setflag=CMGW_SET_CMD;
		}
	}
	else
		setflag=CMGW_ERROR;

	Duster_module_Printf(3,"%s setflag = %0X",__FUNCTION__,setflag);

	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return 1;
}
UINT8 CGSMS_SET_CALLBACK(UINT8 OK_flag,char*resp_str,UINT8 cmd_type)
{
	OSA_STATUS os_status;
	UINT32 setflag;

	Duster_module_Printf(1,"enter %s OK_flag is %d",__FUNCTION__,OK_flag);
	if(OK_flag)
		setflag=CGSMS_SET_CMD;
	else
		setflag=CMS_ERROR;
	os_status = OSAFlagSet( SMS_FlgRef, setflag, OSA_FLAG_OR );
	DUSTER_ASSERT( os_status == OS_SUCCESS );

	return 1;
}


int getSMSCenter()
{
	char 		 *quote_begin=NULL,*quote_end=NULL;
	char 		  *smsc_addr = NULL;
	UINT32 		  flag_value;
	CommandATMsg *sendSMSMsg = NULL;
	OSA_STATUS    osa_status;
	int			  ret = 0;
       Duster_module_Printf(1,"enter %s ",__FUNCTION__);
	sendSMSMsg=(CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
	sendSMSMsg->atcmd   =   sms_malloc(DIALER_MSG_SIZE);
	sendSMSMsg->ok_fmt  =   sms_malloc(10);
	sendSMSMsg->err_fmt =   sms_malloc(15);
	if(!sendSMSMsg || !sendSMSMsg->atcmd || !sendSMSMsg->ok_fmt || !sendSMSMsg->err_fmt)
	{
		Duster_module_Printf(1,"%s sms_malloc fail DUSTER_ASSERT",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	memset(sendSMSMsg->atcmd,'\0',DIALER_MSG_SIZE);
	sprintf(sendSMSMsg->atcmd,"AT+CSCA?\r");

	sendSMSMsg->ok_flag=1;
	if(sendSMSMsg->ok_fmt)
	{
		memset( sendSMSMsg->ok_fmt,	'\0',	10);
		sprintf(sendSMSMsg->ok_fmt,	"+CSCA");
	}
	if(sendSMSMsg->err_fmt)
	{
		memset( sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,	"+CMS ERROR");
	}
	sendSMSMsg->callback_func=&CSCA_GET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_GET_CMD;

	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS);

	flag_value = 0;
	osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CSCA_GET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
	//DUSTER_ASSERT(osa_status == OS_SUCCESS);
	if(flag_value == CSCA_GET_CMD)
	{
		quote_begin = strchr( csca_resp_str,'\"');
		quote_end   = strrchr(csca_resp_str,'\"');
		smsc_addr = sms_malloc(quote_end-quote_begin);
		DUSTER_ASSERT(smsc_addr);
		memset(smsc_addr,		'\0',				quote_end-quote_begin);
		memcpy(smsc_addr,		quote_begin+1,	quote_end-quote_begin-1);
		memset(csca_resp_str,	'\0',				DIALER_MSG_SIZE);
		memcpy(csca_resp_str,	smsc_addr,		strlen(smsc_addr));
		ret = 1;
	}
	else if(flag_value == CMS_ERROR || (osa_status == OS_TIMEOUT))
	{
		Duster_module_Printf(1,"%s,get csca error!",__FUNCTION__);
		smsc_addr = sms_malloc(16);
		DUSTER_ASSERT(smsc_addr);
		memset(smsc_addr,	'\0',	16);
		memcpy(smsc_addr,		DEFAULT_SMSC_ADDR,	strlen(DEFAULT_SMSC_ADDR));
		memset(csca_resp_str,	'\0',					DIALER_MSG_SIZE);
		memcpy(csca_resp_str,	smsc_addr,			strlen(smsc_addr));
		ret = -1;
	}
	if(smsc_addr)
	{
		psm_set_wrapper(PSM_MOD_SMS,"sms_setting","sms_center",smsc_addr);
		sms_free(smsc_addr);
		smsc_addr = NULL;
	}

	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->ok_fmt)
			sms_free(sendSMSMsg->ok_fmt);
		if(sendSMSMsg->err_fmt)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	return ret;
}
/**********************************************************
* function description: get psm handle according to outindex
                input : action--0: need to get first free location--outindex
                                      1: outindex is givein by caller
                outHandle : which psm file the outindex locations in
                  return:1--success/0--fail
***********************************************************/
UINT8 getPSM_index(UINT16 *outindex,UINT16 *outHandle,UINT8 action)
{
	UINT16 i = 0;

	if(action==0)
	{
		for(i = 1; i<=SupportSMSNum; i++)
		{
			if(LocalSMSIndex[i]==0)
			{
				LocalSMSIndex[i]=1;
				break;
			}
		}

		*outindex = i;
	}
	else
		i=*outindex;

	if((i>0)&&(i<=PSM1_max))
		*outHandle = handle_SMS1;
	if((i>PSM1_max)&&(i<=PSM2_max))
		ASSERT(0);
	if((i>PSM2_max)&&(i<=PSM3_max))
		ASSERT(0);
	if((i>PSM3_max)&&(i<=PSM4_max))
		ASSERT(0);
	if((i>PSM4_max)&&(i<=PSM5_max))
		*outHandle = handle_SMS5;


	return 1;

}

//AT+CNMA is required if AT+CSMS=1


void convertStrtoArray(char* str, char *array, int ArrayLength)
{
	int  i, len;
	char tmp[4]= {0};

	len = strlen(str);

	Duster_module_Printf(5,"enter %s: len is %d",__FUNCTION__,len);

	for(i=0; i<ArrayLength; i++)
	{
		memset(tmp, '\0', 4);

		if((len > (i<<1)) && (str[(i<<1)] != '\r') && (str[(i<<1)] != '\n'))
		{
			memcpy(tmp, str+(i<<1), 2);
			array[i] = ConvertASCToChar(tmp);
			Duster_module_Printf(5,"%02X", array[i]);
		}
		else
			break;

	}

	Duster_module_Printf(5,"leave %s",__FUNCTION__);

}
/********************************************************************
  function description : change time"13,04,13,15,15,26,8" to TIMESTAMP

*********************************************************************/
UINT8 changeTimeToTIMESTAMP(char *timebuf,TIMESTAMP *out)
{
	char *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL,*p6=NULL;

	UINT8 Year,Month,Day,Hour,Minute,Second;
	char  tmp[8]= {0};


	Duster_module_Printf(1,"enter %s,timebuf is %s",__FUNCTION__,timebuf);

	if(!timebuf)
	{
		Duster_module_Printf(1,"leave %s",__FUNCTION__);
		return 0 ;
	}
	p1=strchr(timebuf,',');
	if(p1)
	{
		p2=strchr(p1+1,',');
		if(p2)
		{
			p3=strchr(p2+1,',');
			if(p3)
			{
				p4 = strchr(p3+1,',');
				if(p4)
				{
					p5=strchr(p4+1,',');
					if(p5)
						p6=strchr(p5+1,',');

				}
			}
		}
	}

	if(p1&&p2&&p3&&p4&&p5&&p6)
	{
		memset(tmp,'\0',8);
		memcpy(tmp,timebuf,2);
		Year = ConvertStrToInteger(tmp);

		memset(tmp,'\0',8);
		memcpy(tmp,p1+1,p2 - p1 - 1);
		Month = ConvertStrToInteger(tmp);

		memset(tmp,'\0',8);
		memcpy(tmp,p2+1,p3 - p2 -1);
		Day = ConvertStrToInteger(tmp);

		memset(tmp,'\0',8);
		memcpy(tmp,p3+1,p4 -p3 -1);
		Hour = ConvertStrToInteger(tmp);


		memset(tmp,'\0',8);
		memcpy(tmp,p4+1,p5 -p4 -1);
		Minute= ConvertStrToInteger(tmp);

		memset(tmp,'\0',8);
		memcpy(tmp,p5+1,p6 -p5 -1);
		Second = ConvertStrToInteger(tmp);

		Duster_module_Printf(3,"%s,Year=%d,Month=%d,Day=%d,Hour=%d,Minute=%d,Second=%d",__FUNCTION__,Year,Month,Day,Hour,Minute,Second);

		out->tsDay = Day;
		out->tsHour = Hour;
		out->tsMinute = Minute;
		out->tsMonth = Month;
		out->tsSecond = Second;
		out->tsYear = Year;

		Duster_module_Printf(1,"leave %s",__FUNCTION__);
		return 1;

	}
	else
	{

		Duster_module_Printf(1,"leave %s",__FUNCTION__);
		return 0;
	}

}
//added by notion ggj 20151218 for new long sms 
int SMS_get_new_sms_num_for_longsms()
{
	int ret_num = 0;
	int single_num = 0;
	s_MrvFormvars *list_start = NULL;
	s_MrvFormvars *cur_node = NULL;
	SMS_Node	*cur_SMS = NULL;

	Duster_module_Printf(1,	"%s save_location is %d",	__FUNCTION__,	save_location);
	if(new_sms_num == 0)
	{
		return ret_num;
	}

	if(save_location == 0)
	{
		list_start = SimSMS_list_start;
	}
	else if(save_location == 1)
	{
		list_start = LocalSMS_list_start;
	}

	if(!list_start)
	{
		return ret_num;
	}
	single_num = new_sms_num;

	cur_node = list_start;
	while(cur_node && cur_node->value)
	{
		Duster_module_Printf(1,	"%s 1 single_num is %d",	__FUNCTION__,	single_num);

		cur_SMS = (SMS_Node *)cur_node->value;
		if(cur_SMS->isLongSMS && cur_SMS->LSMSinfo && (cur_SMS->LSMSinfo->LSMSflag == START))
		{
			while(cur_node && cur_node->value)
			{
				Duster_module_Printf(1, "%s  2 single_num is %d",	__FUNCTION__,	single_num);
				cur_SMS = (SMS_Node*)cur_node->value;
				if(cur_SMS->LSMSinfo && cur_SMS->LSMSinfo->LSMSflag == END)
					break;
				if(!cur_SMS->LSMSinfo)
					break;
				cur_node = cur_node->next;
				single_num--;
				if(single_num <=0)
				{
					break;
				}
			}
		}

		Duster_module_Printf(1,	"%s 3 single_num is %d",	__FUNCTION__,	single_num);
		cur_node = cur_node->next;
		single_num--;
		ret_num++;
		Duster_module_Printf(1,	"%s 1 ret_num is %d",	__FUNCTION__,	ret_num);
		if(single_num <= 0)
		{
			break;
		}
	}
	Duster_module_Printf(1,	"Leave %s 2 ret_num is %d",	__FUNCTION__,	ret_num);
	return ret_num;
}
int SMS_get_new_sms_num()
{
	Duster_module_Printf(1,	"%s new_sms_num is %d",	__FUNCTION__,	new_sms_num);
	return new_sms_num;
}
int SMS_new_sms_add()
{
	new_sms_num++;
	return new_sms_num;
}
int SMS_set_sms_zero()
{
	new_sms_num = 0;
	return new_sms_num;
}
int SMS_get_new_sms_num_long()
{
	int ret_num = 0;
	int single_num = 0;
	s_MrvFormvars *list_start = NULL;
	s_MrvFormvars *cur_node = NULL;
	SMS_Node	*cur_SMS = NULL;

	Duster_module_Printf(1,	"%s save_location is %d",	__FUNCTION__,	save_location);
	if(new_sms_num_led == 0)
	{
		return ret_num;
	}

	if(save_location == 0)
	{
		list_start = SimSMS_list_start;
	}
	else if(save_location == 1)
	{
		list_start = LocalSMS_list_start;
	}

	if(!list_start)
	{
		return ret_num;
	}
	single_num = new_sms_num_led;

	cur_node = list_start;
	while(cur_node && cur_node->value)
	{
		Duster_module_Printf(1,	"%s 1 single_num is %d",	__FUNCTION__,	single_num);

		cur_SMS = (SMS_Node *)cur_node->value;
		if(cur_SMS->isLongSMS && cur_SMS->LSMSinfo && (cur_SMS->LSMSinfo->LSMSflag == START))
		{
			while(cur_node && cur_node->value)
			{
				Duster_module_Printf(1, "%s  2 single_num is %d",	__FUNCTION__,	single_num);
				cur_SMS = (SMS_Node*)cur_node->value;
				if(cur_SMS->LSMSinfo && cur_SMS->LSMSinfo->LSMSflag == END)
					break;
				if(!cur_SMS->LSMSinfo)
					break;
				cur_node = cur_node->next;
				single_num--;
				if(single_num <=0)
				{
					break;
				}
			}
		}

		Duster_module_Printf(1,	"%s 3 single_num is %d",	__FUNCTION__,	single_num);
		cur_node = cur_node->next;
		single_num--;
		ret_num++;
		Duster_module_Printf(1,	"%s 1 ret_num is %d",	__FUNCTION__,	ret_num);
		if(single_num <= 0)
		{
			break;
		}
	}
	Duster_module_Printf(1,	"Leave %s 2 ret_num is %d",	__FUNCTION__,	ret_num);
	return ret_num;
}
int SMS_get_new_sms_num_led()
{
	Duster_module_Printf(1,	"%s new_sms_num is %d",	__FUNCTION__,	new_sms_num_led);
	Duster_module_Printf(1,	"%s	test long new sms num is %d",	__FUNCTION__, SMS_get_new_sms_num_long());
	return new_sms_num_led;
}
int SMS_new_sms_add_led()
{
	new_sms_num_led++;
	return new_sms_num_led;
}
int SMS_set_sms_zero_led()
{
	new_sms_num_led = 0;
	return new_sms_num_led;
}

/********************************************************************
function description : Change "00310032" to 0x00 0x31 0x00 0x32
            input : pInBuf -- Unicode string 12--"00310032"
                      inLen-- length of pInBuf
            output: unicode if pInBuf
*********************************************************************/
UINT8 CHAR_TO_UINT8(UINT8 *pInBuf, UINT16 inLen, UINT8 *pOutBuf)
{
	UINT16 inIdx,outIdx;
	UINT8  v1,v2;
	inIdx=0;
	outIdx=0;
	if(!pInBuf||!pOutBuf)
	{
		Duster_module_Printf(1,"%s,pInBuf or pOutBuf is NULL",__FUNCTION__);
		return 0;
	}
	while(inIdx<inLen)
	{
		v1=pInBuf[inIdx];
		v2=pInBuf[inIdx+1];
		if((v1>='0')&&(v1<='9'))
			v1=v1-'0';
		else if((v1>='A')&&(v1<='F'))
			v1=v1-'A'+10;
		else if((v1>='a')&&(v1<='f'))
			v1=v1-'a'+10;


		if((v2>='0')&&(v2<='9'))
			v2=v2-'0';
		else if((v2>='A')&&(v2<='F'))
			v2=v2-'A'+10;
		else if((v2>='a')&&(v2<='f'))
			v2=v2-'a'+10;

		pOutBuf[outIdx++]=v1*16+v2;
		inIdx+=2;
	}
	return outIdx;

}
/************************************************************
* function description : change ucs2 data to utf8 data (little endian)
*
****************************************************************/
void u2utf8(char *UCS2_data, char *UTF8_data, unsigned int *pos)
{
	unsigned short uni;
	//unsigned int  test_int = 0;

	//duster_Printf("enter %s",__FUNCTION__);
	//Duster_module_Printf(1,"enter %s",__FUNCTION__);

	uni = *(unsigned short*)UCS2_data;
	//test_int=*pos;

	//Duster_module_Printf(1,"%s,*pos=%d,uni=%X",__FUNCTION__,test_int,uni);

	if((*pos+4) > 320)
		return;

	if(uni < 0x80)
	{
		//	Duster_module_Printf(1,"%s,uni<0x80",__FUNCTION__);
		*(UTF8_data+*pos) = uni;
		(*pos)++;
	}
	else if(uni < 0x800)
	{
		*(UTF8_data+*pos+1) = (0xc0 | (uni >> 6));
		*(UTF8_data+*pos) = (0x80 | (uni & 0x3f));
		*pos+=2;
		//	Duster_module_Printf(1,"%s,uni<0x800",__FUNCTION__);
	}
	else if(uni < 0x10000)
	{
		*(UTF8_data+*pos+2) = (0xe0 | (uni>>12));
		*(UTF8_data+*pos+1) = (0x80 | (uni >> 6 & 0x3f));
		*(UTF8_data+*pos) = (0x80 | (uni & 0x3f));

		//	Duster_module_Printf(1,"%s,uni<0x10000",__FUNCTION__);

		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos+2));
		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos+1));
		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos));

		*pos+=3;
		//duster_Printf("%s:pos = %d",__FUNCTION__,*pos);

	}
	else if(uni < 0x20000)
	{
		*(UTF8_data+*pos+3) = (0xf0 | (uni >> 18));
		*(UTF8_data+*pos+2) = (0x80 | (uni >> 12 & 0x3f));
		*(UTF8_data+*pos+1) = (0x80 | (uni >> 6 & 0x3f));
		*(UTF8_data+*pos) =  (0x80 | (uni & 0x3f));
		*pos+=4;

		//	Duster_module_Printf(1,"%s,uni<0x20000",__FUNCTION__);
	}
	else
	{
		/*we don't deal with it*/
	}

	//duster_Printf("leave %s",__FUNCTION__);
}
/************************************************************
* function description : change ucs2 data to utf8 data (big endian)
*
****************************************************************/

void u2utf8_bigEndian(char *UCS2_data, char *UTF8_data, unsigned int *pos)
{
	unsigned short uni;

	//duster_Printf("enter %s",__FUNCTION__);
//	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	uni = *(unsigned short*)UCS2_data;

	//Duster_module_Printf(1,"%s,*pos=%d,uni=%X",__FUNCTION__,test_int,uni);
	if(uni == 0)
	{
		Duster_module_Printf(1,	"%s meet zero pos = %d",	__FUNCTION__,	*pos);
	}

	if(uni < 0x80)
	{
		//    Duster_module_Printf(1,"%s,uni<0x80",__FUNCTION__);
		*(UTF8_data+*pos) = uni;
		(*pos)++;
	}
	else if(uni < 0x800)
	{
		*(UTF8_data+*pos) = (0xc0 | (uni >> 6));
		*(UTF8_data+*pos+1) = (0x80 | (uni & 0x3f));
		*pos+=2;
		//	Duster_module_Printf(1,"%s,uni<0x800",__FUNCTION__);
	}
	else if(uni < 0x10000)
	{
		*(UTF8_data+*pos) = (0xe0 | (uni>>12));
		*(UTF8_data+*pos+1) = (0x80 | (uni >> 6 & 0x3f));
		*(UTF8_data+*pos+2) = (0x80 | (uni & 0x3f));

		//	Duster_module_Printf(1,"%s,uni<0x10000",__FUNCTION__);

		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos+2));
		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos+1));
		//duster_Printf("%s:%x",__FUNCTION__,*(UTF8_data+*pos));

		*pos+=3;
		//duster_Printf("%s:pos = %d",__FUNCTION__,*pos);

	}
	else if(uni < 0x20000)
	{
		*(UTF8_data+*pos) = (0xf0 | (uni >> 18));
		*(UTF8_data+*pos+1) = (0x80 | (uni >> 12 & 0x3f));
		*(UTF8_data+*pos+2) = (0x80 | (uni >> 6 & 0x3f));
		*(UTF8_data+*pos+3) =  (0x80 | (uni & 0x3f));
		*pos+=4;

		//	Duster_module_Printf(1,"%s,uni<0x20000",__FUNCTION__);
	}
	else
	{
		/*we don't deal with it*/
	}

	//duster_Printf("leave %s",__FUNCTION__);
}

void sms_Queue_init(void)
{
	OSA_STATUS osaStatus;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	CPUartLogPrintf("RcvSMSMsgQ is created");
	osaStatus = OSAMsgQCreate(&RcvSMSMsgQ,
#ifdef  OSA_QUEUE_NAMES
	                          "RcvSMSMsgQ",
#endif
	                          sizeof(DialRespMessage) + 4,
	                          DIALER_CMD_MSG_SIZE,
	                          OSA_PRIORITY);
	if(osaStatus != OS_SUCCESS)
		DUSTER_ASSERT(0);

	gsATPMsgQ[TEL_AT_CMD_ATP_4] = RcvSMSMsgQ;

	osaStatus = OSAMsgQCreate(&RcvSMSATMsgQ,
#ifdef  OSA_QUEUE_NAMES
	                          "RcvSMSATMsgQ",
#endif
	                          sizeof(DialRespMessage) + 4,
	                          DIALER_CMD_MSG_SIZE,
	                          OSA_PRIORITY);
	if(osaStatus != OS_SUCCESS)
		DUSTER_ASSERT(0);

	gsATPMsgQ[TEL_AT_CMD_ATP_5] = RcvSMSATMsgQ;
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
}


int SMS_task_resp_cb(char *in_str,int sATPInd,char ind_resp)
{
	UINT8       	*pBuf = NULL;
	OSA_STATUS       osa_status;
	DialRespMessage  resp_msg = {0};
	OSMsgQRef        tRespMsgQ=0;
	int i;

	DUSTER_ASSERT(in_str != NULL);
	tRespMsgQ = gsATPMsgQ[sATPInd];

	Duster_module_Printf(7,"SMS_task_resp_cb:ind or resp: %d %d %s", ind_resp,strlen(in_str),in_str + 2);

	pBuf = (UINT8 *)malloc(strlen(in_str));
	DUSTER_ASSERT(pBuf != NULL);

	for(i=2; i<strlen(in_str) ; i++)
		Duster_module_Printf(7,"%c", in_str[i]);


	memcpy(pBuf, in_str + 2, strlen(in_str) -2);
	pBuf[strlen(in_str) -2] = '\0';

	Duster_module_Printf(7,"pBuf:%d %s", strlen(pBuf),pBuf);

	for(i=0; i<strlen(in_str)-2 ; i++)
		Duster_module_Printf(7,"%c", pBuf[i]);

	resp_msg.MsgData = (void *)pBuf;

	osa_status = OSAMsgQSend(tRespMsgQ, sizeof(resp_msg), (UINT8 *)&resp_msg, OSA_NO_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS);

	Duster_module_Printf(7,"SMS_task_resp_cb exit");
	return 0;
}


int SMS_AT_task_resp_cb(char *in_str,int sATPInd,char ind_resp)
{
	UINT8 			*pBuf = NULL;
	OSA_STATUS		osa_status;
	DialRespMessage resp_msg = {0};
	OSMsgQRef       tRespMsgQ=0;
	int             i;

	DUSTER_ASSERT(in_str != NULL);
	tRespMsgQ = gsATPMsgQ[sATPInd];

	Duster_module_Printf(7,"SMS_task_resp_cb:ind or resp: %d %d %s", ind_resp,strlen(in_str),in_str + 2);

	pBuf = (UINT8 *)malloc(strlen(in_str));
	DUSTER_ASSERT(pBuf != NULL);

	for(i=2; i<strlen(in_str) ; i++)
		Duster_module_Printf(7,"%c", in_str[i]);


	memcpy(pBuf, in_str + 2, strlen(in_str) -2);
	pBuf[strlen(in_str) -2] = '\0';

	Duster_module_Printf(7,"pBuf:%d %s", strlen(pBuf),pBuf);

	for(i=0; i<strlen(in_str)-2 ; i++)
		Duster_module_Printf(7,"%c", pBuf[i]);

	resp_msg.MsgData = (void *)pBuf;

	osa_status = OSAMsgQSend(tRespMsgQ, sizeof(resp_msg), (UINT8 *)&resp_msg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS);

	Duster_module_Printf(7,"SMS_task_resp_cb exit");
	return 0;
}
/**************************************************************************
   function : read sms form psm when uboot
                 input: smsType -- 1:recvSMS Box
                                            2:SentSMSBox
                                            3:DraftSMSBox
***************************************************************************/
void SMS_getLocalList(s_MrvFormvars **list_start,s_MrvFormvars **list_last,UINT8 smsType)
{

	char 			*str=NULL;
	unsigned int 	i=0;
	char 			ghandle=0;
	char 			tmp[10]= {0},tmp2[25]= {0};
	UINT8 			sms_type=0,refNum=0,segmentNum=0,totalSegNum=0,sms_status=0;
	UINT16			maxSMSNum,getNum,ret;
	SMS_Node 		*tempSMS = NULL;
	s_MrvFormvars 	   *tempData = NULL;
	char		   *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL,*p6=NULL,*p7=NULL,*p8=NULL,*p9=NULL,*p10=NULL,*p11=NULL;

	Duster_module_Printf(1,"enter %s, smsType:0x%x",__FUNCTION__,smsType);

	getNum=0;

	if(smsType == 1 )
	{

		str=psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num",	handle_SMS1);

		maxSMSNum = SupportSMSNum;

		if(str)
		{
			totalLocalSMSNum= ConvertStrToInteger(str);
			if(totalLocalSMSNum == maxSMSNum)
				gDialer2UIPara.LocalSMSFull = 1;
			Duster_module_Printf(1,"%s:totalLocalSMSNum is %d",__FUNCTION__,totalLocalSMSNum);
			sms_free(str);
		}
		else
		{
			Duster_module_Printf(1,"%s,cann't get totalLocalSMSNum",__FUNCTION__);
			totalLocalSMSNum=0;
			//return;
		}

	}

	if(smsType ==2 )
	{
		maxSMSNum = MaxSentSMSNum;

		str=psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_send_num",	handle_SMS1);

		if(str)
		{
			totalLocalSentNum= ConvertStrToInteger(str);
			Duster_module_Printf(1,"%s:totalLocalSentNum is %d",__FUNCTION__,totalLocalSentNum);
			sms_free(str);
		}
		else
		{

			Duster_module_Printf(1,"%s,cann't get totalLocalSentNum",__FUNCTION__);
			totalLocalSentNum=0;
			//return;
		}
	}

	if(smsType == 3 )
	{
		maxSMSNum = MaxDraftSMSNum;

		str=psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	handle_SMS1);

		if(str)
		{
			totalLocalDraftNum= ConvertStrToInteger(str);
			Duster_module_Printf(1,"%s:totalLocalDraftNum is %d",__FUNCTION__,totalLocalDraftNum);
			sms_free(str);
		}
		else
		{

			Duster_module_Printf(1,"%s,cann't get ttalLocalSMSNum",__FUNCTION__);
			totalLocalDraftNum=0;

			//return;
		}
	}


	for(i=1; i<=maxSMSNum; i++)
	{
		if(smsType==1)
			LocalSMSIndex[i] = 0;
		else if(smsType==2)
			SentSMSIndex[i] = 0;
		else if(smsType==3)
			DraftSMSIndex[i]=0;
	}



	for(i=1; i<=maxSMSNum; i++)
	{

		if(smsType==1)
		{
			if(getNum>=totalLocalSMSNum)
				break;
		}
		if(smsType==2)
		{
			if(getNum>=totalLocalSentNum)
				break;
		}
		if(smsType==3)
		{
			if(getNum>=totalLocalDraftNum)
				break;
		}
		tempSMS = sms_malloc(sizeof(SMS_Node));
		memset(tempSMS,	'\0',	sizeof(SMS_Node));
		tempSMS->location	=	1;//mifi
		tempSMS->srcNum	=	NULL;
		tempSMS->isLongSMS	= FALSE;
		tempSMS->LSMSinfo = NULL;
		if(smsType==1)
		{
			LocalSMSIndex[i] = 0;

			if((i>0)&&(i<=PSM1_max))
				ghandle = handle_SMS1;
			if((i>PSM1_max)&&(i<=PSM2_max))
				ASSERT(0);
			if((i>PSM2_max)&&(i<=PSM3_max))
				ASSERT(0);
			if((i>PSM3_max)&&(i<=PSM4_max))
				ASSERT(0);

			memset(tmp,'\0',10);
			sprintf(tmp,"LRCV%d",i);
		}
		if(smsType==2)
		{
			SentSMSIndex[i] =0;
			ghandle=handle_SMS1;
			memset(tmp,'\0',10);
			sprintf(tmp,"LSNT%d",i);
		}

		if(smsType==3)
		{
			DraftSMSIndex[i]=0;
			ghandle=handle_SMS1;
			memset(tmp,'\0',10);
			sprintf(tmp,"LDRT%d",i);
		}

		str = psmfile_get_wrapper(PSM_MOD_SMS,NULL,tmp,ghandle);

		if(str)
		{
			p1=strchr(str,DELIMTER);
			if(p1)
			{
				p2=strchr(p1+1,DELIMTER);
				if(p2)
				{
					p3=strchr(p2+1,DELIMTER);
					if(p3)
					{
						p4=strchr(p3+1,DELIMTER);
						if(p4)
						{
							p5=strchr(p4+1,DELIMTER);
							if(p5)
							{
								p6=strchr(p5+1,DELIMTER);
								if(p6)
								{
									p7=strchr(p6+1,DELIMTER);
									if(p7)
									{
										p8=strchr(p7+1,DELIMTER);
										if(p8)
										{
											p9=strchr(p8+1,DELIMTER);
											if(p9)
												p10=strchr(p9+1,DELIMTER);
											if(p10)
											{
												p11=strchr(p10+1,DELIMTER);
											}

										}
									}
								}
							}
						}

					}
				}
			}

			if(p1&&p2&&p3&&p4&&p5&&p6&&p7&&p8&&p9&&p10&&p11)
			{
				memset(tmp,'\0',8);
				memcpy(tmp,p7+1,p8-p7-1);
				sms_type=ConvertStrToInteger(tmp);

				memset(tmp,'\0',8);
				memcpy(tmp,p5+1,p6-p5-1);
				sms_status = ConvertStrToInteger(tmp);

				memset(tmp,'\0',8);
				memcpy(tmp,p8+1,p9-p8-1);
				refNum=ConvertStrToInteger(tmp);

				memset(tmp,'\0',8);
				memcpy(tmp,p9+1,p10-p9-1);
				segmentNum = ConvertStrToInteger(tmp);

				memset(tmp,'\0',8);
				memcpy(tmp,p10+1,p11-p10-1);
				totalSegNum = ConvertStrToInteger(tmp);

				tempSMS->location = 1;
				Duster_module_Printf(3,"%s,sms_type is %d",__FUNCTION__,sms_type);
				switch(sms_type)
				{
				case NOT_LONG:
					tempSMS->isLongSMS=FALSE;
					tempSMS->psm_location = i;
					if(sms_status == 0)
					{
						tempSMS->sms_status = UNREAD;
					}
					else if(sms_status == 1)
					{
						tempSMS->sms_status = READED;
					}
					else if(sms_status == 2)
					{
						tempSMS->sms_status = SEND_FAIL;
					}
					else if(sms_status == 3)
					{
						tempSMS->sms_status = SEND_SUCCESS;
					}
					else if(sms_status == 4)
					{
						tempSMS->sms_status = DRAFT;
					}
					else if(sms_status == 5)
					{
						tempSMS->sms_status = UNSENT;
					}
					break;
				case ONE_ONLY:
				case START:
				case MIDDLE:
				case END:
				case LONG_SMS:
					tempSMS->isLongSMS=TRUE;
					tempSMS->psm_location = i;
					if(sms_status == 0)
					{
						tempSMS->sms_status = UNREAD;
					}
					else if(sms_status == 1)
					{
						tempSMS->sms_status = READED;
					}
					else if(sms_status == 2)
					{
						tempSMS->sms_status = SEND_FAIL;
					}
					else if(sms_status == 3)
					{
						tempSMS->sms_status = SEND_SUCCESS;
					}
					else if(sms_status == 4)
					{
						tempSMS->sms_status = DRAFT;
					}
					else if(sms_status == 5)
					{
						tempSMS->sms_status = UNSENT;
					}
					tempSMS->LSMSinfo = (LSMS_INFO*)sms_malloc(sizeof(LSMS_INFO));
					DUSTER_ASSERT(tempSMS->LSMSinfo);
					memset(tempSMS->LSMSinfo,	'\0',	sizeof(LSMS_INFO));
					tempSMS->LSMSinfo->referenceNum= refNum;
					tempSMS->LSMSinfo->segmentNum 	= segmentNum;
					tempSMS->LSMSinfo->totalSegment = totalSegNum;
					Duster_module_Printf(1,	"%s psmlocation is %d",	__FUNCTION__,	tempSMS->psm_location);
					Duster_module_Printf(1,	"%s refNum is %d, segmentNum IS %d , totalSegNum is %d",	__FUNCTION__,	refNum, segmentNum, totalSegNum);
					break;
				}

				memset(tmp2,'\0',25);
				memcpy(tmp2,p4+1,p5-p4-1);
				tmp2[24] = '\0';
				tempSMS->savetime = sms_malloc(sizeof(TIMESTAMP));
				DUSTER_ASSERT(tempSMS->savetime);
				memset(tempSMS->savetime,	'\0',	sizeof(TIMESTAMP));
				ret = changeTimeToTIMESTAMP(tmp2,tempSMS->savetime);

				if(tempSMS->isLongSMS)
				{
					tempSMS->srcNum = sms_malloc(p2-p1);
					DUSTER_ASSERT(tempSMS->srcNum);
					memset(tempSMS->srcNum,	'\0',	p2-p1);
					memcpy(tempSMS->srcNum,	p1+1,	p2-p1-1);
				}

				tempData =(s_MrvFormvars*)sms_malloc(sizeof(s_MrvFormvars));
				DUSTER_ASSERT(tempData);
				memset(tempData,	'\0',	sizeof(s_MrvFormvars));
				tempData->value = (char *)tempSMS;

				SMS_insert_Node(list_start,list_last,&tempData,1);

				getNum++;

				Duster_module_Printf(3,"%s smsType = %d getNum = %d",__FUNCTION__,smsType,getNum);
				if(smsType==1)
				{

					LocalSMSIndex[i] = 1;

				}
				if(smsType==2)
				{
					SentSMSIndex[i]=1;

				}
				if(smsType==3)
				{
					DraftSMSIndex[i]=1;
				}
			}

			sms_free(str);
		}
		else if(!str)
		{
			sms_free(tempSMS);
			tempSMS = NULL;
		}

	}
	if(smsType == 2)
		totalLocalNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);

}
UINT16 htons(UINT16 _val)
{
	return ((((UINT16)(_val) & (UINT16)0x00ffU) << 8)
	        |(((UINT16)(_val) & (UINT16)0xff00U) >> 8));
}

UINT32 htonl(UINT32 _val)
{
	return (_val << 24) | ((_val&0xff00) << 8) |
	       ((_val&0xff0000) >> 8) | (_val >> 24);
}
int SMS_Check_Empty();
int smsc_init_done = 0;
void SMS_post_get(int Action, dc_args_t *dca)
{
	char *str = NULL, *str2 = NULL;
	UINT16	page_num = 0,	data_per_page = 0;
	UINT8	mem_store = 1 ,	tags = 0 ;
	MrvXMLElement *root = NULL;
	MrvXMLElement *pChild = NULL;
	int			   nIndex = 0;
	char		   tmpBuf[12] = {0};
	int			   new_num = 0;
	UINT16		   total_unread_msg_num = 0;
	Duster_module_Printf(1,"enter %s Action is %d",__FUNCTION__,	Action);
	if((DUSTER_CONFIG_START  == Action) ||( DUSTER_CONFIG_GETLIST == Action))
	{
		goto EXIT;
	}
	if ( !dca )
		goto EXIT;

	if ( dca->dc_type != DUSTER_CB_ARGS_XML )
		goto EXIT;

	root = dca->dc_root;

	int test =  SMS_Check_Empty();
	Duster_module_Printf(1,	"%s , SMS_Check_Empty is %d",	__FUNCTION__,	test);

	//added by notion ggj 20160115
	if((smsc_init_done == 0) && SMS_FlgRef)
	{
	       if(0 == SMSReadyFlag)  
	       {
	       	Duster_module_Printf(1,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
			getSMSCenter();
			smsc_init_done = 1;
	       }
	}  

	data_per_page = SupportdisplaySMSNum;

	str = psm_get_wrapper(PSM_MOD_SMS,"flag","message_flag");

	if(str&&strncmp(str,	"",	1))
	{
		Duster_module_Printf(1,"%s, message_flag is %s",__FUNCTION__,str);
		str2  = psm_get_wrapper(PSM_MOD_SMS, "get_message", "page_number");
		if(str2)
		{
			page_num = ConvertStrToInteger(str2);
			sms_free(str2);
		}
		if(!strcasecmp(str , "GET_RCV_SMS_LOCAL"))
		{
			mem_store 	 =	  1;
			tags		 =    12;
			//	SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_SIM_SMS"))
		{
			mem_store =	  0;
			tags	  =	  12;
			//	SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_DRAFT_SMS"))
		{
			mem_store =  1;
			tags	   =  11;
			//	SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_SENT_SMS_LOCAL"))
		{
			mem_store =  1;
			tags		=  2;
			//	SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		sms_free(str);
		str = NULL;
		if(tags != 0)
			SMS_get_displaylist(root, mem_store, page_num, data_per_page, tags);
	}
	//psm_set_wrapper(PSM_MOD_SMS,	"flag",	"message_flag",		"");

	//added by notion ggj 20160120
	#ifndef NOTION_SMS_REPORT_ENABLE
	psm_set_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list",	"");
	Duster_module_Printf(1,"enter %s,sms_report_status_list clear!",__FUNCTION__);
       #endif
	   
	/*add new message ind*/
	pChild = MrvFindElement(root ,"new_sms_num");
	if(pChild)
	{

		for(nIndex = 0; nIndex<pChild->nSize; nIndex++)
		{
			MrvDeleteNode(&pChild->pEntries[nIndex]);
		}
		if (pChild->pEntries)
			duster_free(pChild->pEntries);
		pChild->pEntries = NULL;
		pChild->nMax = 0;
		pChild->nSize = 0;
		memset(tmpBuf,	'\0',	12);
		new_num = SMS_get_new_sms_num_for_longsms();//SMS_get_new_sms_num();modified by notion ggj 20151218 for new long sms 
		itoa(new_num,	tmpBuf,	10);
		MrvAddText(pChild,MrvStrdup(tmpBuf,0),1);
		SMS_set_sms_zero();
	}
	/*add sms_unread_long_num*/

	pChild = MrvFindElement(root ,"sms_capacity_info/sms_unread_long_num");
	if(pChild)
	{

		for(nIndex = 0; nIndex<pChild->nSize; nIndex++)
		{
			MrvDeleteNode(&pChild->pEntries[nIndex]);
		}
		if (pChild->pEntries)
			duster_free(pChild->pEntries);
		pChild->pEntries = NULL;
		pChild->nMax = 0;
		pChild->nSize = 0;
		memset(tmpBuf,	'\0',	12);
		total_unread_msg_num = SMS_calc_unread();
		itoa(total_unread_msg_num,	tmpBuf,	10);
		MrvAddText(pChild,MrvStrdup(tmpBuf,0),1);
	}
	str = psm_get_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd_status_result");
	if(str)
	{
		if(strncmp(str,	"",	1))
		{
			Duster_module_Printf(1,	"%s sms_cmd_status_result is %s",	__FUNCTION__,	str);
		}
		duster_free(str);
	}
	str = psm_get_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd");
	if(str)
	{
		if(strncmp(str,	"",	1))
		{
			Duster_module_Printf(1,	"%s sms_cmd is %s",	__FUNCTION__,	str);
		}
		duster_free(str);
	}
EXIT:
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
}

void SMS_post_set(int Action, dc_args_t *dca )
{
	char   	     *str = NULL,*str2 = NULL,result_Str[3],*smsc_Str = NULL,*save_location_str = NULL;
	UINT32  	  flag_value = 0;
	UINT8	      mem_store,page_num, data_per_page, tags,	set_location = 0;
	char	 	 *number=NULL,*sms_time=NULL,*messageBody=NULL,*id=NULL,*encode_type=NULL,*location=NULL,*time_formated = NULL;
	int 	      ret = 0;
	s_MrvFormvars     **list_start = NULL,**list_last=NULL;
	UINT8		  mem_to=0,MV_CP_FLAG=0;
	CommandATMsg  *sendSMSMsg = NULL;
	OSA_STATUS	   osa_status;
	char  	       tmp[8]= {0};
	char		   *sms_over_cs_str = NULL;
	int				sms_over_cs_set = 0;
	int 			sms_need_commit = 1;
	char		   *send_from_draft_id = NULL;
	char			*edit_draft_str = NULL;
	UINT8			edit_draft_id = 0;
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	UINT16			before_local_num = 0;

	Duster_module_Printf(1,"enter %s,Action = %d",__FUNCTION__,	Action);

	if(DUSTER_CONFIG_START  == Action)
	{
		SMS_Data_Init();/*modified by shoujunl; because sms gets ready before phonebook in sac side 131106*/
		PB_init();
		psm_set_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list",	"");
		if(SMS_FlgRef)
		{
		       if((0 == SMSReadyFlag)&&(smsc_init_done == 0))   //added by notion ggj 20160108
		       {
		              Duster_module_Printf(1,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
				ret = getSMSCenter();
				smsc_init_done = 1;
		       }
		}
		
		init_spec_name_match();

		sms_full_num = SupportSMSNum*(100 - sms_full_per)/100;
		
		return;
	}

	before_local_num = totalLocalSMSNum;
		
	if((smsc_init_done == 0) && SMS_FlgRef)
	{
	       if(0 == SMSReadyFlag)   //added by notion ggj 20160108
	       {
	       	Duster_module_Printf(1,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
			ret = getSMSCenter();
			smsc_init_done = 1;
	       }
	}
	data_per_page = SupportdisplaySMSNum;

	str = psm_get_wrapper(PSM_MOD_SMS,	"flag",	"message_flag");
	if(str)
	{
		Duster_module_Printf(1,"%s, message_flag is %s",__FUNCTION__,str);
		str2  = psm_get_wrapper(PSM_MOD_SMS, "get_message", "page_number");
		if(str2)
		{
			page_num = ConvertStrToInteger(str2);
			sms_free(str2);
		}
		if(!strcasecmp(str , "GET_RCV_SMS_LOCAL"))
		{
			mem_store =	  1;
			tags		 =    12;
			sms_need_commit = 0;
			//	SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_SIM_SMS"))
		{
			mem_store =	  0;
			tags	  =	  12;
			sms_need_commit = 0;
			//SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_DRAFT_SMS"))
		{
			mem_store =  1;
			tags	   =  11;
			sms_need_commit = 0;
			//SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "GET_SENT_SMS_LOCAL"))
		{
			mem_store =  1;
			tags		=  2;
			sms_need_commit = 0;
			//SMS_produceList(mem_store,page_num,data_per_page,tags); /* produce message_list*/
		}
		else if(!strcasecmp(str , "SEND_SMS")||!strcasecmp(str,"SAVE_SMS"))
		{
			number      = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "contacts");
			messageBody = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "content");
			encode_type = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "encode_type");
			sms_time    = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "sms_time");
			Duster_module_Printf(1,"%s number is %s,encode_type is %s,sms_time is %s",__FUNCTION__,number,encode_type,sms_time);
			/*Fix send NULL message fail when sending sms first time after burning new image-- start*/
			if(!messageBody)
			{
				psm_set_wrapper(PSM_MOD_SMS,	"send_save_message",	"content",	"");
				messageBody = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "content");
			}
			/*Fix send NULL message fail when sending sms first time after burning new image-- end*/
			if(number&&messageBody&&encode_type&&sms_time)
			{
				//Duster_module_Printf(1,"%s 2 number is %s,encode_type is %s,sms_time is %s",__FUNCTION__);
				time_formated = MrvCgiUnescapeSpecialChars(sms_time);
				if(!strcasecmp(str , "SEND_SMS"))
				{
					list_start = &LocalSendSMS_list_start;
					list_last  = &LocalSendSMS_list_last;
					if(!strchr(number,	','))/*Send SingSMS*/
					{
						ret = SMS_Send(number,messageBody,encode_type,time_formated,list_start,list_last,1,0);
						Duster_module_Printf(1,	"%s ret is %d",	__FUNCTION__,	ret);
					}
					else if(strchr(number,	','))
					{
						ret = SMS_massSend(number,messageBody,encode_type,time_formated,list_start,list_last,1,0);
					}
					send_from_draft_id = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "send_from_draft_id");
					if(send_from_draft_id && strncmp(send_from_draft_id,	"",	1))
					{
						Duster_module_Printf(1,	"%s send_from_draft_id is %s",	__FUNCTION__,	send_from_draft_id);
						SMS_delete_list(send_from_draft_id);
						psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "send_from_draft_id",	"");
					}
					if(send_from_draft_id)
					{
						duster_free(send_from_draft_id);
						send_from_draft_id = NULL;
					}
					memset(result_Str,'\0',3);
					if(ret<0)
						sprintf(result_Str,"%d",2);
					else
						sprintf(result_Str,"%d",3);

					Duster_module_Printf(1,	"%s	result_Str is %s",	__FUNCTION__,	result_Str);
					psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd_status_result",	result_Str);
					psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd",	"4");

					memset(tmp,   '\0',	  8);
					sprintf(tmp,  "%d",   totalLocalSentNum);
					psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
					psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);
				}
				else if(!strcasecmp(str , "SAVE_SMS"))
				{

					list_start = &LocalDraftSMS_list_start;
					list_last  = &LocalDraftSMS_list_last;
					edit_draft_str = psm_get_wrapper(PSM_MOD_SMS,	"send_save_message", "edit_draft_id");
					if(edit_draft_str && strncmp(edit_draft_str,	"",	1))
					{
						edit_draft_id = atoi(edit_draft_str+4);
					}
					if(edit_draft_str)
					{
						duster_free(edit_draft_str);
						edit_draft_str = NULL;
						psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "edit_draft_id",	"");
					}

					ret=SMS_Save(number,messageBody,encode_type,sms_time,1,0,list_start,list_last,	edit_draft_id);
					memset(result_Str,'\0',3);
					if(ret<0)
						sprintf(result_Str,"%d",2);
					else
						sprintf(result_Str,"%d",3);
					psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd_status_result",	result_Str);
					psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd",	"5");
				}
			}
			else
			{
				psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd_status_result",	"2");
				psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd",	"5");
			}
			if(number)
				sms_free(number);
			if(messageBody)
				sms_free(messageBody);
			if(encode_type)
				sms_free(encode_type);
			if(sms_time)
				sms_free(sms_time);
			if(time_formated)
				sms_free(time_formated);

			psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "contacts",	"");
			psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "content",		"");
			psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "encode_type",	"");
			psm_set_wrapper(PSM_MOD_SMS,	"send_save_message", "sms_time",	"");
			psm_commit_other__(handle_SMS1);
		}
		else if(!strcasecmp(str , "DELETE_SMS"))
		{
			str2 = psm_get_wrapper(PSM_MOD_SMS,	"set_message",	"delete_message_id");
			if(str2)
			{
				Duster_module_Printf(1,"%s delete_sms id is %s",__FUNCTION__,str2);
				ret = SMS_delete_list(str2);
				memset(result_Str,	'\0',	3);
				if(ret<0)
					sprintf(result_Str,"%d",2);
				else
					sprintf(result_Str,"%d",3);
				sms_free(str2);
			}
			else
			{
				memset(result_Str,	'\0',	3);
				sprintf(result_Str,"%d",2);
			}
			psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd_status_result",	result_Str);
			psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd",	"6");


			memset(tmp,   '\0',	  8);
			sprintf(tmp,  "%d",   totalLocalSMSNum);
			psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);

			memset(tmp,   '\0',	  8);
			sprintf(tmp,  "%d",   totalLocalSentNum);
			psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);

			memset(tmp,   '\0',	  8);
			sprintf(tmp,  "%d",   totalLocalDraftNum);
			psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp,	handle_SMS1);
			gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
			UIRefresh_Sms_Change();
			psm_commit_other__(handle_SMS1);
		}
		else if(!strcasecmp(str,	"DELETE_ALL_SMS"))
		{
			str2 = psm_get_wrapper(PSM_MOD_SMS,	"set_message",	"delete_all_sms_type");
			if(str2 && strncmp(str2,	"",	1))
			{
				if(strcasecmp(str2,	"INBOX") == 0)
				{
					SMS_clear_list_raw_data(&LocalSMS_list_start,	&LocalSMS_list_last,	1);
					gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
					totalLocalSMSNum = 0;
					psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_rev_num",	"0",	handle_SMS1);
					UIRefresh_Sms_Change();
				}
				else if(strcasecmp(str2,	"SENTBOX") == 0)
				{
					SMS_clear_list_raw_data(&LocalSendSMS_list_start,	&LocalSendSMS_list_last,	2);
					totalLocalSentNum = 0;
					psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_send_num",	"0",	handle_SMS1);
					UIRefresh_Sms_Change();
				}
				else if(strcasecmp(str2,	"DRAFTBOX") == 0)
				{
					SMS_clear_list_raw_data(&LocalDraftSMS_list_start,	&LocalDraftSMS_list_last,	3);
					totalLocalDraftNum = 0;
					psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	"0",	handle_SMS1);
					UIRefresh_Sms_Change();
				}
				else if(strcasecmp(str2,	"SIMSMS") == 0)
				{
					SMS_clear_list_raw_data(&SimSMS_list_start,	&SimSMS_list_last,	1);
					gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
					totalSimSmsNum = 0;
					UIRefresh_Sms_Change();
				}
				psm_commit_other__(handle_SMS1);
				psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd_status_result",	"3");
				psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd",	"6");
			}
			if(str2)
			{
				duster_free(str2);
				str2 = NULL;
			}
		}
		else if(!strcasecmp(str , "SET_MSG_READ"))
		{
			str2 = psm_get_wrapper(PSM_MOD_SMS,	"set_message",	"read_message_id");
			if(str2)
			{
				Duster_module_Printf(1,"%s set_msg_read id is %s", __FUNCTION__, str2);
				ret=SMS_read_list(str2);
				if(ret<0)
					sprintf(result_Str,"%d",2);
				else
					sprintf(result_Str,"%d",3);
				psm_set_wrapper(PSM_MOD_SMS, "flag",	"sms_cmd_status_result",	result_Str);
				psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd",	"6");
				sms_free(str2);
			}
			else
				psm_set_wrapper(PSM_MOD_SMS,NULL,	"set_read_result", 	"2");
			gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
			UIRefresh_Sms_Change();
			psm_commit_other__(handle_SMS1);
		}
		else if(!strcasecmp(str ,   "MOVE_SMS_TO_LOCAL") || !strcasecmp(str , "COPY_SMS_TO_SIM")
		        ||!strcasecmp(str , "MOVE_SMS_TO_SIM")   || !strcasecmp(str , "COPY_SMS_TO_LOCAL"))
		{
			if(!strcasecmp(str ,   "MOVE_SMS_TO_LOCAL"))
			{
				mem_to     = 1;
				MV_CP_FLAG = 1;
			}
			else if(!strcasecmp(str , "COPY_SMS_TO_SIM"))
			{
				mem_to 	 = 0;
				MV_CP_FLAG = 0;
			}
			else if(!strcasecmp(str , "MOVE_SMS_TO_SIM"))
			{
				mem_to     = 0;
				MV_CP_FLAG = 1;
			}
			else if(!strcasecmp(str , "COPY_SMS_TO_LOCAL"))
			{
				mem_to     = 1;
				MV_CP_FLAG = 0;
			}
			str2 = psm_get_wrapper(PSM_MOD_SMS,	"set_message",	"mv_cp_id");
			if(str2)
			{
				Duster_module_Printf(1,"%s, mv_cp_id is %s",__FUNCTION__,str2);
				ret = SMS_MV_CP( str2,mem_to,MV_CP_FLAG);
				memset(result_Str,'\0',3);
				if(ret < 0)
					sprintf(result_Str,"%d",2);
				else if(ret == 2)
					sprintf(result_Str,"%d",9);//parftfailed
				else
					sprintf(result_Str,"%d",3);
				psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd_status_result",	result_Str);
				psm_set_wrapper(PSM_MOD_SMS,	"flag",	"cmd",	"8");
				sms_free(str2);
			}
			else
			{
				Duster_module_Printf(1,"%s,can't get mv_cp_id",__FUNCTION__);
				psm_set_wrapper(PSM_MOD_SMS,	NULL,	"mv_cp_sms_result",	"2");
			}

			memset(tmp,   '\0',	  8);
			sprintf(tmp,  "%d",   totalLocalSMSNum);
			psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);

			memset(tmp,   '\0',	  8);
			sprintf(tmp,  "%d",   totalLocalSentNum);
			psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);
			gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
			UIRefresh_Sms_Change();
			psm_commit_other__(handle_SMS1);
		}
		else if(!strcasecmp(str , "SET_SMS_CENTER"))
		{
			smsc_Str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_center");
			if((smsc_Str)&&(0==SMSReadyFlag)) //added by notion ggj 20160115
			{
	       	       Duster_module_Printf(1,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
				ret = getSMSCenter();
				if(strcmp(smsc_Str , csca_resp_str) && strcmp(smsc_Str,	DEFAULT_SMSC_ADDR))//add proction not to change SMSC address by unkonwn reason
				{
					Duster_module_Printf(1,"%s,old smsc is %s,new smsc is %s",__FUNCTION__, csca_resp_str, smsc_Str);
					sendSMSMsg=(CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
					sendSMSMsg->atcmd=sms_malloc(DIALER_MSG_SIZE);
					sendSMSMsg->ok_fmt=NULL;
					sendSMSMsg->err_fmt=sms_malloc(15);
					if(!sendSMSMsg || !sendSMSMsg->atcmd|| !sendSMSMsg->err_fmt)
					{
						Duster_module_Printf(1,"%s sms_malloc fail DUSTER_ASSERT",__FUNCTION__);
						DUSTER_ASSERT(0);
					}
					memset(sendSMSMsg->atcmd,'\0',DIALER_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,"AT+CSCA=\"%s\",145\r",smsc_Str);
					sendSMSMsg->ok_flag=1;
					memset(sendSMSMsg->err_fmt,	'\0',	15);
					sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
					sendSMSMsg->callback_func=&CSCA_SET_CALLBACK;
					sendSMSMsg->ATCMD_TYPE=TEL_EXT_GET_CMD;
					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);
					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CSCA_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);
					if(flag_value == CSCA_SET_CMD)
					{
						psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"3"); //sucess
						memset(csca_resp_str,	'\0',	DIALER_MSG_SIZE);
						memcpy(csca_resp_str,smsc_Str,strlen(smsc_Str));
						psm_set_wrapper(PSM_MOD_SMS,"sms_setting","sms_center",smsc_Str);
					}
					else
						psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"2"); //sucess
					psm_set_wrapper(PSM_MOD_SMS,	"flag",		"cmd",	"3");

					if(sendSMSMsg)
					{
						if(sendSMSMsg->atcmd)
							sms_free(sendSMSMsg->atcmd);
						if(sendSMSMsg->ok_fmt)
							sms_free(sendSMSMsg->ok_fmt);
						if(sendSMSMsg->err_fmt)
							sms_free(sendSMSMsg->err_fmt);
						sms_free(sendSMSMsg);
					}

				}
			}
			save_location_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"save_location");
			if(save_location_str&&strncmp(save_location_str,	"",	1))
			{
				set_location = ConvertStrToInteger(save_location_str);
				Duster_module_Printf(1,	"%s save_location_str is %d",	__FUNCTION__,	save_location_str);
				if(set_location != save_location)
				{
					save_location = set_location;
					sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
					DUSTER_ASSERT(sendSMSMsg);
					memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));
					sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
					DUSTER_ASSERT(sendSMSMsg->atcmd);
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
					sendSMSMsg->callback_func=&CNMI_SET_CALLBACK;
					sendSMSMsg->ok_flag	=	0;
					sendSMSMsg->err_fmt = sms_malloc(15);
					DUSTER_ASSERT(sendSMSMsg->err_fmt);
					memset(sendSMSMsg->err_fmt,	'\0',	15);
					sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
					sendSMSMsg->ok_fmt=NULL;
					sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
					if(set_location == 1)
						sprintf(sendSMSMsg->atcmd,"at+cnmi=0,2,0,1,0\r");
					else if(set_location == 0)
						sprintf(sendSMSMsg->atcmd,"at+cnmi=0,1,0,1,0\r");

					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CNMI_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					switch(flag_value)
					{
					case CMS_ERROR:
						Duster_module_Printf(3,"%s AT+CNMI failed",__FUNCTION__);
						psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"2"); //sucess
						break;
					case CNMI_SET_CMD:
						psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"3"); //sucess
						break;
					}

					psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"cmd",	"3");
					if(sendSMSMsg)
					{
						if(sendSMSMsg->atcmd)
							sms_free(sendSMSMsg->atcmd);
						if(sendSMSMsg->ok_fmt)
							sms_free(sendSMSMsg->ok_fmt);
						if(sendSMSMsg->err_fmt)
							sms_free(sendSMSMsg->err_fmt);
						sms_free(sendSMSMsg);
					}

				}
			}
			if(save_location_str)
				sms_free(save_location_str);
			sms_over_cs_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_over_cs");
			if(sms_over_cs_str&&strncmp(sms_over_cs_str,	"",	1))
			{
				sms_over_cs_set = atoi(sms_over_cs_str);
				Duster_module_Printf(1,	"%s statoc sms_over_cs is%d, set is %d",	__FUNCTION__, sms_over_cs	,sms_over_cs_set);
				if(sms_over_cs != sms_over_cs_set)
				{
					if((sms_over_cs_set >=0) &&(sms_over_cs_set<= 1))
					{
						sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
						DUSTER_ASSERT(sendSMSMsg);
						memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));
						sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
						DUSTER_ASSERT(sendSMSMsg->atcmd);
						memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
						sendSMSMsg->callback_func=&CGSMS_SET_CALLBACK;
						sendSMSMsg->ok_flag=0;
						sendSMSMsg->err_fmt = sms_malloc(15);
						DUSTER_ASSERT(sendSMSMsg->err_fmt);
						memset(sendSMSMsg->err_fmt,	'\0',	15);
						sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
						sendSMSMsg->ok_fmt=NULL;
						sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
						if(sms_over_cs_set == 1)
						{
							sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	0);
						}
						else if(sms_over_cs_set == 0)
						{
							sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	1);
						}

						osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
						DUSTER_ASSERT(osa_status==OS_SUCCESS);

						flag_value=0;
						osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CGSMS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
						//DUSTER_ASSERT(osa_status==OS_SUCCESS);

						switch(flag_value)
						{
						case CMS_ERROR:
							Duster_module_Printf(3,"%s AT+CGSMS failed",__FUNCTION__);
							psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"2"); //sucess
							break;
						case CGSMS_SET_CMD:
							psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"3"); //sucess
							break;
						}

						if(sendSMSMsg)
						{
							if(sendSMSMsg->atcmd)
								sms_free(sendSMSMsg->atcmd);
							if(sendSMSMsg->ok_fmt)
								sms_free(sendSMSMsg->ok_fmt);
							if(sendSMSMsg->err_fmt)
								sms_free(sendSMSMsg->err_fmt);
							sms_free(sendSMSMsg);
						}
						sms_over_cs = sms_over_cs_set;
					}
				}
			}
			if(sms_over_cs_str)
			{
				sms_free(sms_over_cs_str);
				sms_over_cs_str = NULL;
			}
			psm_set_wrapper(PSM_MOD_SMS,	"flag", "message_flag",	"");//clear this flag
			psm_commit__();
		}
		else if(!strcasecmp(str,	"SMS_SET_ALL_READ"))
		{
			CPUartLogPrintf("%s SMS SET ALL READ",	__FUNCTION__);
			SMS_read_By_list(SimSMS_list_start,	SimSMS_list_last);
			SMS_read_By_list(LocalSMS_list_start,	LocalSMS_list_last);
			psm_set_wrapper(PSM_MOD_SMS,	"flag",	"sms_cmd_status_result",	"3");
			psm_set_wrapper(PSM_MOD_SMS,	"flag", "sms_cmd",	"6");
			psm_commit__();
			psm_commit_other__(handle_SMS1);
			gDialer2UIPara.unreadLocalSMSNum = 0;
			UIRefresh_Sms_Change();
		}
		sms_free(str);
	}
	//psm_set_wrapper(PSM_MOD_SMS,	"flag",	"message_flag",	"");
#if 0
	save_location_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"save_location");
	if(save_location_str&&strncmp(save_location_str,	"",	1))
	{
		set_location = ConvertStrToInteger(save_location_str);
		Duster_module_Printf(1,	"%s save_location_str is %s",	__FUNCTION__,	save_location_str);
		if(set_location != save_location)
		{
			save_location = set_location;
			sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
			DUSTER_ASSERT(sendSMSMsg);
			memset(sendSMSMsg,	0,	sizeof(CommandATMsg));
			sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
			DUSTER_ASSERT(sendSMSMsg->atcmd);
			memset(sendSMSMsg->atcmd,0,DIALER_CMD_MSG_SIZE);
			sendSMSMsg->callback_func=&CNMI_SET_CALLBACK;
			sendSMSMsg->ok_flag=0;
			sendSMSMsg->err_fmt = sms_malloc(15);
			DUSTER_ASSERT(sendSMSMsg->err_fmt);
			memset(sendSMSMsg->err_fmt,0,15);
			sprintf(sendSMSMsg->err_fmt,"ERROR");
			sendSMSMsg->ok_fmt=NULL;
			sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
			if(set_location == 1)
				sprintf(sendSMSMsg->atcmd,"at+cnmi=0,2,0,1,0\r");
			else if(set_location == 0)
				sprintf(sendSMSMsg->atcmd,"at+cnmi=0,1,0,1,0\r");

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CNMI_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			switch(flag_value)
			{
			case CMS_ERROR:
				Duster_module_Printf(3,"%s AT+CNMI failed",__FUNCTION__);
				psm_set_wrapper(PSM_MOD_SMS,NULL,"setSmsSetting_result","2"); //fail
				break;
			case CNMI_SET_CMD:
				psm_set_wrapper(PSM_MOD_SMS,NULL,"setSmsSetting_result","3"); //sucess
				break;
			}
			if(sendSMSMsg)
			{
				if(sendSMSMsg->atcmd)
					sms_free(sendSMSMsg->atcmd);
				if(sendSMSMsg->ok_fmt)
					sms_free(sendSMSMsg->ok_fmt);
				if(sendSMSMsg->err_fmt)
					sms_free(sendSMSMsg->err_fmt);
				sms_free(sendSMSMsg);
			}
		}
	}
	if(save_location_str)
		sms_free(save_location_str);
#endif
	psm_set_wrapper(PSM_MOD_SMS,	"set_message",	"delete_message_id",	"");
	psm_set_wrapper(PSM_MOD_SMS,	"set_message",	"read_message_id",	"");
	psm_set_wrapper(PSM_MOD_SMS,	"set_message",	"mv_cp_id",	"");

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   SupportSMSNum + MaxDraftSMSNum + MaxSentSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_total", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   SupportSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_total", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);

	memset(tmp,   '\0',	  8);
	total_complete_single = SMS_calc_recv_complete_single(LocalSMS_list_start,	LocalSMS_list_last);
	sprintf(tmp,  "%d",   total_complete_single);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num_complete", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   MaxSentSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   MaxDraftSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalDraftNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   gMem1TotalNum);	/*Change SIM Total to Default SIM CARD totals*/
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimSentSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_send_total",   tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimDraftSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_draftbox_total",   tmp);

	/*add messages' number countend by long sms start @140114 start*/
	totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalNum_LongSMS);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_long_num", tmp);


	totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum_LongSMS);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_long_num", tmp);

	total_SIMSMS_Long_num= SMS_calc_Long(SimSMS_list_start, SimSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_SIMSMS_Long_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_long_num", tmp);
	/*add messages' number countend by long sms start @140114 end*/

	/*add unread message number @140207 start*/
	total_unread_msg_num = SMS_calc_unread();
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_unread_msg_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_unread_long_num", tmp);
	/*add unread message number @140207 end*/

	/*add unread message number @141113 start*/
	if(!PlatformIsTPMifi())
	{
		total_unread_msg_num = SMS_calc_unread_List(LocalSMS_list_start,LocalSMS_list_last);
	}
	else
	{
		total_unread_msg_num = SMS_calc_unread_complete(LocalSMS_list_start,LocalSMS_list_last);
	}
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_unread_msg_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_unread_long_num_device", tmp);
	/*add unread message number @141113 end*/

	Duster_module_Printf(1,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",
	                     __FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);
	if(sms_need_commit == 1)
	{
		psm_commit__();
	}

	if(before_local_num < sms_full_num && totalLocalSMSNum >= sms_full_num)
	{
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(1);
	}
	else if(before_local_num >= sms_full_num && totalLocalSMSNum < sms_full_num)
	{
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(0);
	}

	
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
}

/****************************************************
function description : read sim sms when uboot
*****************************************************/
void SMS_readSim_List(void)
{
	UINT32 					i,readSimSmsNum = 0, flag_value;
	OS_STATUS 				osa_status;
	char 	  	   			*str        =  NULL;
	CommandATMsg   			*sendSMSMsg =  NULL;
	char*					 sms_over_cs_str = NULL;
	int						 sms_over_cs_set = 0;

	CPUartLogPrintf("enter %s sim_is_ready is %d",__FUNCTION__,	sim_is_ready);

	osa_status = OSASemaphoreAcquire(PhoneBookSema, OS_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS );
	if(sim_is_ready == 1)
	{
		goto EXIT;
	}

	if(!SMS_FlgRef)
	{
		osa_status = OSAFlagCreate( &SMS_FlgRef );
		DUSTER_ASSERT(osa_status == OS_SUCCESS);
	}

	readSimSmsNum=1;

	//wait for SMS is ready
	i=0;
	while(SMSReadyFlag)
	{
		i++;
		if (i >= 180)            //sim is absent     //modified 60 to 180 by notion ggj for SMS init wait
			goto EXIT;

		OSATaskSleep(200);     /*sleep 1s*/
		CPUartLogPrintf("%s SMSReadyFlag is 1,i=%d",__FUNCTION__,	i);
	}

	sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
	if(sendSMSMsg == NULL)
	{
		Duster_module_Printf(1,"sms_malloc sendSMSMsg failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));

	Duster_module_Printf(1,"%s: SMS is ready",__FUNCTION__);
	str = psm_get_wrapper(PSM_MOD_SMS,NULL,"referenceNum");
	if(str)
	{
		referenceNum=ConvertStrToInteger(str);
		sms_free(str);
	}

	// get SIM capability
	sendSMSMsg->atcmd=sms_malloc(DIALER_CMD_MSG_SIZE);
	DUSTER_ASSERT(sendSMSMsg->atcmd);
	if(sendSMSMsg->atcmd)
	{
		memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
		sprintf(sendSMSMsg->atcmd,"AT+CPMS?\r");
	}
	sendSMSMsg->ok_flag=1;
	sendSMSMsg->ok_fmt=sms_malloc(10);
	if(sendSMSMsg->ok_fmt)
	{
		memset(sendSMSMsg->ok_fmt,	'\0',	10);
		sprintf(sendSMSMsg->ok_fmt,"+CPMS");
	}
	sendSMSMsg->err_fmt=sms_malloc(15);
	if(sendSMSMsg->err_fmt)
	{
		memset(sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	}
	sendSMSMsg->callback_func=&CPMS_GET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_GET_CMD;

	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	flag_value=0;
	osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CPMS_GET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	switch(flag_value)
	{
	case CMS_ERROR:
		Duster_module_Printf(1,"%s, CPMS ERROR",__FUNCTION__);
		goto EXIT;
	case CPMS_GET_CMD:
		break;
	}

	//PDU mode is default mode

	// lock SIM status
	sendSMSMsg->ok_flag = 0;
	memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
	sprintf(sendSMSMsg->atcmd, "AT^LKSMSSTA=1\r");
	if(sendSMSMsg->ok_fmt)
	{
		sms_free(sendSMSMsg->ok_fmt);
		sendSMSMsg->ok_fmt=NULL;
	}
	sendSMSMsg->callback_func=&Init_LKSMSSTA_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;

	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	flag_value=0;
	osa_status = OSAFlagWait(SMS_FlgRef, INIT_LKSMSSTA_ERROR|INIT_LKSMSSTA_SET,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
	//DUSTER_ASSERT(osa_status==OS_SUCCESS);

	switch(flag_value)
	{
	case INIT_LKSMSSTA_ERROR:
		Duster_module_Printf(1,"%s AT^LKSMSSTA failed",__FUNCTION__);
		goto EXIT;
	case INIT_LKSMSSTA_SET:
		break;
	}

	Duster_module_Printf(1,"%s: gMem1TotalNum = %u",__FUNCTION__, gMem1TotalNum);
	Duster_module_Printf(1,"%s: gMem1MsgNum = %u",__FUNCTION__, gMem1MsgNum);

	if(gMem1MsgNum>0)
		for(i=1; i<=gMem1TotalNum; i++)
		{

			memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CMGR=%d\r",i);
			sendSMSMsg->ok_fmt=sms_malloc(10);
			memset(sendSMSMsg->ok_fmt,	'\0',	10);
			sprintf(sendSMSMsg->ok_fmt,"+CMGR:");
			sendSMSMsg->ok_flag=1;
			sendSMSMsg->ATCMD_TYPE=i;//ATCMD_TYPE acts as index
			sendSMSMsg->callback_func=&CMGR_SET_CALLBACK;

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMGR_ERROR|CMGR_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
			//DUSTER_ASSERT(osa_status==OS_SUCCESS);

			if(flag_value == CMGR_ERROR || osa_status == OS_TIMEOUT)
				continue;
			if(flag_value == CMGR_SET_CMD)
			{
				readSimSmsNum++;
				Duster_module_Printf(3,"%s readSimSmsNum=%d",__FUNCTION__,readSimSmsNum);
				if(readSimSmsNum >	gMem1MsgNum)
				{
					Duster_module_Printf(3,"readSimSmsNum is %u",readSimSmsNum);
					Duster_module_Printf(3,"gMem1MsgNum is %u",gMem1MsgNum);
					break;
				}
				if(readSimSmsNum > SupportSimSMSNum)
				{
					Duster_module_Printf(3,"readSimSmsNum is %u,gMem1MsgNum is %u",readSimSmsNum,gMem1MsgNum);
					break;
				}
			}

		}


#if 0
	if(gMem1MsgNum >=  SupportSimSMSNum)
		totalSimSmsNum = SupportSimSMSNum;
	else
		totalSimSmsNum = gMem1MsgNum;
#endif

	totalSimSmsNum = gMem1MsgNum;
	totalSmsNum = totalSimSmsNum + totalLocalSMSNum;

	Duster_module_Printf(3,"%s: totalSimSmsNum = %u ",__FUNCTION__, totalSimSmsNum);
	Duster_module_Printf(3,"%s: gMem1TotalNum = %u ",__FUNCTION__, gMem1TotalNum);



	// unlock SIM status
	sendSMSMsg->ok_flag = 0;
	memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
	sprintf(sendSMSMsg->atcmd, "AT^LKSMSSTA=0\r");
	if(sendSMSMsg->ok_fmt)
	{
		sms_free(sendSMSMsg->ok_fmt);
		sendSMSMsg->ok_fmt=NULL;
	}
	sendSMSMsg->callback_func=&LKSMSSTA_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;


	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	flag_value=0;
	osa_status = OSAFlagWait(SMS_FlgRef, LKSMSSTA_ERROR|LKSMSSTA_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
	//DUSTER_ASSERT(osa_status==OS_SUCCESS);

	switch(flag_value)
	{
	case LKSMSSTA_ERROR:
		Duster_module_Printf(3,"%s AT^LKSMSSTA failed",__FUNCTION__);
		goto EXIT;
	case LKSMSSTA_SET_CMD:
		break;
	}

	str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"save_location");
	if(str)
	{
		if(strncmp(str,	"",	1))
			save_location = ConvertStrToInteger(str);
		Duster_module_Printf(1,	"%s save_location is %d",	__FUNCTION__,	save_location);
		sms_free(str);
	}
	DUSTER_ASSERT((save_location == 0)||(save_location == 1));
	memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
	if(save_location == 1)
		sprintf(sendSMSMsg->atcmd,"at+cnmi=0,2,0,1,0\r");
	else if(save_location == 0)
		sprintf(sendSMSMsg->atcmd,"at+cnmi=0,1,0,1,0\r");
	sendSMSMsg->callback_func=&CNMI_SET_CALLBACK;
	sendSMSMsg->ok_flag=0;
	memset(sendSMSMsg->err_fmt,	'\0',	15);
	sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;

	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	flag_value=0;
	osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CNMI_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
	//DUSTER_ASSERT(osa_status==OS_SUCCESS);

	switch(flag_value)
	{
	case CMS_ERROR:
		Duster_module_Printf(3,"%s AT+CNMI failed",__FUNCTION__);
		goto EXIT;
	case CNMI_SET_CMD:
		break;
	}

	sms_over_cs_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_over_cs");
	if(sms_over_cs_str && strncmp(sms_over_cs_str,	"",	1))
	{
		sms_over_cs_set = atoi(sms_over_cs_str);
	}
	if(sms_over_cs_str)
	{
		sms_free(sms_over_cs_str);
		sms_over_cs_str = NULL;
	}
	if((sms_over_cs_set >=0) &&(sms_over_cs_set<= 1))
	{
		DUSTER_ASSERT(sendSMSMsg);
		DUSTER_ASSERT(sendSMSMsg->atcmd);
		memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
		sendSMSMsg->callback_func=&CGSMS_SET_CALLBACK;
		sendSMSMsg->ok_flag=0;
		DUSTER_ASSERT(sendSMSMsg->err_fmt);
		memset(sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
		sendSMSMsg->ok_fmt=NULL;
		sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
		if(sms_over_cs_set == 1)
		{
			sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	0);
		}
		else if(sms_over_cs_set == 0)
		{
			sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	1);
		}

		osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
		DUSTER_ASSERT(osa_status==OS_SUCCESS);

		flag_value=0;
		osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CGSMS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
		//DUSTER_ASSERT(osa_status==OS_SUCCESS);

		switch(flag_value)
		{
		case CMS_ERROR:
			Duster_module_Printf(3,"%s AT+CGSMS failed",__FUNCTION__);
			psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"2"); //sucess
			break;
		case CGSMS_SET_CMD:
			psm_set_wrapper(PSM_MOD_SMS,	"flag", 	"sms_cmd_status_result",	"3"); //sucess
			break;
		}
		sms_over_cs = sms_over_cs_set;
	}

	sim_is_ready=1;


	if(DM_first_flag==0)
		DM_SENDMSQ();

EXIT:

	if(sendSMSMsg)
	{
		if(sendSMSMsg->atcmd != NULL)
		{
			sms_free(sendSMSMsg->atcmd);
			sendSMSMsg->atcmd=NULL;
		}
		if(sendSMSMsg->ok_fmt != NULL)
		{
			sms_free(sendSMSMsg->ok_fmt);
			sendSMSMsg->ok_fmt=NULL;
		}
		if(sendSMSMsg->err_fmt != NULL)
		{
			sms_free(sendSMSMsg->err_fmt);
			sendSMSMsg->err_fmt=NULL;
		}
		if(sendSMSMsg != NULL)
		{
			sms_free((void*)sendSMSMsg);
		}
	}

	/*Fix bug 510512, Forget to refresh SMS status when diable PIN start*/
	gDialer2UIPara.unreadLocalSMSNum= SMS_calc_unread();
	if(gDialer2UIPara.unreadLocalSMSNum > 0)
		gDialer2UIPara.SMSlist_new_Flag = 1;
	UIRefresh_Sms_Change();
	/*Fix bug 510512, Forget to refresh SMS status when diable PIN end*/
	OSASemaphoreRelease(PhoneBookSema);
	Duster_module_Printf(1,"leave %s",__FUNCTION__);

}
void SMS_Data_Init()
{
	Duster_module_Printf(1,	"enter %s",	__FUNCTION__);
	SMS_Set_LocalSMS_Init(0);
	{
		#ifdef NOTION_ENABLE_JSON
		char* p_message_time_index = NULL;
       	p_message_time_index = psmfile_get_wrapper(PSM_MOD_SMS,NULL,	"sms_message_time_index",	handle_SMS1);
		Duster_module_Printf(1,	"%s,p_message_time_index:%s",	__FUNCTION__,p_message_time_index);
		if(p_message_time_index && strncmp(p_message_time_index,"",1))
		{
			message_time_index = atoi(p_message_time_index);
			Duster_module_Printf(1,	"%s,message_time_index:%s",__FUNCTION__,message_time_index);			
		}
		else
		{
			message_time_index = 0;
		}
       	#endif
	}   
	SMS_getLocalList(&LocalSMS_list_start,      &LocalSMS_list_last,       1);
	SMS_getLocalList(&LocalSendSMS_list_start,  &LocalSendSMS_list_last,   2);
	SMS_getLocalList(&LocalDraftSMS_list_start, &LocalDraftSMS_list_last,  3);

	   
	gDialer2UIPara.unreadLocalSMSNum= SMS_calc_unread();
	if(gDialer2UIPara.unreadLocalSMSNum > 0)
		gDialer2UIPara.SMSlist_new_Flag = 1;
	totalSmsNum = totalSimSmsNum + totalLocalSMSNum;
	UIRefresh_Sms_Change();

	totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
	totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	SMS_Set_LocalSMS_Init(1);

	SMS_readSim_List();
	gDialer2UIPara.unreadLocalSMSNum= SMS_calc_unread();
	if(gDialer2UIPara.unreadLocalSMSNum > 0)
		gDialer2UIPara.SMSlist_new_Flag = 1;

	totalSmsNum = totalSimSmsNum + totalLocalSMSNum;
	Duster_module_Printf(3,"%s: totalSimSmsNum = %u ",__FUNCTION__, totalSimSmsNum);
	Duster_module_Printf(3,"%s: gMem1TotalNum = %u ",__FUNCTION__, gMem1TotalNum);
	UIRefresh_Sms_Change();

	Duster_module_Printf(1,	"leave %s",	__FUNCTION__);
}

int RcvSMSTask()
{
	UINT16 		i,index=0;
	int			smsc_length;
	UINT32		dataLength=0;
	char 	   *p=NULL, *p1=NULL,*p2=NULL,*p3=NULL;
	char 	   *parsebuf=NULL;
	char        SMS_index[12] = "LRCV";
	char        Msg_buf[TEL_AT_SMS_MAX_LEN] = {0};
	char	    sms_cbuf[200]= {0};
	char        tmp[20] = {0};
	OSA_STATUS  osa_status;
	UINT8 		smsaddrIdx=1;
	UINT8		ret;
	DialRespMessage 	 resp_msg;
	DialCmdMessage		 cmd;
	s_MrvFormvars 			*tempData;
	SMS_Node 			*tempSMS	=	NULL;
	int 				ghandle = handle_SMS1;
	CommandATMsg		*sendSMSMsg = NULL;
	UINT32				flag_value = 0;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	if(!SMS_FlgRef)
	{
		osa_status = OSAFlagCreate( &SMS_FlgRef );
		DUSTER_ASSERT(osa_status == OS_SUCCESS);
	}
	//SMS_Data_Init();/*modified by shoujunl; because sms gets ready before phonebook in sac side 131106*/
	//PB_init();
//#ifdef NEZHA_MIFI_V1_R0
//	gDialer2UIPara.unreadLocalSMSNum = calc_unread_SMSTest(LocalSMS_list_start,LocalSMS_list_last);
//	UIRefresh_Sms_Change();
//#endif

	while(1)
	{

		Duster_module_Printf(3,"%s: wait msg",__FUNCTION__);
		osa_status = OSAMsgQRecv(RcvSMSMsgQ, (UINT8 *)&resp_msg, sizeof(resp_msg), OSA_SUSPEND);

		if((osa_status != OS_SUCCESS) && (osa_status != OS_TIMEOUT))
		{
			DUSTER_ASSERT(0);
		}

		if(osa_status == OS_TIMEOUT)
		{
			return -3;
		}

		//don't support deal with msg in txt mode in RcvSMSTask
		if(gSmsFormatMode)
			continue;

		memset(Msg_buf, '\0', TEL_AT_SMS_MAX_LEN);

		if(resp_msg.MsgData != NULL)
		{
			memcpy((char *)Msg_buf, (char *)resp_msg.MsgData, strlen((char *)resp_msg.MsgData));
			Duster_module_Long_Printf(3,"resp_msg: length is %d,  str is %s", strlen((char *)resp_msg.MsgData), (char *)resp_msg.MsgData);
			if(strstr(Msg_buf,	"+CBM"))
			{
				Duster_module_Printf(1,	"recv CBM message, not support");
				free(resp_msg.MsgData);
				continue;
			}
			if(strstr(Msg_buf,	"+CBS"))
			{
				Duster_module_Printf(1,	"recv CBS message, not support");
				free(resp_msg.MsgData);
				continue;
			}
			#ifdef DUSTER_USSD_SUPPORT
			if(strstr(Msg_buf, "+CUSD"))
			{
				process_ussd_ind_data(Msg_buf);
			}
			#endif
			/*Work around for issue 500136 ASSERT when free MsgData*/
			if(strstr(Msg_buf,"+CMT:") || strstr(Msg_buf,"+CMTI:") || strstr(Msg_buf,"+CDS:"))
			{
				free(resp_msg.MsgData);
			}
			if((p1 = strstr(Msg_buf,"+CMT:")))
			{
				if(p1)
				{
					p = p1;
					Duster_module_Printf(1,"p1-p=%d",p1-p);
				}

				else if(p2)
					p = p2;
				else
					p = p3;

				//get pdu length
				p = p+6;


				memcpy(tmp, p, 5);
				for(i=0; i<5; i++)
				{
					if(tmp[i] == '\r')
					{
						tmp[i]='\0';
						break;
					}
				}
				dataLength = ConvertStrToInteger(tmp);
				duster_Printf("dataLength is %u",dataLength);

				//skip SMSC part
				p+=(i+2);
				memset(tmp,	'\0',	5);
				memcpy(tmp, p, 2);
				smsc_length = ConvertASCToChar(tmp);

				Duster_module_Printf(3,"smsc_length=%d",smsc_length);


				memset(sms_cbuf, 	'\0', 	162);
				convertStrtoArray((p+(smsc_length<<1)+2), sms_cbuf, 162);

				memset(SMSsrcAddr,	'\0',	CI_MAX_ADDRESS_LENGTH+1);

				smsaddrIdx=1;
				ret	=	libReadSmsAddress((UINT8 *)sms_cbuf,&smsaddrIdx,(UINT8*)SMSsrcAddr);
				if(ret==0)
				{
					continue;
				}
				Duster_module_Printf(3,"in RcvSMSTask,SMSsrcAddr=%s",SMSsrcAddr);
				if(0==strcmp(SMSsrcAddr,DM_ADDR))
				{
					Duster_module_Printf(3,"IMSI=%s",IMSI);
					psm_set_wrapper(PSM_MOD_IMSI,NULL,"IMSI",IMSI);
					psm_commit__();
					Duster_module_Printf(1,"Receieve %s",DM_ADDR);
					recvDMReply_flag=1;
					continue;
				}
				if(totalLocalSMSNum < SupportSMSNum)
					ret= getPSM_index(&index,&ghandle,0);
				else if(totalLocalSMSNum >= SupportSMSNum)
				{
					SMS_delete_last_Node(&LocalSMS_list_start,&LocalSMS_list_last,&index,&ghandle,1);
				}

				sprintf(SMS_index,"LRCV%d",index);
				Duster_module_Printf(3,"%s: SMS_index is %s",__FUNCTION__, SMS_index);

				//save original SMS into PSM
				parsebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(parsebuf == NULL)
				{
					Duster_module_Printf(1,"%s: there is no enough memory",__FUNCTION__);
					return -1;
				}

				//convert received SMS into messagelist format for web display
				memset(parsebuf, '\0', TEL_AT_SMS_MAX_LEN);

				tempSMS = (SMS_Node*)sms_malloc(sizeof(SMS_Node));
				DUSTER_ASSERT(tempSMS);
				memset(tempSMS,	'\0',	sizeof(SMS_Node));
				tempSMS->srcNum = NULL;
				tempSMS->isLongSMS = FALSE;
				tempSMS->LSMSinfo  = NULL;
				tempSMS->location	=	1;
				parseSMS(index,sms_cbuf,parsebuf,dataLength,0,1,tempSMS);

				//added by nt yecong 20160201,modified by nt yecong 20160216
				#ifdef NT_NOT_SAVE_MMS
				if(1 == ntIsMMS)
				{
					Duster_module_Printf(3,"%s,not save MMS here",__FUNCTION__); 
					ntIsMMS = 0; //added by notion ggj 20160302
					continue;
				}
				#endif
				
				// added by nt yinwei 20160627 for class0 sms
                            if(1 == DCSMSGCLASS_0_Flag)
				{
					Duster_module_Printf(3,"%s,not save MMS here",__FUNCTION__); 
					DCSMSGCLASS_0_Flag = 0; //added by notion yinwei 20160627
					continue;
				}
				

				tempSMS->psm_location=index;
				tempSMS->location = 1;

				tempData=(s_MrvFormvars *)sms_malloc(sizeof(s_MrvFormvars));
				DUSTER_ASSERT(tempData);
				memset(tempData,	'\0',	sizeof(s_MrvFormvars));
				tempData->value=(char *)tempSMS;
				tempSMS	=	NULL;
#ifndef longSMS_enable
				slist_insert_firstNode(&tempData,&LocalSMS_list_start,&LocalSMS_list_last);
#endif
#ifdef longSMS_enable

				SMS_insert_Node(&LocalSMS_list_start,&LocalSMS_list_last,&tempData,0);
#endif
				Duster_module_Printf(3,"%s,parsebuf is %s",__FUNCTION__,parsebuf);

				#ifdef NOTION_ENABLE_JSON
				{
					char tmp_message_time_index_str[20] = {0};
					message_time_index++;   
					sprintf(tmp_message_time_index_str,"%d",message_time_index);    
					Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,"sms_message_time_index",tmp_message_time_index_str,handle_SMS1);	                   
					memset(tmp_message_time_index_str,0,20);
                                   sprintf(tmp_message_time_index_str,"%d%c",message_time_index,DELIMTER);    										
					Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
                                   strcat(parsebuf,tmp_message_time_index_str);
				}		
				#endif
				
				psmfile_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, parsebuf,ghandle);
				
				
				sms_free(parsebuf);
				parsebuf = NULL;
				if(totalLocalSMSNum >= SupportSMSNum)
				{
					totalLocalSMSNum = SupportSMSNum;
					gDialer2UIPara.LocalSMSFull = 1;
				}

				totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
				totalLocalSMSNum++;
				/*Check whether to send AT+CMEMFULL*/
				if(totalLocalSMSNum == sms_full_num)
				{
					sms_send_cmemfull_msg(1);
				}
				
				totalSmsNum = totalSimSmsNum + totalLocalNum_LongSMS;
				memset(tmp,	'\0',	6);
				sprintf(tmp,"%d", totalLocalNum_LongSMS);
				Duster_module_Printf(3 ,"totalLocalNum_LongSMS=%d,tmp is %s",totalLocalNum_LongSMS,tmp);
				psm_set_wrapper(PSM_MOD_SMS, NULL, "totalLocalNum_LongSMS",tmp);

				memset(tmp,	'\0',	6);
				sprintf(tmp,"%d",totalLocalSMSNum);

				Duster_module_Printf(3 ,"totalLocalSMSNum=%d,tmp is %s",totalLocalSMSNum,	tmp);
				psmfile_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);

				gDialer2UIPara.SMSlist_new_Flag = 1;
				if(gWebSMSMSGQ)
				{
					duster_Printf("%s: send msgQ to notice APP",__FUNCTION__);
					cmd.MsgId = 1;
					cmd.MsgData = NULL;
					osa_status = OSAMsgQSend(gWebSMSMSGQ, sizeof(cmd), (UINT8 *)&cmd, OSA_NO_SUSPEND);
					DUSTER_ASSERT(osa_status == OS_SUCCESS);

				}
				SMS_new_sms_add();
				SMS_new_sms_add_led();/*notify LED to start blink*/
//#ifdef NEZHA_MIFI_V1_R0
				gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
				UIRefresh_Sms_Change();
//#endif
				psm_commit_other__(handle_SMS1);
			}
			else if(p = strstr(Msg_buf,"+CDS:")) //process sms status report
			{
				p=p+6;

				memcpy(tmp,p,5);
				for(i=0; i<5; i++)
				{
					if(tmp[i]=='\r')
					{
						tmp[i]='\0';
						break;
					}
				}

				dataLength = ConvertStrToInteger(tmp);
				duster_Printf("+CDS dataLength is %u",dataLength);
				p+=(i+2);
				memset(tmp,	'\0',	5);
				memcpy(tmp, p, 2);
				smsc_length = ConvertASCToChar(tmp);
				Duster_module_Printf(1,"smsc_length=%d",smsc_length);
				memset(sms_cbuf, '\0', 162);
				convertStrtoArray((p+(smsc_length<<1)+2), sms_cbuf, 162);
				ret=parseSMSStatusReport(sms_cbuf);
			}
			else if(p = strstr(Msg_buf,"+CMTI:"))
			{
				Duster_module_Printf(1,"%s recv CMTI %s, strlen(Msg_buf) is %d",	__FUNCTION__,	Msg_buf, strlen(Msg_buf));
				p1 = strrchr(Msg_buf,	',');
				if(p1)
				{
					memset(tmp,	'\0',	6);
					SMS_get_id(p1,	tmp);
					index = ConvertStrToInteger(tmp);
					Duster_module_Printf(1,	"%s index is %d, tmp is %s, strlen(tmp) = %d",	__FUNCTION__,	index,	tmp, strlen(tmp));
					if(index == 0)
					{
						continue;
					}
					sendSMSMsg = sms_malloc(sizeof(CommandATMsg));
					DUSTER_ASSERT(sendSMSMsg);
					sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
					DUSTER_ASSERT(sendSMSMsg->atcmd);
					memset(sendSMSMsg->atcmd,0,DIALER_CMD_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,	"AT^LKSMSSTA=1\r");
					sendSMSMsg->ok_fmt = NULL;
					sendSMSMsg->ok_flag = 0;
					sendSMSMsg->err_fmt=sms_malloc(15);
					DUSTER_ASSERT(sendSMSMsg->err_fmt);
					memset(sendSMSMsg->err_fmt,	'\0',	15);
					sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
					sendSMSMsg->callback_func = &RcvSMS_LKSMSSTA_SET_CALLBACK;
					sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status == OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, RCVSMS_LKSMSSTA_ERROR|RCVSMS_LKSMSSTA_SET,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);

					switch(flag_value)
					{
					case RCVSMS_LKSMSSTA_ERROR:
						Duster_module_Printf(3,"%s AT^LKSMSSTA failed",__FUNCTION__);
						break;
					case RCVSMS_LKSMSSTA_SET:
						Duster_module_Printf(3,"%s AT^LKSMSSTA OK",__FUNCTION__);
						break;
					}
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,"AT+CMGR=%d\r",index);
					sendSMSMsg->ok_fmt=sms_malloc(10);
					DUSTER_ASSERT(sendSMSMsg->ok_fmt);
					memset(sendSMSMsg->ok_fmt,	'\0',	10);
					sprintf(sendSMSMsg->ok_fmt,"+CMGR:");
					sendSMSMsg->ok_flag=1;
					sendSMSMsg->ATCMD_TYPE=index;//ATCMD_TYPE acts as index
					sendSMSMsg->callback_func=&CMGR_SET_CALLBACK;

					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMGR_ERROR|CMGR_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);

					if(flag_value == CMGR_ERROR)
						continue;
					if(flag_value == CMGR_SET_CMD)
					{
						gMem1MsgNum++;
						totalSimSmsNum++;
						if(totalSimSmsNum > gMem1TotalNum)
							totalSimSmsNum = gMem1TotalNum;
						else if(gMem1MsgNum >= gMem1TotalNum)
							gMem1MsgNum = gMem1TotalNum;

						SMS_new_sms_add();
						SMS_new_sms_add_led();/*notify LED to start blink*/
						//#ifdef NEZHA_MIFI_V1_R0
						gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
						UIRefresh_Sms_Change();
					}
					if(sendSMSMsg->ok_fmt)
						sms_free(sendSMSMsg->ok_fmt);
					sendSMSMsg->ok_fmt = NULL;
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,	"AT^LKSMSSTA=0\r");
					sendSMSMsg->ok_flag = 0;
					sendSMSMsg->callback_func = &RcvSMS_LKSMSSTA_SET_CALLBACK;
					sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;

					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, RCVSMS_LKSMSSTA_ERROR|RCVSMS_LKSMSSTA_SET,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);

					switch(flag_value)
					{
					case RCVSMS_LKSMSSTA_ERROR:
						Duster_module_Printf(3,"%s AT^LKSMSSTA failed",__FUNCTION__);
						break;
					case RCVSMS_LKSMSSTA_SET:
						Duster_module_Printf(3,"%s AT^LKSMSSTA OK",__FUNCTION__);
						break;
					}

					if(sendSMSMsg)
					{
						if(sendSMSMsg->atcmd)
							sms_free(sendSMSMsg->atcmd);
						if(sendSMSMsg->ok_fmt)
							sms_free(sendSMSMsg->ok_fmt);
						if(sendSMSMsg->err_fmt)
							sms_free(sendSMSMsg->err_fmt);
						sms_free(sendSMSMsg);
					}
				}
			}
		}
	}
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return 0;
}
/*******************************************************************************************************
 function description : parse sms TPDU according to 3gpp 23040
             inout : index ---used as psm index
             in_buf : TPDU
             out_buf : index#srcAddr#code_type#content#time#status#mem_type#LSMSflag#reference#segmentNum#totalNum#
                          (replace '#' with DELIMTER)
             bufferLength : length of TPDU
             status : 0--unread sms
                         1--read sms
                         2--unSent sms
                         3-- sent sms
              mem_type:0--sim
                              1--device
              smsBuf: new SMS_Node
  notice : when read data from psm, different data sepereate by ';'
              psmfile_set_wrapper : replace ';' with 0x1B
              psmfile_get_wrapper:  replace 0x1B with ';'

  Updated @ 140110 -- to distinguish between SMS and MMS
  					 code_type: 0 -- GSM7; 1 -- UCS2 ; 3-- MMS; 4 -- WAP-PUSH(but not MMS); 5 -- Long message; 6 -- voice mail
*********************************************************************************************************/
void parseSMS(int index, char *in_buf, char *out_buf, UINT32 bufferLength, UINT32 status, UINT32 mem_type,SMS_Node *smsBuf)
{
	int 			udl=0, udhl=0;
	char	   	   *UTF8Buf=NULL, *inputdata=NULL, *outputdata=NULL;
	char 			tmpBuf[80]= {0};
	char 			timebuf[30]= {0};
	char 			smsindex[15];
	UINT32	 		protocolId;
	TIMESTAMP 		timeStamp;
	DCS 			bDCS;
	char 			srcAddr[ CI_MAX_ADDRESS_LENGTH + 1] = {0};
	UINT8 			idx=0;
	BOOL 			hdrpresent = FALSE, validperiod_flag = FALSE;
	UINT32 			FrameLen=0,i,validperiod_Idx=0;
	unsigned int	pos = 0;
	int 			inlen,number,outlen;
	int 			test_inlen=0;
	UINT8 			septet_udhl=0;
	char 			unsupport[7]= {0x20,	0x20,	0x20,	0x20,	0x20,	0x20};  //don't support such type SMS
	OSA_STATUS 		osa_status = 0;
	UINT8 			addrlen = 0,RefLen = 0;
	UINT16			RefValue = 0;
	UINT16			MMSDestPort = 0;
	UINT16			MMSSrcPort	= 0;
	char			MMSContentType[32];
	char			parsedContentType[32];
	UINT8			isMMS = 0;
	UINT8			isWAPPush = 0;	/*Dst Port 2948(0xB84)*/
	UINT8			processedIEILen = 0;
	UINT8			isVoiceMail = 0;
	UINT8			extensionType = 0;
	UINT8			gsm7Tabletype = 0;

	Duster_module_Printf(2,"enter %s: index = %d",__FUNCTION__, index);
	Duster_module_Printf(2,"%s: try to get WebSMSSema (%d)",__FUNCTION__, WebSMSSema);

	osa_status = OSASemaphoreAcquire(WebSMSSema, OS_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS );

	Duster_module_Printf(3,"%s: get WebSMSSema",__FUNCTION__);

	UTF8Buf = sms_malloc(Msg_Numer*512);
	if(UTF8Buf == NULL)
		goto exit;

	memset(UTF8Buf,	'\0',	Msg_Numer*320);

	//parse read SMS and save it in dlist format
	if(in_buf != NULL)
	{
		if (in_buf[idx]  & 0x40)
		{
			hdrpresent = TRUE;
		}
		else
			hdrpresent = FALSE;

		idx++;
		if((status == 2) || (status == 3))                //sent ot unsent SMS
		{
			idx++;
			switch((in_buf[idx-2]>>3 ) & 0x3)
			{
			case 0:                                               //no validperiod
				validperiod_flag = FALSE;
				break;
			case 1:                                                //enhance format
			case 3:                                                //Absolute format
				validperiod_flag = TRUE;
				validperiod_Idx =7;
				break;
			case 2:                                                //relative format
				validperiod_flag = TRUE;
				validperiod_Idx =1;
				break;
			default:
				duster_Printf("don't support this valid period format");
				break;
			}

			if(status ==3)
				smsBuf->sms_status=SEND_SUCCESS;
			if(status ==2)
				smsBuf->sms_status=UNSENT;

			duster_Printf("%s: validperiod_flag is %u, validperiod_Idx is %u",__FUNCTION__, validperiod_flag, validperiod_Idx);
		}

		Duster_module_Printf(1,"idx=%d",idx);//now idx points to ADDRL
		/*updated for decoding phone number which type is Alphanumeric*/
		libReadSmsAddress((UINT8 *)in_buf, &idx, (UINT8 *)srcAddr);
		duster_Printf("%s: srcAddr  is %s",__FUNCTION__, srcAddr);

#if 0
		memset(smsBuf->srcNum,0,16);
		addrlen=strlen(srcAddr); //in SMS_Node  just support  src'length <16
		if(addrlen>15)
			addrlen=15;
		memcpy(smsBuf->srcNum,srcAddr,addrlen);
#endif
		libReadSmsProtocolId((UINT8 *)in_buf, &idx, &protocolId);
		duster_Printf("%s: protocolId  is %d",__FUNCTION__, protocolId);

		libReadSmsDcs((UINT8 *)in_buf, &idx, &bDCS);
		duster_Printf("%s: DCS  is %u",__FUNCTION__, bDCS.alphabet);

		if((status == 0) || (status == 1))                //read ot unread SMS
		{
			if(status ==0)
				smsBuf->sms_status=UNREAD;
			if(status ==1)
				smsBuf->sms_status=READED;
			libReadSmsTimeStamp((UINT8 *)in_buf, &idx, &timeStamp);
			sprintf(timebuf, "%02d,%02d,%02d,%02d,%02d,%02d,%c%d", timeStamp.tsYear, timeStamp.tsMonth, timeStamp.tsDay,timeStamp.tsHour, timeStamp.tsMinute, timeStamp.tsSecond, timeStamp.tsZoneSign, timeStamp.tsTimezone);
			duster_Printf("%s: timestamp is %s",__FUNCTION__, timebuf);
			smsBuf->savetime = sms_malloc(sizeof(TIMESTAMP));
			DUSTER_ASSERT(smsBuf->savetime);
			memset(smsBuf->savetime,	'\0',	sizeof(TIMESTAMP));
			memcpy(smsBuf->savetime, &timeStamp, sizeof(TIMESTAMP));
		}
		else
		{
			sprintf(timebuf,"0,0,0,0,0,0+8");
			if(validperiod_flag == TRUE)
			{
				idx += validperiod_Idx;
			}
		}
		smsBuf->isLongSMS = FALSE;
		udl = in_buf[idx];
		if(hdrpresent)
		{
			idx++; //now idx points to UDHL
			udhl = in_buf[idx];
			while(udhl > processedIEILen)
			{
				Duster_module_Printf(1, "%s processedIEILen is %d type is %x",	__FUNCTION__,	processedIEILen,	in_buf[idx + processedIEILen + 1]);
				if(in_buf[idx + processedIEILen + 1] == 0x00 )//Concatenated short messages, 8-bit reference number
				{
					FrameLen = idx + processedIEILen + 2 +  in_buf[idx + processedIEILen + 1 + 1] + 1;
					RefValue = in_buf[idx + processedIEILen  + 1 + 2 ];
					smsBuf->isLongSMS=TRUE;
					addrlen=strlen(srcAddr);
					smsBuf->srcNum	=	sms_malloc(addrlen+1);
					DUSTER_ASSERT(smsBuf->srcNum);
					memset(smsBuf->srcNum,	'\0',	addrlen+1);
					memcpy(smsBuf->srcNum,	srcAddr,	addrlen);
					Duster_module_Printf(1,"hdrpresent 8-bit is 0,Refvalue is %d",RefValue);
					smsBuf->LSMSinfo = sms_malloc(sizeof(LSMS_INFO));
					DUSTER_ASSERT(smsBuf->LSMSinfo);
					memset(smsBuf->LSMSinfo,	'\0',	sizeof(LSMS_INFO));
					smsBuf->LSMSinfo->LSMSflag	=	LONG_SMS;
					smsBuf->LSMSinfo->totalSegment	=	in_buf[idx + processedIEILen  + 1 + 3];
					smsBuf->LSMSinfo->segmentNum	=	in_buf[idx + processedIEILen  + 1 + 4];
					smsBuf->LSMSinfo->referenceNum	=	RefValue;
					smsBuf->LSMSinfo->receivedNum	=	0;
					smsBuf->LSMSinfo->isRecvAll 	=	FALSE;
				}
				else if(in_buf[idx + processedIEILen + 1] == 0x08 )//Concatenated short messages, 16-bit reference number
				{
					FrameLen = idx + processedIEILen + 2 +  in_buf[idx + processedIEILen + 1 + 1] + 1;
					RefValue=(in_buf[idx + processedIEILen  + 1 + 2 ]<<8)+in_buf[idx + processedIEILen  + 1 + 3 ];
					smsBuf->isLongSMS	=	TRUE;
					addrlen	=	strlen(srcAddr);
					smsBuf->srcNum	=	sms_malloc(addrlen+1);
					DUSTER_ASSERT(smsBuf->srcNum);
					memset(smsBuf->srcNum,	'\0',	addrlen+1);
					memcpy(smsBuf->srcNum,	srcAddr,	addrlen);
					Duster_module_Printf(1,"hdrpresent 16-bit is 8,RefValue is %d",RefValue);
					smsBuf->LSMSinfo = sms_malloc(sizeof(LSMS_INFO));
					DUSTER_ASSERT(smsBuf->LSMSinfo);
					memset(smsBuf->LSMSinfo,	'\0',	sizeof(LSMS_INFO));
					smsBuf->LSMSinfo->LSMSflag 	= 	LONG_SMS;
					smsBuf->LSMSinfo->totalSegment	= 	in_buf[idx + processedIEILen  + 1 + 4 ];
					smsBuf->LSMSinfo->segmentNum	=	in_buf[idx + processedIEILen  + 1 + 5 ];
					smsBuf->LSMSinfo->referenceNum	=	RefValue;
					smsBuf->LSMSinfo->receivedNum	=	0;
					smsBuf->LSMSinfo->isRecvAll		=	FALSE;
				}
				else if(in_buf[idx + processedIEILen + 1] == 0x05 )//Application Port addressing scheme, 16bits	M-Notification Ind Must be 0X05
				{
					FrameLen = idx + processedIEILen + 2 +  in_buf[idx + processedIEILen + 1 + 1] + 1;
					MMSDestPort = (in_buf[idx + processedIEILen + 3]<<8) + in_buf[idx + processedIEILen + 4];/*MMS DestPort must be 0x0B84(2948)*/
					MMSSrcPort  = (in_buf[idx + processedIEILen + 5]<<8) + in_buf[idx + processedIEILen + 6];/*MMS SrcPort must be 0x23F0(9200)*/
					Duster_module_Printf(1, "%s MMSDestPort is %d,	MMSSrcProt is %d",	__FUNCTION__,MMSDestPort,MMSSrcPort);
					if(MMSDestPort == 2948)
					{
						isWAPPush = 1;
					}
				}
				else if(in_buf[idx + processedIEILen + 1] == 0x23 )
				{
					Duster_module_Printf(1, "%s Enhanced VOICE MAIL Information",	__FUNCTION__);
					isVoiceMail = 1;

				}
				else if(in_buf[idx + processedIEILen + 1] == 0x01 )/*special SMS Message Indication*/
				{
					Duster_module_Printf(1, "%s Special SMS Message Indication",	__FUNCTION__);
					Duster_module_Printf(1, "%s first Otc is %02x", __FUNCTION__,	in_buf[idx + processedIEILen + 1 + 2 ]);

					if(in_buf[idx + processedIEILen + 1 + 2 ]&0x03 == 0x00)/*Voice Message Waiting*/
					{
						isVoiceMail = 1;
					}
				}
				else if(in_buf[idx + processedIEILen + 1] == 0x24 )
				{
					extensionType = in_buf[idx + processedIEILen + 1 + 2];
					Duster_module_Printf(1,	"%s extensionType is 0x%02x",	__FUNCTION__,	extensionType);
				}
				else if(in_buf[idx + processedIEILen + 1] == 0x25 )
				{
					gsm7Tabletype = in_buf[idx + processedIEILen + 1 + 2];
					Duster_module_Printf(1,	"%s gsm7Tabletype is 0x%02x",	__FUNCTION__,	gsm7Tabletype);
				}
				/*added by notion ggj 20160612 for SKT sms*/
				else if(in_buf[idx + processedIEILen + 1] == 0x22)
				{
				       int tmpindex = idx + processedIEILen + 1 + 2;
					libReadSmsAddress((UINT8 *)in_buf, &tmpindex, (UINT8 *)srcAddr);
					Duster_module_Printf(1,	"%s IEI:srcAddr  is %s",	__FUNCTION__,srcAddr);
				}
				else
				{
					Duster_module_Printf(1,	"%s MIFI don't support this format, break",	__FUNCTION__);
				}
				processedIEILen += 2 +  in_buf[idx + processedIEILen + 1 + 1];
			}
			udhl++;
			FrameLen = idx + udhl;
		}
		else
		{
			FrameLen = idx + 1;
		}
		Duster_module_Printf(1,	"%s udhl is %d",	__FUNCTION__,	udhl);
		if(bDCS.alphabet == 0)/*GSM7 coded*/
		{
			if(udhl*8%7)
				septet_udhl = udhl*8/7+1;
			else
				septet_udhl = udhl*8/7;

		}
		else if(!smsBuf->isLongSMS)
		{
			smsBuf->isLongSMS=FALSE;
		}
		Duster_module_Printf(1,	"%s idx is %d udhl is %d",	__FUNCTION__,	idx,	udhl);

		if(FrameLen > bufferLength)
			FrameLen = bufferLength;

		inlen = udl - udhl;
		Duster_module_Printf(3,"hdrpresent=%d, FrameLen=%d, udl=%d, udhl=%d",hdrpresent,FrameLen, udl, udhl);

		if(inlen + FrameLen >160)
			inlen = 160 - FrameLen;


		if(mem_type==0) //sim sms
		{
			memset(smsindex,	'\0',	15);
			sprintf(smsindex,"%s","SRCV");
			memset(tmpBuf,	'\0',	80);
			sprintf(tmpBuf,"%d",index);
			strcat(smsindex,tmpBuf);

		}
		else if(mem_type==1) //Local sms
		{
			memset(smsindex,	'\0',	15);
			sprintf(smsindex,"%s","LRCV");
			memset(tmpBuf,	'\0',	80);
			sprintf(tmpBuf,"%d",index);
			strcat(smsindex,tmpBuf);
		}

		Duster_module_Printf(1, "%s extensionType is 0x%02x",	__FUNCTION__,	extensionType);
		sprintf(out_buf, "%s%c%s%c",smsindex,DELIMTER,srcAddr,DELIMTER);
		if(inlen)
		{

			switch(bDCS.alphabet)
			{
			case 0: 			//gsm7
			{
				if(gsm7Tabletype < 0x04)
				{
					test_inlen = udl-septet_udhl;
					Duster_module_Printf(3,"udhl-septet_udhl=%d",test_inlen);
					inlen =libDecodeGsm7BitData((UINT8 *)&in_buf[FrameLen], test_inlen, (UINT8 *)UTF8Buf,udhl,	gsm7Tabletype, extensionType);

					duster_Printf("inlen=%d",inlen);

					memset(tmpBuf,	'\0',	80);
					if(isVoiceMail != 1)
					{
						sprintf(tmpBuf,"%d%c",0,DELIMTER);
					}
					else if(isVoiceMail == 1)
					{
						sprintf(tmpBuf,"%d%c",6,DELIMTER);
					}
					strcat(out_buf,tmpBuf);

					str_replaceChar(UTF8Buf,';',SEMI_REPLACE);
					str_replaceChar(UTF8Buf,'=',EQUAL_REPLACE);
					strcat(out_buf,UTF8Buf);
				}
				else if(extensionType > 0x03)
				{
					memset(tmpBuf,'\0',	80);
					sprintf(tmpBuf,"%d%c",0,DELIMTER);
					strcat(out_buf,tmpBuf);
					memcpy(out_buf+strlen(out_buf), unsupport, 6);
					Duster_module_Printf("%s: don't support this extension Table",__FUNCTION__);
				}
				break;
			}

			case 2:          //UCS2
			{
				inputdata = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(inputdata == NULL)
				{
					sms_free(UTF8Buf);
					goto exit;
				}

				outputdata = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(outputdata == NULL)
				{
					sms_free(UTF8Buf);
					sms_free(inputdata);
					goto exit;
				}

				memset(inputdata,'\0',	TEL_AT_SMS_MAX_LEN);
				memset(outputdata,	'\0',	TEL_AT_SMS_MAX_LEN);

				// 2 bytes aligned
				memcpy((char*)inputdata, &in_buf[FrameLen], inlen);

				for(i=0; i<inlen; i++)
					Duster_module_Printf(5,"%x, %x",in_buf[FrameLen+i], inputdata[i]);

				number = inlen>>1;

				//convert to big-endian in every char only
				duster_Printf("inlen=%d",inlen);

				//duster_Printf("%d",number);
				for(i=0; i<number; i++)
				{
					*(UINT16*)(outputdata+(i<<1)) =  htons(*(UINT16*)(inputdata+(i<<1)));
					Duster_module_Printf(5,"input [%x] = %x",inputdata+(i<<1), *(UINT16*)(inputdata+(i<<1)));
					Duster_module_Printf(5,"output [%x] = %x",outputdata+(i<<1), *(UINT16*)(outputdata+(i<<1)));
				}

				//convert from UCS-2 to UTF-8 for Web display

				Duster_module_Printf(1,"%s,before pos is %d",__FUNCTION__,pos);

				for(i=0; i<=(inlen>>1)-1; i++)
				{
					u2utf8_bigEndian(outputdata+(i<<1), UTF8Buf, &pos);
				}


				Duster_module_Printf(1,"%s,after pos is %d",__FUNCTION__,pos);

				for(i=0; i<inlen; i++)
				{
					Duster_module_Printf(5,"outputdata[%d] = %02x",i,outputdata[i]);
				}


				memset(tmpBuf,	'\0',	80);
				if(isVoiceMail != 1)
				{
					sprintf(tmpBuf,"%d%c",1,DELIMTER);
				}
				else if(isVoiceMail == 1)
				{
					sprintf(tmpBuf,"%d%c",6,DELIMTER);
				}
				strcat(out_buf,tmpBuf);


				str_replaceChar(UTF8Buf,';',SEMI_REPLACE);
				str_replaceChar(UTF8Buf,'=',EQUAL_REPLACE);
				strcat(out_buf,UTF8Buf);

				sms_free(outputdata);
				outputdata = NULL;

				sms_free(inputdata);
				inputdata = NULL;

				Duster_module_Long_Printf(5," out_buf = %s", out_buf);

				break;
			}
			#ifdef 	NOTION_ENABLE_KSC5601_UCS2_CONVERT
			case 4:
			{
				inputdata = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(inputdata == NULL)
				{
					sms_free(UTF8Buf);
					goto exit;
				}

				outputdata = sms_malloc(TEL_AT_SMS_MAX_LEN);
				if(outputdata == NULL)
				{
					sms_free(UTF8Buf);
					sms_free(inputdata);
					goto exit;
				}

				memset(inputdata,'\0',	TEL_AT_SMS_MAX_LEN);
				memset(outputdata,	'\0',	TEL_AT_SMS_MAX_LEN);

				// 2 bytes aligned
				memcpy((char*)inputdata, &in_buf[FrameLen], inlen);

				for(i=0; i<inlen; i++)
					Duster_module_Printf(5,"%x, %x",in_buf[FrameLen+i], inputdata[i]);

				number = inlen>>1;

				//convert to big-endian in every char only
				duster_Printf("inlen=%d",inlen);

				//duster_Printf("%d",number);
				/*for(i=0; i<number; i++)
				{
					*(UINT16*)(outputdata+(i<<1)) =  htons(*(UINT16*)(inputdata+(i<<1)));
					Duster_module_Printf(5,"input [%x] = %x",inputdata+(i<<1), *(UINT16*)(inputdata+(i<<1)));
					Duster_module_Printf(5,"output [%x] = %x",outputdata+(i<<1), *(UINT16*)(outputdata+(i<<1)));
				}*/
         			//convert from KSC5601 to UCS-2   
                                //added by yuanfutang 20160601 begin
				
                            //const unsigned char ksc5601str[] = {0xBE, 0xC8, 0xB3, 0xE7, 0xC7, 0xCF, 0xBC, 0xBC, 0xBF, 0xE4, 0x2E, 0x20, 0xB8, 0xB8, 0xB3, 0xAA, 0xBC, 0xAD, 0x20, 0xB9, 0xDD, 0xB0, 0xA1, 0xBF, 0xF6, 0xBF, 0xE4, 0x2E, 0x00};
				outlen = ksc5601_str_to_ucs2((unsigned short *)outputdata,inputdata, inlen);
				
				//added by yuanfutang 20160601 end
					
				//convert from UCS-2 to UTF-8 for Web display

				Duster_module_Printf(1,"%s,before pos is %d",__FUNCTION__,pos);
                       
				for(i=0; i<=(outlen>>1)-1; i++)
				{
					u2utf8_bigEndian(outputdata+(i<<1), UTF8Buf, &pos);
				}


				Duster_module_Printf(1,"%s,after pos is %d",__FUNCTION__,pos);

				for(i=0; i<inlen; i++)
				{
					Duster_module_Printf(5,"outputdata[%d] = %02x",i,outputdata[i]);
				}


				memset(tmpBuf,	'\0',	80);
				if(isVoiceMail != 1)
				{
					sprintf(tmpBuf,"%d%c",1,DELIMTER);
				}
				else if(isVoiceMail == 1)
				{
					sprintf(tmpBuf,"%d%c",6,DELIMTER);
				}
				strcat(out_buf,tmpBuf);


				str_replaceChar(UTF8Buf,';',SEMI_REPLACE);
				str_replaceChar(UTF8Buf,'=',EQUAL_REPLACE);
				strcat(out_buf,UTF8Buf);

				sms_free(outputdata);
				outputdata = NULL;

				sms_free(inputdata);
				inputdata = NULL;

				Duster_module_Long_Printf(5," out_buf = %s", out_buf);

				break;
			}
			#endif
			case 1:		/*8--bit*/
			{
				if(MMSDestPort == 2948 && (MMSSrcPort == 9200))
				{
					Duster_module_Printf(1,	"%s 8-BIT Port Match",	__FUNCTION__);
					Duster_module_Printf(1,	"%s	transaction ID is 0x%x",	__FUNCTION__,	in_buf[FrameLen]);
					Duster_module_Printf(1,	"%s PDU Type is 0x%x",	__FUNCTION__,	in_buf[FrameLen + 1]);
					if(in_buf[FrameLen + 1] == 0x06)	/*MMS should be 0x06*/
					{
						Duster_module_Printf(1,	"%s MMS WSP PDU length is %d",	__FUNCTION__,	in_buf[FrameLen + 2]);
						if(in_buf[FrameLen + 2] < 0x1E)
						{
							Duster_module_Printf(1,	"%s Content_Type is coded as Binary",	__FUNCTION__);
							if((in_buf[FrameLen + 3] & 0x7F) == 0x3E)
							{
								Duster_module_Printf(1,	"%s 1 this new sms is a M-Notification Ind",	__FUNCTION__);
								isMMS = 1;
								ntIsMMS = 1; //added by notion ggj 20160317 for MMS disable
							}
						}
						else if(in_buf[FrameLen + 2] > 0x1E)	/*application/vnd.wap.mms-message*/
						{
							memset(MMSContentType,	'\0',	32);
							memcpy(MMSContentType,	"application/vnd.wap.mms-message",	31);
							memset(parsedContentType,	'\0',	32);
							memcpy(parsedContentType,	in_buf + FrameLen + 3,	31);
							Duster_module_Printf(1,	"%s MMSContentType is %s parsedContentType is %s",	__FUNCTION__,	MMSContentType,parsedContentType);
							if(strncmp(parsedContentType,	MMSContentType,	strlen(MMSContentType)) == 0)
							{
								Duster_module_Printf(1,	"%s  2 this new sms is a M-Notification Ind",	__FUNCTION__);
								isMMS = 1;
								ntIsMMS = 1;
							}
						}
					}
					//added by notion ggj 20160318
					else if(in_buf[FrameLen + 1] == 0x61)	/*MMS include 0x61 for beeline*/
					{
					       Duster_module_Printf(1,	"%s  3 this new sms is a M-Notification Ind for beeline",	__FUNCTION__);
						ntIsMMS = 1;
					}
				}
				memset(tmpBuf,	'\0',	80);
				if(isMMS  == 1)
				{
					sprintf(tmpBuf,	"%d%c",	3,	DELIMTER);
				}
				else if((isMMS == 0) && (isWAPPush == 0) && (isVoiceMail != 1))
				{
					sprintf(tmpBuf,"%d%c",	0,	DELIMTER);
				}
				else if((isMMS == 0) && (isWAPPush == 1))
				{
					sprintf(tmpBuf,"%d%c",	4,	DELIMTER);
				}
				else if(isVoiceMail == 1)
				{
					sprintf(tmpBuf,"%d%c",	6,	DELIMTER);
				}
				strcat(out_buf,tmpBuf);
				memcpy(out_buf+strlen(out_buf), unsupport, 6);
				break;
			}
			default:
				memset(tmpBuf,	'\0',	80);
				sprintf(tmpBuf,"%d%c",0,DELIMTER);
				strcat(out_buf,tmpBuf);
				memcpy(out_buf+strlen(out_buf), unsupport, 6);
				duster_Printf("%s: don't support such data code scheme",__FUNCTION__);
			}
		}
		else if(inlen == 0)/*for empty SMS*/
		{
			memset(tmpBuf,0,60);
			sprintf(tmpBuf,"%d%c",0,DELIMTER);
			strcat(out_buf,tmpBuf);
			strcat(out_buf, " ");
			duster_Printf("%s: Empty SMS",__FUNCTION__);
		}

		memset(tmpBuf, '\0', 80);
		/*add msgClass @140207*/
		if(smsBuf->isLongSMS)
			sprintf(tmpBuf, "%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c",0x1a,timebuf,DELIMTER,status,DELIMTER,mem_type,DELIMTER,smsBuf->LSMSinfo->LSMSflag,DELIMTER,smsBuf->LSMSinfo->referenceNum,DELIMTER,smsBuf->LSMSinfo->segmentNum,DELIMTER,smsBuf->LSMSinfo->totalSegment,DELIMTER,bDCS.msgClass,DELIMTER);
		else
			sprintf(tmpBuf, "%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c",0x1a,timebuf,DELIMTER,status,DELIMTER,mem_type,DELIMTER,NOT_LONG,DELIMTER,0,DELIMTER,0,DELIMTER,0,DELIMTER,bDCS.msgClass,DELIMTER);
		strcat(out_buf, tmpBuf);

		Duster_module_Long_Printf(3, "%s: index: %d, str=%s",__FUNCTION__,index, out_buf);
	}

	sms_free(UTF8Buf);
	UTF8Buf = NULL;

exit:
	OSASemaphoreRelease(WebSMSSema);
	Duster_module_Printf(3,"%s: release WebSMSSema",__FUNCTION__);

	Duster_module_Printf(2,"leave %s",__FUNCTION__);
}
/************************************************************************************
  function description: parse status report message TPDU

**************************************************************************************/
UINT8 parseSMSStatusReport(char *in_buf)
{

	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	UINT8	idx=0,	TP_MR,	TP_STATUS;
	UINT8	preSubmitAddr[ CI_MAX_ADDRESS_LENGTH + 1] = {0};
	char	timebuf[30]= {0};
	int 	retVal = 0, status = -1;
	char	*str  = NULL,	*new_val = NULL;
	OSA_STATUS osa_status = 0;
	TIMESTAMP preSubmitTimeStamp,ST_ReportTimeStamp;
	char	*p1 = NULL, *p2 = NULL, *p3 =NULL;
	char 	*tempReportList = NULL;
	UINT16	pre_len = 0;

	osa_status = OSASemaphoreAcquire(WebSMSSema, OS_SUSPEND);
	ASSERT(osa_status == OS_SUCCESS );
	Duster_module_Printf(3,"%s: get WebSMSSema",__FUNCTION__);
	if(in_buf!=NULL)
	{
		Duster_module_Printf(1,"%s,inbuf[idx]=%2x",in_buf[idx]);
		if(((in_buf[idx]&0x03)==0x02)&&((in_buf[idx]&0x20)==0x00)) //TPMIT 0x10 STATUS REPORT TP_SRQ 0x00--submit status report
		{
			idx++;
			TP_MR=in_buf[idx];
			Duster_module_Printf(3,"%s,TP_MR=%d",__FUNCTION__,TP_MR);
			idx++;
			libReadSmsAddress((UINT8 *)in_buf, &idx, preSubmitAddr);
			Duster_module_Printf(1,"%s: preSubmitAddr  is %s",__FUNCTION__, preSubmitAddr);
			libReadSmsTimeStamp((UINT8 *)in_buf, &idx, &preSubmitTimeStamp);
			sprintf(timebuf, "20%02d-%02d-%02d %02d:%02d:%02d", preSubmitTimeStamp.tsYear, preSubmitTimeStamp.tsMonth, preSubmitTimeStamp.tsDay,preSubmitTimeStamp.tsHour, preSubmitTimeStamp.tsMinute, preSubmitTimeStamp.tsSecond);
			duster_Printf("%s: preSubmitTimeStamp is %s",__FUNCTION__, timebuf);
			libReadSmsTimeStamp((UINT8 *)in_buf, &idx, &ST_ReportTimeStamp);
			memset(timebuf,	'\0',	20);
			sprintf(timebuf, "20%02d-%02d-%02d %02d:%02d:%02d", ST_ReportTimeStamp.tsYear, ST_ReportTimeStamp.tsMonth, ST_ReportTimeStamp.tsDay,ST_ReportTimeStamp.tsHour, ST_ReportTimeStamp.tsMinute, ST_ReportTimeStamp.tsSecond);
			duster_Printf("%s: ST_ReportTimeStamp is %s",__FUNCTION__, timebuf);
			TP_STATUS=in_buf[idx];
			Duster_module_Printf(1,"%s,TP_STATUS=%d",__FUNCTION__,TP_STATUS);
			if((TP_STATUS==0x00)||(TP_STATUS==0x01)||(TP_STATUS==0x02))
			{
				Duster_module_Printf(1,"Received %s StatusReport : OK!",preSubmitAddr);
				if((recvDMReply_flag==0)&&(0==strcmp(preSubmitAddr,DM_ADDR)))
				{
					psm_set_wrapper(PSM_MOD_IMSI,NULL,"IMSI",IMSI);
					psm_commit__();
					recvDMReply_flag = 1;
					Duster_module_Printf(1,"%s,Received DM Status Report ",__FUNCTION__);
					retVal = 2;
					status = 3;
				}
				else if(strcmp(preSubmitAddr,DM_ADDR))
				{
					status = 1;
				}
			}
			else
			{
				status = 0;
				Duster_module_Printf(1,"Received %s StatusReport : fail!",preSubmitAddr);
			}
			if((status == 0)||(status == 1))
			{
				str = psm_get_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list");
				if(str)
				{
					if(strncmp(str, "", 1) == 0)
					{
						pre_len = 0;
						sms_free(str);
						str = NULL;
					}
					else
					{
						p1 = strrchr(str,	'^');
						if(p1)
						{
							p2 = strrchr(p1 - 1,	'^');
							if(p2)
							{
								p3 = strrchr(p2 - 1,	'^');
								if(p3)
								{
									Duster_module_Printf(1,	"%s more than 3 records now",	__FUNCTION__);
									tempReportList = duster_malloc(strlen(p1 + 1) + 2);
									DUSTER_ASSERT(tempReportList);
									memset(tempReportList,	0,	strlen(p1 + 1) + 2);
									memcpy(tempReportList,	p1+1,	strlen(p1 + 1));
									duster_free(str);
									str= tempReportList;
								}
							}
						}
						pre_len = strlen(str);
					}
				}
				if(pre_len)
				{
					new_val = duster_realloc(str,	pre_len + strlen(preSubmitAddr) + 5);
				}
				else if(pre_len == 0)
				{
					new_val = sms_malloc(strlen(preSubmitAddr) + 5);
					ASSERT(new_val);
					memset(new_val, '\0',	strlen(preSubmitAddr) + 5);
				}
				str = sms_malloc(strlen(preSubmitAddr) + 5);
				ASSERT(str);
				memset(str, '\0',	strlen(preSubmitAddr) + 5);
				sprintf(str,	"%s%c%d%c", preSubmitAddr,	'#',	status, '^');
				strcat(new_val, str);
				sms_free(str);
				str = NULL;
				Duster_module_Printf(1,	"%s sms_report_status_list new_val:%s",__FUNCTION__,new_val);				
				psm_set_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list",	new_val);
				sms_free(new_val);
				new_val = NULL;
			}
		}
		retVal =  -2;//not submit sms status report
	}
	else
	{
		retVal =  -1;//the buff is NULL
	}
	retVal = 1;
	OSASemaphoreRelease(WebSMSSema);
	Duster_module_Printf(3,"%s: release WebSMSSema",__FUNCTION__);
	Duster_module_Printf(1,"LEAVE %s",__FUNCTION__);
	return retVal;
}
s_MrvFormvars* SMS_delete_first(s_MrvFormvars *start,s_MrvFormvars *last)
{

	s_MrvFormvars 		*temp=start;
	SMS_Node 		*tempSMS;
	UINT32  		 flag_value;
	char 			 SMS_index[12];
	char 			 tmp[4] = {0};
	OSA_STATUS 		 osa_status;
	CommandATMsg 	*sendSMSMsg = NULL;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	sendSMSMsg = sms_malloc(sizeof(CommandATMsg));
	if(sendSMSMsg)
	{
		sendSMSMsg->atcmd= sms_malloc(DIALER_MSG_SIZE);
		sendSMSMsg->err_fmt=sms_malloc(15);
		sendSMSMsg->ok_flag=1;
		sendSMSMsg->ok_fmt=NULL;
		if(sendSMSMsg->err_fmt)
		{
			memset(sendSMSMsg->err_fmt,	'\0',	15);
			sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
		}
		sendSMSMsg->callback_func=&CMGD_SET_CALLBACK;
		sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
	}


	tempSMS=(SMS_Node *)start->value;
	if(tempSMS->location==0)//sim sms
	{
		memset(SMS_index,	'\0',	12);
		sprintf(SMS_index,"%s","S_index");
		memset(tmp,	'\0',	4);
		sprintf(tmp,"%d",tempSMS->psm_location);
		strcat(SMS_index,tmp);
		psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, "");

		memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
		sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",tempSMS->psm_location);

		osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
		DUSTER_ASSERT(osa_status==OS_SUCCESS);

		flag_value=0;
		osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
		//DUSTER_ASSERT(osa_status==OS_SUCCESS);


		if( flag_value == CMS_ERROR)
		{
			duster_Printf("leave %s: AT+CMGD return error",__FUNCTION__);
		}
		totalSimSmsNum--;
	}
	else if(tempSMS->location==1)  //local SMS
	{
		memset(SMS_index,	'\0',	12);
		sprintf(SMS_index,"%s","L_index");
		memset(tmp,	'\0',	4);
		sprintf(tmp,"%d",tempSMS->psm_location);
		strcat(SMS_index,tmp);
		psm_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"");
		LocalSMSIndex[tempSMS->psm_location] = 0;
		totalLocalSMSNum--;
	}
	if(!start->next)
	{
		Duster_module_Printf(3,"%s delete only node before",__FUNCTION__);
		tempSMS = (SMS_Node *)(temp->value);
		if(tempSMS->LSMSinfo)
			sms_free(tempSMS->LSMSinfo);
		if(tempSMS->srcNum)
			sms_free(tempSMS->srcNum);
		sms_free(temp->value);
		tempSMS = NULL;
		sms_free(temp);
		Duster_module_Printf(3,"%s delete only node after",__FUNCTION__);
		last=NULL;
	}
	else
	{

		start=start->next;
		tempSMS = (SMS_Node *)(temp->value);
		if(tempSMS->LSMSinfo)
			sms_free(tempSMS->LSMSinfo);
		if(tempSMS->srcNum)
			sms_free(tempSMS->srcNum);
		sms_free(temp->value);
		sms_free(temp);
		tempSMS = NULL;
	}

	Duster_module_Printf(1,"leave %s",__FUNCTION__);

	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	return start;
}
/******************************************************************************
	*	action :	1--RcvSMS
				2--SentSMS
				3--Draft
	*
********************************************************************************/
int  SMS_delete_last_Node(s_MrvFormvars **start, s_MrvFormvars **last,	UINT16 *outIndex,	UINT16	*outHandle,	int action)
{
	s_MrvFormvars 		*tempNode = NULL,	*priorNode = NULL,	*last_Long_Node = NULL;
	SMS_Node 		*SMS_Data = NULL;
	CommandATMsg 	*sendSMSMsg = NULL;
	OSA_STATUS 		 osa_status;
	UINT32			 flag_value = 0;
	int 			 retVal = 0,	lastLongFlag = 0;
	char			 tmpBuf[20] = {0};

	Duster_module_Printf(1,"enter %s",	__FUNCTION__);
	SMS_DeleteInsert_lock();

	sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
	if(!sendSMSMsg)
	{
		Duster_module_Printf(1, "%s sms_malloc sendSMSMsg failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));
	sendSMSMsg->atcmd   = sms_malloc(DIALER_MSG_SIZE);
	sendSMSMsg->err_fmt = sms_malloc(15);
	if(!sendSMSMsg->atcmd || !sendSMSMsg->err_fmt)
	{
		Duster_module_Printf(1,"%s,duster malloc failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	sendSMSMsg->ok_flag = 1;
	sendSMSMsg->ok_fmt  = NULL;
	memset( sendSMSMsg->err_fmt,	'\0',	15);
	sprintf(sendSMSMsg->err_fmt,"%s","+CMS ERROR");
	sendSMSMsg->callback_func = &CMGD_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE    = TEL_EXT_SET_CMD;


	if(!*start)
	{
		retVal = 2;
		goto EXIT;
	}
	else if(!(*start)->next)
	{
		SMS_Data = (SMS_Node*)((*start)->value);
		*outIndex = SMS_Data->psm_location;
		if(action == 1)
		{
			getPSM_index(outIndex,	outHandle,	1);
		}
		if(SMS_Data->location == 0)
		{
			memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",SMS_Data->psm_location);

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
			//DUSTER_ASSERT(osa_status==OS_SUCCESS);

			if( flag_value == CMS_ERROR)
			{
				duster_Printf("%s: AT+CMGD return error",__FUNCTION__);
				retVal = -1;
			}
		}
		else if(SMS_Data->location == 1)
		{
			if(action == 1)
			{
				sprintf(tmpBuf,"%s%d","LRCV",SMS_Data->psm_location);
				LocalSMSIndex[SMS_Data->psm_location] = 0;
			}
			else if(action == 2)
			{
				sprintf(tmpBuf,"%s%d","LSNT",SMS_Data->psm_location);
				SentSMSIndex[SMS_Data->psm_location] = 0;
			}
			else if(action == 3)
			{
				sprintf(tmpBuf,"%s%d","LDRT",SMS_Data->psm_location);
				DraftSMSIndex[SMS_Data->psm_location] = 0;
			}
			psmfile_set_wrapper(PSM_MOD_SMS,	NULL,	tmpBuf,	"",	*outHandle);
		}
		Duster_module_Printf(1,"%s delete only one last node",__FUNCTION__);
		sms_free((*start)->value);
		sms_free((*start));
		*start = NULL;
		*last=NULL;
		retVal = 1;
		goto EXIT;
	}
	else
	{
		priorNode=*start;
		tempNode=*start;

		while(tempNode&&tempNode->next)
		{
			SMS_Data = (SMS_Node*)(tempNode->value);
			if(SMS_Data->isLongSMS&&(SMS_Data->LSMSinfo->LSMSflag==START))
			{
				last_Long_Node = tempNode;
				while(tempNode)
				{
					SMS_Data=(SMS_Node*)tempNode->value;
					priorNode = tempNode;
					tempNode  = tempNode->next;
					if(SMS_Data->LSMSinfo->LSMSflag==END)
						break;
				}
				if(!tempNode&&SMS_Data->LSMSinfo->LSMSflag == END)
				{
					lastLongFlag = 1;
					break;
				}
			}
			else
			{
				priorNode=tempNode;
				tempNode=priorNode->next;
			}
		}
		if(lastLongFlag)
		{
			SMS_Data = (SMS_Node*)last_Long_Node->value;
			Duster_module_Printf(1,"%s	last node is Long SMS,Received is %d,total is %d",
			                     __FUNCTION__,SMS_Data->LSMSinfo->receivedNum,SMS_Data->LSMSinfo->totalSegment);
			tempNode = priorNode = last_Long_Node;
			SMS_Data = (SMS_Node*)tempNode->value;
			while(tempNode->next)
			{
				priorNode = tempNode;
				tempNode  = tempNode->next;
			}
			DUSTER_ASSERT(priorNode&&tempNode);
			SMS_Data = (SMS_Node*)priorNode->value;
			switch(SMS_Data->LSMSinfo->LSMSflag)
			{
			case START:
				SMS_Data->LSMSinfo->LSMSflag = ONE_ONLY;
				break;
			case MIDDLE:
				SMS_Data->LSMSinfo->LSMSflag = END;
				break;
			}

		}
		DUSTER_ASSERT(tempNode);
		SMS_Data = (SMS_Node*)tempNode->value;
		*outIndex = SMS_Data->psm_location;
		Duster_module_Printf(1,	"%s , outIndex is %d",	__FUNCTION__,	*outIndex);
		if(action == 1)
			getPSM_index(outIndex,	outHandle,	1);

		if(SMS_Data->location == 0)
		{
			memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",SMS_Data->psm_location);

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
			//DUSTER_ASSERT(osa_status==OS_SUCCESS);

			if( flag_value == CMS_ERROR)
			{
				duster_Printf("%s: AT+CMGD return error",__FUNCTION__);
				retVal = -1;
			}
		}
		else if(SMS_Data->location == 1)
		{
			if(action == 1)
			{
				sprintf(tmpBuf,"%s%d","LRCV",SMS_Data->psm_location);
				LocalSMSIndex[SMS_Data->psm_location] = 0;
			}
			else if(action == 2)
			{
				sprintf(tmpBuf,"%s%d","LSNT",SMS_Data->psm_location);
				SentSMSIndex[SMS_Data->psm_location] = 0;
			}
			else if(action == 3)
			{
				sprintf(tmpBuf,"%s%d","LDRT",SMS_Data->psm_location);
				DraftSMSIndex[SMS_Data->psm_location] = 0;
			}
			Duster_module_Printf(1,"%s delete index is %s, handle is %d",__FUNCTION__,tmpBuf,	*outHandle);
			psmfile_set_wrapper(PSM_MOD_SMS,	NULL,	tmpBuf,	"",	*outHandle);
		}

		sms_free(tempNode->value);
		sms_free(tempNode);
		priorNode->next=NULL;
		retVal = 1;
	}
	if(action == 1)
		totalLocalSMSNum--;
	else if(action == 2)
		totalLocalSentNum--;
	else if(action == 3)
		totalLocalDraftNum--;

EXIT:
	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	SMS_DeleteInsert_unlock();
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return retVal;

}
/*********************************************************************************
*	Updated @ 131227 by shoujul
*	Issue : delete concatenated SMS on SIMCARD
*	delete_Long_SMS_flag : 0 -- do not have delete long SMS.
							1 -- have deleted single SMS in LongSMS successfully
***********************************************************************************/
static UINT8 delete_Long_SMS_flag = 0;
void SMS_set_delete_LFlag()
{
	delete_Long_SMS_flag = 1;
}
void SMS_clear_delete_LFlag()
{
	delete_Long_SMS_flag = 0;
}
UINT8 SMS_get_delete_LFlag()
{
	return delete_Long_SMS_flag;
}
/*********************************************************************************
* function description : index -delete index
                                 sms_type: 1 RCV SMS
                                                 2 SENT SMS
*                                               3 DRAFT SMS
*	Updated @ 131227 by shoujul
*	Issue : delete concatenated SMS on SIMCARD
***********************************************************************************/
int SMS_delete_By_index(UINT16 index,s_MrvFormvars **start, s_MrvFormvars **last,UINT8 smsType)
{
	SMS_Node* 		tempSMS=NULL;
	s_MrvFormvars*		begin, *prior,*tempNode;
	UINT32  		flag_value;
	char 			SMS_index[12];
	int 			outIdx = 0;
	char 			tmpBuf[11]= {0};
	CommandATMsg   *sendSMSMsg = NULL;
	OSA_STATUS 		osa_status;
	int 			ret = 1;

	Duster_module_Printf(1,"enter %s,index is %d",__FUNCTION__,index);
	SMS_DeleteInsert_lock();

	if(!*start)
	{
		Duster_module_Printf(1,"*start is NULL,leave %s",__FUNCTION__);
		ret = -1;
		goto EXIT ;
	}
	sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
	if(!sendSMSMsg)
	{
		Duster_module_Printf(1, "%s sms_malloc sendSMSMsg failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	sendSMSMsg->atcmd   = sms_malloc(DIALER_MSG_SIZE);
	sendSMSMsg->err_fmt = sms_malloc(15);
	if(!sendSMSMsg->atcmd || !sendSMSMsg->err_fmt)
	{
		Duster_module_Printf(1,"%s,duster malloc failed",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	sendSMSMsg->ok_flag = 1;
	sendSMSMsg->ok_fmt  = NULL;
	memset( sendSMSMsg->err_fmt,	'\0',	15);
	sprintf(sendSMSMsg->err_fmt,"%s","+CMS ERROR");
	sendSMSMsg->callback_func = &CMGD_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE    = TEL_EXT_SET_CMD;

	tempSMS = (SMS_Node*)((*start)->value);
	if(!tempSMS)
	{
		Duster_module_Printf(1,"%s tempSMS is NULL",__FUNCTION__);
		DUSTER_ASSERT(0);
	}
	Duster_module_Printf(1,"%s here1",__FUNCTION__);
	Duster_module_Printf(1,"%s tempSMS->psm_location is %d",__FUNCTION__,tempSMS->psm_location);

	if(tempSMS->psm_location == index)
	{
		tempNode=*start;
		Duster_module_Printf(1,"delete the first SMS_Node");
		Duster_module_Printf(1,"befor start->value->sim_index is %d",tempSMS->psm_location);
		if((tempSMS->location == 0) && (smsType == 1))//sim sms
		{
			Duster_module_Printf(1,"SMS_delete_By_index,delete sim sms ");
			memset(SMS_index,	'\0',	12);
			sprintf(SMS_index,"%s%d","SRCV",index);
			Duster_module_Printf(1,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
			psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, "");

			memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",tempSMS->psm_location);

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
			//DUSTER_ASSERT(osa_status==OS_SUCCESS);

			if( flag_value == CMS_ERROR)
			{
				duster_Printf("%s: AT+CMGD return error",__FUNCTION__);
				if(tempSMS->isLongSMS)
				{
					if(SMS_get_delete_LFlag())/*Delete single SMS successfullu before*/
					{
						ret = 1;
					}
					else
					{
						ret = -1;
					}
					if(tempSMS->LSMSinfo->LSMSflag == END)/*delete last Long SMS node, clear flag*/
					{
						SMS_clear_delete_LFlag();
					}
				}
				else	//not long message
				{
					ret = -1;
				}
			}
			else if((flag_value == CMGD_SET_CMD) && (tempSMS->isLongSMS))
			{
				if(tempSMS->LSMSinfo->LSMSflag != END)
				{
					SMS_set_delete_LFlag();	/*have deleted single SMS in LongSMS successfully*/
				}
				else if(tempSMS->LSMSinfo->LSMSflag == END) /*delete last Long SMS node, clear flag*/
				{
					SMS_clear_delete_LFlag();
				}
			}
			if(ret > 0)
			{
				totalSimSmsNum--;
			}
		}
		else if(tempSMS->location==1)  //local SMS
		{
			Duster_module_Printf(3,"%s,delete local SMS",__FUNCTION__);
			memset(SMS_index,	'\0',	12);
			if(smsType==1)
			{
				sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location);
				LocalSMSIndex[tempSMS->psm_location] = 0;
				getPSM_index(&(tempSMS->psm_location),&outIdx,1);
				Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
				psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",outIdx);
				totalLocalSMSNum--;

				memset(tmpBuf,	'\0',	11);
				sprintf(tmpBuf,"%d",totalLocalSMSNum);
				psm_set_wrapper(PSM_MOD_SMS,NULL,"totalLocalSMSNum",tmpBuf);
			}
			if(smsType==2)
			{
				sprintf(SMS_index,"LSNT%d",tempSMS->psm_location);
				SentSMSIndex[tempSMS->psm_location] = 0;
				Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
				psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
				totalLocalSentNum--;
			}
			if(smsType==3)
			{
				sprintf(SMS_index,"LDRT%d",tempSMS->psm_location);
				DraftSMSIndex[tempSMS->psm_location] = 0;
				Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
				psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
				totalLocalDraftNum--;

			}
		}

		if(ret > 0)
		{
			if(!(*start)->next)
			{
				Duster_module_Printf(3,"%s delete only node before",__FUNCTION__);
				tempSMS = (SMS_Node *)tempNode->value;
				if(tempSMS->srcNum)
					sms_free(tempSMS->srcNum);
				tempSMS->srcNum = NULL;
				if(tempSMS->LSMSinfo)
					sms_free(tempSMS->LSMSinfo);
				tempSMS->LSMSinfo = NULL;
				sms_free(tempNode->value);
				sms_free(tempNode);
				tempSMS = NULL;
				Duster_module_Printf(3,"%s delete only node after",__FUNCTION__);
				*last=NULL;
				*start=NULL;
			}
			else
			{
				*start=(*start)->next;
				tempSMS = (SMS_Node *)tempNode->value;
				if(tempSMS->srcNum)
					sms_free(tempSMS->srcNum);
				tempSMS->srcNum = NULL;
				if(tempSMS->LSMSinfo)
					sms_free(tempSMS->LSMSinfo);
				tempSMS->LSMSinfo = NULL;
				sms_free(tempNode->value);
				sms_free(tempNode);
			}
		}
		goto EXIT;
	}
	begin=*start;
	while(*start)
	{

		prior=*start;
		if(!(*start)->next)
		{
			*start = begin;
			goto EXIT;
		}
		tempNode=(*start)->next;
		tempSMS=(SMS_Node*)tempNode->value;
		if(tempSMS->psm_location == index)
		{
			if((tempSMS->location == 0)&&(smsType == 1))//sim sms
			{

				Duster_module_Printf(3,"%s,delete sim sms ",__FUNCTION__);
				memset(SMS_index,	'\0',	12);
				sprintf(SMS_index,"%s%d","sRCV",tempSMS->psm_location);
				Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
				psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, "");
				memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
				sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",tempSMS->psm_location);

				osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
				DUSTER_ASSERT(osa_status==OS_SUCCESS);

				flag_value=0;
				osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
				//DUSTER_ASSERT(osa_status==OS_SUCCESS);

				if( flag_value == CMS_ERROR)
				{
					duster_Printf("%s: AT+CMGD return error",__FUNCTION__);
					if(tempSMS->isLongSMS)
					{
						if(SMS_get_delete_LFlag())/*Delete single SMS successfullu before*/
						{
							ret = 1;
						}
						else
						{
							ret = -1;
						}
						if(tempSMS->LSMSinfo->LSMSflag == END)/*delete last Long SMS node, clear flag*/
						{
							SMS_clear_delete_LFlag();
						}
					}
					else //not  long message
					{
						ret = -1;
					}

				}
				else if((flag_value == CMGD_SET_CMD) && (tempSMS->isLongSMS))
				{
					if(tempSMS->LSMSinfo->LSMSflag != END)
					{
						SMS_set_delete_LFlag(); /*have deleted single SMS in LongSMS successfully*/
					}
					else if(tempSMS->LSMSinfo->LSMSflag == END) /*delete last Long SMS node, clear flag*/
					{
						SMS_clear_delete_LFlag();
					}
				}
				if(ret > 0)
				{
					totalSimSmsNum--;
				}

			}
			else if(tempSMS->location==1)  //local SMS
			{
				Duster_module_Printf(3,"%s,delete Local sms ",__FUNCTION__);
				memset(SMS_index,	'\0',	12);
				if(smsType==1)
				{
					sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location);
					LocalSMSIndex[tempSMS->psm_location] = 0;
					getPSM_index(&(tempSMS->psm_location),&outIdx,1);
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",outIdx);
					totalLocalSMSNum--;
				}
				if(smsType==2)
				{
					sprintf(SMS_index,"LSNT%d",tempSMS->psm_location);
					SentSMSIndex[tempSMS->psm_location] = 0;
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
					totalLocalSentNum--;
				}
				if(smsType==3)
				{
					sprintf(SMS_index,"LDRT%d",tempSMS->psm_location);
					DraftSMSIndex[tempSMS->psm_location] = 0;
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
					totalLocalDraftNum--;
				}
			}
			if(ret > 0)
			{
				if((*start)->next==*last)
				{
					tempSMS = (SMS_Node *)tempNode->value;
					if(tempSMS->srcNum)
						sms_free(tempSMS->srcNum);
					tempSMS->srcNum = NULL;
					if(tempSMS->LSMSinfo)
						sms_free(tempSMS->LSMSinfo);
					tempSMS->LSMSinfo = NULL;

					sms_free(tempNode->value);
					sms_free(tempNode);
					(*start)->next= NULL;
					*last=prior;
					tempSMS = NULL;
				}
				else
				{
					(*start)->next=(*start)->next->next;

					tempSMS = (SMS_Node *)tempNode->value;
					if(tempSMS->srcNum)
						sms_free(tempSMS->srcNum);
					tempSMS->srcNum = NULL;
					if(tempSMS->LSMSinfo)
						sms_free(tempSMS->LSMSinfo);
					tempSMS->LSMSinfo = NULL;
					sms_free(tempNode->value);
					sms_free(tempNode);
				}
			}
			break;

		}
		*start=(*start)->next;
	}
	*start=begin;

EXIT:

	if(smsType==1)
	{
		totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
		totalSmsNum=totalSimSmsNum+totalLocalSMSNum;
		totalSMSNum_LongSMS=totalSimSmsNum+totalLocalNum_LongSMS;
	}
	else if(smsType == 2)
	{
		totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	}

	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}

	SMS_DeleteInsert_unlock();
	Duster_module_Printf(1,"leave %s, ret = %d",__FUNCTION__, ret);

	return ret;
}
/************************************************************************
*  function description : make sms status readed
                 input : index --read sms index
*
**************************************************************************/
int SMS_read_By_index(UINT16 index,s_MrvFormvars *start,s_MrvFormvars *last)
{

	char 	   	   *str = NULL, *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL;
	SMS_Node   	   *tempSMS;
	s_MrvFormvars       *tempData;
	char 			SMS_index[12];
	char 			temp[4];
	int 			outIdx = 0,ret = 1;
	OSA_STATUS 		osa_status;
	UINT32 			flag_value;
	CommandATMsg   *sendSMSMsg = NULL;

	sendSMSMsg = sms_malloc(sizeof(CommandATMsg));
	DUSTER_ASSERT(sendSMSMsg);
	sendSMSMsg->atcmd= sms_malloc(DIALER_MSG_SIZE);
	DUSTER_ASSERT(sendSMSMsg->atcmd);
	sendSMSMsg->err_fmt=sms_malloc(15);
	sendSMSMsg->ok_flag=1;
	sendSMSMsg->ok_fmt=sms_malloc(10);
	if(sendSMSMsg->ok_fmt)
	{
		memset(sendSMSMsg->ok_fmt,	'\0',	10);
		sprintf(sendSMSMsg->ok_fmt,"+CMGR");
	}
	if(sendSMSMsg->err_fmt)
	{
		memset(sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	}
	sendSMSMsg->callback_func=&CMGR_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=255;


	tempData=start;
	while(tempData!=NULL)
	{
		tempSMS=(SMS_Node *)tempData->value;
		if(index==tempSMS->psm_location)
		{
			memset(SMS_index,'\0',	12);
			memset(temp,'\0',	4);
			tempSMS->sms_status=READED;

			if(tempSMS->location==0)//sim
			{
				sprintf(SMS_index,"%s%d","SRCV",index);
				str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
			}
			else if(tempSMS->location==1) //local sms
			{
				sprintf(SMS_index,"%s%d","LRCV",index);
				getPSM_index(&index,&outIdx,1);
				str = psmfile_get_wrapper(PSM_MOD_SMS,NULL,SMS_index,outIdx);
			}

			if(str != NULL)
			{
				p1 = strchr(str, DELIMTER);
				p2 = strchr(p1+1,DELIMTER);
				p3 = strchr(p2+1, DELIMTER);
				p4 = strchr(p3+1, DELIMTER);
				p5 = strchr(p4+1, DELIMTER);
				*(p5+1) = '1';


				if(tempSMS->location==0)
				{
					psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, str);
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,"AT+CMGR=%d\r",tempSMS->psm_location);
					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMGR_ERROR|CMGR_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);

					if( flag_value ==CMGR_ERROR)
					{
						ret = -1;
						tempSMS->sms_status=UNREAD;
						duster_Printf(" %s: AT+CMGR return error",__FUNCTION__);
					}

				}
				else if(tempSMS->location == 1)
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,str,outIdx);

				sms_free(str);
				str = NULL;
			}
			break;
		}
		tempData=tempData->next;
	}
	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->ok_fmt != NULL)
			sms_free(sendSMSMsg->ok_fmt);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	return ret;
}
/************************************************************************
*  function description : make sms status readed
                 input : msg_id --read sms index string,seperate by ';'
                           LRCV1;LRCV2;LRCV3;
*
**************************************************************************/

int SMS_read_list( char * msg_id)
{
	char 	*str=NULL;
	UINT8	 index;
	char 	*p1,*p2;
	char	 temp[4];
	int		 ret1=1,ret2=1;


	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	//get index of SMS to be deleted in read_sms_list

	if(msg_id)
	{
		str=sms_malloc(strlen(msg_id)+5);
		memset(str,	'\0',	strlen(msg_id)+5);
		memcpy(str,msg_id,strlen(msg_id));
	}
	else
		str = psm_get_wrapper(PSM_MOD_SMS, NULL, "read_message_table");

	if((strncmp(str,"",1))&&(str!=NULL))
	{
		if(*(str+strlen(msg_id)-1)!=';')
		{
			*(str+strlen(msg_id))=';';
		}
		str_replaceChar(str,',',';');

		Duster_module_Printf(3,"%s: read_message_table is %s", __FUNCTION__, str);
		p1=str,
		p2=strchr(str,';');
		while(p2)
		{
			memset(temp,	'\0',	4);
			if((p2-p1)<4)
			{
				Duster_module_Printf(1,"p2-p1=%d <4",p2-p1);
				break;
			}
			else
			{
				memcpy(temp,p1,4);
				if(!strncmp(temp,"SRCV",4))//sim sms
				{
					memset(temp,	'\0',	4);
					memcpy(temp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(temp);
					Duster_module_Printf(3,"%s will read SIM SMS index is %d",__FUNCTION__,index);
					ret1=SMS_read_By_index(index,SimSMS_list_start,SimSMS_list_last);
				}
				else if(!strncmp(temp,"LRCV",4))
				{
					memset(temp,	'\0',	4);
					memcpy(temp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(temp);
					ret1=SMS_read_By_index(index,LocalSMS_list_start,LocalSMS_list_last);
				}
			}
			p1=p2+1;
			p2=strchr(p1,';');
			ret2 = ret1<0?ret1:ret2;
		}


		UnreadSMSNum=SMS_calc_unread();
		if(UnreadSMSNum==0)
			gDialer2UIPara.SMSlist_new_Flag = 0;
		else if(UnreadSMSNum>0)
			gDialer2UIPara.SMSlist_new_Flag = 1;

		gDialer2UIPara.unreadLocalSMSNum = UnreadSMSNum;

		UIRefresh_Sms_Change();

		Duster_module_Printf(2,"%s: read_message_list is not NULL", __FUNCTION__);
	}

	else
		Duster_module_Printf(2,"%s: read_message_list is NULL", __FUNCTION__);

	if(str)
	{
		sms_free(str);
		str = NULL;
	}

	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return ret2;
}
/*****************************************************************************
*function description :  calculate total unread message items' number in list_start
                                 long sms counted as one message
******************************************************************************/
UINT16 SMS_calc_unread_List(s_MrvFormvars* list_start,s_MrvFormvars *list_last)
{
	s_MrvFormvars *temp;
	SMS_Node *tempSMS;
	UINT16    unread_Num=0;

	if(!list_start)
		return 0;
	temp=list_start;
	while(temp)
	{
		tempSMS=(SMS_Node*)temp->value;
		if(tempSMS->sms_status==UNREAD)
		{
			if(tempSMS->LSMSinfo && tempSMS->LSMSinfo->LSMSflag==START)//Skip long sms
			{
				temp=temp->next;
				while(temp)
				{
					tempSMS=(SMS_Node*)temp->value;
					if(tempSMS->LSMSinfo && tempSMS->LSMSinfo->LSMSflag == END)
						break;
					if(!tempSMS->LSMSinfo)
						break;
					temp=temp->next;
				}
			}
			unread_Num++;
		}
		temp=temp->next;
	}
	return unread_Num;
}
/*****************************************************************************
	add @ 14-12-22
	Calculate  the concatenated message when all messages has been received
		or more than 5 messages has been received.

******************************************************************************/
UINT16 SMS_calc_unread_complete(s_MrvFormvars* list_start,s_MrvFormvars *list_last)
{
	s_MrvFormvars *temp;
	SMS_Node *tempSMS;
	UINT16    unread_Num=0;

	if(!list_start)
		return 0;
	temp=list_start;
	while(temp)
	{
		tempSMS=(SMS_Node*)temp->value;
		if(!tempSMS->isLongSMS)
		{

			if( tempSMS->sms_status == UNREAD )
			{
				unread_Num++;
			}
		}
		else if(tempSMS->isLongSMS	&&	(tempSMS->LSMSinfo->LSMSflag == START))//long SMS more than one segment
		{
			if(	!tempSMS->LSMSinfo->isRecvAll && (tempSMS->LSMSinfo->totalSegment <= 5) && (tempSMS->LSMSinfo->receivedNum <= 5))
			{
				temp = temp->next;
				while(temp)
				{
					tempSMS=(SMS_Node*)temp->value;
					if(tempSMS->LSMSinfo->LSMSflag == END)
					{
						break;
					}
					temp = temp->next;
				}
				temp = temp->next;
				continue;
			}
			if(tempSMS->sms_status == UNREAD)
			{
				unread_Num++;
				temp=temp->next;
				while(temp)
				{
					tempSMS=(SMS_Node*)temp->value;
					if(tempSMS->LSMSinfo && tempSMS->LSMSinfo->LSMSflag == END)
						break;
					if(!tempSMS->LSMSinfo)
						break;
					temp=temp->next;
				}
			}
		}
		temp=temp->next;
	}
	return unread_Num;
}
/*****************************************************************************
*function description :  calculate total unread message items' number
                                 long sms counted as one message
******************************************************************************/

UINT16 SMS_calc_unread(void)
{
	UINT16 total_num,LocalNum,SimNum;

	if(!PlatformIsTPMifi())
	{
		LocalNum = SMS_calc_unread_List(LocalSMS_list_start,LocalSMS_list_last);
		SimNum =SMS_calc_unread_List(SimSMS_list_start,SimSMS_list_last);
	}
	else
	{
		LocalNum = 	SMS_calc_unread_complete(LocalSMS_list_start,LocalSMS_list_last);
		SimNum =	SMS_calc_unread_complete(SimSMS_list_start,SimSMS_list_last);
	}

	total_num = LocalNum+SimNum;

	return total_num;
}
/*****************************************************************************
*function description :  calculate total  message items' number
                                 long sms counted as one message
******************************************************************************/
UINT16 SMS_calc_recv_complete_long(s_MrvFormvars* list_start,s_MrvFormvars *list_last)
{
	s_MrvFormvars *temp;
	SMS_Node *tempSMS;
	UINT16	  totalSMSNum = 0;

	if(!list_start)
		return 0;
	temp=list_start;
	while(temp)
	{
		tempSMS=(SMS_Node*)temp->value;
		if(!tempSMS->isLongSMS)
		{
			totalSMSNum++;
		}
		else if(tempSMS->isLongSMS	&&	(tempSMS->LSMSinfo->LSMSflag == START))//long SMS more than one segment
		{
			if( !tempSMS->LSMSinfo->isRecvAll && (tempSMS->LSMSinfo->totalSegment <= 5) && (tempSMS->LSMSinfo->receivedNum <= 5))
			{
				temp = temp->next;
				while(temp)
				{
					tempSMS=(SMS_Node*)temp->value;
					if(tempSMS->LSMSinfo->LSMSflag == END)
					{
						break;
					}
					temp = temp->next;
				}
				temp = temp->next;
				continue;
			}
			totalSMSNum++;
			temp=temp->next;
			while(temp)
			{
				tempSMS=(SMS_Node*)temp->value;
				if(tempSMS->LSMSinfo && tempSMS->LSMSinfo->LSMSflag == END)
					break;
				if(!tempSMS->LSMSinfo)
					break;
				temp=temp->next;
			}
		}
		temp = temp->next;
	}
	return totalSMSNum;
}
UINT16 SMS_calc_recv_complete_single(s_MrvFormvars* list_start,s_MrvFormvars *list_last)
{
	s_MrvFormvars *temp;
	SMS_Node *tempSMS;
	UINT16	  totalSMSNum = 0;

	if(!list_start)
		return 0;
	temp=list_start;
	while(temp)
	{
		tempSMS=(SMS_Node*)temp->value;
		if(!tempSMS->isLongSMS)
		{
			totalSMSNum++;
		}
		else if(tempSMS->isLongSMS	&&	(tempSMS->LSMSinfo->LSMSflag == START))//long SMS more than one segment
		{
			if( !tempSMS->LSMSinfo->isRecvAll && (tempSMS->LSMSinfo->totalSegment <= 5) && (tempSMS->LSMSinfo->receivedNum <= 5))
			{
				temp = temp->next;
				while(temp)
				{
					tempSMS=(SMS_Node*)temp->value;
					if(tempSMS->LSMSinfo->LSMSflag == END)
					{
						break;
					}
					temp = temp->next;
				}
				temp = temp->next;
				continue;
			}
			totalSMSNum++;
			temp=temp->next;
			while(temp)
			{
				totalSMSNum++;
				tempSMS=(SMS_Node*)temp->value;
				if(tempSMS->LSMSinfo && tempSMS->LSMSinfo->LSMSflag == END)
					break;
				if(!tempSMS->LSMSinfo)
					break;
				temp=temp->next;
			}
		}
		temp=temp->next;
	}
	return totalSMSNum;
}
UINT16 SMS_calc_Long(s_MrvFormvars* list_start,s_MrvFormvars *list_last)
{
	s_MrvFormvars *tempNode;
	SMS_Node *tempSMS;
	UINT16 	  totalSMSNum=0;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	if(!list_start)
	{
		Duster_module_Printf(1,"leave %s,totalSMSNum=%d,",__FUNCTION__,totalSMSNum);
		return 0;
	}
	if(PlatformIsTPMifi())
	{
		totalSMSNum = SMS_calc_recv_complete_long(list_start,	list_last);
		return totalSMSNum;
	}

	tempNode = list_start;

	while(tempNode && tempNode->value)
	{
		tempSMS=(SMS_Node*)tempNode->value;
		if(tempSMS->isLongSMS && tempSMS->LSMSinfo && (tempSMS->LSMSinfo->LSMSflag==START))
		{
			while(tempNode && tempNode->value)
			{
				tempSMS=(SMS_Node*)tempNode->value;
				if(tempSMS->LSMSinfo->LSMSflag==END)
					break;
				tempNode=tempNode->next;
			}
		}
		totalSMSNum++;
		tempNode=tempNode->next;

	}
	Duster_module_Printf(1,"leave %s,totalSMSNum=%d,",__FUNCTION__,totalSMSNum);
	return totalSMSNum;

}
/*****************************************************************************
*function description :  delete all message in list_start
******************************************************************************/
#if 0
int SMS_delete_all(s_MrvFormvars** list_start)
{
	SMS_Node		*tempSMS;
	char 			 SMS_index[12];
	char		 	 tmp[4] = {0};
	UINT8 			 smsIndex;
	int 			 outghandle = 0;
	OSA_STATUS 		 osa_status;
	UINT32 			 flag_value;
	int				 ret1=1,ret2=1;
	CommandATMsg 	*sendSMSMsg = NULL;

	sendSMSMsg = sms_malloc(sizeof(CommandATMsg));
	sendSMSMsg->atcmd= sms_malloc(DIALER_MSG_SIZE);
	sendSMSMsg->err_fmt=sms_malloc(15);
	sendSMSMsg->ok_flag=1;
	sendSMSMsg->ok_fmt=NULL;
	memset(sendSMSMsg->err_fmt,0,15);
	sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	sendSMSMsg->callback_func=&CMGD_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;

	while(*list_start)
	{
		void *p=*list_start;

		tempSMS=(SMS_Node*)(*list_start)->value;
		if(tempSMS->location==0)//sim sms
		{
			memset(SMS_index,0,12);
			sprintf(SMS_index,"%s","SRCV");
			memset(tmp,0,4);
			smsIndex=tempSMS->psm_location;
			sprintf(tmp,"%d",smsIndex);
			strcat(SMS_index,tmp);
			psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, "");

			memset(sendSMSMsg->atcmd,0,DIALER_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",tempSMS->sim_location);


			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);

			if( flag_value == CMS_ERROR)
			{
				ret1 = -1;
				duster_Printf("leave %s: AT+CMGD return error",__FUNCTION__);
			}
		}
		else if(tempSMS->location==1)  //local SMS
		{
			memset(SMS_index,0,12);
			sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location);
			getPSM_index(&(tempSMS->psm_location),&outghandle,0);
			psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",outghandle);
			LocalSMSIndex[tempSMS->psm_location] = 0;
		}
		sms_free((*list_start)->value);
		*list_start=(*list_start)->next;

		sms_free(p);
		ret2=ret1<0?ret1:ret2;

	}
	*list_start=NULL;

	totalLocalSMSNum=0;
	totalSimSmsNum=0;
	totalSmsNum=0;

#ifdef NEZHA_MIFI_V1_R0
	gDialer2UIPara.unreadLocalSMSNum = 0;
	UIRefresh_Sms_Change();
#endif

	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	return ret2;

}
#endif
/********************************************************************
* function description : delele sms in delste_str
*                   input   : delete_str "LRCV1,LRCV2,LRCV3,"
*
*
*********************************************************************/
int SMS_delete_list( char * delete_str)
{
	char	 *str=NULL;
	UINT16 	  index;
	char 	 *p1,*p2;
	char 	  tmp[8];
	int 	  ret1= 1,ret2=1;

	Duster_module_Printf(1,"enter %s,delete_str is %s",__FUNCTION__,delete_str);


	//get index of SMS to be deleted in delete_sms_list
	if(delete_str)
	{

		str=sms_malloc(strlen(delete_str)+5);
		memset(str,	0,	strlen(delete_str)+5);
		memcpy(str,delete_str,strlen(delete_str));
		str_replaceChar(str,',',';');
	}
	else
	{
		Duster_module_Printf(1,"%s, delete_str is NULL,return",__FUNCTION__);
		return -1;
	}

	if((strncmp(str,"",1))&&(str!=NULL))
	{

		duster_Printf("%s: delete_str is %s", __FUNCTION__, str);

		p1=str;
		p2=strchr(p1+1,';');
		while(p2)
		{
			memset(tmp,	'\0',8);
			if((p2-p1)>7)
			{
				Duster_module_Printf(1,"p2-p1=%d >7",p2-p1);
				break;
			}
			else
			{
				memcpy(tmp,p1,4);
				if(!strncmp(tmp,"SRCV",4))//sim sms
				{
					memset(tmp,	'\0',	4);
					memcpy(tmp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(tmp);
					Duster_module_Printf(3,"will delete SIM SMS index is %d",index);
					ret1 = SMS_delete_By_index(index,&SimSMS_list_start,&SimSMS_list_last,1);
				}
				else if(!strncmp(tmp,"LRCV",4))
				{
					memset(tmp,'\0',	4);
					memcpy(tmp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(tmp);
					Duster_module_Printf(3,"will delete Local SMS index is %d",index);
					ret1=SMS_delete_By_index(index,&LocalSMS_list_start,&LocalSMS_list_last,1);
				}
				else if(!strncmp(tmp,"LSNT",4))
				{
					memset(tmp,'\0',	4);
					memcpy(tmp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(tmp);
					Duster_module_Printf(3,"will delete Local SEND index is %d",index,2);
					ret1=SMS_delete_By_index(index,&LocalSendSMS_list_start,&LocalSendSMS_list_last,2);

				}
				else if(!strncmp(tmp,"LDRT",4))
				{
					memset(tmp,	'\0',	4);
					memcpy(tmp,p1+4,p2-p1-4);
					index=ConvertStrToInteger(tmp);
					Duster_module_Printf(3,"will delete Local DRAFT index is %d",index,3);
					ret1 = SMS_delete_By_index(index,&LocalDraftSMS_list_start,&LocalDraftSMS_list_last,3);
				}
			}
			ret2=ret1<0?ret1:ret2;
			p1=p2+1;
			p2=strchr(p1,';');
		}
		Duster_module_Printf(3,"%s: delete_str is not NULL", __FUNCTION__);
	}
	else
		Duster_module_Printf(3,"%s: delete_str is NULL", __FUNCTION__);

	if(str)
	{
		sms_free(str);
		str = NULL;
	}

//#ifdef NEZHA_MIFI_V1_R0
	gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
	if(gDialer2UIPara.unreadLocalSMSNum >0)
		gDialer2UIPara.SMSlist_new_Flag = 1;
	else
		gDialer2UIPara.SMSlist_new_Flag = 0;
	UIRefresh_Sms_Change();
//#endif

	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return ret2;
}

/*****************************************************************************
* function descprition :  walk through pb_list , and match the sms srcnumber and phone
					number in phonebook,	return match name if founded (UNICODE).
					Return the firsts match name

					prefix num such as +86 is defined by E.164, and country code(cc)
					can be 1 to 3 octs. discard country code and match.

					return value is a pointer which points to name in pb list, so do not
					change the valus.
*
*****************************************************************************/
extern	s_MrvFormvars *PB_Sim_List_Start ;
extern  s_MrvFormvars *PB_Sim_List_Last;
extern	s_MrvFormvars *PB_Local_List_Start ;

static int name_spec_match = 0;
int set_special_match(int type)
{
	name_spec_match = type;
	if(type == 0)
		psm_set_wrapper(PSM_MOD_SMS, NULL, "name_spec_match",	"0");
	else
		psm_set_wrapper(PSM_MOD_SMS, NULL, "name_spec_match",	"1");
	return 0;
}
int init_spec_name_match()
{
	char *str = NULL;
	str = psm_get_wrapper(PSM_MOD_SMS, NULL, "name_spec_match");
	if(str && strncmp(str,	"",	1))
	{
		name_spec_match = atoi(str);
	}
	if(str)
	{
		duster_free(str);
		str = NULL;
	}
	return 0;
}

int get_spec_name_match()
{
	return name_spec_match;
}

/*****************************************************************************
* function descprition : for xml ,
* Updated @ 140110	add xml tag	message_type: 0 -- SMS ; 1 -- MMS ; 2 -- WAP-PUSH; 3 -- voice mail; 5 -- Long SMS
*****************************************************************************/
int SMS_get_displaylist(MrvXMLElement * root, UINT8 mem_store,UINT8 page_num,UINT8 data_per_page,UINT8 tags)
{
	char 			*str = NULL;
	char			 SMS_index[12] = {0}, Item_index_buf[12] = {0};
	char			 tmp[20] = {0};
	UINT32			 total_sms_num = 0;
	OSA_STATUS		 osa_status = 0;
	UINT32			 localSMS_skipNum = 0,shownLocalSMS = 0,total_page_num = 0;
	s_MrvFormvars 		*tempNode = NULL,*list_start=NULL,*list_last=NULL;
	SMS_Node	 	*tempSMS;
	char			 LongSMS_index[100] = {0};
	char			 tmpSubject[400];
	char			*p1 = NULL,*p2 = NULL,*p3 = NULL,*p4 = NULL,*p5 = NULL,*p6 = NULL,*tmp_str = NULL;
	char 			*ucs_char = NULL,	*buff_SMSSubject = NULL;
	UINT16			 ghandle = 0;
	int 		     ret     = 0;
	MrvXMLElement 	*pMessage_list = NULL,*pNewTtemEle = NULL,*pnewChild = NULL;
	UINT8			 Item_index = 1;
	char 			 *find_name = NULL,	*add_name = NULL,	*add_name_char = NULL;
	int 			  name_len = 0;
	int 			  nIndex = 0;
	UINT8			  code_type = 255;
	char 			 *p7 = NULL, *p8 = NULL, *p9 = NULL, *p10 = NULL, *p11 = NULL, *p12 = NULL;
	UINT8			  class_type = 255;
	int					sent_status = 0;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	Duster_module_Printf(1,"%s,mem_store=%d,page_num=%d,data_per_page=%d,tags=%d",__FUNCTION__,mem_store,page_num,data_per_page,tags);

	Duster_module_Printf(1,"%s: try to get WebSMSSema (%d)",__FUNCTION__, WebSMSSema);
	osa_status = OSASemaphoreAcquire(WebSMSSema, OS_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS );
	Duster_module_Printf(1,"%s: get WebSMSSema",__FUNCTION__);
	Duster_module_Printf(1,"%s root->lpszname is %s" ,__FUNCTION__,root->lpszName);

	pMessage_list = MrvFindElement(root,"get_message/message_list");
	if(!pMessage_list)
	{
		Duster_module_Printf(1,"%s can not find pMessage_list",__FUNCTION__);
		goto EXIT;
	}
	Duster_module_Printf(1,	"%s find message_list,pMessage_list->lpszname is %s",	__FUNCTION__, pMessage_list->lpszName);

	/*notify led to stop blink start*/
	SMS_get_new_sms_num_led();
	SMS_set_sms_zero_led();
	UIRefresh_Sms_Change();
	/*notify led to stop blink start*/

	if(mem_store == 0)
	{
		if(totalSimSmsNum == 0)
		{
			Duster_module_Printf(1,"%s there're no sim sms",__FUNCTION__);
			memset( tmp, '\0', 	20);
			sprintf(tmp, "%d",	0);
			goto EXIT;
		}
		else if(totalSimSmsNum > 0)
		{
			list_start		 = SimSMS_list_start;
			list_last		 = SimSMS_list_last;
			total_sms_num	 = SMS_calc_Long(SimSMS_list_start,	SimSMS_list_last);
		}
	}
	if(mem_store == 1)
	{
		if(tags == 12) /*get recv sms*/
		{
			totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
			if(totalLocalNum_LongSMS == 0)
			{
				Duster_module_Printf(1,"%s there're no local recv sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalNum_LongSMS > 0)
			{
				list_start	   = LocalSMS_list_start;
				list_last	   = LocalSMS_list_last;
				total_sms_num  = totalLocalNum_LongSMS;
			}

		}
		else if(tags == 2)/* get local sent sms*/
		{
			totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
			if(totalLocalSentNum == 0 )
			{
				Duster_module_Printf(1,"%s there're no local sent sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalSentNum >0 )
			{
				list_start		 = LocalSendSMS_list_start;
				list_last		 = LocalSendSMS_list_last;
				total_sms_num	 = totalLocalSentNum_LongSMS;
			}
		}
		else if(tags == 11)/*get local draft sms*/
		{
			if(totalLocalDraftNum == 0)
			{
				Duster_module_Printf(1,"%s there're no local draft sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalDraftNum > 0)
			{
				list_start		 = LocalDraftSMS_list_start;
				list_last		 = LocalDraftSMS_list_last;
				total_sms_num	 = totalLocalDraftNum;
			}
		}
	}


	total_page_num = total_sms_num/data_per_page;
	if(total_sms_num%data_per_page)
		total_page_num++;

	memset( tmp, '\0',   	20);
	sprintf(tmp, "%d",	total_page_num);
	psm_set_wrapper(PSM_MOD_SMS,"get_message","total_number",tmp);

	if(total_sms_num <= (page_num - 1)*SupportdisplaySMSNum)
	{
		Duster_module_Printf(1,	"%s page number wrong",	__FUNCTION__);
		goto EXIT;
	}
	if(total_sms_num>(page_num - 1)*SupportdisplaySMSNum)
	{
		localSMS_skipNum = (page_num-1)*SupportdisplaySMSNum;
		tempNode		 =  list_start;
		while(localSMS_skipNum && tempNode)
		{
			tempSMS = (SMS_Node*)tempNode->value;
			if(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == START))
			{
				while(tempNode)
				{
					tempSMS  = (SMS_Node*)tempNode->value;
					if((tempSMS->isLongSMS) && tempSMS->LSMSinfo->LSMSflag == END)
						break;
					tempNode = tempNode->next;
				}
			}
			localSMS_skipNum--;
			if(!tempNode)
				break;
			tempNode  = tempNode->next;
		}
	}

	buff_SMSSubject      =  sms_malloc(MAX_LSMS_SEGMENT_NUM<<9);
	DUSTER_ASSERT(buff_SMSSubject);
	memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);

	shownLocalSMS = 0;
	while(tempNode)
	{
		tempSMS = (SMS_Node*)tempNode->value;
		if((!tempSMS->isLongSMS)||	(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == ONE_ONLY))) //not long sms or long sms just one segment
		{

			if(PlatformIsTPMifi())
			{
				if(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == ONE_ONLY))
				{
					tempNode = tempNode->next;
					continue;
				}
			}

			memset(SMS_index,	'\0',	12);
			if(mem_store == 0)//sim
			{
				//current not support sim rcv/sent display sepereatly
#if 0
				if((tags == 2)&&((tempSMS->sms_status != SEND_SUCCESS)&&(tempSMS->sms_status != UNSENT)))
				{
					tempNode = tempNode->next;
					continue;
				}
				if((tags == 12)&&((tempSMS->sms_status != READED)&&(tempSMS->sms_status != UNREAD)))
				{
					tempNode = tempNode->next;
					continue;
				}
#endif
				sprintf(SMS_index,"%s%d","SRCV",tempSMS->psm_location);
				Duster_module_Printf(1,"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
				Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 12)&&(mem_store == 1))//local
			{
				sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(1,	"%s:SMS_index is %s",__FUNCTION__, SMS_index);
				ret = getPSM_index(&(tempSMS->psm_location),&ghandle,1);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
				Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 11)&&(mem_store == 1))
			{

				sprintf(SMS_index,"%s%d","LDRT",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(1,"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
				Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 2)&&(mem_store == 1))
			{
				sprintf(SMS_index,"%s%d","LSNT",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(1,	"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
				Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
			}

			if(str)
			{
				p1 = strchr(str, DELIMTER);
				p2 = strchr(p1+1,DELIMTER);
				p3 = strchr(p2+1,DELIMTER);
				p4 = strchr(p3+1,DELIMTER);
				p5 = strchr(p4+1,DELIMTER);
				p6 = strchr(p5+1,DELIMTER);
				p7 = strchr(p6+1,DELIMTER );
				p8 = strchr(p7+1, DELIMTER);
				p9 = strchr(p8+1, DELIMTER);
				p10 = strchr(p9+1, DELIMTER);
				p11 = strchr(p10+1, DELIMTER);
				p12 = strchr(p11+1, DELIMTER);

				if(p1&&p2&&p3&&p4&&p5&&p6)
				{
					pNewTtemEle = MrvAddElement(pMessage_list,MrvStrdup(DL_XML_ELEMENT_ITEM,0),0,1);
					if(!pNewTtemEle)
						goto EXIT;
					memset(Item_index_buf,	'\0',	12);
					snprintf(Item_index_buf, 10, "%d", Item_index);
					Item_index++;
					MrvAddAttribute(pNewTtemEle,MrvStrdup(DL_XML_ATTR_INDEX,0),MrvStrdup(Item_index_buf,0),1); /*Item index = '1'*/
					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("index", 0),	0,	1);
					if(!pnewChild)
						DUSTER_ASSERT(0);
					tmp_str = sms_malloc(p1 - str +1);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p1 - str +1);
					memcpy(tmp_str,   str, p1-str);/*index*/
					MrvAddText(pnewChild,	tmp_str, 	1);

					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("from", 0),	0,	1);
					if(!pnewChild)
						DUSTER_ASSERT(0);
					tmp_str = sms_malloc(p2-p1);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p2-p1);
					memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/

					/*search match name in Phonebook*/
					find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);

					/*name is xml is consisted as : name;number*/
					if(tmp_str)
					{
						name_len = strlen(find_name) + strlen(tmp_str) + 1;	/* 1 octs for ' ; '*/
					}
					add_name = sms_malloc(name_len + 1);
					DUSTER_ASSERT(add_name);
					memset(add_name,	'\0',	name_len + 1);
					if(find_name)
					{
						strcat(add_name,	find_name);
					}
					strcat(add_name,	";");
					strcat(add_name,	tmp_str);
					sms_free(tmp_str);
					add_name_char = utf8_to_unicode_char(add_name);
					sms_free(add_name);
					MrvAddText(pnewChild,	add_name_char, 	1);

					memset(tmpSubject,      '\0',    400 );
					memset(buff_SMSSubject, '\0',    MAX_LSMS_SEGMENT_NUM<<9);
					if((p4-p3-1)>400)
					{
						Duster_module_Printf(1,"%s subject too long",__FUNCTION__);
						DUSTER_ASSERT(0);
					}
					memcpy(tmpSubject,      p3+1, p4-p3-1);
					tmpSubject[399] = '\0';
					str_replaceChar(tmpSubject, SEMI_REPLACE,  ';');
					str_replaceChar(tmpSubject, EQUAL_REPLACE, '=');
					ucs_char = utf8_to_unicode_char(tmpSubject);
					DUSTER_ASSERT(ucs_char);
					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("subject", 0),	0,	1);
					if(!pnewChild)
						DUSTER_ASSERT(0);
					MrvAddText(pnewChild,	ucs_char, 	1);

					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("received", 0),	0,	1);/*sms_time*/
					if(!pnewChild)
						DUSTER_ASSERT(0);
					tmp_str = sms_malloc(p5-p4);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p5-p4);
					memcpy(tmp_str,	p4+1,	p5-p4-1);
					MrvAddText(pnewChild,	tmp_str, 	1);

					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("status", 0),	0,	1);/*sms_status*/
					if(!pnewChild)
						DUSTER_ASSERT(0);
					tmp_str = sms_malloc(p6-p5);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p6-p5);
					memcpy(tmp_str,	p5+1,	p6-p5-1);
					MrvAddText(pnewChild,	tmp_str, 1);

					memset( tmp, '\0', 	20);
					DUSTER_ASSERT(( p3 - p2 -1) <	20);
					memcpy(tmp,	p2 + 1,	p3 - p2 -1);
					code_type = atoi(tmp);
					Duster_module_Printf(1,	"%d code_type is %d",	__FUNCTION__,	code_type);
					pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("message_type", 0),	0,	1);
					if(!pnewChild)
						DUSTER_ASSERT(0);
					if((code_type != 3) && (code_type != 4) && (code_type != 6) )
					{
						MrvAddText(pnewChild,	MrvStrdup("0",	0), 	1);	/*SMS*/
					}
					else if(code_type == 3)
					{
						MrvAddText(pnewChild,	MrvStrdup("1",	0), 	1);	/*MMS*/
					}
					else if(code_type == 4)
					{
						MrvAddText(pnewChild,	MrvStrdup("2",	0), 	1);	/*WAP-PUSH*/
					}
					else if(code_type == 6)
					{
						MrvAddText(pnewChild,	MrvStrdup("3",	0), 	1);	/*VOICE MAIL*/
					}

					/*add class_type @140207 start*/
					if(p11 && p12)
					{
						memset( tmp, '\0', 	20);
						DUSTER_ASSERT(( p11 - p12 -1) <	20);
						memcpy(tmp,	p11 + 1,	p12 - p11 -1);
						class_type = atoi(tmp);
						pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("class_type", 0),	0,	1);
						if(!pnewChild)
							DUSTER_ASSERT(0);
						MrvAddText(pnewChild,	MrvStrdup(tmp,	0), 	1); /*class type*/
					}
					/*add class_type @140207 end*/

					if(str)
						sms_free(str);
					str = NULL;
				}
				else
				{
					Duster_module_Printf(1,"%s get sms from psm wrong",__FUNCTION__);
					if(str)
					{
						sms_free(str);
						str = NULL;
					}
					tempNode = tempNode->next;
					continue;
				}
				if(str)
				{
					sms_free(str);
					str = NULL;
				}
			}
			else if(!str)
			{
				Duster_module_Printf(1,"%s can't get SMS_index %s",__FUNCTION__,SMS_index);
				tempNode = tempNode->next;
				continue;
			}
			shownLocalSMS++;
		}
		else if(tempSMS->isLongSMS	&&	(tempSMS->LSMSinfo->LSMSflag == START))//long SMS more than one segment
		{
			if(PlatformIsTPMifi())
			{
				if(	!tempSMS->LSMSinfo->isRecvAll && (tempSMS->LSMSinfo->totalSegment <= MAX_LSMS_SEGMENT_NUM) && (tempSMS->LSMSinfo->receivedNum <= MAX_LSMS_SEGMENT_NUM) )
				{
					tempNode = tempNode->next;
					while(tempNode)
					{
						tempSMS=(SMS_Node*)tempNode->value;
						if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							break;
						}
						tempNode=tempNode->next;
					}
					tempNode=tempNode->next;
					continue;
				}
			}
			sent_status = 0;
			memset(LongSMS_index,	'\0',	100);
			memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);
			while(tempNode)
			{
				tempSMS=(SMS_Node*)tempNode->value;
				memset(SMS_index,	'\0',	12);
				if(mem_store == 0)//sim
				{
					//current not support sim rcv/sent display sepereatly
#if 0
					if((tags == 2)&&((tempSMS->sms_status != SEND_SUCCESS)&&(tempSMS->sms_status != UNSENT)))
					{
						tempNode = tempNode->next;
						continue;
					}
					if((tags == 12)&&((tempSMS->sms_status != READED)&&(tempSMS->sms_status != UNREAD)))
					{
						tempNode = tempNode->next;
						continue;
					}
#endif
					sprintf(SMS_index,"%s%d","SRCV",tempSMS->psm_location);
					str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
					Duster_module_Printf(1,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 12)&&(mem_store == 1))//local
				{
					sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location); //Local SMS
					ret = getPSM_index(&(tempSMS->psm_location),&ghandle,1);
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
					Duster_module_Printf(1,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 11)&&(mem_store == 1))
				{

					sprintf(SMS_index,"%s%d","LDRT",tempSMS->psm_location); //Local SMS
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
					Duster_module_Printf(1,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 2)&&(mem_store == 1))
				{
					sprintf(SMS_index,"%s%d","LSNT",tempSMS->psm_location); //Local SMS
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
					Duster_module_Printf(1,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(1,"%s,str is %s",__FUNCTION__,str);
				}

				if(tags == 2)
				{
					Duster_module_Printf(1,	"tempSMS->sms_status :%d",	tempSMS->sms_status);
					if(tempSMS->sms_status == SEND_SUCCESS)
					{
						sent_status++;
					}
				}
				
				if(str)
				{

					p1 = strchr(str, DELIMTER);
					p2 = strchr(p1+1,DELIMTER);
					p3 = strchr(p2+1,DELIMTER);
					p4 = strchr(p3+1,DELIMTER);
					p5 = strchr(p4+1,DELIMTER);
					p6 = strchr(p5+1,DELIMTER);
					p7 = strchr(p6+1,DELIMTER );
					p8 = strchr(p7+1, DELIMTER);
					p9 = strchr(p8+1, DELIMTER);
					p10 = strchr(p9+1, DELIMTER);
					p11 = strchr(p10+1, DELIMTER);
					p12 = strchr(p11+1, DELIMTER);

					if(p1&&p2&&p3&&p4&&p5&&p6)
					{
						strcat(LongSMS_index, SMS_index);
						strcat(LongSMS_index,",");
						memset(tmpSubject,    '\0',  400);
						if((p4-p3-1)>400)
						{
							Duster_module_Printf(1,"%s content too long",__FUNCTION__);
							DUSTER_ASSERT(0);
						}
						if(tempSMS->LSMSinfo->LSMSflag == START)
						{
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p3 - p2 -1) <	20);
							memcpy(tmp,	p2 + 1,	p3 - p2 -1);
							code_type = atoi(tmp);
						}
						memcpy(buff_SMSSubject+strlen(buff_SMSSubject),	p3+1,	p4-p3-1);
						if(tempSMS->LSMSinfo->LSMSflag!=END)
						{
							tempNode=tempNode->next;
							if(str)
								sms_free(str);
							str = NULL;
						}
						else if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							str_replaceChar(buff_SMSSubject,SEMI_REPLACE, ';');
							str_replaceChar(buff_SMSSubject,EQUAL_REPLACE,'=');

							ucs_char = utf8_to_unicode_char(buff_SMSSubject);
							DUSTER_ASSERT(ucs_char);

							pNewTtemEle = MrvAddElement(pMessage_list,MrvStrdup(DL_XML_ELEMENT_ITEM,0),0,1);
							if(!pNewTtemEle)
								goto EXIT;
							memset(Item_index_buf,	'\0',	12);
							snprintf(Item_index_buf, 10, "%d", Item_index);
							Item_index++;
							MrvAddAttribute(pNewTtemEle,MrvStrdup(DL_XML_ATTR_INDEX,0),MrvStrdup(Item_index_buf,0),1); /*Item index = '1'*/
							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("index", 0),	0,	1);
							if(!pnewChild)
								DUSTER_ASSERT(0);
							MrvAddText(pnewChild,MrvStrdup(LongSMS_index,0),1);


							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("from", 0),	0,	1);
							if(!pnewChild)
								DUSTER_ASSERT(0);
							tmp_str = sms_malloc(p2-p1);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str,	'\0',	p2-p1);
							memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/

							/*search match name in Phonebook*/
							find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);

							/*name is xml is consisted as : name;number*/
							if(tmp_str)
							{
								name_len = strlen(find_name) + strlen(tmp_str) + 1; /* 1 octs for ' ; '*/
							}
							add_name = sms_malloc(name_len + 1);
							DUSTER_ASSERT(add_name);
							memset(add_name,	'\0',	name_len + 1);
							if(find_name)
							{
								strcat(add_name,	find_name);
							}
							strcat(add_name,	";");
							strcat(add_name,	tmp_str);
							sms_free(tmp_str);
							add_name_char = utf8_to_unicode_char(add_name);
							sms_free(add_name);
							MrvAddText(pnewChild,	add_name_char, 	1);

							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("subject", 0),	0,	1);
							if(!pnewChild)
								DUSTER_ASSERT(0);
							MrvAddText(pnewChild,	ucs_char,	1);

							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("received", 0),	0,	1);/*sms_time*/
							if(!pnewChild)
								DUSTER_ASSERT(0);
							tmp_str = sms_malloc(p5-p4);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str, '\0',	p5-p4);
							memcpy(tmp_str, p4+1,	p5-p4-1);
							MrvAddText(pnewChild,	tmp_str,	1);

							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("status", 0), 0,	1);/*sms_status*/
							if(!pnewChild)
								DUSTER_ASSERT(0);
							tmp_str = sms_malloc(p6-p5);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str, '\0',	p6-p5);
							memcpy(tmp_str, p5+1,	p6-p5-1);
							//MrvAddText(pnewChild,	tmp_str, 1);
							if( tags == 2)
							{
								Duster_module_Printf(1,	"sent_status:%d,totalSegment:%d",	sent_status,	tempSMS->LSMSinfo->totalSegment);
								if((sent_status < tempSMS->LSMSinfo->totalSegment) || (sent_status > 5)) //modified by notion ggj 201603211630
								{
									MrvAddText(pnewChild,	MrvStrdup("2", 0),	1);
								}
								else 
								{
									MrvAddText(pnewChild,	MrvStrdup("3", 0),	1);
								}
							}
							else
								MrvAddText(pnewChild,	tmp_str, 1);

							Duster_module_Printf(1,	"%d code_type is %d",	__FUNCTION__,	code_type);
							pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("message_type", 0),	0,	1);
							if(!pnewChild)
								DUSTER_ASSERT(0);
							if((code_type != 3) && (code_type != 4) && (code_type != 6) )
							{
								MrvAddText(pnewChild,	MrvStrdup("5",	0), 	1);	/*SMS*/
							}
							else if(code_type == 3)
							{
								MrvAddText(pnewChild,	MrvStrdup("1",	0), 	1);	/*MMS*/
							}
							else if(code_type == 4)
							{
								MrvAddText(pnewChild,	MrvStrdup("2",	0), 	1);	/*MMS*/
							}
							else if(code_type == 6)
							{
								MrvAddText(pnewChild,	MrvStrdup("3",	0), 	1); /*VOICE MAIL*/
							}

							/*add class_type @140207 start*/
							if(p11 && p12)
							{
								memset( tmp, '\0', 	20);
								DUSTER_ASSERT(( p11 - p12 -1) < 20);
								memcpy(tmp, p11 + 1,	p12 - p11 -1);
								class_type = atoi(tmp);
								pnewChild = MrvAddElement(pNewTtemEle,	MrvStrdup("class_type", 0), 0,	1);
								if(!pnewChild)
									DUSTER_ASSERT(0);
								MrvAddText(pnewChild,	MrvStrdup(tmp,	0), 	1); /*class type*/
							}
							/*add class_type @140207 end*/
							if(str)
								sms_free(str);
							str = NULL;
							break;
						}
					}
					else
					{
						Duster_module_Printf(1,"%s SMS_index get wrong",__FUNCTION__,SMS_index);
						if(str)
						{
							sms_free(str);
							str = NULL;
						}
						tempNode = tempNode->next;
						continue;
					}
					if(str)
					{
						sms_free(str);
						str = NULL;
					}
				}
				else if(!str)
				{
					Duster_module_Printf(1,"%s SMS_index is %s not found,error!",__FUNCTION__,SMS_index);
					tempNode=tempNode->next;
					while(tempNode)
					{
						tempSMS=(SMS_Node*)tempNode->value;
						if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							break;
						}
						tempNode=tempNode->next;
					}
					tempNode=tempNode->next;
					continue;
				}
			}
			shownLocalSMS++;
		}
		else
		{
			/*add log to trace SMS page ERROR*/
			Duster_module_Printf(1,"%s SMS_ERROR, tempSMS->isLongSMS is %d",__FUNCTION__,tempSMS->isLongSMS);
			Duster_module_Printf(1,"%s SMS_ERROR, tempSMS->psmlocation is %d",__FUNCTION__,tempSMS->psm_location);
			if(tempSMS->isLongSMS && tempSMS->LSMSinfo)
			{
				Duster_module_Printf(1,"%s SMS_ERROR, tempSMS->LSMSinfo->LSMSflag is %d",__FUNCTION__,tempSMS->LSMSinfo->LSMSflag);
			}
		}
		if(shownLocalSMS >= SupportdisplaySMSNum)
			break;
		tempNode=tempNode->next;
		if(pMessage_list)
		{
			Duster_module_Printf(1,"%s, pMessage_list->nSize is %d",	__FUNCTION__,	pMessage_list->nSize);
		}
		if(pNewTtemEle)
		{
			Duster_module_Printf(1,"%s, pNewTtemEle->nSize is %d",	__FUNCTION__,	pNewTtemEle->nSize);
		}
	}
EXIT:
	if(buff_SMSSubject)
		sms_free(buff_SMSSubject);
	pMessage_list = MrvFindElement(root,"get_message/total_number");
	if(pMessage_list)
	{

		for(nIndex = 0; nIndex<pMessage_list->nSize; nIndex++)
		{
			MrvDeleteNode(&pMessage_list->pEntries[nIndex]);
		}
		if (pMessage_list->pEntries)
			duster_free(pMessage_list->pEntries);
		pMessage_list->pEntries = NULL;
		pMessage_list->nMax = 0;
		pMessage_list->nSize = 0;
		memset(SMS_index,	'\0',	12);
		itoa(total_page_num, SMS_index, 10);
		MrvAddText(pMessage_list,MrvStrdup(SMS_index,0),1);
	}
	Duster_module_Printf(1,"%s,	pMessage_list->nSize is %d",	__FUNCTION__,	pMessage_list->nSize);
	OSASemaphoreRelease(WebSMSSema);
	Duster_module_Printf(3,"%s: release WebSMSSema",__FUNCTION__);
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
}
void Web_SMS_task_init()
{
	OSA_STATUS osaStatus;
	osaStatus = OSASemaphoreCreate(&SMSInsertDeleteSema, 1, OSA_FIFO);
	ASSERT(osaStatus == OS_SUCCESS);
	CPUartLogPrintf("enter Web_SMS_task_init");

	osaStatus  = OSATaskCreate(&gRcvSMSTask,
	                           gRcvSMSTaskStack,
	                           DIALER_TASK_STACK_SIZE,
	                           DIALER_TASK_PRIORITY,
	                           (char *)"RcvSMSTask" ,
	                           RcvSMSTask,
	                           (void *)0);

	//open PSM record for saving web-SMS
//	psm_open__(PSM_MOD_SMS, NULL,handle_duster);

	duster_call_module_get_delimiters(PSM_MOD_SMS, &fd, &rd);

	CPUartLogPrintf("leave Web_SMS_task_init");
}


void Web_SMS_MsgQ_Reg(OSMsgQRef RegMsgQ)
{
	duster_Printf("enter %s",__FUNCTION__);
	gWebSMSMSGQ = RegMsgQ;
}
/************************************************************************
function description : send messageQ to envoke DM process

**************************************************************************/
void DM_SENDMSQ(void)
{
	OSA_STATUS 				 osa_status;
	char 				    *str = NULL;
	WlanClientStatusMessage  msg_buf;

	msg_buf.MsgId=DM_register;
	Duster_module_Printf(1,"DM_SENDMSQ: sim_is_ready=%d,gCurrRegStatus=%d",sim_is_ready,gCurrRegStatus);
	str = psm_get_wrapper(PSM_MOD_DEVICE_MANAGEMENT,	NULL,	"DM_SWITCH");
	if(str&&strncmp(str,	"1",	1))
	{
		duster_free(str);
		str = NULL;
		return;
	}
	if(str)
	{
		duster_free(str);
		str = NULL;
	}

	if((sim_is_ready == 1)&&(gCurrRegStatus == 1))
	{
		Duster_module_Printf(1,"SEND DM Register SMS");
		osa_status = OSAMsgQSend(gWlanIndMSGQ, sizeof(WlanClientStatusMessage), (UINT8*)&msg_buf, OSA_NO_SUSPEND);
		DM_first_flag=1;
	}

}
unsigned long int sms_mktime(TIMESTAMP time)
{
	unsigned int year=time.tsYear+2000;
	unsigned int month=time.tsMonth;
	unsigned int day=time.tsDay;
	unsigned int hour=time.tsHour;
	unsigned int min=time.tsMinute;
	unsigned int sec=time.tsSecond;
	unsigned long int ret_value=0;

	Duster_module_Printf(1,"%s,Year is %d,Month is %d,Day is %d,Hour is %d,Min is %d,Sec is %d",
	                     __FUNCTION__,year,month,day,hour,min,sec);

	if(0>=(int)(month-=2))
	{
		month+=12;
		year-=1;

	}

	ret_value = ABS((((
	                      (unsigned long)(year/4-year/100+year/400+367*month/12+day)+year*365-719499
	                  )*24+hour
	                 )*60+min
	                )*60+sec);

	Duster_module_Printf(1,"%s,ret_value = %d",__FUNCTION__,ret_value);
	return ret_value;

}
/****************************************************************************************
 function description : calculate two sms time interv,if interv>interv_minutes return 0,else return 1
*****************************************************************************************/
UINT8 compareTimeInterv(s_MrvFormvars * new_node,s_MrvFormvars * psm_node,int interv_minutes)
{
	SMS_Node *newSMS,*psmSMS;
	TIMESTAMP *savetime = NULL;
	unsigned long int t1_sec = 0,t2_sec = 0,interv_sec = 0,interv_t1_t2 = 0;

	savetime = (TIMESTAMP *)sms_malloc(sizeof(TIMESTAMP));
	DUSTER_ASSERT(savetime);
	memset(savetime,	'\0',	sizeof(TIMESTAMP));

	newSMS=(SMS_Node*)new_node->value;
	psmSMS=(SMS_Node*)psm_node->value;
	Duster_module_Printf(1,"%s, psmSMS->location =  %d",	__FUNCTION__,psmSMS->location);

	SMS_Gettime_FromPSM(psmSMS,	savetime);

	t1_sec=sms_mktime(*(newSMS->savetime));
	t2_sec=sms_mktime(*savetime);
	sms_free(savetime);

	interv_sec=interv_minutes*60;

	interv_t1_t2=abs(t1_sec-t2_sec);

	if(interv_t1_t2<=interv_sec)
	{
		return 1;
	}
	else
		return 0;


}
/**************************************************************************************
 function description: insert new sms node into list start,sorted by rcv time
****************************************************************************************/
void SMS_insertNode_Sort_ByTime(s_MrvFormvars ** start,s_MrvFormvars **last,s_MrvFormvars **newNode)
{
	unsigned long int		 new_sec = 0,cur_sec = 0;
	SMS_Node 				*newSMS = NULL,*curSMS = NULL;
	s_MrvFormvars 				*curNode=NULL,*priorNode=NULL;
	UINT8 					 inserted = 0;
	TIMESTAMP				 *save_time;
	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	if(*start==NULL)
	{
		Duster_module_Printf(3,"%s : start is NULL",__FUNCTION__);
		MrvSlistAdd(*newNode,start,last);
		Duster_module_Printf(1,"leave %s ",__FUNCTION__);
		return ;
	}
	newSMS = (SMS_Node*)((*newNode)->value);
	save_time = (TIMESTAMP*)sms_malloc(sizeof(TIMESTAMP));
	DUSTER_ASSERT(save_time);
	memset(save_time,	'\0',	sizeof(TIMESTAMP));
	new_sec = sms_mktime(*(newSMS->savetime));

	curNode = *start;

	while(curNode)
	{
		curSMS = (SMS_Node*)curNode->value;
		memset(save_time,	'\0',	sizeof(TIMESTAMP));
		SMS_Gettime_FromPSM(curSMS,	save_time);
		cur_sec = sms_mktime(*save_time);
		Duster_module_Printf(1,"new_sec = %d, cur_sec = %d",__FUNCTION__,	new_sec,	cur_sec);
		if(new_sec > cur_sec)
		{
			inserted = 1;
			break;
		}
		if(curSMS->isLongSMS&&(curSMS->LSMSinfo->LSMSflag==START))
		{
			priorNode = curNode;
			curNode = curNode->next;
			while(curNode)
			{
				curSMS = (SMS_Node*)curNode->value;
				if(curSMS->LSMSinfo->LSMSflag == END)
				{
					priorNode = curNode;
					curNode = curNode->next;
					break;
				}
				priorNode = curNode;
				curNode = curNode->next;
			}
		}
		else
		{
			priorNode = curNode;
			curNode = curNode->next;
		}
	}

	if(inserted)
	{
		if(!priorNode)
		{
			Duster_module_Printf(1,"%s: enter as the firstNode",__FUNCTION__);
			slist_insert_firstNode(newNode,start,last);
		}
		else
		{
			priorNode->next = *newNode;
			(*newNode)->next = curNode;
		}
	}
	else
		MrvSlistAdd(*newNode,start,last);

	sms_free(save_time);

	Duster_module_Printf(1,"leave %s",__FUNCTION__);

}
/**************************************************************************************
 function description: insert new sms node into list start
                  input   :action: 0--normal
                                        1-- sorted by time
****************************************************************************************/
void SMS_insert_Node(s_MrvFormvars ** start,s_MrvFormvars **last,s_MrvFormvars **newNode,UINT8 action)
{

	SMS_Node 	*curSMS,*newSMS;
	s_MrvFormvars 	*priorNode,*curNode;
	UINT8		 afterReceivedNum=0;
	UINT8		 insertCase=0;
	SMS_Node *beginSMS = NULL;//added by shoujunl @140127


	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	SMS_DeleteInsert_lock();

	newSMS=(SMS_Node*)((*newNode)->value);

	if(!newSMS->isLongSMS) //not LongSMS
	{
		Duster_module_Printf(3,"%s,not long sms insrt first Node",__FUNCTION__);
		if(action == 0)
			slist_insert_firstNode(newNode,start,last);
		else if(action == 1) //sort by time when initialization first time
			SMS_insertNode_Sort_ByTime(start,last, newNode);
		if(newSMS->savetime)
			sms_free(newSMS->savetime);
		newSMS->savetime = NULL;
		Duster_module_Printf(3,"leave %s",__FUNCTION__);
		goto EXIT;
	}

	if(newSMS->isLongSMS)// is LongSMS buf error
	{
		if((newSMS->LSMSinfo->totalSegment==0)||(newSMS->LSMSinfo->segmentNum==0))
		{
			Duster_module_Printf(3,"%s received or segmentNum is 0 ",__FUNCTION__);

			newSMS->LSMSinfo->LSMSflag=ONE_ONLY;
			newSMS->LSMSinfo->receivedNum = 1;

			if(action ==0)
				slist_insert_firstNode(newNode,start,last);
			else if(action == 1)
				SMS_insertNode_Sort_ByTime(start,last, newNode);
			if(newSMS->savetime)
				sms_free(newSMS->savetime);
			newSMS->savetime = NULL;
			Duster_module_Printf(1,"leave %s",__FUNCTION__);
			goto EXIT;

		}
	}
	/******************Long SMS  Segment*************/
	if(!*start) //list is NULL
	{
		Duster_module_Printf(1,"%s,is LongSMS list is NULL",__FUNCTION__);
		newSMS->LSMSinfo->LSMSflag=ONE_ONLY;
		newSMS->LSMSinfo->receivedNum=1;
		(*newNode)->next=NULL;
		*start=*newNode;
		*last= *newNode;
		if(newSMS->savetime)
			sms_free(newSMS->savetime);
		newSMS->savetime = NULL;
		Duster_module_Printf(1,"leave %s",__FUNCTION__);
		goto EXIT;
	}
	if((action == 0)&&(compareTimeInterv(*newNode,*start,120)==0))
	{

		Duster_module_Printf(1,"%s new sms coming time is 120 mins later than start SMS",__FUNCTION__);
		newSMS->LSMSinfo->receivedNum=1;
		newSMS->LSMSinfo->LSMSflag=ONE_ONLY;
		slist_insert_firstNode(newNode,start,last);
		if(newSMS->savetime)
			sms_free(newSMS->savetime);
		newSMS->savetime = NULL;
		Duster_module_Printf(1,"leave %s",__FUNCTION__);

		goto EXIT;
	}



	curNode=*start;
	priorNode=NULL;

	while(curNode!=NULL)
	{
		curSMS=(SMS_Node*)curNode->value;
		Duster_module_Printf(1,"%s cur list has at least one node",__FUNCTION__);

		//Duster_module_Printf(3,"%s,newSMS->srcNum is %s,curSMS->srcNum is %s",__FUNCTION__,newSMS->srcNum,curSMS->srcNum);
		Duster_module_Printf(3,"%s curSMS->receivedNum is %d,curSMS->totalSegment is %d",__FUNCTION__,curSMS->LSMSinfo->receivedNum,curSMS->LSMSinfo->totalSegment);
		Duster_module_Printf(3,"%s,newSMS->refrerenceNum=%d,curSMS->referenceNum=%d",__FUNCTION__,newSMS->LSMSinfo->referenceNum,curSMS->LSMSinfo->referenceNum);
		Duster_module_Printf(3,"%s,newSMS->segmentNum=%d,curSMS->segmentNum=%d",__FUNCTION__,newSMS->LSMSinfo->segmentNum,curSMS->LSMSinfo->segmentNum);

		if(curSMS->isLongSMS&&(curSMS->LSMSinfo->LSMSflag == START))
		{
			if( curSMS->LSMSinfo->isRecvAll || (curSMS->LSMSinfo->receivedNum >= MAX_LSMS_SEGMENT_NUM))
			{
				Duster_module_Printf(3,"%s,cur long SMS has received all",__FUNCTION__);
				while(curNode)
				{
					curNode=curNode->next;
					curSMS=(SMS_Node*)curNode->value;
					if(curSMS->LSMSinfo->LSMSflag==END)
						break;
				}

				if(curNode->next==NULL)
					break;
				else
				{

					priorNode=curNode;
					curNode=curNode->next;
				}
			}
		}


		curSMS=(SMS_Node*)curNode->value;
		if(curSMS->isLongSMS &&	!curSMS->LSMSinfo->isRecvAll)
		{
			Duster_module_Printf(1,"%s curSMS->psmlocation is %d",__FUNCTION__,curSMS->psm_location);
			DUSTER_ASSERT(newSMS->srcNum);
			DUSTER_ASSERT(curSMS->srcNum);
			Duster_module_Printf(1,"%s, newSMS->srcNum is %s len is %d,",__FUNCTION__,newSMS->srcNum ,strlen(newSMS->srcNum));
			Duster_module_Printf(1,"%s, curSMS->srcNum is %s len is %d,",__FUNCTION__,curSMS->srcNum ,strlen(curSMS->srcNum));
		}
		if(curSMS->isLongSMS&&(!strcmp(newSMS->srcNum,curSMS->srcNum))
		        &&(newSMS->LSMSinfo->referenceNum	==	curSMS->LSMSinfo->referenceNum)
		        &&(newSMS->LSMSinfo->totalSegment==	curSMS->LSMSinfo->totalSegment)
		        &&(compareTimeInterv(*newNode,curNode,10)== 1)
		        &&curSMS->LSMSinfo->receivedNum < MAX_LSMS_SEGMENT_NUM)
		{
			Duster_module_Printf(1,	"%s curSMS->LSMSinfo->LSMSflag is %d",	__FUNCTION__,	curSMS->LSMSinfo->LSMSflag);
			if(curSMS->LSMSinfo->LSMSflag == ONE_ONLY)
			{

				curSMS->LSMSinfo->receivedNum++;
				newSMS->LSMSinfo->receivedNum=curSMS->LSMSinfo->receivedNum;
				Duster_module_Printf(1,	"%s curSMS->LSMSinfo->segmentNum is %d,newSMS->LSMSinfo->segmentNum is %d",	__FUNCTION__, curSMS->LSMSinfo->segmentNum, newSMS->LSMSinfo->segmentNum);
				if(newSMS->LSMSinfo->segmentNum > curSMS->LSMSinfo->segmentNum)
				{
					curSMS->LSMSinfo->LSMSflag=START;
					Duster_module_Printf(3,"%s,curSMS->receivedNum is %d,curSMS->totalSegment is %d",__FUNCTION__,curSMS->LSMSinfo->receivedNum,curSMS->LSMSinfo->totalSegment);
					if(curSMS->LSMSinfo->receivedNum==curSMS->LSMSinfo->totalSegment)
					{
						Duster_module_Printf(3,"%s set curSMS->isRecvAll TRUE",__FUNCTION__);
						curSMS->LSMSinfo->isRecvAll=TRUE;
						sms_free(curSMS->srcNum);
						curSMS->srcNum = NULL;
						sms_free(newSMS->srcNum);
						newSMS->srcNum = NULL;
						newSMS->LSMSinfo->isRecvAll = TRUE;
					}
					newSMS->LSMSinfo->LSMSflag=END;
					insertCase=1;
					break;
				}
				else if(newSMS->LSMSinfo->segmentNum < curSMS->LSMSinfo->segmentNum)
				{
					if(newSMS->LSMSinfo->receivedNum==newSMS->LSMSinfo->totalSegment)
					{
						newSMS->LSMSinfo->isRecvAll=TRUE;
						sms_free(curSMS->srcNum);
						curSMS->srcNum = NULL;
						sms_free(newSMS->srcNum);
						newSMS->srcNum = NULL;
						curSMS->LSMSinfo->isRecvAll = TRUE;
					}

					curSMS->LSMSinfo->LSMSflag=END;
					newSMS->LSMSinfo->LSMSflag=START;
					insertCase=2;
					break;
				}

			}
			if(curSMS->LSMSinfo->LSMSflag == START)
			{
				//modified by shoujunl @140127
				beginSMS = curSMS;
				afterReceivedNum 	= curSMS->LSMSinfo->receivedNum + 1;

#if 0
				curSMS->LSMSinfo->receivedNum++;
				afterReceivedNum 	= curSMS->LSMSinfo->receivedNum;
				newSMS->LSMSinfo->receivedNum	= afterReceivedNum;
				if(curSMS->LSMSinfo->receivedNum == curSMS->LSMSinfo->totalSegment)
				{
					curSMS->LSMSinfo->isRecvAll=TRUE;
					sms_free(curSMS->srcNum);
					curSMS->srcNum = NULL;
					sms_free(newSMS->srcNum);
					newSMS->srcNum = NULL;
					newSMS->LSMSinfo->isRecvAll = TRUE;
				}
#endif
				while(curNode)
				{
					Duster_module_Printf(1,	"%s begin while loop",	__FUNCTION__);
					curSMS=(SMS_Node*)curNode->value;
					if(newSMS->LSMSinfo->segmentNum > curSMS->LSMSinfo->segmentNum)
					{
						curSMS->LSMSinfo->receivedNum	=	afterReceivedNum;
						newSMS->LSMSinfo->receivedNum = afterReceivedNum;
						if(curSMS->LSMSinfo->receivedNum == curSMS->LSMSinfo->totalSegment)
						{
							curSMS->LSMSinfo->isRecvAll = TRUE;
						}
						if(newSMS->LSMSinfo->receivedNum == newSMS->LSMSinfo->totalSegment)
						{
							newSMS->LSMSinfo->isRecvAll = TRUE;
						}
						if(curSMS->LSMSinfo->LSMSflag == END)
						{
							curSMS->LSMSinfo->LSMSflag=MIDDLE;
							newSMS->LSMSinfo->LSMSflag=END;
							insertCase=1;
							break;
						}
						else
						{
							priorNode=curNode;
							curNode=curNode->next;
							continue;
						}
					}
					else if(newSMS->LSMSinfo->segmentNum < curSMS->LSMSinfo->segmentNum)
					{
						curSMS->LSMSinfo->receivedNum	=	afterReceivedNum;
						newSMS->LSMSinfo->receivedNum = afterReceivedNum;
						if(curSMS->LSMSinfo->receivedNum == curSMS->LSMSinfo->totalSegment)
						{
							curSMS->LSMSinfo->isRecvAll = TRUE;
						}
						if(newSMS->LSMSinfo->receivedNum == newSMS->LSMSinfo->totalSegment)
						{
							newSMS->LSMSinfo->isRecvAll = TRUE;
						}

						if(curSMS->LSMSinfo->LSMSflag == START)
						{
							curSMS->LSMSinfo->LSMSflag = MIDDLE;
							newSMS->LSMSinfo->LSMSflag = START;
							if(newSMS->LSMSinfo->receivedNum == newSMS->LSMSinfo->totalSegment)
								newSMS->LSMSinfo->isRecvAll = TRUE;
							insertCase=2;
							break;
						}
						else if((curSMS->LSMSinfo->LSMSflag == MIDDLE)||(curSMS->LSMSinfo->LSMSflag == END))
						{
							newSMS->LSMSinfo->LSMSflag=MIDDLE;
							insertCase=2;
							break;
						}
					}
					else if(newSMS->LSMSinfo->segmentNum == curSMS->LSMSinfo->segmentNum)
					{
						Duster_module_Printf(1,	"%s same segmentNum is %d ",	__FUNCTION__,	newSMS->LSMSinfo->segmentNum);
						break;
					}

				}
			}
		}

		if(insertCase)
		{
			if(beginSMS->LSMSinfo->receivedNum == beginSMS->LSMSinfo->totalSegment)
			{
				beginSMS->LSMSinfo->isRecvAll=TRUE;
				if(curSMS->srcNum)
				{
					sms_free(curSMS->srcNum);
					curSMS->srcNum = NULL;
				}
			}
			if(newSMS->LSMSinfo->receivedNum == newSMS->LSMSinfo->totalSegment)
			{
				if(newSMS->srcNum)
				{
					sms_free(newSMS->srcNum);
					newSMS->srcNum = NULL;
				}
				newSMS->LSMSinfo->isRecvAll = TRUE;
			}
			break;
		}

		priorNode=curNode;
		curNode=curNode->next;


	}

	switch(insertCase)
	{
	case 0: //insert first Node for long SMS
		Duster_module_Printf(3,"%s case 0",__FUNCTION__);

		newSMS->LSMSinfo->receivedNum=1;
		newSMS->LSMSinfo->LSMSflag=ONE_ONLY;
		
		if(action == 0)
			slist_insert_firstNode(newNode,start,last);
		else if(action == 1)
			SMS_insertNode_Sort_ByTime(start,last,newNode);
		break;
	case 1: //insert  after curNode

		Duster_module_Printf(3,"%s case 1",__FUNCTION__);
		if(!curNode->next)
			MrvSlistAdd(*newNode,start,last);
		else
		{
			(*newNode)->next=curNode->next;
			curNode->next=*newNode;
		}
		break;
	case 2: //insert before curNode

		Duster_module_Printf(3,"%s case 2",__FUNCTION__);
		if(!priorNode)
			slist_insert_firstNode(newNode,start,last);
		else
		{
			priorNode->next=*newNode;
			(*newNode)->next=curNode;
		}
		break;


	}

	if(newSMS->savetime)
		sms_free(newSMS->savetime);
	newSMS->savetime = NULL;

EXIT:
	SMS_DeleteInsert_unlock();
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return;
}

BOOL utf8_to_usc2(UINT8 *inbuf,UINT8*outbuf)
{
	UINT8	   *strP;
	UINT16 		temp1,temp2;
	BOOL 		is_unrecongnized=FALSE;


	Duster_module_Printf(3,"enter %s",__FUNCTION__);
	strP=inbuf;
	if(!inbuf||!outbuf)
	{
		Duster_module_Printf(3,"%s,inbuf or outbuf is Null",__FUNCTION__);
		return FALSE;
	}

	//Duster_module_Printf(1,"%s *strP=%02X",__FUNCTION__,*strP);

	while(*strP!=NULL)
	{

		//Duster_module_Printf(1,"%s *strP=%02X",__FUNCTION__,*strP);
		if(0x00==(*strP&0x80))
		{
			//	Duster_module_Printf(3,"%s,1 byte",__FUNCTION__);
			// 1byte UTF-8 character
			*outbuf=*strP;
			outbuf++;
			*outbuf=0x00;
			strP+=1;
			is_unrecongnized=TRUE;
			outbuf++;
		}
		else  if((0xC0==(*strP&0xE0))&&(0x80==(*(strP+1)&0xC0)))
		{

			//	Duster_module_Printf(3,"%s,2 bytes",__FUNCTION__);
			temp1=(UINT16)(*strP&0x1F);
			temp1<<=6;
			temp1|=(UINT16)(*(strP+1)&0x3F);
			memcpy(outbuf,&temp1,2);
			strP+=2;
			outbuf+=2;
			is_unrecongnized=TRUE;
		}
		else if((0xE0==(*strP&0xF0))&&(0x80==(*(strP+1)&0xC0))&&(0x80==(*(strP+2)&0xC0)))
		{

			//	Duster_module_Printf(3,"%s,3 bytes",__FUNCTION__);
			temp1=(UINT16)(*strP&0x0F);
			temp1<<=12;
			temp2=(UINT16)(*(strP+1)&0x3F);
			temp2<<=6;
			temp1=temp1|temp2|(UINT16)(*(strP+2)&0x3F);
			memcpy(outbuf,&temp1,2);
			strP+=3;
			outbuf+=2;
			is_unrecongnized=TRUE;
		}
		else
		{
			//unrecongnize byte
			is_unrecongnized=FALSE;
			break;

		}
	}
	Duster_module_Printf(3,"leave %s",__FUNCTION__);

	return is_unrecongnized;

}
/*********************************************************************************
* function description : calculate if string pSrc is a long sms,and how to split pSrc
*
**********************************************************************************/
/*Updated @ 20140219 for extending ASCII*/
SplitInfo calcGSM7Info(char *pSrc)
{

	Duster_module_Printf(1,"enter %s,strlen(pSrc)=%d",__FUNCTION__,strlen(pSrc));

	UINT16	 	GSM7length,singleLen;
	UINT8		tmp,totalSegNum;
	UINT8 		i,j=0;
	SplitInfo 	SrcInfo;
	UINT16 		pos=0;
	UINT16 		mSrcLen = 0;
	UINT16		unprocessLen = 0;
	UINT8		GSM7ByteLen = 0 ; /*Byte length in TABLE GSM7_Default*/


	mSrcLen = strlen(pSrc);

	memset(SrcInfo.pos,	'\0',	14);
	GSM7length=0;
	singleLen=0;
	tmp=0;
	totalSegNum=0;
	i=0;

	while(*(pSrc+pos)!=NULL)
	{
		unprocessLen = mSrcLen - pos + 1;
		if((unprocessLen > 2)&&(0xE0==(*(pSrc+pos)&0xF0))&&(0x80==(*(pSrc+pos+1)&0xC0))&&(0x80==(*(pSrc+pos+2)&0xC0)))
		{
			Duster_module_Printf(1,	"%s 3 bytes UTF8 ",	__FUNCTION__);
			/*Euro Sigh 0xE2, 0X82, 0XAC*/
			/*Pound Sign 0xEF 0xBF, 0xA1*/
			/*RMB Sign 0xEF 0xBF, 0xA5*/
			Duster_module_Printf(1,	"%s,%02x,%02x,%02x ",	__FUNCTION__,	*(pSrc+pos),*(pSrc+pos+1),	*(pSrc+pos+2));
			if((*(pSrc+pos) == 0xE2) && (*(pSrc+pos + 1) == 0x82) && (*(pSrc+pos+2) == 0xAC))
			{
				GSM7length += 2;
				if( (singleLen == 152) ||(singleLen == 153) )	/*Before this character*/
				{
					singleLen=0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos - 1;
					singleLen +=2;
				}
				else if(singleLen == 151)	/*Add this character*/
				{
					singleLen=0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos + 2;
				}
				else
				{
					singleLen +=2;
				}
			}
			else if(((*(pSrc+pos) == 0xEF) && (*(pSrc+pos + 1) == 0xBF) && (*(pSrc+pos+2) == 0xA1))
			        ||((*(pSrc+pos) == 0xEF) && (*(pSrc+pos + 1) == 0xBF) && (*(pSrc+pos+2) == 0xA15)))
			{
				GSM7length ++;
				singleLen++;
				if(singleLen == 153)
				{
					singleLen = 0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos + 2;
				}

			}
			pos += 3;
		}
		else if((unprocessLen > 1) &&(0xC0==(*(pSrc+pos)&0xE0))&&(0x80==(*(pSrc+pos+1)&0xC0)))	/*2 bytes UTF8 all match 1 byte in GSM7_Default Table */
		{
			Duster_module_Printf(1,	"%s 2 bytes UTF8 ",	__FUNCTION__);
			GSM7length ++;
			singleLen++;
			if(singleLen == 153)
			{
				singleLen = 0;
				totalSegNum++;
				SrcInfo.pos[i++] = pos + 1;
			}
			pos += 2;
		}
		else if(0x00 == (*(pSrc+pos)&0x80))
		{
			Duster_module_Printf(1,	"%s 1 bytes UTF8 ",	__FUNCTION__);
			tmp=*(pSrc+pos);
			if((tmp==0x0c)||(tmp==0x5E)||(tmp==0x7B)||(tmp==0x7D)||(tmp==0x5C)	/*0X 1B XX*/
			        ||(tmp==0x5B)||(tmp==0x7E)||(tmp==0x5D)||(tmp==0x7C))
			{
				GSM7length +=2;
				if( (singleLen == 152) ||(singleLen == 153) )	/*Before this character*/
				{
					singleLen=0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos - 1;
					singleLen += 2;
				}
				else if(singleLen == 151)	/*Add this character*/
				{
					singleLen=0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos;
				}
				else
				{
					singleLen += 2;
				}

			}
			else
			{
				GSM7length++;
				singleLen++;
				if(singleLen == 153)
				{
					singleLen=0;
					totalSegNum++;
					SrcInfo.pos[i++] = pos;
				}
			}
			pos++;
		}
	}
	Duster_module_Printf(2,	"%s SrcInfo.pos[i-1] is %d,	strlen(pSrc) is %d",	__FUNCTION__,	SrcInfo.pos[i-1],	strlen(pSrc));
	if(SrcInfo.pos[i-1] != strlen(pSrc) - 1)
	{
		totalSegNum++;
		SrcInfo.pos[i]= strlen(pSrc) - 1 ;
	}
	SrcInfo.gsm7length = GSM7length;
	SrcInfo.totalSegmentNum = totalSegNum;

	Duster_module_Printf(2,"%s,GSM7length is %d,totalSegmentNum is %d",__FUNCTION__,SrcInfo.gsm7length,SrcInfo.totalSegmentNum);

	for(j=0; j<=i; j++)
		Duster_module_Printf(2,"%s ,SrcInfo->pos[j]=%d",__FUNCTION__,SrcInfo.pos[j]);

	Duster_module_Printf(1,"leave %s",__FUNCTION__);

	return SrcInfo;

}


/*****************************************************************************************
  function  description: check if pSrc has multi bytes
  1 -- unicode
  0 -- ascii
******************************************************************************************/
UINT8 scanMultiBytes(char *pSrc) //not accrate enough GSM 7 bit Default alphabet and extension table
{
	UINT16 i;
	for(i=0; i<strlen(pSrc); i++)
		if(*(pSrc+i)&0x80)
			return 1;
	return 0;
}
/********************************************************************************************
 function descriptio : calculate the length of the ucs2 string
*********************************************************************************************/
UINT16 ucs2Count(UINT8 *pSrc)
{
	UINT16 i;
	for(i=0; pSrc[i]+pSrc[i+1]!=0; i+=2);
	return i;
}
extern BOOL msgGetFailCause();/*added by shoujunl 131023*/
#define SMS_RETRY_SEND_TIME 1
#define SMS_RETRY_WAIT_TIME 1200 /*ostick 0.05*600 = 3 secs*/

/**********************************************************************************************************************
  function descprition : send sms or write sms to sim card
  params: number -- dst phone number
              content -- usc2 char string
              encode_type -- UNICODE/gsm7_default
              sendTime -- '13,04,13,17,10,26,+8'
              action 1:send sms
                       2:move sent to sim
                       3:move rcv to sim
               status:used in AT+CMGW,see AT doc.
***********************************************************************************************************************/
int SMS_Send(char *number,char *content,char *encode_type,char *sendTime,s_MrvFormvars **list_start,s_MrvFormvars **list_last,UINT8 action,UINT8 sms_status )
{
	char   resp_str[DIALER_MSG_SIZE] = {'\0'};
	int   ret = 0;
	char  *quote_begin=NULL;
	char  *quote_end=NULL;
	char   smsc_addr[64]= {'\0'};
	char   invert_Smsc[64]= {'\0'};
	char   *invertDestNum = NULL;
	UINT8  smsc_length,destNumlength,smscpart_len = 0,sendLen=0;
	char   smsc_length_char[3]= {'\0'},destNum_len_char[3]= {'\0'};
	UINT8  codeScheme;
	UINT8  septet_num,smsdata_octs;
	char   dataLength[3];

	UINT16 gsm7Length,usc2Length,ucsOct_Num,beforeTPDULen,ucs2CharLen;
	char   *gsm7codedbuff = NULL,*pduDataChar = NULL;
	char   *smsData=NULL;
	char   smsc_type[3],first_oct_char[3];
	UINT8  first_oct=0x11;
	char   *str=NULL;
	UINT16 tpvp;
	char   TP_VP[3],SCTS[15];
	UINT8  statusReport,total_SegmentNum,sequenceNum;
	char   TP_MR[3],TP_PID[3],TP_DCS[3],Udhl_Char[3],Identifier_Char[3],Ref_Char[3],totalSeg_Char[3],Sequence_Char[3];
	char   DA_TYPE[3];
	char   TPDU_Buff[600];
	UINT16 i,startPos,j;
	char   *singleSMSdataBuf = NULL,*singleSMSUtf8 = NULL,indexbuf[15];
	char   *savebuf= NULL ;
	char   timebuf[30]= {0},refNumBuf[5];
	UINT32 flag_value;
	SplitInfo     curSplitInfo;
	s_MrvFormvars     *tempData=NULL;
	SMS_Node     *tempSMS = NULL;
	CommandATMsg *sendSMSMsg=NULL;
	UINT16        psmIndex, ghandle;
	OSA_STATUS    osa_status;
	int			  retrySendTime = 0;
	int			  smsNeedReSend = FALSE;

	Duster_module_Printf(1,"enter %s,action is %d",__FUNCTION__,action);

	if(action==1)
		psm_set_wrapper(PSM_MOD_SMS,NULL,"send_sms_result","1");
	else if((action==2)||(action==3))
		psm_set_wrapper(PSM_MOD_SMS,NULL,"save_sms_result","1");

	pduDataChar = sms_malloc(320);
	singleSMSdataBuf = sms_malloc(480);
	Duster_module_Printf(1,"%s pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
	if(!pduDataChar||!singleSMSdataBuf)
	{

		Duster_module_Printf(1,"%s: there is no enough memory",__FUNCTION__);
		ret =-1;
		goto EXIT;
	}

	smsData =   sms_malloc(5*640+1);
	sendSMSMsg=(CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
	sendSMSMsg->atcmd=  sms_malloc(DIALER_MSG_SIZE);
	sendSMSMsg->ok_fmt= sms_malloc(10);
	sendSMSMsg->err_fmt=sms_malloc(15);


	if(!sendSMSMsg||!sendSMSMsg->atcmd||!sendSMSMsg->ok_fmt||!sendSMSMsg->err_fmt)
	{
		Duster_module_Printf(1,"%s: there is no enough memory",__FUNCTION__);
		ret = -1;
		goto EXIT;
	}

	if((action==1)||(action==2))
		first_oct=0x11;
	else if(action==3)
		first_oct=0x04;


	memset(TP_MR,	'\0',	3);
	memcpy(TP_MR,	"00",	2);
	memset(TP_PID,	'\0',	3);
	memcpy(TP_PID,"00",2);
	memset(TP_DCS,	'\0',	3);
	memset(pduDataChar,	'\0',	320);
	memset(first_oct_char,	'\0',	3);
	memset(Udhl_Char,	'\0',	3);
	memcpy(Udhl_Char,	"05",	2);
	memset(Identifier_Char,	'\0',	3);
	memcpy(Identifier_Char,"00",2);


	memset(timebuf,'\0',30);

	Duster_module_Printf(1,"%s sendTime is %s",__FUNCTION__,sendTime);
	if(sendTime&&(strlen(sendTime)<30))
	{
		memcpy(timebuf,sendTime,strlen(sendTime));
	}
	else
	{
		sprintf(timebuf,"13,04,13,17,10,26,+8");
	}

	str_replaceChar(timebuf,';',',');

	sendSMSMsg->ok_flag=1;

	memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
	sprintf(sendSMSMsg->atcmd,"AT+CSCA?\r");
	memset(sendSMSMsg->ok_fmt,	'\0',	10);
	sprintf(sendSMSMsg->ok_fmt,"+CSCA");
	memset(sendSMSMsg->err_fmt,	'\0',	15);
	sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	sendSMSMsg->callback_func=&CSCA_GET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE=TEL_EXT_GET_CMD;


	osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
	DUSTER_ASSERT(osa_status==OS_SUCCESS);

	flag_value=0;
	osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CSCA_GET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
	//DUSTER_ASSERT(osa_status==OS_SUCCESS);

	if( flag_value == CMS_ERROR)
	{
		duster_Printf("leave %s: AT+CSCA return error",__FUNCTION__);
		ret =-1;
		goto EXIT;
	}

	memcpy(resp_str,csca_resp_str,strlen(csca_resp_str));

	Duster_module_Printf(3,"GET CSCA:%s", resp_str);
	quote_begin	=	strchr(resp_str,'\"');
	quote_end	=	strrchr(resp_str,'\"');
	if((!quote_begin)||(!quote_end))
	{
		Duster_module_Printf(3,"+CSCA:quote_begin,quote_end NULL");
		ret = -3;//quote error
		goto EXIT;
	}
	Duster_module_Printf(3,"quote_end-quote_begin=%d",quote_end-quote_begin);
	if((quote_end-quote_begin)<=3)//not support smsc's length<3
		memcpy(smsc_addr,DEFAULT_SMSC_ADDR,	strlen(DEFAULT_SMSC_ADDR));
	else
	{

		memset(smsc_type,	'\0',	3);
		if(*(quote_begin+1)=='+')
		{
			memcpy(smsc_type,"91",2);//INTERNATIONAL NUMBER
			memcpy(smsc_addr,quote_begin+2,quote_end-quote_begin-2);//+CSCA:"+8613800551500" smsc="8613800551500"
		}
		else
		{

			memcpy(smsc_type,"81",2);//UNKNOWN NUMBER
			memcpy(smsc_addr,quote_begin+1,quote_end-quote_begin-1);//+CSCA:"8613800551500" smsc="8613800551500"

		}
	}
	invert_PhoneNumbers(smsc_addr,invert_Smsc,strlen(smsc_addr));
	Duster_module_Printf(3,"smsc_addr=%s",smsc_addr);
	Duster_module_Printf(3,"invert_Smsc=%s",invert_Smsc);
	smsc_length=1+strlen(invert_Smsc)/2;
	if(smsc_length<16)
	{
		smsc_length_char[0]='0';
		itoa(smsc_length,smsc_length_char+1,16);
	}
	else
		itoa(smsc_length,smsc_length_char,16);

	Duster_module_Printf(3,"smsc_length=%d",smsc_length);
	Duster_module_Printf(3,"smsc_length_char=%s",smsc_length_char);


	//current contacts is number

	if(!number)
	{
		number=sms_malloc(15);
		memset(number,	'\0',	15);
		memcpy(number,"10086",5);
	}


	if(number)
	{
		memset(DA_TYPE,	'\0',	3);
		destNumlength=strlen(number);
		invertDestNum = sms_malloc(destNumlength + 5);
		DUSTER_ASSERT(invertDestNum);
		memset(invertDestNum,	'\0',	destNumlength + 5);
		if(*number=='+')
		{
			memcpy(DA_TYPE,"91",2);
			destNumlength--;
			invert_PhoneNumbers(number + 1,invertDestNum,destNumlength);
		}
		else
		{
			memcpy(DA_TYPE,"81",2);
			invert_PhoneNumbers(number,invertDestNum,destNumlength);
		}
		Duster_module_Printf(3,"%s destNumlength=%d,invertDestNum is %s",__FUNCTION__,destNumlength,invertDestNum);

		if(destNumlength<16)
		{
			destNum_len_char[0]='0';
			itoa(destNumlength,destNum_len_char+1,16);
		}
		else
			itoa(destNumlength,destNum_len_char,16);

	}
	else
	{
		Duster_module_Printf(3,"%s,contacts number is NULL",__FUNCTION__);

		Duster_module_Printf(1,"leave %s",__FUNCTION__);
		ret= -1;
		goto EXIT;
	}

	if((action==1)||(action==2))
	{
		str=psm_get_wrapper(PSM_MOD_SMS,"sms_setting","save_time");//default relative TP-VP is 143 0x8F
		if(str)
		{
			memset(TP_VP,	'\0',	3);
			tpvp=ConvertStrToInteger(str);
			if(tpvp>255)
			{
				Duster_module_Printf(3,"%s tpvp is wrong ",__FUNCTION__);
				ret= -1;
				goto EXIT;
			}

			if(tpvp<16)
			{
				TP_VP[0]='0';
				itoa(tpvp,TP_VP+1,16);
			}
			else
				itoa(tpvp,TP_VP,16);
			Duster_module_Printf(3,"%s,vpf is present,TP_VP is %s",__FUNCTION__,TP_VP);
			sms_free(str);
			str = NULL;
		}

		memcpy(TP_VP,"8F",2);
	}
	else if(action==3)
	{
		memset(SCTS,	'\0',	15);
		ret = decodeTimeFormat(sendTime,SCTS);
		if(ret == 0)
			sprintf(SCTS,"%s","00000000000000");
	}

	str=psm_get_wrapper(PSM_MOD_SMS,"sms_setting","status_report"); //set status_report
	if(str&&(action!=3))
	{
		statusReport=ConvertStrToInteger(str);
		Duster_module_Printf(3,"%s,statusReport is %d",__FUNCTION__,statusReport);
		if(statusReport==1)
		{
			Duster_module_Printf(3,"%s statusReport is asked",__FUNCTION__);
			first_oct=first_oct|0x20;
		}
		sms_free(str);
		str = NULL;

	}

	if(first_oct<16)
	{
		first_oct_char[0]='0';
		itoa(first_oct,first_oct_char+1,16);
	}
	else
		itoa(first_oct,first_oct_char,16);

	if(content!=NULL)
	{
		//Duster_module_Printf(1,"%s, content is %s,strlen(content)=%d",__FUNCTION__,content,strlen(content));
		memset(smsData,	'\0',	5*640+1);

		if(!strcmp(encode_type,"GSM7_default"))
		{
			codeScheme=0;
			memcpy(TP_DCS,"00",2);//GSM7
			if(strlen(content)>3200)
			{
				Duster_module_Printf(3,"%s gsm7 content is too long,more than 5 items",__FUNCTION__);
				ret = -1;
				goto EXIT;
			}
			smsData = unicodechar_to_utf8_bigdean(content);
			Duster_module_Printf(3,"%s,strlen(smsData)=%d",__FUNCTION__,strlen(smsData));
		}
		else if(!strcmp(encode_type,"UNICODE"))
		{
			if(strlen(content)>1400)
			{
				Duster_module_Printf(3,"%s,UCS2 content is too long,more than 5 items",__FUNCTION__);
				ret =  -1;
				goto EXIT;
			}
			codeScheme = 1;
			memcpy(TP_DCS,"08",2);//UCS2
		}
		else
		{
			Duster_module_Printf(3,"%s encode_type is %s error!",__FUNCTION__,encode_type);
			ret = -1;
			goto EXIT;
		}

		memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
		sprintf(sendSMSMsg->atcmd,"AT+CMGF=%d\r",0);
		if(sendSMSMsg->ok_fmt)
		{
			sms_free(sendSMSMsg->ok_fmt);
			sendSMSMsg->ok_fmt = NULL;
		}
		sendSMSMsg->ok_flag=1;
		sendSMSMsg->callback_func=&CMGF_SET_CALLBACK;


		osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
		DUSTER_ASSERT(osa_status==OS_SUCCESS);

		flag_value=0;
		osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGF_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
		//DUSTER_ASSERT(osa_status==OS_SUCCESS);

		if( flag_value == CMS_ERROR)
		{
			duster_Printf("leave %s: AT+CMGF return error",__FUNCTION__);
			ret = -1;
			goto EXIT;
		}

		memset(TPDU_Buff,	'\0',	600);
		memcpy(TPDU_Buff,smsc_length_char,2);//smsc length
		memcpy(TPDU_Buff+strlen(TPDU_Buff),smsc_type,2);//smsc type
		memcpy(TPDU_Buff+strlen(TPDU_Buff),invert_Smsc,strlen(invert_Smsc));//smsc
		smscpart_len = strlen(TPDU_Buff);
		memcpy(TPDU_Buff+strlen(TPDU_Buff),first_oct_char,2);// first_oct
		if(action!=3)
			memcpy(TPDU_Buff+strlen(TPDU_Buff),TP_MR,strlen(TP_MR));//TP_MR
		memcpy(TPDU_Buff+strlen(TPDU_Buff),destNum_len_char,2);//DA_LEN
		memcpy(TPDU_Buff+strlen(TPDU_Buff),DA_TYPE,2);//DA TYPE
		memcpy(TPDU_Buff+strlen(TPDU_Buff),invertDestNum,strlen(invertDestNum));//dest Number
		memcpy(TPDU_Buff+strlen(TPDU_Buff),TP_PID,2); //TP PID
		memcpy(TPDU_Buff+strlen(TPDU_Buff),TP_DCS,2);//TP DCS
		if((action==1)||(action==2))
			memcpy(TPDU_Buff+strlen(TPDU_Buff),TP_VP,2);//TP-VP
		else if(action==3)
			memcpy(TPDU_Buff+strlen(TPDU_Buff),SCTS,14);
		if(invertDestNum)
			sms_free(invertDestNum);
		invertDestNum = NULL;

		memset(dataLength,	'\0',	3);
		if(codeScheme==0) //GSM7 bit
		{
			curSplitInfo = calcGSM7Info(smsData);
			gsm7Length = curSplitInfo.gsm7length;
			Duster_module_Printf(3,"%s, gsm7Length is %d",__FUNCTION__,gsm7Length);
			if(gsm7Length <= 160)//single sms
			{
				Duster_module_Printf(3,"%s,send 1 sms gsm7 coded",__FUNCTION__);
				if(!gsm7codedbuff)
					gsm7codedbuff = sms_malloc(165);
				DUSTER_ASSERT(gsm7codedbuff);
				memset(gsm7codedbuff,	'\0',	165);
				septet_num=libEncodeGsm7BitData((UINT8 *)smsData,strlen(smsData),gsm7codedbuff,0);

				if(septet_num<16)
				{
					dataLength[0]='0';
					itoa(septet_num,dataLength+1,16);
				}
				else
					itoa(septet_num,dataLength,16);

				if(septet_num*7%8)
					smsdata_octs=septet_num*7/8+1;
				else
					smsdata_octs=septet_num*7/8;
				memset(pduDataChar,	'\0',	320);
				CHAR_TO_2CHAR((UINT8 *)gsm7codedbuff,smsdata_octs,(UINT8 *)pduDataChar);
				sms_free(gsm7codedbuff);
				gsm7codedbuff = NULL;
				memset(singleSMSdataBuf,	'\0',	480);
				memcpy(singleSMSdataBuf,smsData,strlen(smsData));
			}
			else if(gsm7Length > 160)
			{
				Duster_module_Printf(3,"%s,send concatenated sms gsm7 coded",__FUNCTION__);
				if(referenceNum<16)
				{
					Ref_Char[0]='0';
					itoa(referenceNum,Ref_Char+1,16);
				}
				else
					itoa(referenceNum,Ref_Char,16);

				Duster_module_Printf(3,"%s,cur referenceNum is %d,Ref_Char is %s",__FUNCTION__,referenceNum,Ref_Char);
				if(referenceNum==255)
					referenceNum=1;
				else
					referenceNum++;

				memset(refNumBuf,	'\0',	5);
				sprintf(refNumBuf,"%d",referenceNum);
				//Duster_module_Printf(1,"referenceNum updated %s",refNumBuf);
				psm_set_wrapper(PSM_MOD_SMS,NULL,"referenceNum",refNumBuf);

				total_SegmentNum=curSplitInfo.totalSegmentNum;

				if(total_SegmentNum<16)
				{
					totalSeg_Char[0]='0';
					itoa(total_SegmentNum,totalSeg_Char+1,16);
				}
				else
					itoa(total_SegmentNum,totalSeg_Char,16);

				Duster_module_Printf(3,"%s,curSplitInfo->totalSegmentNum is %d,totalSeg_Char is %s",__FUNCTION__,curSplitInfo.totalSegmentNum,totalSeg_Char);

				first_oct=first_oct|0x40;

				memset(first_oct_char,	'\0',	3);

				//reset first_oct_char
				if(first_oct<16)
				{
					first_oct_char[0]='0';
					itoa(first_oct,first_oct_char+1,16);
				}
				else
					itoa(first_oct,first_oct_char,16);

				memcpy(TPDU_Buff+4+strlen(invert_Smsc),first_oct_char,2);

				i=0;
				sequenceNum=1;

				if(action ==1)
				{
					//mkreplaceAuto(total_SegmentNum,list_start,list_last);
				}

				while(i<total_SegmentNum)
				{
					if(i==0)
					{
						startPos=0;
						gsm7Length=curSplitInfo.pos[0]+1;
						Duster_module_Printf(3,"%s,i=%d,gsm7length=%d,startPos=%d",__FUNCTION__,i,gsm7Length,startPos);
					}
					else if(i>0)
					{
						startPos=curSplitInfo.pos[i-1]+1;
						gsm7Length=curSplitInfo.pos[i]-startPos+1;
						Duster_module_Printf(3,"%s,i=%d,gsm7length=%d,startPos=%d",__FUNCTION__,i,gsm7Length,startPos);
					}

					memset(singleSMSdataBuf,	'\0',	480);
					memcpy(singleSMSdataBuf,smsData+startPos,gsm7Length);

					if(!gsm7codedbuff)
						gsm7codedbuff = sms_malloc(165);
					DUSTER_ASSERT(gsm7codedbuff);
					memset(gsm7codedbuff,	'\0',	165);
					septet_num=libEncodeGsm7BitData((UINT8 *)singleSMSdataBuf,gsm7Length,gsm7codedbuff,6);


					if(septet_num*7%8)
						smsdata_octs=septet_num*7/8+1;
					else
						smsdata_octs=septet_num*7/8;

					septet_num+=7;

					if(septet_num<16)
					{
						dataLength[0]='0';
						itoa(septet_num,dataLength+1,16);
					}
					else
						itoa(septet_num,dataLength,16);

					memset(pduDataChar,	'\0',	320);

					CHAR_TO_2CHAR((UINT8 *)gsm7codedbuff,smsdata_octs,(UINT8 *)pduDataChar);
					sms_free(gsm7codedbuff);
					gsm7codedbuff = NULL;

					if(sequenceNum<16)
					{
						Sequence_Char[0]='0';
						itoa(sequenceNum,Sequence_Char+1,16);
					}
					else
						itoa(sequenceNum,Sequence_Char,16);

					beforeTPDULen=strlen(TPDU_Buff);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),dataLength,2);//udl
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Udhl_Char,2);//udhl
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Identifier_Char,2);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),"03",2);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Ref_Char,2);//referenceNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),totalSeg_Char,2);//Total SegmentNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Sequence_Char,2);//sequenceNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),pduDataChar,strlen(pduDataChar));

					Duster_module_Printf(3,"%s TPDU_Buff is %s",__FUNCTION__,TPDU_Buff);

					memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
					if(!sendSMSMsg->ok_fmt)
						sendSMSMsg->ok_fmt=sms_malloc(10);
					memset(sendSMSMsg->ok_fmt,	'\0',	10);
					sendSMSMsg->ok_flag=1;
					*(TPDU_Buff+strlen(TPDU_Buff))=0x1a;
					/*added by shoujunl for SMS reSend 131023*/
					if(action == 1)
					{
						retrySendTime = SMS_RETRY_SEND_TIME;
					}
					else
					{
						retrySendTime = 1;
					}
					Duster_module_Printf(1,	"%s smscpart_len is %d, strlen(TPDU_Buff) is %d",	__FUNCTION__, smscpart_len, strlen(TPDU_Buff));
					while(retrySendTime)
					{
						smsNeedReSend = FALSE;
						if(action == 1)
						{
							sendLen=(strlen(TPDU_Buff)-1-smscpart_len)/2;
							sprintf(sendSMSMsg->atcmd,"AT+CMGS=%d\r%s",sendLen,TPDU_Buff);
							sprintf(sendSMSMsg->ok_fmt,"+CMGS");
							sendSMSMsg->callback_func=&CMGS_SET_CALLBACK;
						}
						else if((action == 2)||(action == 3))
						{
							sendLen=(strlen(TPDU_Buff)-1-smscpart_len)/2;
							Duster_module_Printf(1,"%s,sendLen=%d",__FUNCTION__,sendLen);
							sprintf(sendSMSMsg->atcmd,"AT+CMGW=%d,%d\r%s",sendLen,sms_status,TPDU_Buff);
							sprintf(sendSMSMsg->ok_fmt,"+CMGW");
							sendSMSMsg->callback_func=&CMGW_SET_CALLBACK;
						}

						osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
						DUSTER_ASSERT(osa_status==OS_SUCCESS);

						flag_value=0;
						if(action==1)
						{
							osa_status = OSAFlagWait(SMS_FlgRef, CMGS_ERROR|CMGS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
							//DUSTER_ASSERT(osa_status==OS_SUCCESS);
							if(flag_value == CMGS_ERROR)
							{
								Duster_module_Printf(3,"%s,CMGS return error",__FUNCTION__);
								ret = -1;
								smsNeedReSend = msgGetFailCause();
							}
							else if(flag_value == CMGS_SET_CMD)
							{
								if(ret >= 0)
								{
									ret = 1;
								}
							}
						}
						else if((action==2)||(action==3))
						{
							osa_status = OSAFlagWait(SMS_FlgRef, CMGW_ERROR|CMGW_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
							//DUSTER_ASSERT(osa_status==OS_SUCCESS);
							if(flag_value == CMGW_ERROR)
							{
								Duster_module_Printf(3,"%s,CMGW return error",__FUNCTION__);
								ret = -1;
								goto EXIT;
							}
							else if(flag_value == CMGW_SET_CMD)
							{
								if(ret >= 0)
								{
									ret  = 1;
								}
							}
							break;
						}
						if(!smsNeedReSend)
						{
							break;
						}
						else if(smsNeedReSend && (retrySendTime != 0))
						{
							retrySendTime-- ;
							OSATaskSleep(SMS_RETRY_WAIT_TIME);
						}
					}
					if(action == 1)
					{
						if(totalLocalSentNum < MaxSentSMSNum)
						{
							for(j=1; j<MaxSentSMSNum+1; j++)
							{
								if(SentSMSIndex[j]==0)
								{
									psmIndex=j;
									SentSMSIndex[j]=1;
									break;
								}
							}
						}
						else if(totalLocalSentNum == MaxSentSMSNum)
						{
							ghandle = handle_SMS1;
							SMS_delete_last_Node(list_start,list_last,&psmIndex,&ghandle, 2);
						}
					}
					tempData=(s_MrvFormvars *)sms_malloc(sizeof(s_MrvFormvars));
					memset(tempData,	'\0',	sizeof(s_MrvFormvars));
					tempData->value=sms_malloc(sizeof(SMS_Node));
					tempSMS=(SMS_Node *)tempData->value;
					memset(tempSMS,	'\0',	sizeof(SMS_Node));
					tempSMS->isLongSMS=TRUE;
					if(action==1)
					{
						tempSMS->psm_location=psmIndex;
						tempSMS->location = 1;
						if(flag_value == CMGS_ERROR)
							tempSMS->sms_status = SEND_FAIL;
						else
							tempSMS->sms_status = SEND_SUCCESS;
						Duster_module_Printf(1,	"%s flag_value: %d, tempSMS->sms_status:%d",	__FUNCTION__,flag_value,	tempSMS->sms_status);
					}
					else if((action==2)||(action==3))
					{
						tempSMS->psm_location=writeSMSIndex;
						tempSMS->location = 0;
						writeSMSIndex = 0;

						if(sms_status==0)
							tempSMS->sms_status = UNREAD;
						else if(sms_status==1)
							tempSMS->sms_status = READED;
						else if(sms_status ==2)
							tempSMS->sms_status = UNSENT;
						else if(sms_status ==3)
							tempSMS->sms_status = SEND_SUCCESS;
					}
					tempSMS->srcNum = sms_malloc(strlen(number)+1);
					DUSTER_ASSERT(tempSMS->srcNum);
					memset(tempSMS->srcNum,	'\0',	strlen(number)+1);
					memcpy(tempSMS->srcNum,	number,	strlen(number));
					tempSMS->LSMSinfo = sms_malloc(sizeof(LSMS_INFO));
					DUSTER_ASSERT(tempSMS->LSMSinfo);
					memset(tempSMS->LSMSinfo,	'\0',	sizeof(LSMS_INFO));
					tempSMS->LSMSinfo->referenceNum	=	referenceNum;
					tempSMS->LSMSinfo->totalSegment	=	total_SegmentNum;
					tempSMS->LSMSinfo->segmentNum	=	sequenceNum;
					tempSMS->savetime = sms_malloc(sizeof(TIMESTAMP));
					DUSTER_ASSERT(tempSMS->savetime);
					memset(tempSMS->savetime,	'\0',	sizeof(TIMESTAMP));
					transTimeToSTAMP(timebuf,tempSMS->savetime);

					if(action==1)
					{
						SMS_insert_Node(list_start,list_last,&tempData,1);

						if(!savebuf)
							savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
						DUSTER_ASSERT(savebuf);
						memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
						str_replaceChar(singleSMSdataBuf,';',SEMI_REPLACE);
						str_replaceChar(singleSMSdataBuf,'=',EQUAL_REPLACE);
						sprintf(savebuf,"%s%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c","LSNT",tempSMS->psm_location,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSdataBuf,DELIMTER,timebuf,DELIMTER,tempSMS->sms_status,
						        DELIMTER,1,DELIMTER,LONG_SMS,DELIMTER,tempSMS->LSMSinfo->referenceNum,DELIMTER,tempSMS->LSMSinfo->segmentNum,DELIMTER,
						        tempSMS->LSMSinfo->totalSegment,DELIMTER);
						
						#ifdef NOTION_ENABLE_JSON
						{
						       char tmp_message_time_index_str[20] = {0};
							message_time_index++;   
							sprintf(tmp_message_time_index_str,"%d",message_time_index);    
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,"sms_message_time_index",tmp_message_time_index_str,handle_SMS1);	                   
							memset(tmp_message_time_index_str,0,20);
                                          	sprintf(tmp_message_time_index_str,"%d%c",message_time_index,DELIMTER);    										
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
                                          	strcat(savebuf,tmp_message_time_index_str);
						}				  
						#endif				  	
						memset(indexbuf,	'\0',	15);
						sprintf(indexbuf,"%s%d","LSNT",psmIndex);
						psmfile_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf,handle_SMS1);
						totalLocalSentNum++;
						sms_free(savebuf);
						savebuf = NULL;
					}
					else if((action==2)||(action==3))
					{

						Duster_module_Printf(3,"%s,list_start location is %x",__FUNCTION__,list_start);
						SMS_insert_Node(list_start,list_last,&tempData,1);
						if(!savebuf)
							savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
						DUSTER_ASSERT(savebuf);
						memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
						str_replaceChar(singleSMSdataBuf,';',SEMI_REPLACE);
						str_replaceChar(singleSMSdataBuf,'=',EQUAL_REPLACE);
						sprintf(savebuf,"%s%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c","SRCV",tempSMS->psm_location,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSdataBuf,DELIMTER,timebuf,DELIMTER,tempSMS->sms_status,
						        DELIMTER,1,DELIMTER,LONG_SMS,DELIMTER,tempSMS->LSMSinfo->referenceNum,DELIMTER,tempSMS->LSMSinfo->segmentNum,DELIMTER,
						        tempSMS->LSMSinfo->totalSegment,DELIMTER);

						memset(indexbuf,	'\0',	15);
						sprintf(indexbuf,"%s%d","SRCV",psmIndex);
						psm_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf);
						totalSimSentSmsNum++;
						totalSimSmsNum++;
						sms_free(savebuf);
						savebuf = NULL;
					}

					memset(TPDU_Buff+beforeTPDULen,	'\0',	(strlen(TPDU_Buff)-beforeTPDULen));
					sequenceNum++;
					i++;
				}
				goto EXIT;
			}
		}
		else if(codeScheme==1)//UCS2
		{
			ucs2CharLen=strlen(content);
			usc2Length=ucs2CharLen>>1;
			Duster_module_Printf(3,"%s,ucs2CharLen=%d,usc2Length=%d",__FUNCTION__,ucs2CharLen,usc2Length);
			Duster_module_Printf(1,"%s 1 pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
			if(usc2Length<=140)
			{

				//convert byte order
				Duster_module_Printf(3,"%s send one ucs2 coded message",__FUNCTION__);

				memset(pduDataChar, '\0',	320);
				memcpy(pduDataChar,content,strlen(content));
				if(usc2Length<16)
				{
					dataLength[0]='0';
					itoa(usc2Length,dataLength+1,16);
				}
				else
					itoa(usc2Length,dataLength,16);
				singleSMSdataBuf = unicodechar_to_utf8_bigdean(content);
			}
			else if(usc2Length>140) //cont
			{
				first_oct=first_oct|0x40;

				memset(first_oct_char,	'\0',	3);

				//reset first_oct_char
				if(first_oct<16)
				{
					first_oct_char[0]='0';
					itoa(first_oct,first_oct_char+1,16);
				}
				else
					itoa(first_oct,first_oct_char,16);

				memcpy(TPDU_Buff+4+strlen(invert_Smsc),first_oct_char,2);

				total_SegmentNum=usc2Length/134;
				if(usc2Length%134!=0)
					total_SegmentNum+=1;
				Duster_module_Printf(3,"%s total_SegmentNum is %d,",__FUNCTION__,total_SegmentNum);


				if(total_SegmentNum<16)
				{
					totalSeg_Char[0]='0';
					itoa(total_SegmentNum,totalSeg_Char+1,16);
				}
				else
					itoa(total_SegmentNum,totalSeg_Char,16);

				if(referenceNum<16)
				{
					Ref_Char[0]='0';
					itoa(referenceNum,Ref_Char+1,16);
				}
				else
					itoa(referenceNum,Ref_Char,16);

				Duster_module_Printf(3,"%s,cur referenceNum is %d,Ref_Char is %s",__FUNCTION__,referenceNum,Ref_Char);

				if(referenceNum==255)
					referenceNum=1;
				else
					referenceNum++;

				memset(refNumBuf,	'\0',	5);
				sprintf(refNumBuf,"%d",referenceNum+1);
				psm_set_wrapper(PSM_MOD_SMS,NULL,"referenceNum",refNumBuf);

				sequenceNum=1;

				if(action ==1)
				{
					//mkreplaceAuto(total_SegmentNum,list_start,list_last);
				}

				while(sequenceNum<=total_SegmentNum)
				{
					Duster_module_Printf(3,"%s,sequenceNum=%d,total_SegmentNum=%d",__FUNCTION__,sequenceNum,total_SegmentNum);
					Duster_module_Printf(1,"%s 2 pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);

					if(sequenceNum==1)
						startPos=0;
					else if(sequenceNum>1)
						startPos=(sequenceNum-1)*134*2;

					if(sequenceNum<total_SegmentNum)
						ucsOct_Num=134*2;
					else if(sequenceNum==total_SegmentNum)
						ucsOct_Num=strlen(content)-(sequenceNum-1)*134*2;

					Duster_module_Printf(3,"%s, startPos is %d,ucsOct_Num is %d",__FUNCTION__,startPos,ucsOct_Num);
					memset(singleSMSdataBuf,	'\0',	300);
					memcpy(singleSMSdataBuf,content+startPos,ucsOct_Num);

					memset(Sequence_Char,	'\0',	3);

					if(sequenceNum<16)
					{
						Sequence_Char[0]='0';
						itoa(sequenceNum,Sequence_Char+1,16);
					}
					else
						itoa(sequenceNum,Sequence_Char,16);

					Duster_module_Printf(3,"%s,Sequence_Char is %s",__FUNCTION__,Sequence_Char);

					ucsOct_Num=ucsOct_Num>>1;


					ucsOct_Num+=6;

					if(ucsOct_Num<16)
					{
						dataLength[0]='0';
						itoa(ucsOct_Num,dataLength+1,16);
					}
					else
						itoa(ucsOct_Num,dataLength,16);

					beforeTPDULen=strlen(TPDU_Buff);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),dataLength,2);//udl
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Udhl_Char,2);//udhl
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Identifier_Char,2);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),"03",2);
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Ref_Char,2);//referenceNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),totalSeg_Char,2);//Total SegmentNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),Sequence_Char,2);//sequenceNum
					memcpy(TPDU_Buff+strlen(TPDU_Buff),singleSMSdataBuf,strlen(singleSMSdataBuf));
					Duster_module_Printf(3,"%s TPDU_Buff is %s",__FUNCTION__,TPDU_Buff);

					memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
					if(!sendSMSMsg->ok_fmt)
						sendSMSMsg->ok_fmt=sms_malloc(10);
					memset(sendSMSMsg->ok_fmt,	'\0',	10);
					sendSMSMsg->ok_flag=1;
					*(TPDU_Buff+strlen(TPDU_Buff))=0x1a;

					/*added by shoujunl for SMS reSend 131023*/
					if(action == 1)
					{
						retrySendTime = SMS_RETRY_SEND_TIME;
					}
					else
					{
						retrySendTime = 1;
					}
					while(retrySendTime)
					{
						smsNeedReSend = FALSE;
						if(action==1)
						{
							sendLen=(strlen(TPDU_Buff)-1-smscpart_len)/2;
							sprintf(sendSMSMsg->atcmd,"AT+CMGS=%d\r%s",sendLen,TPDU_Buff);
							sprintf(sendSMSMsg->ok_fmt,"+CMGS");
							sendSMSMsg->callback_func=&CMGS_SET_CALLBACK;
						}
						else if((action==2)||(action==3))
						{
							sendLen=(strlen(TPDU_Buff)-1-smscpart_len)/2;
							Duster_module_Printf(1,"%s smscpart_len is %d,strlen(TPDU_Buff) is %s",__FUNCTION__,smscpart_len,strlen(TPDU_Buff));
							Duster_module_Printf(1,"%s,sendLen is %d",__FUNCTION__,sendLen);
							sprintf(sendSMSMsg->atcmd,"AT+CMGW=%d,%d\r%s",sendLen,sms_status,TPDU_Buff);
							sprintf(sendSMSMsg->ok_fmt,"+CMGW");
							sendSMSMsg->callback_func=&CMGW_SET_CALLBACK;

							Duster_module_Printf(1,"%s 3 pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
						}

						osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
						DUSTER_ASSERT(osa_status==OS_SUCCESS);

						flag_value=0;
						if(action==1)
						{
							osa_status = OSAFlagWait(SMS_FlgRef, CMGS_ERROR|CMGS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
							//DUSTER_ASSERT(osa_status==OS_SUCCESS);
							if(flag_value == CMGS_ERROR)
							{
								Duster_module_Printf(3,"%s,CMGS return error",__FUNCTION__);
								ret = -1;
								smsNeedReSend = msgGetFailCause();
							}
							else if(flag_value == CMGS_SET_CMD)
							{
								if(ret >= 0)
								{
									ret =1;
								}
							}
						}
						else if((action==2)||(action==3))
						{
							osa_status = OSAFlagWait(SMS_FlgRef, CMGW_ERROR|CMGW_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
							//DUSTER_ASSERT(osa_status==OS_SUCCESS);
							if(flag_value == CMGW_ERROR)
							{
								Duster_module_Printf(3,"%s,CMGW return error",__FUNCTION__);
								ret = -1;
								goto EXIT;
							}
							else if(flag_value == CMGW_SET_CMD)
							{
								if(ret >= 0)
								{
									ret = 1;
								}
							}
							break;
						}

						if(!smsNeedReSend)
						{
							break;
						}
						else if(smsNeedReSend && (retrySendTime > 0))
						{
							retrySendTime-- ;
							OSATaskSleep(SMS_RETRY_WAIT_TIME);
						}
					}
					memset(TPDU_Buff+beforeTPDULen,	'\0',	(strlen(TPDU_Buff)-beforeTPDULen));
					singleSMSUtf8 = unicodechar_to_utf8_bigdean(singleSMSdataBuf);
					tempData=(s_MrvFormvars *)sms_malloc(sizeof(s_MrvFormvars));
					memset(tempData,	'\0',	sizeof(s_MrvFormvars));
					tempData->value=sms_malloc(sizeof(SMS_Node));

					tempSMS=(SMS_Node *)tempData->value;
					memset(tempSMS,	'\0',	sizeof(SMS_Node));

					tempSMS->isLongSMS=TRUE;
					tempSMS->LSMSinfo = sms_malloc(sizeof(LSMS_INFO));
					DUSTER_ASSERT(tempSMS->LSMSinfo);
					memset(tempSMS->LSMSinfo,	'\0',	sizeof(LSMS_INFO));

					tempSMS->LSMSinfo->referenceNum=referenceNum;
					tempSMS->LSMSinfo->segmentNum=sequenceNum;
					tempSMS->LSMSinfo->totalSegment=total_SegmentNum;

					tempSMS->srcNum = sms_malloc(strlen(number)+1);
					memset(tempSMS->srcNum,	'\0',	strlen(number)+1);
					memcpy(tempSMS->srcNum,	number,	strlen(number));
					tempSMS->savetime = sms_malloc(sizeof(TIMESTAMP));
					DUSTER_ASSERT(tempSMS->savetime);
					memset(tempSMS->savetime,	'\0',	sizeof(TIMESTAMP));
					transTimeToSTAMP(timebuf,tempSMS->savetime);

					Duster_module_Printf(1,"%s 5 pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
					if(action==1)
					{
						if(totalLocalSentNum < MaxSentSMSNum)
						{
							for(j=1; j<MaxSentSMSNum+1; j++)
							{
								if(SentSMSIndex[j]==0)
								{
									psmIndex=j;
									SentSMSIndex[j]=1;
									break;
								}
							}
						}
						else if(totalLocalSentNum >= MaxSentSMSNum)
						{
							ghandle = handle_SMS1;
							SMS_delete_last_Node(list_start,list_last,&psmIndex,&ghandle, 2);
						}

						tempSMS->psm_location=psmIndex;
						tempSMS->location = 1;
						if(flag_value == CMGS_ERROR)
							tempSMS->sms_status = SEND_FAIL;
						else
							tempSMS->sms_status = SEND_SUCCESS;
						Duster_module_Printf(1,	"%s flag_value: %d, tempSMS->sms_status:%d",	flag_value,	tempSMS->sms_status);
						if(!savebuf)
							savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
						DUSTER_ASSERT(savebuf);
						memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
						str_replaceChar(singleSMSUtf8,';',SEMI_REPLACE);
						str_replaceChar(singleSMSUtf8,'=',EQUAL_REPLACE);
						sprintf(savebuf,"%s%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c","LSNT",tempSMS->psm_location,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSUtf8,DELIMTER,timebuf,DELIMTER,
						        tempSMS->sms_status,DELIMTER,1,DELIMTER,LONG_SMS,DELIMTER,tempSMS->LSMSinfo->referenceNum,DELIMTER,
						        tempSMS->LSMSinfo->segmentNum,DELIMTER,tempSMS->LSMSinfo->totalSegment,DELIMTER);
						#ifdef NOTION_ENABLE_JSON
						{
						       char tmp_message_time_index_str[20] = {0};
							message_time_index++;   
							sprintf(tmp_message_time_index_str,"%d",message_time_index);    
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,"sms_message_time_index",tmp_message_time_index_str,handle_SMS1);	                   
							memset(tmp_message_time_index_str,0,20);
                                          	sprintf(tmp_message_time_index_str,"%d%c",message_time_index,DELIMTER);    										
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
                                          	strcat(savebuf,tmp_message_time_index_str);
						}				  
						#endif	
						memset(indexbuf,	'\0',	15);
						sprintf(indexbuf,"%s%d","LSNT",psmIndex);
						psmfile_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf,handle_SMS1);

						SMS_insert_Node(list_start,list_last,&tempData,1);
						totalLocalSentNum++;
						sms_free(savebuf);
						savebuf = NULL;

					}
					else if((action==2)||(action==3))
					{
						tempSMS->location = 0;
						tempSMS->psm_location	=	writeSMSIndex;
						writeSMSIndex= 0;

						if(sms_status==0)
							tempSMS->sms_status = UNREAD;
						else if(sms_status==1)
							tempSMS->sms_status = READED;
						else if(sms_status ==2)
							tempSMS->sms_status = UNSENT;
						else if(sms_status ==3)
							tempSMS->sms_status = SEND_SUCCESS;

						if(!savebuf)
							savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
						DUSTER_ASSERT(savebuf);
						memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
						str_replaceChar(singleSMSUtf8,';',SEMI_REPLACE);
						str_replaceChar(singleSMSUtf8,'=',EQUAL_REPLACE);
						sprintf(savebuf,"%s%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c","SRCV",tempSMS->psm_location,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSUtf8,DELIMTER,timebuf,DELIMTER,
						        tempSMS->sms_status,DELIMTER,1,DELIMTER,LONG_SMS,DELIMTER,tempSMS->LSMSinfo->referenceNum,DELIMTER,
						        tempSMS->LSMSinfo->segmentNum,DELIMTER,tempSMS->LSMSinfo->totalSegment,DELIMTER);
						memset(indexbuf,	'\0',	15);
						sprintf(indexbuf,"%s%d","SRCV",tempSMS->psm_location);
						psm_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf);

						Duster_module_Printf(3,"%s,list_start location is %x",__FUNCTION__,list_start);
						SMS_insert_Node(list_start,list_last,&tempData,1);
						totalSimSentSmsNum++;
						totalSimSmsNum++;
						sms_free(savebuf);
						savebuf = NULL;

						Duster_module_Printf(1,"%s 6 pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
					}
					if(singleSMSUtf8)
					{
						sms_free(singleSMSUtf8);
						singleSMSUtf8 = NULL;
					}
					sequenceNum++;
				}
				Duster_module_Printf(1,"leave %s",__FUNCTION__);
				goto EXIT;
			}
		}
		//form TPDU -single SMS

		if(action ==1)
		{
			//mkreplaceAuto(1,list_start,list_last);
		}

		memcpy(TPDU_Buff+strlen(TPDU_Buff),dataLength,2);//DATA LENGTH
		memcpy(TPDU_Buff+strlen(TPDU_Buff),pduDataChar,strlen(pduDataChar));
		Duster_module_Printf(3,"%s TPDU_Buff is %s",__FUNCTION__,TPDU_Buff);


		memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
		if(!sendSMSMsg->ok_fmt)
			sendSMSMsg->ok_fmt=sms_malloc(10);
		memset(sendSMSMsg->ok_fmt,	'\0',	10);
		*(TPDU_Buff+strlen(TPDU_Buff))=0x1a;
		sendSMSMsg->ok_flag=1;

		if(action == 1)
		{
			retrySendTime = SMS_RETRY_SEND_TIME;
		}
		else
		{
			retrySendTime = 1;
		}

		while(retrySendTime)
		{
			smsNeedReSend = FALSE;
			if(action == 1)
			{
				sprintf(sendSMSMsg->atcmd,"AT+CMGS=%d\r%s",(strlen(TPDU_Buff)-1-smscpart_len)/2,TPDU_Buff);
				sprintf(sendSMSMsg->ok_fmt,"+CMGS");
				sendSMSMsg->callback_func=&CMGS_SET_CALLBACK;
			}
			else if((action == 2)||(action == 3))
			{
				sprintf(sendSMSMsg->atcmd,"AT+CMGW=%d,%d\r%s",(strlen(TPDU_Buff)-1-smscpart_len)/2,sms_status,TPDU_Buff);
				sprintf(sendSMSMsg->ok_fmt,"+CMGW");
				sendSMSMsg->callback_func=&CMGW_SET_CALLBACK;
			}

			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);


			flag_value=0;
			if(action == 1)
			{
				osa_status = OSAFlagWait(SMS_FlgRef, CMGS_ERROR|CMGS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
				//DUSTER_ASSERT(osa_status==OS_SUCCESS);
				if(flag_value == CMGS_ERROR)
				{
					Duster_module_Printf(3,"%s,CMGS return error",__FUNCTION__);
					ret = -1;
					smsNeedReSend = msgGetFailCause();
				}
				else if(flag_value ==CMGS_SET_CMD)
				{
					ret = 1;
				}
			}
			else if((action==2)||(action==3))
			{
				osa_status = OSAFlagWait(SMS_FlgRef, CMGW_ERROR|CMGW_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
				//DUSTER_ASSERT(osa_status==OS_SUCCESS);
				if(flag_value == CMGW_ERROR)
				{
					Duster_module_Printf(1,"%s,CMGW return error",__FUNCTION__);
					ret = -1;
					goto EXIT;
				}
				else if(flag_value == CMGW_SET_CMD)
				{
					ret = 1;
				}
				break;
			}

			if(!smsNeedReSend)
			{
				break;
			}
			else if(smsNeedReSend && (retrySendTime > 0))
			{
				retrySendTime-- ;
				OSATaskSleep(SMS_RETRY_WAIT_TIME);
			}
		}
		tempData= sms_malloc(sizeof(s_MrvFormvars));
		memset(tempData,	'\0',	sizeof(s_MrvFormvars));
		tempData->value=sms_malloc(sizeof(SMS_Node));
		tempSMS=(SMS_Node*)tempData->value;
		memset(tempSMS,	'\0',	sizeof(SMS_Node));
		tempSMS->srcNum = NULL;
		tempSMS->LSMSinfo = NULL;
		tempSMS->savetime = sms_malloc(sizeof(TIMESTAMP));
		DUSTER_ASSERT(tempSMS->savetime);
		memset(tempSMS->savetime,	'\0',	sizeof(TIMESTAMP));

		if(action == 1)
		{
			if(totalLocalSentNum < MaxSentSMSNum)
			{
				for(j=1; j<MaxSentSMSNum+1; j++)
				{
					if(SentSMSIndex[j]==0)
					{
						psmIndex=j;
						SentSMSIndex[j]=1;
						break;
					}
				}
			}
			else if(totalLocalSentNum >= MaxSentSMSNum)
			{
				ghandle = handle_SMS1;
				SMS_delete_last_Node(list_start,list_last,&psmIndex,&ghandle, 2);
			}
			tempSMS->location = 1;
			if(ret > 0)
			{
				tempSMS->sms_status = SEND_SUCCESS;
			}
			else if(ret < 0)
			{
				tempSMS->sms_status = SEND_FAIL;
			}
			tempSMS->isLongSMS = FALSE;
			tempSMS->psm_location=psmIndex;
			transTimeToSTAMP(timebuf,tempSMS->savetime);

			if(!savebuf)
				savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
			DUSTER_ASSERT(savebuf);
			memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
			str_replaceChar(singleSMSdataBuf,';',SEMI_REPLACE);
			str_replaceChar(singleSMSdataBuf,'=',EQUAL_REPLACE);
			sprintf(savebuf,"LSNT%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c",psmIndex,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSdataBuf,DELIMTER,
			        timebuf,DELIMTER,tempSMS->sms_status,DELIMTER,1,DELIMTER,NOT_LONG,DELIMTER,0,DELIMTER,0,DELIMTER,0,DELIMTER);
						#ifdef NOTION_ENABLE_JSON
						{
						       char tmp_message_time_index_str[20] = {0};
							message_time_index++;   
							sprintf(tmp_message_time_index_str,"%d",message_time_index);    
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,"sms_message_time_index",tmp_message_time_index_str,handle_SMS1);	                   
							memset(tmp_message_time_index_str,0,20);
                                          	sprintf(tmp_message_time_index_str,"%d%c",message_time_index,DELIMTER);    										
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
                                          	strcat(savebuf,tmp_message_time_index_str);
						}				  
						#endif				
			memset(indexbuf,	'\0',	15);
			sprintf(indexbuf,"%s%d","LSNT",psmIndex);
			psmfile_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf,handle_SMS1);

			totalLocalSentNum++;
		}
		else if(action == 2 || action == 3)
		{
			tempSMS->location = 0;
			tempSMS->psm_location=writeSMSIndex;
			writeSMSIndex=0;
			tempSMS->isLongSMS = FALSE;
			if(sms_status==0)
				tempSMS->sms_status = UNREAD;
			else if(sms_status==1)
				tempSMS->sms_status = READED;
			else if(sms_status ==2)
				tempSMS->sms_status = UNSENT;
			else if(sms_status ==3)
				tempSMS->sms_status = SEND_SUCCESS;

			transTimeToSTAMP(timebuf,tempSMS->savetime);

			if(!savebuf)
				savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
			DUSTER_ASSERT(savebuf);
			memset(savebuf,	'\0',	TEL_AT_SMS_MAX_LEN);
			str_replaceChar(singleSMSdataBuf,';',SEMI_REPLACE);
			str_replaceChar(singleSMSdataBuf,'=',EQUAL_REPLACE);
			sprintf(savebuf,"SRCV%d%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c",tempSMS->psm_location,DELIMTER,number,DELIMTER,codeScheme,DELIMTER,singleSMSdataBuf,DELIMTER,
			        timebuf,DELIMTER,tempSMS->sms_status,DELIMTER,1,DELIMTER,NOT_LONG,DELIMTER,0,DELIMTER,0,DELIMTER,0,DELIMTER);
			memset(indexbuf,	'\0',	15);
			sprintf(indexbuf,"%s%d","SRCV",tempSMS->psm_location);
			Duster_module_Printf(3,"%s,indexbuf is %s",__FUNCTION__,indexbuf);
			psm_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf);

			if(action==2)
			{
				totalSimSentSmsNum++;
			}
			else if(action==3)
			{
				totalSimRcvSmsNum++;
			}
			totalSimSmsNum++;
			totalSmsNum++;
		}
		if(savebuf)
		{
			sms_free(savebuf);
		}
		savebuf = NULL;
		SMS_insert_Node(list_start,list_last,&tempData,1);
	}

EXIT:

	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	memset(indexbuf,	'\0',	15);
	sprintf(indexbuf,"%d",totalLocalSentNum);
	Duster_module_Printf(1,"%s indexbuf is %s",__FUNCTION__,indexbuf);
	psm_set_wrapper(PSM_MOD_SMS,NULL,"totalLocalSentNum",indexbuf);

	totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		if(sendSMSMsg->ok_fmt != NULL)
			sms_free(sendSMSMsg->ok_fmt);
		sms_free(sendSMSMsg);
	}
	Duster_module_Printf(1,"%s 1 before sms_free",__FUNCTION__);
	if(smsData  != NULL)
		sms_free(smsData);

	Duster_module_Printf(1,"%s pduDataChar malloc at 0x%x",__FUNCTION__,pduDataChar);
	Duster_module_Printf(1,"%s 3 before sms_free",__FUNCTION__);
	if(pduDataChar != NULL)
		sms_free(pduDataChar);

	Duster_module_Printf(1,"%s 4 before sms_free",__FUNCTION__);
	if(singleSMSdataBuf  != NULL)
		sms_free(singleSMSdataBuf);

	Duster_module_Printf(1,"leave %s, ret is %d",__FUNCTION__,	ret);

	return ret;

}
int SMS_massSend(char *number_str,char *content,char *encode_type,char *sendTime,s_MrvFormvars **list_start,s_MrvFormvars **list_last,UINT8 action,UINT8 sms_status)
{
	char *pS=NULL,*pE=NULL;
	char number[50];
	int ret1=0,ret2=0;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	pS=number_str;
	pE=strchr(pS,',');
	while(pE)
	{
		if(((pE-pS)<2)||((pE-pS)>50))
		{
			pS=pE+1;
			pE=strchr(pS,',');
			continue;
		}
		memset(number,	'\0',	50);
		memcpy(number,pS,pE-pS);
		Duster_module_Printf(1,"%s,number is %s",__FUNCTION__,number);
		ret1 = SMS_Send(number,content,encode_type,sendTime,list_start,list_last,action,sms_status);
		ret2=ret1<0?ret1:ret2;

		pS=pE+1;
		pE=strchr(pS,',');
	}
	return ret2;

}
/************************************************************************
  function description:  check if sent sms list is full,if full,delete oldest items
              params: totalNum--new total sent messages items' number
************************************************************************/
UINT8 mkreplaceAuto(UINT8 totalNum,s_MrvFormvars** start,s_MrvFormvars** last)
{
	s_MrvFormvars *tmpNode=NULL,*lastStartNode=NULL;
	SMS_Node *tmpSMS=NULL;
	UINT8 delNum = 0;
	delNum = totalNum;
	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	if((totalLocalSentNum+totalNum)<=MaxSentSMSNum)
		goto EXIT;
	else if((totalLocalSentNum+totalNum)>MaxSentSMSNum)
	{
		while(delNum)
		{
			tmpNode=*start;
			while(tmpNode->next)
			{
				tmpSMS= (SMS_Node*)tmpNode->value;
				if((tmpSMS->isLongSMS)&&(tmpSMS->LSMSinfo->LSMSflag==START))
					lastStartNode=tmpNode;
				tmpNode=tmpNode->next;
			}
			tmpSMS=(SMS_Node*)tmpNode->value;
			if(tmpSMS->isLongSMS&&(tmpSMS->LSMSinfo->LSMSflag!=ONE_ONLY))
			{
				while(lastStartNode)
				{
					tmpSMS=(SMS_Node*)lastStartNode->value;
					SMS_delete_By_index(tmpSMS->psm_location,start,last,2);
					Duster_module_Printf(1,"%s delete long sms,index is %d",__FUNCTION__,tmpSMS->psm_location);
					delNum--;
					lastStartNode=lastStartNode->next;
				}
			}
			else if(!tmpSMS->isLongSMS||(tmpSMS->isLongSMS&&(tmpSMS->LSMSinfo->LSMSflag==ONE_ONLY)))
			{
				SMS_delete_By_index(tmpSMS->psm_location,start,last,2);
				Duster_module_Printf(1,"%s delete  sms,index is %d",__FUNCTION__,tmpSMS->psm_location);
				delNum--;
			}

		}
	}
EXIT:
	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return 1;
}

/*
  Input:sms_time:12;01;02;12;30;45;+8
  output:21102021035480
*/
UINT8 decodeTimeFormat(char *sms_time,char *out_time)
{

	char *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL,*p6=NULL;
	UINT8 ret =0;

	Duster_module_Printf(1,	"%s sms_time is %s",	__FUNCTION__,	sms_time);

	str_replaceChar(sms_time,',',';');
	p1=strchr(sms_time,';');
	p2=strchr(p1+1,';');
	p3=strchr(p2+1,';');
	p4=strchr(p3+1,';');
	p5=strchr(p4+1,';');
	p6=strchr(p5+1,';');

	if(p1&&p2&&p3&&p4&&p5&&p6)
	{
		*out_time=*(sms_time+1);
		*(out_time+1)=*sms_time;
		*(out_time+2)=*(p1+2);
		*(out_time+3)=*(p1+1);
		*(out_time+4)=*(p2+2);
		*(out_time+5)=*(p2+1);
		*(out_time+6)=*(p3+2);
		*(out_time+7)=*(p3+1);
		*(out_time+8)=*(p4+2);
		*(out_time+9)=*(p4+1);
		*(out_time+10)=*(p5+2);
		*(out_time+11)=*(p5+1);
		*(out_time+12)=*(p6+2);
		*(out_time+13)='0';
		ret = 1;
	}
	else
	{
		Duster_module_Printf(1,"%s input time format error",__FUNCTION__);
		ret = 0;
	}



	Duster_module_Printf(1,"%s out_time is %s",__FUNCTION__,out_time);
	return ret;


}
UINT8 transTimeToSTAMP(char *sms_time,TIMESTAMP *out_time)
{

	char *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL,*p6=NULL;
	char tmpbuf[5]= {0};
	UINT8 ret =0;
	Duster_module_Printf(1,"enter %s,sms_time is %s",__FUNCTION__,sms_time);

	str_replaceChar(sms_time,';',',');
	p1=strchr(sms_time,',');
	p2=strchr(p1+1,',');
	p3=strchr(p2+1,',');
	p4=strchr(p3+1,',');
	p5=strchr(p4+1,',');
	p6=strchr(p5+1,',');

	if(p1&&p2&&p3&&p4&&p5&&p6)
	{
		memset(tmpbuf,	'\0',	5);
		memcpy(tmpbuf,sms_time,2);
		out_time->tsYear = ConvertStrToInteger(tmpbuf);

		memset(tmpbuf,	'\0',	5);
		memcpy(tmpbuf,p1+1,p2 - p1 - 1);
		out_time->tsMonth = ConvertStrToInteger(tmpbuf);

		memset(tmpbuf,'\0',	5);
		memcpy(tmpbuf,p2+1,p3 -p2 -1);
		out_time->tsDay = ConvertStrToInteger(tmpbuf);

		memset(tmpbuf,	'\0',	5);
		memcpy(tmpbuf,p3+1,p4 - p3 -1);
		out_time->tsHour = ConvertStrToInteger(tmpbuf);

		memset(tmpbuf,	'\0',	5);
		memcpy(tmpbuf,p4+1,p5 -p4 -1);
		out_time->tsMinute = ConvertStrToInteger(tmpbuf);

		memset(tmpbuf,	'\0',	5);
		memcpy(tmpbuf,p5+1,p6 -p4 -1);
		out_time->tsSecond = ConvertStrToInteger(tmpbuf);


		//memset(tmpbuf,0,5);
		//memcpy(tmpbuf,p6+2,1);
		out_time->tsTimezone = 8;

		ret = 1;
	}
	else
		ret =0;

	Duster_module_Printf(1,"leave %s",__FUNCTION__);

	return ret;



}
#if 0
void insert_SendNode_AfterPos(s_MrvFormvars ** start,s_MrvFormvars **last,s_MrvFormvars **newNode,UINT16 pos)
{
	UINT8 i=0;
	SMS_Node *tmpSMS1=NULL,*tmpSMS2=NULL;
	s_MrvFormvars *tmpNode,*nextNode;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);

	tmpNode=*start;

	if(pos==0)
	{
		tmpSMS1=(SMS_Node *)((*newNode)->value);
		tmpSMS1->LSMSinfo->LSMSflag=END;
		tmpSMS1=(SMS_Node *)((*start)->value);
		tmpSMS1->LSMSinfo->LSMSflag=START;
		slist_insert_firstNode(newNode,start,last);
	}
	else
	{
		tmpSMS2=(SMS_Node*)((*newNode)->value);
		tmpSMS2->LSMSinfo->LSMSflag = END;
		while(tmpNode)
		{
			tmpSMS1=(SMS_Node *)tmpNode->value;
			switch(tmpSMS1->LSMSflag)
			{
			case START:
				break;
			case ONE_ONLY:
				tmpSMS1->LSMSflag = START;
				break;
			case MIDDLE:
				break;
			case END:
				tmpSMS1->LSMSflag = MIDDLE;
				break;
			}
			if(++i==pos)
			{
				if(!tmpNode->next)
				{
					MrvSlistAdd(*newNode , start,  last);
					break;
				}
				else
				{
					nextNode=tmpNode->next;
					tmpNode->next=*newNode;
					(*newNode)->next=nextNode;
					break;
				}
			}
		}
	}

	Duster_module_Printf(1,"leave %s", __FUNCTION__);
	return;

}
#endif
/**********************************************************************************************************************
  function descprition : save sms to device
  params: number -- dst phone number
		   content -- usc2 char string
		   encode_type -- UNICODE/gsm7_default
		   saveTime -- '13,04,13,17,10,26,+8'
		   location: 0 -sim card/1--device
		   tags -- unused
		   edit_id : used when edit draft message
***********************************************************************************************************************/
int SMS_Save(char *number,char *content,char *encode_type,char *saveTime,UINT8 location,UINT8 tags,s_MrvFormvars **list_start,s_MrvFormvars **list_last,UINT8 edit_id )
{

	char  		*savebuf=NULL,*utf8Buf=NULL,*ucs2Buf=NULL,*ucs2BufVers=NULL;
	char 		 timebuf[30]= {0},*timebuf2 = NULL;
	UINT16		 i,saveIndex;
	char 		 indexbuf[6]= {0};
	s_MrvFormvars 	*tempData=NULL;
	SMS_Node    *tempSMS;
	UINT8        maxSMSNum = 5,code_scheme =3,totoal_SegNum=0;
	UINT16       charLen=0,ucs2Len=0,pos=0,	outhandle;
	char         tmpBuf[11]= {0};
	SplitInfo	 curSplitInfo;
	int  	     ret=1;

	Duster_module_Printf(1,"enter %s",__FUNCTION__);
	Duster_module_Printf(3,"number is %s",number);

	if(!number||!content||!saveTime||!encode_type)
	{
		Duster_module_Printf(1,"%s param error",__FUNCTION__);
		return -1;
	}

	charLen=strlen(content);
	if(charLen>maxSMSNum*640)
	{
		Duster_module_Printf(1,"%s,content too long",__FUNCTION__);
		return -1;
	}
	if(saveTime&&(strlen(saveTime)<30))
	{
		memcpy(timebuf,saveTime,strlen(saveTime));
		timebuf2 = MrvCgiUnescapeSpecialChars(timebuf);
	}
	else
	{
		sprintf(timebuf,"0-0-0 0:0:0");
	}
	Duster_module_Printf(1,"%s timebuf is %s",__FUNCTION__,timebuf);

	str_replaceChar(timebuf,';',',');
	ucs2Buf = sms_malloc(maxSMSNum*320+1);
	ucs2BufVers = sms_malloc(maxSMSNum*320+1);
	if(!ucs2Buf||!ucs2BufVers)
	{
		Duster_module_Printf(1,"%s: there is no enough memory",__FUNCTION__);
		if(ucs2Buf)
		{
			sms_free(ucs2Buf);
			ucs2Buf = NULL;
		}
		if(ucs2BufVers)
		{
			sms_free(ucs2BufVers);
			ucs2BufVers = NULL;
		}
		return -1;
	}
	memset(ucs2Buf, '\0', maxSMSNum*320+1);

	memset(ucs2BufVers, '\0', maxSMSNum*320+1);
	memset(indexbuf,	'\0',6);
	if(edit_id == 0)
	{
		if(totalLocalDraftNum <	MaxDraftSMSNum)
		{
			for(i=1; i<=MaxDraftSMSNum; i++)
			{
				if(DraftSMSIndex[i]==0)
				{
					DraftSMSIndex[i] = 1;
					saveIndex=i;
					i++;
					break;
				}
			}
		}
		else if(totalLocalDraftNum >=	MaxDraftSMSNum)
		{
			outhandle = handle_SMS1;
			SMS_delete_last_Node(list_start,list_last,&saveIndex,&outhandle,3);
		}
	}
	else if(edit_id > 0)
	{
		saveIndex = edit_id;
	}

	CHAR_TO_UINT8((UINT8*)content,charLen,(UINT8*)ucs2Buf);
	ucs2Len=ucs2Count((UINT8*)ucs2Buf);

	for(i=0; i<(ucs2Len>>1); i++)
	{
		*(UINT16*)(ucs2BufVers+(i<<1)) =  htons(*(UINT16*)(ucs2Buf+(i<<1)));
	}
	sms_free(ucs2Buf);
	ucs2Buf = NULL;

	utf8Buf = sms_malloc(maxSMSNum*640+1);
	DUSTER_ASSERT(utf8Buf);
	memset(utf8Buf, '\0', maxSMSNum*640+1);
	for(i=0; i<=(ucs2Len>>1)-1; i++)
	{
		u2utf8_bigEndian(ucs2BufVers+(i<<1), utf8Buf, &pos);
	}
	sms_free(ucs2BufVers);
	ucs2BufVers = NULL;

	if(strcmp(encode_type,"UNICODE")==0)
	{
		code_scheme=1;
		if(ucs2Len<=140)
			totoal_SegNum=1;
		else
		{
			totoal_SegNum=ucs2Len/134;
			if(ucs2Len%134!=0)
				totoal_SegNum+=1;
		}

		Duster_module_Printf(1,"%s,UNICODE,totoal_SegNum =%d",__FUNCTION__,totoal_SegNum);
	}
	else if(strcmp(encode_type,"GSM7_default")==0)
	{
		code_scheme=0;

		curSplitInfo=calcGSM7Info(utf8Buf);
		totoal_SegNum=curSplitInfo.totalSegmentNum;
		Duster_module_Printf(1,"%s,GSM7_default,totoal_SegNum =%d",__FUNCTION__,totoal_SegNum);

	}
	else
	{
		Duster_module_Printf(1,"%s encode_type error",__FUNCTION__);
		ret= -1;
		goto EXIT;
	}

	if(edit_id == 0) /*new added*/
	{
		tempData = sms_malloc(sizeof(s_MrvFormvars));
		tempData->value=sms_malloc(sizeof(SMS_Node));
		if(!tempData||!tempData->value)
		{
			Duster_module_Printf(1,"%s,sms_malloc failed",__FUNCTION__);
		}
		memset(tempData->value,	'\0',	sizeof(SMS_Node));
		tempSMS=(SMS_Node *)tempData->value;
		tempSMS->location=1;
		tempSMS->psm_location=saveIndex;
		tempSMS->sms_status=DRAFT;
		tempSMS->isLongSMS=FALSE;
		tempSMS->srcNum = NULL;
		tempSMS->LSMSinfo = NULL;
		slist_insert_firstNode(&tempData,list_start,list_last);
	}
	sprintf(indexbuf,"LDRT%d",saveIndex);
	str_replaceChar(utf8Buf,';',SEMI_REPLACE);
	str_replaceChar(utf8Buf,'=',EQUAL_REPLACE);
	savebuf = sms_malloc(maxSMSNum*640+1);
	DUSTER_ASSERT(savebuf);
	memset(savebuf,	'\0',	maxSMSNum*640);
	sprintf(savebuf,"%s%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c",indexbuf,DELIMTER,number,DELIMTER,code_scheme,DELIMTER,utf8Buf,DELIMTER,timebuf2,DELIMTER,DRAFT,DELIMTER,location,DELIMTER,NOT_LONG,DELIMTER,totoal_SegNum,DELIMTER,0,DELIMTER,0,DELIMTER);
						#ifdef NOTION_ENABLE_JSON
						{
						       char tmp_message_time_index_str[20] = {0};
							message_time_index++;   
							sprintf(tmp_message_time_index_str,"%d",message_time_index);    
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,"sms_message_time_index",tmp_message_time_index_str,handle_SMS1);	                   
							memset(tmp_message_time_index_str,0,20);
                                          	sprintf(tmp_message_time_index_str,"%d%c",message_time_index,DELIMTER);    										
							Duster_module_Printf(1,"%s,tmp_message_time_index_str is %s,message_time_index:%d",__FUNCTION__,tmp_message_time_index_str,message_time_index);				
                                          	strcat(savebuf,tmp_message_time_index_str);
						}				  
						#endif	
	Duster_module_Printf(1,"%s,savebuf is %s",__FUNCTION__,savebuf);
	psmfile_set_wrapper(PSM_MOD_SMS,NULL,indexbuf,savebuf,handle_SMS1);

	if(edit_id ==0 )
	{
		totalLocalDraftNum++;
		memset(tmpBuf,	'\0',	11);
		sprintf(tmpBuf,"%d",totalLocalDraftNum);
		psm_set_wrapper(PSM_MOD_SMS,NULL,"totalLocalDraftNum",tmpBuf);
		psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	tmpBuf,	handle_SMS1);
	}
EXIT:

	sms_free(savebuf);
	sms_free(utf8Buf);

	if(timebuf2)
		sms_free(timebuf2);

	Duster_module_Printf(1,"leave %s",__FUNCTION__);
	return ret;

}
/*
 action : 0 copy
             1 move
*/
int SMS_MV_CP(char *msg_id,UINT8 mem_to,UINT8 action)
{
	UINT8 		srcList = 0,code_type = 0,totalNum=0,write_action = 0;
	char 		encodeType[15]= {0};
	int 		ret1= 1,ret2= 1;
	char 		tmp[25]= {0},SMS_index[12]= {0},date[25]= {0};
	UINT16 		index=0,i=0,psm_index=0;
	int			ghandle = handle_SMS1,ret = 0;
	s_MrvFormvars 	**dstList_start=NULL,**dstList_last=NULL;
	s_MrvFormvars	*srcList_start = NULL , *srcList_last = NULL;
	s_MrvFormvars	*newNode=NULL,*tmpNode=NULL;
	SMS_Node 	*tmpSMS=NULL,*newSMS=NULL;
	char		*pS=NULL,*pE=NULL,*str = NULL;
	char 		*p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL,*p6 = NULL,*p7 = NULL,*p8 = NULL,*p9 = NULL,*p10 = NULL,*p11 =NULL;
	char 		*buff_SMSSubject=NULL,*long_ucs2buf=NULL,*tmpSubject=NULL;
	char		*ucs_char = NULL;
	char		*src_number = NULL;
	char 		*delete_msg_id = NULL;
	int			part_fail = 0;
	int			successed_flag = 0;

	Duster_module_Printf(1,"enter %s,msg_id is %s,mem_to is %d",__FUNCTION__,msg_id,mem_to);

	if(!msg_id||((mem_to != 0)&&(mem_to != 1)))
	{
		Duster_module_Printf(1,"%s,params error",__FUNCTION__);
		return -1;
	}
	if(!strncmp(msg_id,"LRCV",4))
	{
		srcList = 1;
		srcList_start = LocalSMS_list_start;
		srcList_last  = LocalSMS_list_last;
		write_action  = 3;
	}
	else if(!strncmp(msg_id,"LSNT",4))
	{
		srcList = 2;
		srcList_start = LocalSendSMS_list_start;
		srcList_last  = LocalSendSMS_list_last;
		write_action  = 2;
	}
	else if(!strncmp(msg_id,"SRCV",4))
	{
		srcList = 3;
		srcList_start = SimSMS_list_start;
		srcList_last  = SimSMS_list_last;
	}

	if(mem_to == 0)
	{
		dstList_start = &SimSMS_list_start;
		dstList_last  = &SimSMS_list_last;
	}
	else if(mem_to == 1)
	{
		dstList_start = &LocalSMS_list_start;
		dstList_last  = &LocalSMS_list_last;

	}

	tmpSubject      = sms_malloc(300);
	DUSTER_ASSERT(tmpSubject);
	memset(tmpSubject,      '\0', 300);

	str_replaceChar(msg_id,';',',');
	delete_msg_id = duster_malloc(strlen(msg_id) + 1);
	DUSTER_ASSERT(delete_msg_id);
	memset(delete_msg_id,	'\0',	strlen(msg_id) + 1);
	pS=msg_id;
	pE=strchr(msg_id,',');
	if(mem_to == 0)
	{
		while(pE)
		{
			memset(delete_msg_id,	'\0',	strlen(msg_id) + 1);
			memset(tmp,	'\0',	25);
			if((pE-pS)<4)
			{
				pS=pE+1;
				pE=strchr(pS,',');
				continue;
			}
			memcpy(tmp,pS+4,pE-pS-4);
			index=ConvertStrToInteger(tmp);
			Duster_module_Printf(1,"%s index is %d",__FUNCTION__,index);
			if(action == 1)
			{
				if(!strncmp(msg_id,"LRCV",4))
				{
					srcList_start = LocalSMS_list_start;
				}
				else if(!strncmp(msg_id,"LSNT",4))
				{
					srcList_start = LocalSendSMS_list_start;
				}
			}
			tmpNode = srcList_start;
			while(tmpNode)
			{
				tmpSMS = (SMS_Node *)tmpNode->value;
				if((srcList == 1)||(srcList == 2))
				{
					if(tmpSMS->psm_location == index)
						break;
				}
				else if(srcList == 3)
				{
					if(tmpSMS->psm_location == index)
						break;
				}
				tmpNode = tmpNode->next;
			}
			if(tmpNode)
			{
				tmpSMS = (SMS_Node *)tmpNode->value;
				if(tmpSMS->isLongSMS&&(tmpSMS->LSMSinfo->LSMSflag == START))
				{
					Duster_module_Printf(1,"%s Move or copy Long SMS",__FUNCTION__);
					if(!buff_SMSSubject)
						buff_SMSSubject = sms_malloc(5<<9);		//SupportdisplaySMSNum*512
					DUSTER_ASSERT(buff_SMSSubject);
					memset(buff_SMSSubject, '\0', 5<<9);
					while(tmpNode)
					{
						tmpSMS=(SMS_Node*)tmpNode->value;
						memset(SMS_index,	'\0',	12);

						if(srcList == 1)
						{
							sprintf(SMS_index,"%s%d","LRCV",tmpSMS->psm_location); //Local SMS
							ret = getPSM_index(&(tmpSMS->psm_location),&ghandle,1);
							str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
						}
						else if(srcList == 2)
						{

							sprintf(SMS_index,"%s%d","LSNT",tmpSMS->psm_location); //Local SMS
							str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
						}
						else if(srcList ==3)
						{
							sprintf(SMS_index,"%s%d","SRCV",tmpSMS->psm_location);
							str = psm_get_wrapper(PSM_MOD_SMS,NULL,SMS_index);

						}
						strcat(delete_msg_id,	SMS_index);
						strcat(delete_msg_id,		",");

						Duster_module_Printf(1,"%s,	delete_msg_id is %s",__FUNCTION__,	delete_msg_id);
						if(str)
						{
							p1 = strchr(str, DELIMTER);
							p2 = strchr(p1+1,DELIMTER);
							p3 = strchr(p2+1,DELIMTER);
							p4 = strchr(p3+1,DELIMTER);

							if(p1&&p2&&p3&&p4)
							{
								if(!src_number )
								{
									src_number = duster_malloc(p2 - p1);
									DUSTER_ASSERT(src_number);
									memset(src_number,	'\0',	p2 - p1);
									memcpy(src_number,	p1 + 1,	p2 - p1 -1);
									Duster_module_Printf(3,	"%s src_number is %s",	__FUNCTION__,	src_number);
								}
								memset(tmpSubject,	'\0',   300);
								memcpy(tmpSubject,p3+1,p4-p3-1);
								tmpSubject[299] = '\0';
								Duster_module_Printf(3,"%s strlen(tmpSubject)=%d",__FUNCTION__,strlen(tmpSubject));
								strcat(buff_SMSSubject,tmpSubject);
								Duster_module_Printf(3,"%s strlen(buff_SMSSubject)=%d",__FUNCTION__,strlen(buff_SMSSubject));
								if(tmpSMS->LSMSinfo->LSMSflag == START)
								{

									memset(tmpSubject, '\0',      300);
									memset(tmp,        '\0',      25);
									DUSTER_ASSERT( (p3-p2-1) < 25);
									memcpy(tmp,        p2+1,   p3-p2-1);
									code_type = ConvertStrToInteger(tmp);
									totalNum  =  tmpSMS->LSMSinfo->totalSegment;
								}

								if(tmpSMS->LSMSinfo->LSMSflag!=END)
								{
									tmpNode = tmpNode->next;
									sms_free(str);
									str = NULL;
								}
								else if(tmpSMS->LSMSinfo->LSMSflag==END)
								{

									str_replaceChar(buff_SMSSubject,SEMI_REPLACE, ';');
									str_replaceChar(buff_SMSSubject,EQUAL_REPLACE,'=');
									ucs_char = utf8_to_unicode_char(buff_SMSSubject);
									sms_free(buff_SMSSubject);
									buff_SMSSubject = NULL;
									memset(date, 	'\0', 25);
									p5=strchr(p4+1, DELIMTER);
									memcpy(date,p4+1,p5-p4-1);//date
									sms_free(str);
									str = NULL;
									break;
								}

							}
							if(str)
								sms_free(str);
							str = NULL;
						}

					}
					memset(encodeType,	'\0',	15);
					if((code_type == 0) || (code_type == 3) || (code_type == 4))
					{
						memcpy(encodeType,"GSM7_default",12);
					}
					else if(code_type == 1)
					{
						memcpy(encodeType,"UNICODE",7);
					}
				}
				else if(!tmpSMS->isLongSMS||((tmpSMS->isLongSMS)&&(tmpSMS->LSMSinfo->LSMSflag==ONE_ONLY)))
				{
					if(srcList == 1)
					{
						sprintf(SMS_index,"%s%d","LRCV",tmpSMS->psm_location); //Local SMS
						ret = getPSM_index(&(tmpSMS->psm_location),&ghandle,1);
						str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
					}
					else if(srcList == 2)
					{

						sprintf(SMS_index,"%s%d","LSNT",tmpSMS->psm_location); //Local SMS
						str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
					}
					else if(srcList ==3)
					{
						sprintf(SMS_index,"%s%d","SRCV",tmpSMS->psm_location);
						str = psm_get_wrapper(PSM_MOD_SMS,NULL,SMS_index);

					}
					strcat(delete_msg_id,	SMS_index);
					strcat(delete_msg_id,		",");
					Duster_module_Printf(1,"%s,	delete_msg_id is %s",__FUNCTION__,	delete_msg_id);
					if(str)
					{
						p1=strchr(str,  DELIMTER);
						p2=strchr(p1+1, DELIMTER);
						p3=strchr(p2+1, DELIMTER);
						p4=strchr(p3+1, DELIMTER);
						p5=strchr(p4+1, DELIMTER);
						if(p1&&p2&&p3&&p4&&p5)
						{
							src_number = duster_malloc(p2 - p1);
							DUSTER_ASSERT(src_number);
							memset(src_number,	'\0',	p2 - p1);
							memcpy(src_number,	p1 + 1, p2 - p1 -1);
							Duster_module_Printf(3, "%s src_number is %s",	__FUNCTION__,	src_number);
							memset(tmp, '\0',   25);
							DUSTER_ASSERT((p3-p2-1) < 25); /*trace  4693*/
							memcpy(tmp, p2+1,p3-p2-1);
							code_type=ConvertStrToInteger(tmp);
							Duster_module_Printf(3,"%s code_type is %d",__FUNCTION__,code_type);
							memset(tmpSubject,  '\0',    300);
							if(!long_ucs2buf)
								long_ucs2buf	= sms_malloc(5<<9);
							DUSTER_ASSERT(long_ucs2buf);
							memset(long_ucs2buf,	'\0',    5<<9);
							memcpy(tmpSubject,  p3+1, p4-p3-1);
							str_replaceChar(tmpSubject,SEMI_REPLACE, ';');
							str_replaceChar(tmpSubject,EQUAL_REPLACE,'=');
							ucs_char = utf8_to_unicode_char(tmpSubject);
							if(p5&&((p5-p4-1)<25))
							{
								memset(date,	'\0',	25);
								memcpy(date,p4+1,p5-p4-1);
								//goform_duster_Printf("%s,date is %s",__FUNCTION__,date);
							}
							memset(encodeType,	'\0',	15);
							if((code_type == 0) || (code_type == 3) || (code_type == 4))
							{
								memcpy(encodeType,"GSM7_default",12);
							}
							else if(code_type == 1)
							{
								memcpy(encodeType,"UNICODE",7);
							}
						}
						if(str)
							sms_free(str);
						str = NULL;
					}
				}
			}

			Duster_module_Printf(1,"%s,date is %s",__FUNCTION__,date);
			ret1= SMS_Send(src_number,ucs_char,encodeType,date,dstList_start,dstList_last,write_action,tmpSMS->sms_status);
			Duster_module_Printf(1,	"%s ret1 = %d",	__FUNCTION__,	ret1);
			if(src_number)
			{
				duster_free(src_number);
				src_number = NULL;
			}
			if((successed_flag == 0) && (ret1 > 0))
			{
				successed_flag = 1;
			}
			if((ret1  > 0) &&(action == 1))
			{
				ret2 = SMS_delete_list(delete_msg_id);
			}
			else if((ret1 < 0 ) && (successed_flag == 1) )
			{
				successed_flag = 2; //part failed
			}
			ret2 = ret1<0?ret1:ret2;
			if(ret2 < 0)
			{
				if(totalSimSmsNum == gMem1TotalNum)
				{
					break;
				}
			}
			Duster_module_Printf(1,	"%s ret2 = %d",	__FUNCTION__,	ret2);
			while(totalNum>1)
			{
				pS=pE+1;
				pE=strchr(pS,',');
				totalNum--;
			}
			pS=pE+1;
			pE=strchr(pS,',');
			totalNum = 0;

		}
	}
	else if(mem_to == 1)
	{
		while(pE)
		{
			memset(tmp,	'\0',	25);
			if((pE-pS)<4)
			{
				pS=pE+1;
				pE=strchr(pS,',');
				continue;
			}
			memcpy(tmp,pS+4,pE-pS-4);
			index=ConvertStrToInteger(tmp);
			Duster_module_Printf(3,"%s index is %d",__FUNCTION__,index);
			tmpNode = srcList_start;
			while(tmpNode)
			{
				tmpSMS = (SMS_Node *)tmpNode->value;
				if(tmpSMS->psm_location== index)
					break;
				tmpNode = tmpNode->next;
			}
			if(tmpNode)
			{
				tmpSMS = (SMS_Node*)tmpNode->value;
				memset(SMS_index,	'\0',	12);
				sprintf(SMS_index,"%s%d","SRCV",tmpSMS->psm_location);
				Duster_module_Printf(3,"%s,src SMS_index is %s",__FUNCTION__,SMS_index);
				strcat(delete_msg_id,	SMS_index);
				strcat(delete_msg_id,		",");
				str = psm_get_wrapper(PSM_MOD_SMS,NULL,SMS_index);
				if(str)
				{
					if(strncmp(str,	"",	1) != 0)
					{
						p1 = strchr(str,	DELIMTER);
						DUSTER_ASSERT(p1);
						memset(tmp,	'\0',	25);
						if((tmpSMS->sms_status==READED)||(tmpSMS->sms_status==UNREAD))
						{
							if(!tmpSMS->isLongSMS)
							{
								if(totalLocalSMSNum  == SupportSMSNum)
								{
									if(successed_flag == 1)
									{
										successed_flag = 2;
									}
									ret2 = -1;
									goto EXIT;
								}
							}
							else if(tmpSMS->isLongSMS && tmpSMS->LSMSinfo && (tmpSMS->LSMSinfo->LSMSflag == START))
							{
								if((totalLocalSMSNum + tmpSMS->LSMSinfo->totalSegment) > SupportSMSNum)
								{
									if(successed_flag == 1)
									{
										successed_flag = 2;
									}
									ret2 = -1;
									goto EXIT;
								}
							}
							ret = getPSM_index(&psm_index,&ghandle,0);
							sprintf(tmp,"LRCV%d",psm_index);
							Duster_module_Printf(3,"%s,dst SMS_index is %s",__FUNCTION__,tmp);
							if(!long_ucs2buf)
								long_ucs2buf	= sms_malloc(5<<9);
							DUSTER_ASSERT(long_ucs2buf);
							memset(long_ucs2buf, '\0',  5<<9);
							memcpy(long_ucs2buf, tmp,strlen(tmp));
							memcpy(long_ucs2buf+strlen(long_ucs2buf),	p1,	strlen(str)- (p1 - str)); /*add protection for bug 4693*/
							//memcpy(long_ucs2buf+strlen(long_ucs2buf),str+strlen(SMS_index),	strlen(str)-strlen(SMS_index));
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,tmp,long_ucs2buf,ghandle);
							sms_free(long_ucs2buf);
							long_ucs2buf = NULL;
							totalLocalSMSNum++;
						}
						else if((tmpSMS->sms_status == SEND_FAIL)||(tmpSMS->sms_status == SEND_SUCCESS))
						{
							if(!tmpSMS->isLongSMS)
							{
								if(totalLocalSentNum  = MaxSentSMSNum)
								{
									if(successed_flag == 1)
									{
										successed_flag = 2;
									}
									ret2 = -1;
									goto EXIT;
								}
							}
							else if(tmpSMS->isLongSMS && tmpSMS->LSMSinfo && (tmpSMS->LSMSinfo->LSMSflag == START))
							{
								if((totalLocalSentNum + tmpSMS->LSMSinfo->totalSegment) > MaxSentSMSNum)
								{
									if(successed_flag == 1)
									{
										successed_flag = 2;
									}
									ret2 = -1;
									goto EXIT;
								}
							}
							for(i=1; i<=MaxSentSMSNum; i++)
							{
								if(SentSMSIndex[i]==0)
								{
									psm_index=i;
									SentSMSIndex[i]=1;
									break;
								}
							}
							sprintf(tmp,"LSNT%d",psm_index);
							if(!long_ucs2buf)
								long_ucs2buf	= sms_malloc(5<<9);
							DUSTER_ASSERT(long_ucs2buf);
							memset(long_ucs2buf,	'\0',	5<<9);
							memcpy(long_ucs2buf,tmp,strlen(tmp));
							memcpy(long_ucs2buf+strlen(long_ucs2buf),	p1,	strlen(str)- (p1 - str)); /*add protection for bug 4693*/
							//memcpy(long_ucs2buf+strlen(long_ucs2buf),str+strlen(SMS_index),strlen(str)-strlen(SMS_index));
							psmfile_set_wrapper(PSM_MOD_SMS,NULL,tmp,long_ucs2buf,handle_SMS1);
							sms_free(long_ucs2buf);
							long_ucs2buf = NULL;
							dstList_start = &LocalSendSMS_list_start;
							dstList_last = &LocalSendSMS_list_last;
							totalLocalSentNum++;
						}
						newNode        = (s_MrvFormvars*)sms_malloc(sizeof(s_MrvFormvars));
						newNode->value = (void*)    sms_malloc(sizeof(SMS_Node));
						newSMS         = (SMS_Node*)newNode->value;
						DUSTER_ASSERT(newSMS);
						memset(newSMS,	'\0',     sizeof(SMS_Node));
						memcpy(newSMS,tmpSMS,sizeof(SMS_Node));

						p1 = strchr(str,	DELIMTER);
						p2 = strchr(p1 + 1,	DELIMTER);
						p3 = strchr(p2 + 1,	DELIMTER);
						p4 = strchr(p3 + 1,	DELIMTER);
						p5 = strchr(p4 + 1,	DELIMTER);
						p6 = strchr(p5 + 1,	DELIMTER);
						p7 = strchr(p6 + 1,	DELIMTER);
						p8 = strchr(p7 + 1,	DELIMTER);
						p9 = strchr(p8 + 1,	DELIMTER);
						p10 = strchr(p9 + 1,	DELIMTER);
						p11 = strchr(p10 + 1,	DELIMTER);

						if(tmpSMS->srcNum)
						{
							newSMS->srcNum = sms_malloc(strlen(tmpSMS->srcNum)+1);
							DUSTER_ASSERT(newSMS->srcNum);
							memset(newSMS->srcNum,	'\0',	strlen(tmpSMS->srcNum)+1);
							memcpy(newSMS->srcNum,	tmpSMS->srcNum,	strlen(tmpSMS->srcNum));
						}
						else
						{
							if(p1&&p2)
							{
								newSMS->srcNum = sms_malloc(p2- p1);
								DUSTER_ASSERT(newSMS->srcNum);
								memset(newSMS->srcNum,	'\0',	p2 - p1);
								memcpy(newSMS->srcNum,	p1+1,	p2-p1-1);
							}
							Duster_module_Printf(1,"%s,	newSMS->srcNum is %s,",__FUNCTION__,newSMS->srcNum);
						}
						newSMS->savetime = (TIMESTAMP *)sms_malloc(sizeof(TIMESTAMP));
						DUSTER_ASSERT(newSMS->savetime);
						memset(newSMS->savetime,	'\0',	sizeof(TIMESTAMP));
						if(tmpSMS->savetime)
						{
							memcpy(newSMS->savetime,	tmpSMS->savetime,	sizeof(TIMESTAMP));
						}
						else
						{
							memset(tmp,	'\0',	25);
							memcpy(tmp,	p4+1,	p5 - p4 - 1);
							transTimeToSTAMP(tmp,	newSMS->savetime);
						}

						if(tmpSMS->isLongSMS)
						{
							newSMS->LSMSinfo = sms_malloc(sizeof(LSMS_INFO));
							DUSTER_ASSERT(newSMS->LSMSinfo);
							memset(newSMS->LSMSinfo,	'\0',	sizeof(LSMS_INFO));
							if(tmpSMS->LSMSinfo)
							{
								memcpy(newSMS->LSMSinfo,	tmpSMS->LSMSinfo,	sizeof(LSMS_INFO));
							}
							else
							{
								if(p1&&p2&&p3&&p4&&p5&&p6&&p7&&p8&&p9&&p10&&p11)
								{
									memset(tmp,	'\0',	25);
									memcpy(tmp,	p8+1,	p9 - p8 -1);
									newSMS->LSMSinfo->referenceNum = ConvertStrToInteger(tmp);
									memset(tmp,	'\0',	25);
									memcpy(tmp,	p9+1,	p10 - p9 -1);
									newSMS->LSMSinfo->segmentNum = ConvertStrToInteger(tmp);
									memset(tmp,	'\0',	25);
									memcpy(tmp,	p10+1,	p11 - p10 - 1);
									newSMS->LSMSinfo->totalSegment = ConvertStrToInteger(tmp);
									Duster_module_Printf(1,"%s,referenceNum is %d,segmentNum is %d,total is %d",__FUNCTION__,newSMS->LSMSinfo->referenceNum,
									                     newSMS->LSMSinfo->segmentNum,newSMS->LSMSinfo->totalSegment);
								}
								else
									DUSTER_ASSERT(0);
							}
							newSMS->LSMSinfo->receivedNum = 0;
							newSMS->LSMSinfo->isRecvAll = FALSE;
						}
						newSMS->psm_location = psm_index;
						newSMS->location     = 1;
						SMS_insert_Node(dstList_start,dstList_last,&newNode,1);
						if(successed_flag == 0)
						{
							successed_flag = 1;
						}
					}
					sms_free(str);
					str = NULL;
				}
			}
			pS=pE+1;
			pE=strchr(pS,',');
			totalNum = 0;
		}

	}
EXIT:
	if(ucs_char)
		sms_free(ucs_char);
	if(tmpSubject)
		sms_free(tmpSubject);

	if((action == 1) && (mem_to == 1) && delete_msg_id)
	{
		ret1=SMS_delete_list(delete_msg_id);
		Duster_module_Printf("%s after delete ret1 =%d",	__FUNCTION__,	ret1);
	}
	if(delete_msg_id)
	{
		duster_free(delete_msg_id);
		delete_msg_id = NULL;
	}
	if(buff_SMSSubject)
	{
		sms_free(buff_SMSSubject);
		buff_SMSSubject = NULL;
	}
	if(long_ucs2buf)
	{
		sms_free(long_ucs2buf);
		long_ucs2buf = NULL;
	}
	ret2 = ret1<0?ret1:ret2;
	if(successed_flag == 2)
	{
		Duster_module_Printf(1,"leave %s successed_flag = %d",__FUNCTION__,	successed_flag);
		return successed_flag;
	}
	else
	{
		Duster_module_Printf(1,"leave %s ret2 = %d",__FUNCTION__,	ret2);
		return ret2;
	}
}
char *SMS_Gettime_FromPSM(SMS_Node *in_node,	TIMESTAMP *time)
{
	char 	buf[25]	;
	char 	*str	=	NULL;
	int		outIdx	=	0;
	char 	*p1 = NULL,	*p2 = NULL,	*p3 = NULL, *p4 = NULL, *p5 =NULL;
	memset(buf,	'\0',	25);
	Duster_module_Printf(1,"enter %s, in_node->location = %d",	__FUNCTION__,	in_node->location);
	if(in_node->location ==	0)
	{
		sprintf(buf,	"SRCV%d",	in_node->psm_location);
		str	=	psm_get_wrapper(PSM_MOD_SMS,	NULL,	buf);
	}
	else if(in_node->location	==	1)
	{
		if( (in_node->sms_status == UNSENT) ||(in_node->sms_status == SEND_SUCCESS) || (in_node->sms_status == SEND_FAIL) )
		{
			sprintf(buf,	"LSNT%d",	in_node->psm_location);
			str=	psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	buf,	handle_SMS1);
		}
		else if(in_node->sms_status == DRAFT)
		{
			sprintf(buf,	"LDRT%d",	in_node->psm_location);
			str=	psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	buf,	handle_SMS1);
		}
		else
		{
			getPSM_index(&(in_node->psm_location),&outIdx,1);
			sprintf(buf,	"LRCV%d",	in_node->psm_location);
			str=	psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	buf,	outIdx);
		}
	}
	Duster_module_Printf(1,"%s,	sms_index is %s",	__FUNCTION__,	buf);

	if(str)
	{
		p1 = strchr(str,	DELIMTER);
		p2 = strchr(p1+1,	DELIMTER);
		p3 = strchr(p2+1,	DELIMTER);
		p4 = strchr(p3+1,	DELIMTER);
		p5 = strchr(p4+1,	DELIMTER);
		if(p1&&p2&&p3&&p4&&p5)
		{
			memset(buf,	'\0',	25);
			memcpy(buf,	p4+1,	p5-p4-1);
			buf[24] = '\0';
			Duster_module_Printf(1,"%s,	time is %s",__FUNCTION__,	buf);
			changeTimeToTIMESTAMP(buf,	time);
		}
		sms_free(str);
		str = NULL;
	}
	Duster_module_Printf(1,"leave %s",	__FUNCTION__);
}
char *utf8_to_unicode_char(char *input)
{
	int src_len = 0,	i = 0;
	int usc2Length = 0;
	char *ucs2buf = NULL;
	char *convertedBuff = NULL;
	char *ucs_char = NULL;

	if(!input)
		return NULL;

	src_len = strlen(input) + 1;
	ucs2buf = sms_malloc(src_len*2 +1);
	DUSTER_ASSERT(ucs2buf);
	memset(ucs2buf,'\0',src_len*2 +1);
	utf8_to_usc2((UINT8*)input,(UINT8*)ucs2buf);
	usc2Length=ucs2Count((UINT8 *)ucs2buf);
	convertedBuff = sms_malloc(usc2Length +1);
	DUSTER_ASSERT(convertedBuff);
	memset(convertedBuff,'\0',usc2Length +1);
	for(i=0; i<usc2Length>>1; i++)
	{
		*(UINT16*)(convertedBuff+(i<<1)) =	htons(*(UINT16*)(ucs2buf+(i<<1)));
	}
	ucs_char = sms_malloc(usc2Length*2 + 1);
	DUSTER_ASSERT(ucs_char);
	memset(ucs_char,	'\0',	usc2Length*2 + 1);
	CHAR_TO_2CHAR((UINT8 *)convertedBuff,usc2Length,(UINT8 *)ucs_char);

	sms_free(ucs2buf);
	sms_free(convertedBuff);
	return ucs_char;
}
/***********************************************************************
	*	function : Transfer Unicode to UTF8 bigEnd
	*
	*
************************************************************************/
char* unicodechar_to_utf8_bigdean(char 	*inbuf)
{
	char *retStr = NULL;
	char *ucs2Buff = NULL,	*versUcsBuf = NULL;
	int	  unicodechar_len = 0,	usc2Length = 0,	i = 0;
	unsigned int pos = 0;
	int	  ucs_vers_len = 0,	ucs_count_len = 0;

	Duster_module_Printf(1,"enter %s",	__FUNCTION__);
	if(!inbuf)
		return retStr;
	unicodechar_len = strlen(inbuf);
	ucs2Buff = sms_malloc(unicodechar_len/2 + 1);
	DUSTER_ASSERT(ucs2Buff);
	memset(ucs2Buff,	'\0',	unicodechar_len/2 + 1);
	CHAR_TO_UINT8(inbuf,strlen(inbuf),(UINT8 *)ucs2Buff);
	usc2Length = unicodechar_len/2;
	ucs_count_len = ucs2Count((UINT8 *)ucs2Buff);
	Duster_module_Printf(1,"%s,usc2Length=%d,	ucs_count_len is %d",__FUNCTION__,usc2Length, ucs_count_len);

	versUcsBuf= sms_malloc(usc2Length + 1);
	DUSTER_ASSERT(versUcsBuf);
	memset(versUcsBuf,'\0',usc2Length + 1);
	for(i=0; i<(usc2Length>>1); i++)
	{
		*(UINT16*)(versUcsBuf+(i<<1)) =  htons(*(UINT16*)(ucs2Buff+(i<<1)));
	}
	ucs_vers_len = ucs2Count((UINT8* )versUcsBuf);
	Duster_module_Printf(1,"%s,ucs_vers_len=%d, usc2Length = %d",__FUNCTION__,ucs_vers_len,	usc2Length);
	sms_free(ucs2Buff);
	ucs2Buff = NULL;
	retStr = sms_malloc((int)(usc2Length*3/2) + 5);
	ASSERT(retStr);
	memset(retStr,	'\0',	(int)(usc2Length*3/2)+5);
	for(i=0; i<=(usc2Length>>1)-1; i++)
	{
		u2utf8_bigEndian(versUcsBuf+(i<<1), retStr, &pos);
	}
	sms_free(versUcsBuf);
	versUcsBuf = NULL;
	Duster_module_Printf(1,	"leave %s pos is %d",	__FUNCTION__,	pos);
	if(retStr)
	{
		Duster_module_Printf(1,	"%s strlen(retStr) is %d",	__FUNCTION__,	strlen(retStr)	)
	}
	return retStr;

}
/********************************************************************
*  added by shoujunl	131010
*	return :    0 -- Error
			1 -- Initializing
*		      2 -- Empty
*			3 -- Has Data
*********************************************************************/
int SMS_Check_Empty()
{
	int ret_val 	= 0;
	int	init_status = 0;

	init_status = SMS_Get_LocalSMS_Init();
	if(init_status < 1)
	{
		ret_val = 1;
	}
	else if(init_status == 1)
	{
		if((totalLocalSMSNum == 0) && (totalLocalDraftNum == 0) && (totalLocalSentNum == 0))
		{
			ret_val = 2;
		}
		else
		{
			ret_val = 3;
		}
	}
	return ret_val;
}
/********************************************************************
*  added by shoujunl	010213
*	return :    0 -- Error
*		      	1 -- Empty
*				2 -- Has Data
*********************************************************************/
int SMS_Check_Device_Empty()
{
	char *str = NULL;

	str = psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num",	handle_SMS1);
	if(str)
	{
		totalLocalSMSNum= ConvertStrToInteger(str);
		Duster_module_Printf(1,"%s:totalLocalSMSNum is %d",__FUNCTION__,totalLocalSMSNum);
		sms_free(str);
	}
	else
	{
		Duster_module_Printf(1,"%s,cann't get totalLocalSMSNum",__FUNCTION__);
		totalLocalSMSNum=0;
		//return;
	}

	str=psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_send_num",	handle_SMS1);

	if(str)
	{
		totalLocalSentNum= ConvertStrToInteger(str);
		Duster_module_Printf(1,"%s:totalLocalSentNum is %d",__FUNCTION__,totalLocalSentNum);
		sms_free(str);
	}
	else
	{

		Duster_module_Printf(1,"%s,cann't get totalLocalSentNum",__FUNCTION__);
		totalLocalSentNum=0;
		//return;
	}

	str=psmfile_get_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	handle_SMS1);
	if(str)
	{
		totalLocalDraftNum= ConvertStrToInteger(str);
		Duster_module_Printf(1,"%s:totalLocalDraftNum is %d",__FUNCTION__,totalLocalDraftNum);
		sms_free(str);
	}
	else
	{

		Duster_module_Printf(1,"%s,cann't get ttalLocalSMSNum",__FUNCTION__);
		totalLocalDraftNum=0;
	}
	if((totalLocalSMSNum == 0) && (totalLocalDraftNum == 0) && (totalLocalSentNum == 0))
	{
		return 1;
	}
	else
	{
		return 2;
	}

}
void SMS_clear_list(s_MrvFormvars **start, s_MrvFormvars **last)
{
	s_MrvFormvars  *cur_node = NULL;
	SMS_Node	*cur_SMS	=	NULL;
	if(!*start)	/*list is NULL*/
	{
		return;
	}
	SMS_DeleteInsert_lock();
	while(*start)
	{
		cur_node = *start;
		if(cur_node->value)
		{
			cur_SMS = (SMS_Node*)cur_node->value;
			if(cur_SMS->srcNum)
			{
				sms_free(cur_SMS->srcNum);
				cur_SMS->srcNum = NULL;
			}
			if(cur_SMS->savetime)
			{
				sms_free(cur_SMS->savetime);
				cur_SMS->savetime = NULL;
			}
			if(cur_SMS->LSMSinfo)
			{
				sms_free(cur_SMS->LSMSinfo);
				cur_SMS->LSMSinfo = NULL;
			}
			sms_free(cur_node->value);
			cur_node->value = NULL;
		}
		*start = (*start)->next;
		sms_free(cur_node);
	}
	*start = NULL;
	*last = NULL;
	SMS_DeleteInsert_unlock();
	return;
}
extern void PB_delete_SIM_INFO(s_MrvFormvars **start,s_MrvFormvars **last);
void SMS_delete_all_SIM_info()
{
	SMS_clear_list(&SimSMS_list_start,	&SimSMS_list_last);
	sim_is_ready = 0;
	DM_first_flag = 0;
	PB_delete_SIM_INFO(&PB_Sim_List_Start,&PB_Sim_List_Last);
}
/************************************************************************
*  function description : make all sms status readed
*
**************************************************************************/
int SMS_read_By_list(s_MrvFormvars *start,s_MrvFormvars *last)
{

	char 	   	   *str = NULL, *p1=NULL,*p2=NULL,*p3=NULL,*p4=NULL,*p5=NULL;
	SMS_Node   	   *tempSMS;
	s_MrvFormvars       *tempData;
	char 			SMS_index[12];
	char 			temp[4];
	int 			outIdx = 0,ret = 0;
	OSA_STATUS 		osa_status;
	UINT32 			flag_value;
	CommandATMsg   *sendSMSMsg = NULL;
	UINT16			index = 0;

	sendSMSMsg = sms_malloc(sizeof(CommandATMsg));
	DUSTER_ASSERT(sendSMSMsg);
	sendSMSMsg->atcmd= sms_malloc(DIALER_MSG_SIZE);
	DUSTER_ASSERT(sendSMSMsg->atcmd);
	sendSMSMsg->err_fmt=sms_malloc(15);
	sendSMSMsg->ok_flag=1;
	sendSMSMsg->ok_fmt=sms_malloc(10);
	if(sendSMSMsg->ok_fmt)
	{
		memset(sendSMSMsg->ok_fmt,	'\0',	10);
		sprintf(sendSMSMsg->ok_fmt,"+CMGR");
	}
	if(sendSMSMsg->err_fmt)
	{
		memset(sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
	}
	sendSMSMsg->callback_func = &CMGR_SET_CALLBACK;
	sendSMSMsg->ATCMD_TYPE= 255;


	tempData = start;
	while(tempData!=NULL)
	{
		if(tempData->value)
		{
			tempSMS=(SMS_Node *)tempData->value;
			if(tempSMS->sms_status == READED)
			{
				tempData = tempData->next;
				continue;
			}
			memset(SMS_index,'\0',	12);
			memset(temp,'\0',	4);
			tempSMS->sms_status = READED;
			index = tempSMS->psm_location;
			if(tempSMS->location==0)//sim
			{
				sprintf(SMS_index,"%s%d","SRCV",index);
				str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
			}
			else if(tempSMS->location==1) //local sms
			{
				sprintf(SMS_index,"%s%d","LRCV",index);
				getPSM_index(&index,&outIdx,1);
				str = psmfile_get_wrapper(PSM_MOD_SMS,NULL,SMS_index,outIdx);
			}

			if(str != NULL)
			{
				p1 = strchr(str, DELIMTER);
				p2 = strchr(p1+1,DELIMTER);
				p3 = strchr(p2+1, DELIMTER);
				p4 = strchr(p3+1, DELIMTER);
				p5 = strchr(p4+1, DELIMTER);
				*(p5+1) = '1';


				if(tempSMS->location==0)
				{
					psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, str);
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
					sprintf(sendSMSMsg->atcmd,"AT+CMGR=%d\r",tempSMS->psm_location);
					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMGR_ERROR|CMGR_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					//DUSTER_ASSERT(osa_status==OS_SUCCESS);

					if( flag_value ==CMGR_ERROR)
					{
						ret = -1;
						tempSMS->sms_status = UNREAD;
						duster_Printf(" %s: AT+CMGR return error",__FUNCTION__);
					}

				}
				else if(tempSMS->location == 1)
				{
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,str,outIdx);
				}

				sms_free(str);
				str = NULL;
			}
		}
		tempData = tempData->next;
	}
	if(sendSMSMsg != NULL)
	{
		if(sendSMSMsg->atcmd != NULL)
			sms_free(sendSMSMsg->atcmd);
		if(sendSMSMsg->ok_fmt != NULL)
			sms_free(sendSMSMsg->ok_fmt);
		if(sendSMSMsg->err_fmt != NULL)
			sms_free(sendSMSMsg->err_fmt);
		sms_free(sendSMSMsg);
	}
	return ret;
}
void SMS_clear_list_raw_data(s_MrvFormvars **start, s_MrvFormvars **last,	int smsType)
{
	s_MrvFormvars   *cur_node = NULL;
	SMS_Node		*cur_SMS = NULL;
	UINT32  		flag_value;
	char 			SMS_index[12];
	int 			outIdx = 0;
	char 			tmpBuf[11]= {0};
	CommandATMsg   *sendSMSMsg = NULL;
	OSA_STATUS 		osa_status;
	if(!*start)	/*list is NULL*/
	{
		return;
	}
	if(smsType == 1)
	{
		sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
		if(!sendSMSMsg)
		{
			Duster_module_Printf(1, "%s sms_malloc sendSMSMsg failed",__FUNCTION__);
			DUSTER_ASSERT(0);
		}
		sendSMSMsg->atcmd   = sms_malloc(DIALER_MSG_SIZE);
		sendSMSMsg->err_fmt = sms_malloc(15);
		if(!sendSMSMsg->atcmd || !sendSMSMsg->err_fmt)
		{
			Duster_module_Printf(1,"%s,duster malloc failed",__FUNCTION__);
			DUSTER_ASSERT(0);
		}
		sendSMSMsg->ok_flag = 1;
		sendSMSMsg->ok_fmt  = NULL;
		memset( sendSMSMsg->err_fmt,	'\0',	15);
		sprintf(sendSMSMsg->err_fmt,"%s","+CMS ERROR");
		sendSMSMsg->callback_func = &CMGD_SET_CALLBACK;
		sendSMSMsg->ATCMD_TYPE    = TEL_EXT_SET_CMD;
		SMS_DeleteInsert_lock();
	}
	while(*start)
	{
		cur_node = *start;
		if(cur_node->value)
		{
			cur_SMS = (SMS_Node*)cur_node->value;

			if((cur_SMS->location == 0) && (smsType == 1))//sim sms
			{
				Duster_module_Printf(1,"SMS_delete_By_index,delete sim sms ");
				memset(SMS_index,	'\0',	12);
				sprintf(SMS_index,"%s%d","SRCV",	cur_SMS->psm_location);
				Duster_module_Printf(1,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
				psm_set_wrapper(PSM_MOD_SMS, NULL, SMS_index, "");

				memset(sendSMSMsg->atcmd,	'\0',	DIALER_MSG_SIZE);
				sprintf(sendSMSMsg->atcmd,"AT+CMGD=%d\r",cur_SMS->psm_location);

				osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
				DUSTER_ASSERT(osa_status==OS_SUCCESS);
				osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CMGD_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);

			}
			else if(cur_SMS->location==1)  //local SMS
			{
				Duster_module_Printf(3,"%s,delete local SMS",__FUNCTION__);
				memset(SMS_index,	'\0',	12);
				if(smsType==1)
				{
					sprintf(SMS_index,"%s%d","LRCV",cur_SMS->psm_location);
					LocalSMSIndex[cur_SMS->psm_location] = 0;
					getPSM_index(&(cur_SMS->psm_location),&outIdx,1);
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",outIdx);
					totalLocalSMSNum--;

					memset(tmpBuf,	'\0',	11);
					sprintf(tmpBuf,"%d",totalLocalSMSNum);
					psm_set_wrapper(PSM_MOD_SMS,NULL,"totalLocalSMSNum",tmpBuf);
				}
				if(smsType==2)
				{
					sprintf(SMS_index,"LSNT%d",cur_SMS->psm_location);
					SentSMSIndex[cur_SMS->psm_location] = 0;
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
					totalLocalSentNum--;
				}
				if(smsType==3)
				{
					sprintf(SMS_index,"LDRT%d",cur_SMS->psm_location);
					DraftSMSIndex[cur_SMS->psm_location] = 0;
					Duster_module_Printf(3,"%s,SMS_index is %s",__FUNCTION__,SMS_index);
					psmfile_set_wrapper(PSM_MOD_SMS,NULL,SMS_index,"",handle_SMS1);
					totalLocalDraftNum--;
				}
			}
			if(cur_SMS->srcNum)
			{
				sms_free(cur_SMS->srcNum);
				cur_SMS->srcNum = NULL;
			}
			if(cur_SMS->savetime)
			{
				sms_free(cur_SMS->savetime);
				cur_SMS->savetime = NULL;
			}
			if(cur_SMS->LSMSinfo)
			{
				sms_free(cur_SMS->LSMSinfo);
				cur_SMS->LSMSinfo = NULL;
			}
			sms_free(cur_node->value);
			cur_node->value = NULL;
		}
		*start = (*start)->next;
		sms_free(cur_node);
	}
	*start = NULL;
	*last = NULL;
	if(smsType == 1)
	{
		SMS_DeleteInsert_unlock();
		if(sendSMSMsg != NULL)
		{
			if(sendSMSMsg->atcmd != NULL)
				sms_free(sendSMSMsg->atcmd);
			if(sendSMSMsg->err_fmt != NULL)
				sms_free(sendSMSMsg->err_fmt);
			sms_free(sendSMSMsg);
		}
	}
	return;
}

#define TEST_NUM "12345"
#define TEST_MSG_CONTENT "test full msg"
s_MrvFormvars * SMS_new_test_node(int type)
{
	SMS_Node * tempSMS = NULL;	
	int 		ghandle = handle_SMS1;
	UINT16	index;
	int 	psmIndex = 0;
	TIME cp_time;
	char *savebuf = NULL;
	s_MrvFormvars * new_Node = NULL;
	char SMSIndexBuf[12] = NULL;
	char timebuf[30] = {0};
		
	GetCurrentCompTime(&cp_time);
	snprintf(timebuf,	sizeof(timebuf),	"%d,%d,%d,%d,%d,%d,+8",
		cp_time.year - 2000,	cp_time.month,	cp_time.day,	cp_time.hour,	cp_time.min,	cp_time.sec);
	
	if(type == READED || type == UNREAD)
		 getPSM_index(&psmIndex,&ghandle,0);
	else if(type == SEND_SUCCESS || type == SEND_FAIL)
	{
		for(index =1; index <=MaxSentSMSNum; index++)
		{
			if(SentSMSIndex[index]==0)
			{
				psmIndex = index;
				SentSMSIndex[index]=1;
				break;
			}
		}
	}
	else 
	{
		for(index =1; index <= MaxDraftSMSNum; index++)
		{
			if(DraftSMSIndex[index]==0)
			{
				DraftSMSIndex[index] = 1;
				psmIndex = index;
				break;
			}
		}
	}
	
	tempSMS = (SMS_Node *)sms_malloc(sizeof(SMS_Node));
	DUSTER_ASSERT(tempSMS);
	memset(tempSMS, '\0',	sizeof(SMS_Node));
	tempSMS->isLongSMS = FALSE;
	tempSMS->LSMSinfo  = NULL;
	tempSMS->location	=	1;
	tempSMS->psm_location = psmIndex;
	tempSMS->location = 1;
	tempSMS->sms_status = type;
		
	new_Node=(s_MrvFormvars *)sms_malloc(sizeof(s_MrvFormvars));
	DUSTER_ASSERT(new_Node);
	memset(new_Node,	'\0',	sizeof(s_MrvFormvars));
	new_Node->value=(char *)tempSMS;
	
	memset(SMSIndexBuf,	0,	sizeof(SMSIndexBuf));
	if(type == READED || type == UNREAD)
	{
		snprintf(SMSIndexBuf,	sizeof(SMSIndexBuf),	"LRCV%d",	psmIndex);
	}
	else if(type == SEND_SUCCESS || type == SEND_FAIL)
	{
		snprintf(SMSIndexBuf,	sizeof(SMSIndexBuf),	"LSNT%d",	psmIndex);
	}
	else 
	{
		snprintf(SMSIndexBuf,	sizeof(SMSIndexBuf),	"LDRT%d",	psmIndex);
	}
	
	if(!savebuf)
		savebuf = sms_malloc(TEL_AT_SMS_MAX_LEN);
	DUSTER_ASSERT(savebuf);
	memset(savebuf, '\0',	TEL_AT_SMS_MAX_LEN);
	sprintf(savebuf,"%s%c%s%c%d%c%s%c%s%c%d%c%d%c%d%c%d%c%d%c%d%c",SMSIndexBuf, DELIMTER,TEST_NUM,DELIMTER,0,DELIMTER,TEST_MSG_CONTENT,DELIMTER,timebuf,DELIMTER,tempSMS->sms_status,
			DELIMTER,1,DELIMTER,NOT_LONG,DELIMTER,0,DELIMTER,0,DELIMTER,0,DELIMTER,0,DELIMTER);
	psmfile_set_wrapper(PSM_MOD_SMS, NULL, SMSIndexBuf, savebuf,	ghandle);
	sms_free(savebuf);
	return new_Node;
}

int make_msg_full(int type)
{
	int total_num = 0;
	int cur_num = 0;
	char tmp[8] = {0};
	s_MrvFormvars *new_node = NULL;
	int before_sms_num = totalLocalSMSNum ;

	if(type == READED || type == UNREAD)
	{
		total_num = SupportSMSNum;
		cur_num = totalLocalSMSNum;
	}
	else if(type == SEND_SUCCESS || type == SEND_FAIL)
	{
		total_num = MaxSentSMSNum;
		cur_num = totalLocalSentNum;
	}
	else 
	{
		total_num = MaxDraftSMSNum;
		cur_num = totalLocalDraftNum;
	}

	while(cur_num < total_num)
	{
		new_node = SMS_new_test_node(type);
		
		if(type == READED || type == UNREAD)
		{
			 totalLocalSMSNum++;
			 SMS_insert_Node(&LocalSMS_list_start,&LocalSMS_list_last,&new_node,0);
		}
		else if(type == SEND_SUCCESS || type == SEND_FAIL)
		{
			 totalLocalSentNum++;
			 SMS_insert_Node(&LocalSendSMS_list_start,&LocalSendSMS_list_last,&new_node,0);
		}
		else 
		{
			 totalLocalDraftNum++;
			 SMS_insert_Node(&LocalDraftSMS_list_start,&LocalDraftSMS_list_last,&new_node,0);
		}
		cur_num++;
		Duster_module_Printf(1,	"%s total_num:%d,	cur_num:%d",	total_num,	cur_num);
	}
	memset(tmp,   '\0',   8);
	sprintf(tmp,  "%d",   totalLocalSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);
	
	memset(tmp,   '\0',   8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);
	
	memset(tmp,   '\0',   8);
	sprintf(tmp,  "%d",   totalLocalDraftNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp,	handle_SMS1);
	psm_commit_other__(handle_SMS1);

	if(before_sms_num < sms_full_num)
	{
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(1);
	}
	
	return 1;
}
int clear_msg_list(int type)
{	
	int before_sms_num = totalLocalSMSNum ;
	
	if(type == READED || type == UNREAD)
	{
		SMS_clear_list_raw_data(&LocalSMS_list_start,	&LocalSMS_list_last,	1);
		gDialer2UIPara.unreadLocalSMSNum = 0;
		totalLocalSMSNum = 0;
		psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_rev_num",	"0",	handle_SMS1);
		UIRefresh_Sms_Change();
	}
	else if(type == SEND_SUCCESS || type == SEND_FAIL)
	{
		SMS_clear_list_raw_data(&LocalSendSMS_list_start,	&LocalSendSMS_list_last,	2);
		totalLocalSentNum = 0;
		psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_send_num",	"0",	handle_SMS1);
		UIRefresh_Sms_Change();
	}
	else 
	{
		SMS_clear_list_raw_data(&LocalDraftSMS_list_start,	&LocalDraftSMS_list_last,	3);
		totalLocalDraftNum = 0;
		psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	"0",	handle_SMS1);
		UIRefresh_Sms_Change();
	}
	psm_commit_other__(handle_SMS1);
	
	if(before_sms_num >= sms_full_num)
	{
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(0);
	}
	return 1;
	
}
void sms_send_cmemfull_msg(UINT8 type)
{
	#ifndef NOTION_SMS_CMEFULL_DISABLE    //added by notion ggj 20160428 
	OSA_STATUS				 osa_status;
	WlanClientStatusMessage  msg_buf;
	
	msg_buf.MsgId = sendAT;
	msg_buf.MsgData = (CHAR *)malloc(32);	
	DUSTER_ASSERT(msg_buf.MsgData);
	memset(msg_buf.MsgData,	0,	32);
	snprintf(msg_buf.MsgData,	32,	"AT+CMEMFULL=%d\r\n",	type);
	osa_status = OSAMsgQSend(gWlanIndMSGQ, sizeof(WlanClientStatusMessage), &msg_buf, OSA_NO_SUSPEND);
	#endif
}
/*add API for Notion begin */
/*=======================================
	storage : 0 --SIM
			   1 -- Device
==========================================*/
UINT16 get_unread_sms_number(unsigned int storage)
{
	UINT16 unread = 0;

	if(storage == 0)
	{
		unread = SMS_calc_unread_List(SimSMS_list_start,	SimSMS_list_last);
	}
	else if(storage == 1)
	{
		unread = SMS_calc_unread_List(LocalSMS_list_start,	LocalSMS_list_last);
	}
	else if(storage == 3)
	{
		unread += SMS_calc_unread_List(SimSMS_list_start,	SimSMS_list_last);
		unread += SMS_calc_unread_List(LocalSMS_list_start,	LocalSMS_list_last);
	}
	return unread;
}
/*========================================================
	Get the last recved unread msg, and set to read status
	Input:
		storage : 0 --SIM
				  1 -- device
		smsNumber: the sms number
		timeStamp: 15,06,22,16,07,56,+8 length: 24
		smsContent: UTF-8 length: 1200; format: EF 80 80
	Return:
		O -- Get fail
		1 -- OK
	Notice: If the buffer length is not enough, Crash will happen.
=========================================================*/
unsigned int get_unread_sms_record(unsigned int storage, unsigned char *smsNumber,
                                   unsigned char *timeStamp,	unsigned char *smsContent)
{
	s_MrvFormvars *list_start = NULL;
	s_MrvFormvars *list_last = NULL;
	s_MrvFormvars *list_node = NULL;
	SMS_Node * sms_data  = NULL;
	char *sms_str = NULL;
	char indexbuf[8] = {0};
	char *p1 = NULL,*p2 = NULL, *p3 = NULL,*p4 =NULL,*p5 = NULL,*p6 = NULL;

	UINT16	unread = 0;

	if(storage == 0)
	{
		list_start = SimSMS_list_start;
		list_last = SimSMS_list_last;
	}
	else if(storage == 1)
	{
		list_start = LocalSMS_list_start;
		list_last = LocalSMS_list_last;
	}
	else
	{
		return 0;
	}

	unread = SMS_calc_unread_List(list_start,	list_last);
	if(unread == 0)
		return 0;

	list_node = list_start;
	while(list_node)
	{
		sms_data = (SMS_Node*)list_node->value;
		if(sms_data->LSMSinfo && sms_data->LSMSinfo->LSMSflag == START)
		{
			if(sms_data->sms_status == UNREAD)
			{
				//set to read
				while(list_node)
				{
					sms_data = (SMS_Node*)list_node->value;
					if(storage == 0)
					{
						memset(indexbuf,	0,	sizeof(indexbuf));
						snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"SRCV",	sms_data->psm_location);
						sms_str = psm_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf);
					}
					else
					{
						memset(indexbuf,	0,	sizeof(indexbuf));
						snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"LRCV",	sms_data->psm_location);
						sms_str = psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf,	handle_SMS1);
					}
					if(sms_str)
					{

						p1 = strchr(sms_str, DELIMTER);
						p2 = strchr(p1+1,DELIMTER);
						p3 = strchr(p2+1,DELIMTER);
						p4 = strchr(p3+1,DELIMTER);
						p5 = strchr(p4+1,DELIMTER);
						p6 = strchr(p5+1,DELIMTER);

						if(p1&&p2&&p3&&p4&&p5&&p6)
						{
							memcpy(smsContent +	strlen(smsContent),	p3+1,	p4-p3-1);
							if(sms_data->LSMSinfo->LSMSflag != END)
							{
								if(sms_str)
									sms_free(sms_str);
								sms_str = NULL;
								SMS_read_By_index(sms_data->psm_location,	list_start,	list_last);
							}
							else if(sms_data->LSMSinfo->LSMSflag == END)
							{
								str_replaceChar(smsContent,SEMI_REPLACE, ';');
								str_replaceChar(smsContent,EQUAL_REPLACE,'=');
								memcpy(smsNumber, p1+1,p2-p1-1);/*srcAddr*/
								memcpy(timeStamp, p4+1,	p5-p4-1);
								if(sms_str)
									sms_free(sms_str);
								sms_str = NULL;
								SMS_read_By_index(sms_data->psm_location,	list_start,	list_last);
								break;
							}
						}
						else
						{
							if(sms_str)
								sms_free(sms_str);
							return	0;
						}
					}
					list_node = list_node->next;
				}
				break;
			}
			else
			{
				while(list_node)
				{
					sms_data = (SMS_Node*)list_node->value;
					if(sms_data->LSMSinfo)
					{
						if(sms_data->LSMSinfo->LSMSflag == END)
							break;
					}
					else
						break;
					list_node = list_node->next;
				}
			}
		}
		else
		{
			if(sms_data->sms_status == UNREAD)
			{
				//Set to read
				if(storage == 0)
				{
					memset(indexbuf,	0,	sizeof(indexbuf));
					snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"SRCV",	sms_data->psm_location);
					sms_str = psm_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf);
				}
				else
				{
					memset(indexbuf,	0,	sizeof(indexbuf));
					snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"LRCV",	sms_data->psm_location);
					sms_str = psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf,	handle_SMS1);
				}
				if(sms_str)
				{

					p1 = strchr(sms_str, DELIMTER);
					p2 = strchr(p1+1,DELIMTER);
					p3 = strchr(p2+1,DELIMTER);
					p4 = strchr(p3+1,DELIMTER);
					p5 = strchr(p4+1,DELIMTER);
					p6 = strchr(p5+1,DELIMTER);

					if(p1&&p2&&p3&&p4&&p5&&p6)
					{
						memcpy(smsContent +	strlen(smsContent),	p3+1,	p4-p3-1);
						str_replaceChar(smsContent,SEMI_REPLACE, ';');
						str_replaceChar(smsContent,EQUAL_REPLACE,'=');
						memcpy(smsNumber, p1+1,p2-p1-1);/*srcAddr*/
						memcpy(timeStamp, p4+1,	p5-p4-1);
					}
					sms_free(sms_str);
					sms_str = NULL;
					SMS_read_By_index(sms_data->psm_location,	list_start, list_last);
				}
				break;
			}
		}
		list_node = list_node->next;
	}
	return 1;
}
/*add API for Notion end */

/*added by notion ggj 20160119 for JSON data */
#ifdef NOTION_ENABLE_JSON
void sms_set_get_all_num(UINT16* p_total_SIMSMS_Long_num,UINT16* p_total_unread_msg_num,UINT16* p_total_complete_single)
{
	char  	       tmp[8]= {0};	
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	UINT16			before_local_num = 0;
	before_local_num = totalLocalSMSNum;
	Duster_module_Printf(3,"enter %s",__FUNCTION__);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   SupportSMSNum + MaxDraftSMSNum + MaxSentSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_total", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   SupportSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_total", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);

	memset(tmp,   '\0',	  8);
	total_complete_single = SMS_calc_recv_complete_single(LocalSMS_list_start,	LocalSMS_list_last);
	sprintf(tmp,  "%d",   total_complete_single);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num_complete", tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   MaxSentSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   MaxDraftSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalDraftNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   gMem1TotalNum);	
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_total",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_num",	  tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimSentSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_send_total",   tmp);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalSimDraftSmsNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_draftbox_total",   tmp);


	totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalNum_LongSMS);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_long_num", tmp);

	totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum_LongSMS);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_long_num", tmp);

	total_SIMSMS_Long_num= SMS_calc_Long(SimSMS_list_start, SimSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_SIMSMS_Long_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_sim_long_num", tmp);

	total_unread_msg_num = SMS_calc_unread();
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_unread_msg_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_unread_long_num", tmp);

	total_unread_msg_num = SMS_calc_unread_List(LocalSMS_list_start,LocalSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   total_unread_msg_num);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_unread_long_num_device", tmp);

	*p_total_complete_single = total_complete_single;
	*p_total_SIMSMS_Long_num = total_SIMSMS_Long_num;
	*p_total_unread_msg_num = total_unread_msg_num;
	
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);
	psm_commit__();
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,before_local_num is %d,sms_full_num is %d",__FUNCTION__,totalLocalSMSNum,before_local_num,sms_full_num);
	if((before_local_num < sms_full_num) && (totalLocalSMSNum >= sms_full_num))
	{
		Duster_module_Printf(3,"%s,	sms_send_cmemfull_msg(1); totalLocalSMSNum is %d,before_local_num is %d,sms_full_num is %d",__FUNCTION__,totalLocalSMSNum,before_local_num,sms_full_num);
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(1);
	}
	else if((before_local_num >= sms_full_num) && (totalLocalSMSNum < sms_full_num))
	{
		Duster_module_Printf(3,"%s,	sms_send_cmemfull_msg(0); totalLocalSMSNum is %d,before_local_num is %d,sms_full_num is %d",__FUNCTION__,totalLocalSMSNum,before_local_num,sms_full_num);	
		/*SEND CMEMFULL*/
		sms_send_cmemfull_msg(0);
	}	
	Duster_module_Printf(3,"leave %s",__FUNCTION__);
}
void Sms_json_data_get_newSmsNum(int* p_new_num ,UINT16* p_total_unread_msg_num)
{
	int	new_num = 0;
	UINT16 total_unread_msg_num = 0;
	new_num = SMS_get_new_sms_num_for_longsms();
	SMS_set_sms_zero();
	total_unread_msg_num = SMS_calc_unread();
	*p_new_num = new_num;
	*p_total_unread_msg_num = total_unread_msg_num;
	Duster_module_Printf(3,"%s:  new_num:%d,total_unread_msg_num:%d",__FUNCTION__,new_num,total_unread_msg_num);
}

int SMS_json_data_get_displaylist(char * sms_json_data, UINT8 mem_store,UINT8 page_num,UINT8 data_per_page,UINT8 tags)
{
	char 			*str = NULL;
	char			 SMS_index[12] = {0}, Item_index_buf[12] = {0};
	char			 tmp[20] = {0};
	UINT32			 total_sms_num = 0;
	OSA_STATUS		 osa_status = 0;
	UINT32			 localSMS_skipNum = 0,shownLocalSMS = 0,total_page_num = 0;
	s_MrvFormvars 		*tempNode = NULL,*list_start=NULL,*list_last=NULL;
	SMS_Node	 	*tempSMS;
	char			 LongSMS_index[100] = {0};
	char			 tmpSubject[400];
	char			*p1 = NULL,*p2 = NULL,*p3 = NULL,*p4 = NULL,*p5 = NULL,*p6 = NULL,*tmp_str = NULL;
	char 			*ucs_char = NULL,	*buff_SMSSubject = NULL;
	UINT16			 ghandle = 0;
	int 		     ret     = 0;
	MrvXMLElement 	*pMessage_list = NULL,*pNewTtemEle = NULL,*pnewChild = NULL;
	UINT8			 Item_index = 1;
	char 			 *find_name = NULL,	*add_name = NULL,	*add_name_char = NULL;
	int 			  name_len = 0;
	int 			  nIndex = 0;
	UINT8			  code_type = 255;
	char 			 *p7 = NULL, *p8 = NULL, *p9 = NULL, *p10 = NULL, *p11 = NULL, *p12 = NULL,*p13 = NULL;
	UINT8			  class_type = 255;
	int					sent_status = 0;

	Duster_module_Printf(3,"enter %s",__FUNCTION__);
	Duster_module_Printf(3,"%s,mem_store=%d,page_num=%d,data_per_page=%d,tags=%d",__FUNCTION__,mem_store,page_num,data_per_page,tags);

	Duster_module_Printf(3,"%s: try to get WebSMSSema (%d)",__FUNCTION__, WebSMSSema);
	osa_status = OSASemaphoreAcquire(WebSMSSema, OS_SUSPEND);
	DUSTER_ASSERT(osa_status == OS_SUCCESS );
	Duster_module_Printf(3,"%s: get WebSMSSema",__FUNCTION__);

	/*notify led to stop blink start*/
	SMS_get_new_sms_num_led();
	SMS_set_sms_zero_led();
	UIRefresh_Sms_Change();
	/*notify led to stop blink start*/

	if(mem_store == 0)
	{
		if(totalSimSmsNum == 0)
		{
			Duster_module_Printf(3,"%s there're no sim sms",__FUNCTION__);
			memset( tmp, '\0', 	20);
			sprintf(tmp, "%d",	0);
			goto EXIT;
		}
		else if(totalSimSmsNum > 0)
		{
			list_start		 = SimSMS_list_start;
			list_last		 = SimSMS_list_last;
			total_sms_num	 = SMS_calc_Long(SimSMS_list_start,	SimSMS_list_last);
		}
	}
	if(mem_store == 1)
	{
		if(tags == 12) /*get recv sms*/
		{
			totalLocalNum_LongSMS = SMS_calc_Long(LocalSMS_list_start,LocalSMS_list_last);
			if(totalLocalNum_LongSMS == 0)
			{
				Duster_module_Printf(3,"%s there're no local recv sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalNum_LongSMS > 0)
			{
				list_start	   = LocalSMS_list_start;
				list_last	   = LocalSMS_list_last;
				total_sms_num  = totalLocalNum_LongSMS;
			}

		}
		else if(tags == 2)/* get local sent sms*/
		{
			totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
			if(totalLocalSentNum == 0 )
			{
				Duster_module_Printf(3,"%s there're no local sent sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalSentNum >0 )
			{
				list_start		 = LocalSendSMS_list_start;
				list_last		 = LocalSendSMS_list_last;
				total_sms_num	 = totalLocalSentNum_LongSMS;
			}
		}
		else if(tags == 11)/*get local draft sms*/
		{
			if(totalLocalDraftNum == 0)
			{
				Duster_module_Printf(3,"%s there're no local draft sms",__FUNCTION__);
				goto EXIT;
			}
			else if(totalLocalDraftNum > 0)
			{
				list_start		 = LocalDraftSMS_list_start;
				list_last		 = LocalDraftSMS_list_last;
				total_sms_num	 = totalLocalDraftNum;
			}
		}
	}


	total_page_num = total_sms_num/data_per_page;
	if(total_sms_num%data_per_page)
		total_page_num++;

	memset( tmp, '\0',   	20);
	sprintf(tmp, "%d",	total_page_num);
	psm_set_wrapper(PSM_MOD_SMS,"get_message","total_number",tmp);

	if(total_sms_num <= (page_num - 1)*SupportdisplaySMSNum)
	{
		Duster_module_Printf(3,	"%s page number wrong",	__FUNCTION__);
		goto EXIT;
	}
	if(total_sms_num>(page_num - 1)*SupportdisplaySMSNum)
	{
		localSMS_skipNum = (page_num-1)*SupportdisplaySMSNum;
		tempNode		 =  list_start;
		while(localSMS_skipNum && tempNode)
		{
			tempSMS = (SMS_Node*)tempNode->value;
			if(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == START))
			{
				while(tempNode)
				{
					tempSMS  = (SMS_Node*)tempNode->value;
					if((tempSMS->isLongSMS) && tempSMS->LSMSinfo->LSMSflag == END)
						break;
					tempNode = tempNode->next;
				}
			}
			localSMS_skipNum--;
			if(!tempNode)
				break;
			tempNode  = tempNode->next;
		}
	}

	buff_SMSSubject      =  sms_malloc(MAX_LSMS_SEGMENT_NUM<<9);
	DUSTER_ASSERT(buff_SMSSubject);
	memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);

	shownLocalSMS = 0;
	while(tempNode)
	{
		tempSMS = (SMS_Node*)tempNode->value;
		if((!tempSMS->isLongSMS)/*||	(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == ONE_ONLY))*/) //not long sms or long sms just one segment
		{
			memset(SMS_index,	'\0',	12);
			if(mem_store == 0)//sim
			{
				sprintf(SMS_index,"%s%d","SRCV",tempSMS->psm_location);
				Duster_module_Printf(3,"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
				Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 12)&&(mem_store == 1))//local
			{
				sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(3,	"%s:SMS_index is %s",__FUNCTION__, SMS_index);
				ret = getPSM_index(&(tempSMS->psm_location),&ghandle,1);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
				Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 11)&&(mem_store == 1))
			{

				sprintf(SMS_index,"%s%d","LDRT",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(3,"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
				Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
			}
			else if((tags == 2)&&(mem_store == 1))
			{
				sprintf(SMS_index,"%s%d","LSNT",tempSMS->psm_location); //Local SMS
				Duster_module_Printf(3,	"%s:SMS_index is %s,not LongSMS",__FUNCTION__, SMS_index);
				str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
				Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
			}

			if(str)
			{
				p1 = strchr(str, DELIMTER);
				p2 = strchr(p1+1,DELIMTER);
				p3 = strchr(p2+1,DELIMTER);
				p4 = strchr(p3+1,DELIMTER);
				p5 = strchr(p4+1,DELIMTER);
				p6 = strchr(p5+1,DELIMTER);
				p7 = strchr(p6+1,DELIMTER );
				p8 = strchr(p7+1, DELIMTER);
				p9 = strchr(p8+1, DELIMTER);
				p10 = strchr(p9+1, DELIMTER);
				p11 = strchr(p10+1, DELIMTER);
				p12 = strchr(p11+1, DELIMTER);
                            p13 = strchr(p12+1, DELIMTER); 
				if(p1&&p2&&p3&&p4&&p5&&p6)
				{
					tmp_str = sms_malloc(p1 - str +1);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p1 - str +1);
					memcpy(tmp_str,   str, p1-str);/*index*/
					strcat(sms_json_data,"{\"index\":\"");
					strcat(sms_json_data,tmp_str);
					strcat(sms_json_data,"\",");
                                   sms_free(tmp_str);
					tmp_str = sms_malloc(p2-p1);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p2-p1);
					memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/

					/*search match name in Phonebook*/
					find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);

					/*name is xml is consisted as : name;number*/
					if(tmp_str)
					{
						name_len = strlen(find_name) + strlen(tmp_str) + 1;	/* 1 octs for ' ; '*/
					}
					add_name = sms_malloc(name_len + 1);
					DUSTER_ASSERT(add_name);
					memset(add_name,	'\0',	name_len + 1);
					if(find_name)
					{
						strcat(add_name,	find_name);
					}
					strcat(add_name,	";");
					strcat(add_name,	tmp_str);
					sms_free(tmp_str);
					add_name_char = utf8_to_unicode_char(add_name);
					sms_free(add_name);
					strcat(sms_json_data,"\"from\":\"");
					strcat(sms_json_data,add_name_char);
					strcat(sms_json_data,"\",");
					sms_free(add_name_char);

					
					memset(tmpSubject,      '\0',    400 );
					memset(buff_SMSSubject, '\0',    MAX_LSMS_SEGMENT_NUM<<9);
					if((p4-p3-1)>400)
					{
						Duster_module_Printf(3,"%s subject too long:%d",__FUNCTION__,(p4-p3-1));
						DUSTER_ASSERT(0);
					}
					memcpy(tmpSubject,      p3+1, p4-p3-1);
					tmpSubject[399] = '\0';
					str_replaceChar(tmpSubject, SEMI_REPLACE,  ';');
					str_replaceChar(tmpSubject, EQUAL_REPLACE, '=');
					ucs_char = utf8_to_unicode_char(tmpSubject);
					DUSTER_ASSERT(ucs_char);
					strcat(sms_json_data,"\"subject\":\"");
					strcat(sms_json_data,ucs_char);
					strcat(sms_json_data,"\",");
                                   sms_free(ucs_char);

					
					/*sms_time*/
					tmp_str = sms_malloc(p5-p4);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p5-p4);
					memcpy(tmp_str,	p4+1,	p5-p4-1);
					strcat(sms_json_data,"\"received\":\"");
					strcat(sms_json_data,tmp_str);
					strcat(sms_json_data,"\",");
					sms_free(tmp_str);

					/*sms_status*/
					tmp_str = sms_malloc(p6-p5);
					DUSTER_ASSERT(tmp_str);
					memset(tmp_str,	'\0',	p6-p5);
					memcpy(tmp_str,	p5+1,	p6-p5-1);
					strcat(sms_json_data,"\"status\":\"");
					strcat(sms_json_data,tmp_str);
					strcat(sms_json_data,"\",");
					sms_free(tmp_str);

                                   /*message_type*/ 
					memset( tmp, '\0', 	20);
					DUSTER_ASSERT(( p3 - p2 -1) <	20);
					memcpy(tmp,	p2 + 1,	p3 - p2 -1);
					code_type = atoi(tmp);
					strcat(sms_json_data,"\"message_type\":\"");
					if((code_type != 3) && (code_type != 4) && (code_type != 6) )
					{
						strcat(sms_json_data,"0");    /*SMS*/
					}
					else if(code_type == 3)
					{
						strcat(sms_json_data,"1"); 	/*MMS*/
					}
					else if(code_type == 4)
					{
						strcat(sms_json_data,"2");	/*WAP-PUSH*/
					}
					else if(code_type == 6)
					{
						strcat(sms_json_data,"3");	/*VOICE MAIL*/
					}
               
					/*class_type*/ 
					if(tags == 12)
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p12 - p11 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"class_type\":\"");
							strcat(sms_json_data,tmp);
						}

						if(p12 && p13)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p13 - p12 -1) <	20);
							memcpy(tmp,	p12 + 1,	p13 - p12 -1);
							strcat(sms_json_data,"\"message_time_index\":\"");
							strcat(sms_json_data,tmp);							
						}
					}
					else if(tags == 2)
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p12 - p11 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"message_time_index\":\"");
							strcat(sms_json_data,tmp);
						}					
					}
					else
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p11 - p12 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"class_type\":\"");
							strcat(sms_json_data,tmp);
						}						
					}
					strcat(sms_json_data,"\"}");
					if(str)
					{
						sms_free(str);
						str = NULL;
					}	
				}
				else
				{
					Duster_module_Printf(3,"%s get sms from psm wrong",__FUNCTION__);
					if(str)
					{
						sms_free(str);
						str = NULL;
					}
					tempNode = tempNode->next;
					continue;
				}
				if(str)
				{
					sms_free(str);
					str = NULL;
				}
			}
			else if(!str)
			{
				Duster_module_Printf(3,"%s can't get SMS_index %s",__FUNCTION__,SMS_index);
				tempNode = tempNode->next;
				continue;
			}
			shownLocalSMS++;
		}
		else if(tempSMS->isLongSMS	&&	(tempSMS->LSMSinfo->LSMSflag == START))//long SMS more than one segment
		{
			if(1)
			{
				Duster_module_Printf(3,"%s  isRecvAll:%d,totalSegment:%d,receivedNum:%d",__FUNCTION__,tempSMS->LSMSinfo->isRecvAll,tempSMS->LSMSinfo->totalSegment,tempSMS->LSMSinfo->receivedNum);			         
				if(!tempSMS->LSMSinfo->isRecvAll && (tempSMS->LSMSinfo->totalSegment <= MAX_LSMS_SEGMENT_NUM) && (tempSMS->LSMSinfo->receivedNum <= MAX_LSMS_SEGMENT_NUM) )
				{
					tempNode = tempNode->next;
					while(tempNode)
					{
						tempSMS=(SMS_Node*)tempNode->value;
						if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							break;
						}
						tempNode=tempNode->next;
					}
					tempNode=tempNode->next;
					continue;
				}
			}
			sent_status = 0;
			memset(LongSMS_index,	'\0',	100);
			memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);
			while(tempNode)
			{
				tempSMS=(SMS_Node*)tempNode->value;
				memset(SMS_index,	'\0',	12);
				if(mem_store == 0)//sim
				{
					sprintf(SMS_index,"%s%d","SRCV",tempSMS->psm_location);
					str = psm_get_wrapper(PSM_MOD_SMS, NULL, SMS_index);
					Duster_module_Printf(3,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 12)&&(mem_store == 1))//local
				{
					sprintf(SMS_index,"%s%d","LRCV",tempSMS->psm_location); //Local SMS
					ret = getPSM_index(&(tempSMS->psm_location),&ghandle,1);
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,ghandle);
					Duster_module_Printf(3,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 11)&&(mem_store == 1))
				{

					sprintf(SMS_index,"%s%d","LDRT",tempSMS->psm_location); //Local SMS
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
					Duster_module_Printf(3,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
				}
				else if((tags == 2)&&(mem_store == 1))
				{
					sprintf(SMS_index,"%s%d","LSNT",tempSMS->psm_location); //Local SMS
					str = psmfile_get_wrapper(PSM_MOD_SMS, NULL, SMS_index,handle_SMS1);
					Duster_module_Printf(3,"%s:SMS_index is %s, LongSMS",__FUNCTION__, SMS_index);
					Duster_module_Printf(3,"%s,str is %s",__FUNCTION__,str);
				}

				if(tags == 2)
				{
					Duster_module_Printf(3,	"tempSMS->sms_status :%d",	tempSMS->sms_status);
					if(tempSMS->sms_status == SEND_SUCCESS)
					{
						sent_status++;
					}
				}
				
				if(str)
				{

					p1 = strchr(str, DELIMTER);
					p2 = strchr(p1+1,DELIMTER);
					p3 = strchr(p2+1,DELIMTER);
					p4 = strchr(p3+1,DELIMTER);
					p5 = strchr(p4+1,DELIMTER);
					p6 = strchr(p5+1,DELIMTER);
					p7 = strchr(p6+1,DELIMTER );
					p8 = strchr(p7+1, DELIMTER);
					p9 = strchr(p8+1, DELIMTER);
					p10 = strchr(p9+1, DELIMTER);
					p11 = strchr(p10+1, DELIMTER);
					p12 = strchr(p11+1, DELIMTER);
					p13 = strchr(p12+1, DELIMTER);
					if(p1&&p2&&p3&&p4&&p5&&p6)
					{
						strcat(LongSMS_index, SMS_index);
						strcat(LongSMS_index,",");
						memset(tmpSubject,    '\0',  400);
						if((p4-p3-1)>400)
						{
							Duster_module_Printf(3,"%s content too long",__FUNCTION__);
							DUSTER_ASSERT(0);
						}
						if(tempSMS->LSMSinfo->LSMSflag == START)
						{
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p3 - p2 -1) <	20);
							memcpy(tmp,	p2 + 1,	p3 - p2 -1);
							code_type = atoi(tmp);
						}
						memcpy(buff_SMSSubject+strlen(buff_SMSSubject),	p3+1,	p4-p3-1);
						if(tempSMS->LSMSinfo->LSMSflag!=END)
						{
							tempNode=tempNode->next;
							if(str)
								sms_free(str);
							str = NULL;
						}
						else if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							str_replaceChar(buff_SMSSubject,SEMI_REPLACE, ';');
							str_replaceChar(buff_SMSSubject,EQUAL_REPLACE,'=');

							ucs_char = utf8_to_unicode_char(buff_SMSSubject);
							DUSTER_ASSERT(ucs_char);
							strcat(sms_json_data,"{\"index\":\"");
							strcat(sms_json_data,LongSMS_index);
							strcat(sms_json_data,"\",");
							
							/*from*/
							tmp_str = sms_malloc(p2-p1);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str,	'\0',	p2-p1);
							memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/

							/*search match name in Phonebook*/
							find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);

							/*name is xml is consisted as : name;number*/
							if(tmp_str)
							{
								name_len = strlen(find_name) + strlen(tmp_str) + 1; /* 1 octs for ' ; '*/
							}
							add_name = sms_malloc(name_len + 1);
							DUSTER_ASSERT(add_name);
							memset(add_name,	'\0',	name_len + 1);
							if(find_name)
							{
								strcat(add_name,	find_name);
							}
							strcat(add_name,	";");
							strcat(add_name,	tmp_str);
							sms_free(tmp_str);
							add_name_char = utf8_to_unicode_char(add_name);
							sms_free(add_name);
							strcat(sms_json_data,"\"from\":\"");
							strcat(sms_json_data,add_name_char);
							strcat(sms_json_data,"\",");
							sms_free(add_name_char);

							//subject
							strcat(sms_json_data,"\"subject\":\"");
							strcat(sms_json_data,ucs_char);
							strcat(sms_json_data,"\",");
                                                 sms_free(ucs_char);
							
							/*sms_time*/
							tmp_str = sms_malloc(p5-p4);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str, '\0',	p5-p4);
							memcpy(tmp_str, p4+1,	p5-p4-1);
							strcat(sms_json_data,"\"received\":\"");
							strcat(sms_json_data,tmp_str);
							strcat(sms_json_data,"\",");
                                                 sms_free(tmp_str);  
							
							/*sms_status*/
							tmp_str = sms_malloc(p6-p5);
							DUSTER_ASSERT(tmp_str);
							memset(tmp_str, '\0',	p6-p5);
							memcpy(tmp_str, p5+1,	p6-p5-1);
							strcat(sms_json_data,"\"status\":\"");
							if( tags == 2)
							{
								Duster_module_Printf(3,	"sent_status:%d,totalSegment:%d",	sent_status,	tempSMS->LSMSinfo->totalSegment);
								if((sent_status < tempSMS->LSMSinfo->totalSegment) || (sent_status > 5))
								{
									strcat(sms_json_data,"2");
								}
								else 
								{
									strcat(sms_json_data,"3");
								}
							}
							else
								strcat(sms_json_data,tmp_str);
							strcat(sms_json_data,"\",");
							sms_free(tmp_str);	

							Duster_module_Printf(3,	"%d code_type is %d",	__FUNCTION__,	code_type);
							//message_type
							strcat(sms_json_data,"\"message_type\":\"");
							if((code_type != 3) && (code_type != 4) && (code_type != 6) )
							{
								strcat(sms_json_data,"0");    /*SMS*/
							}
							else if(code_type == 3)
							{
								strcat(sms_json_data,"1"); 	/*MMS*/
							}
							else if(code_type == 4)
							{
								strcat(sms_json_data,"2");	/*WAP-PUSH*/
							}
							else if(code_type == 6)
							{
								strcat(sms_json_data,"3");	/*VOICE MAIL*/
							}
							/*class_type*/
					if(tags == 12)
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p12 - p11 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"class_type\":\"");
							strcat(sms_json_data,tmp);
						}

						if(p12 && p13)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p13 - p12 -1) <	20);
							memcpy(tmp,	p12 + 1,	p13 - p12 -1);
							strcat(sms_json_data,"\"message_time_index\":\"");
							strcat(sms_json_data,tmp);							
						}
					}
					else if(tags == 2)
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p12 - p11 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"message_time_index\":\"");
							strcat(sms_json_data,tmp);
						}					
					}
					else
					{
						if(p11 && p12)
						{
							strcat(sms_json_data,"\",");					
							memset( tmp, '\0', 	20);
							DUSTER_ASSERT(( p11 - p12 -1) <	20);
							memcpy(tmp,	p11 + 1,	p12 - p11 -1);
							strcat(sms_json_data,"\"class_type\":\"");
							strcat(sms_json_data,tmp);
						}						
					}
					
							/*class_type*/
							strcat(sms_json_data,"\"}");
							if(str)
								sms_free(str);
							str = NULL;
							break;
						}
					}
					else
					{
						Duster_module_Printf(3,"%s SMS_index get wrong",__FUNCTION__,SMS_index);
						if(str)
						{
							sms_free(str);
							str = NULL;
						}
						tempNode = tempNode->next;
						continue;
					}
					if(str)
					{
						sms_free(str);
						str = NULL;
					}
				}
				else if(!str)
				{
					Duster_module_Printf(3,"%s SMS_index is %s not found,error!",__FUNCTION__,SMS_index);
					tempNode=tempNode->next;
					while(tempNode)
					{
						tempSMS=(SMS_Node*)tempNode->value;
						if(tempSMS->LSMSinfo->LSMSflag == END)
						{
							break;
						}
						tempNode=tempNode->next;
					}
					tempNode=tempNode->next;
					continue;
				}
			}
			shownLocalSMS++;
		}
		else if(tempSMS->isLongSMS&&(tempSMS->LSMSinfo->LSMSflag == ONE_ONLY))	
		{
			tempNode=tempNode->next;
			continue;
		}
		else
		{
			/*add log to trace SMS page ERROR*/
			Duster_module_Printf(3,"%s SMS_ERROR, tempSMS->isLongSMS is %d",__FUNCTION__,tempSMS->isLongSMS);
			Duster_module_Printf(3,"%s SMS_ERROR, tempSMS->psmlocation is %d",__FUNCTION__,tempSMS->psm_location);
			if(tempSMS->isLongSMS && tempSMS->LSMSinfo)
			{
				Duster_module_Printf(3,"%s SMS_ERROR, tempSMS->LSMSinfo->LSMSflag is %d",__FUNCTION__,tempSMS->LSMSinfo->LSMSflag);
			}
		}
		if(shownLocalSMS >= SupportdisplaySMSNum)
			break;
		tempNode=tempNode->next;
		if(tempNode!=NULL)
		{
			strcat(sms_json_data,",");
		}	
	}
EXIT:
	if(buff_SMSSubject)
		sms_free(buff_SMSSubject);
	OSASemaphoreRelease(WebSMSSema);
	Duster_module_Printf(3,"%s: release WebSMSSema",__FUNCTION__);
	Duster_module_Printf(3,"leave %s",__FUNCTION__);
}
void Sms_json_data_getDeviceSms(char* p_message_flag,int page_number, char* sms_json_data, int* p_sms_nv_total, int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draft_total,int* p_sms_nv_draft_num)
{
	char *str = NULL;
	UINT16	page_num = 0,	data_per_page = 0;
	UINT8	mem_store = 1 ,	tags = 0 ;          
	data_per_page = SupportdisplaySMSNum;
	str = p_message_flag;
	Duster_module_Printf(3,"enter %s",__FUNCTION__);	
	if(str&&strncmp(str,	"",	1))
	{
		Duster_module_Printf(3,"%s, message_flag is %s",__FUNCTION__,str);
		page_num = page_number;
		if(!strcasecmp(str , "GET_RCV_SMS_LOCAL"))
		{
			mem_store 	 =	  1;
			tags		 =    12;
		}
		else if(!strcasecmp(str , "GET_DRAFT_SMS"))
		{
			mem_store =  1;
			tags	   =  11;
		}
		else if(!strcasecmp(str , "GET_SENT_SMS_LOCAL"))
		{
			mem_store =  1;
			tags		=  2;
		}
		if(tags != 0)
			SMS_json_data_get_displaylist(sms_json_data, mem_store, page_num, data_per_page, tags);
	}  
	
       *p_sms_nv_total = SupportSMSNum + MaxDraftSMSNum + MaxSentSMSNum;//sms_capacity_info-->sms_nv_total
       *p_sms_nv_rev_total = SupportSMSNum;//sms_capacity_info-->sms_nv_rev_total
       *p_sms_nv_send_total = MaxSentSMSNum;//sms_capacity_info-->sms_nv_send_total    
       *p_sms_nv_draft_total = MaxDraftSMSNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;//sms_capacity_info-->sms_nv_rev_num
       *p_sms_nv_send_num = totalLocalSentNum;//sms_capacity_info-->sms_nv_send_num 
       *p_sms_nv_draft_num = totalLocalDraftNum;
        Duster_module_Printf(3,"leave %s",__FUNCTION__);
}

void Sms_json_data_getSimSms(int page_number, char* sms_json_data, int* p_sms_sim_total, int* p_sms_sim_num,int* p_sms_sim_long_num)
{
	char *str = NULL;
	UINT16	page_num = 0,	data_per_page = 0;
	UINT8	mem_store = 1 ,	tags = 0 ;
	int 	total_SIMSMS_Long_num = 0;
	data_per_page = SupportdisplaySMSNum;
	page_num = page_number;
	mem_store =	  0;
	tags	  =	  12;
	Duster_module_Printf(3,"enter %s",__FUNCTION__);
	if(sms_json_data)
	{
		SMS_json_data_get_displaylist(sms_json_data, mem_store, page_num, data_per_page, tags);
	}
	if(p_sms_sim_total)
	{
       	*p_sms_sim_total = gMem1TotalNum; // sms_capacity_info-->sms_sim_total
       	Duster_module_Printf(3,"%s,sms_sim_total :%d",__FUNCTION__,*p_sms_sim_total);
	}
	if(p_sms_sim_num)
	{
       	*p_sms_sim_num = totalSimSmsNum; // sms_capacity_info-->sms_sim_num	
       	Duster_module_Printf(3,"%s,sms_sim_num :%d",__FUNCTION__,*p_sms_sim_num);       	
	}
       if(p_sms_sim_long_num)
       {
       	total_SIMSMS_Long_num= SMS_calc_Long(SimSMS_list_start, SimSMS_list_last);
		*p_sms_sim_long_num = total_SIMSMS_Long_num;
       	Duster_module_Printf(3,"%s,sms_sim_long_num :%d",__FUNCTION__,*p_sms_sim_long_num);       			
       } 
	Duster_module_Printf(3,"leave %s",__FUNCTION__);
}

void Sms_json_data_sendSms(char* p_contacts,char* p_content,char* p_encode_type,char* p_sms_time,char* p_send_from_draft_id,char* p_sms_cmd_status_result,char* p_sms_cmd,int* p_sms_nv_send_num)
{
	char	 	 *number=NULL,*sms_time=NULL,*messageBody=NULL,*id=NULL,*encode_type=NULL,*location=NULL,*time_formated = NULL,*send_from_draft_id = NULL;
	s_MrvFormvars     **list_start = NULL,**list_last=NULL;
	int 	      ret = 0;
	char result_Str[3];
	char  	       tmp[8]= {0};	
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	
	number      = p_contacts;
	messageBody = p_content;
	encode_type = p_encode_type;
	sms_time    = p_sms_time;
	send_from_draft_id = p_send_from_draft_id;
	Duster_module_Printf(3,"%s number is %s,encode_type is %s,sms_time is %s",__FUNCTION__,number,encode_type,sms_time);
	if(number&&messageBody&&encode_type&&sms_time)
	{
		time_formated = MrvCgiUnescapeSpecialChars(sms_time);
		list_start = &LocalSendSMS_list_start;
		list_last  = &LocalSendSMS_list_last;
		if(!strchr(number,	','))/*Send SingSMS*/
		{
			ret = SMS_Send(number,messageBody,encode_type,time_formated,list_start,list_last,1,0);
			Duster_module_Printf(3,	"%s ret is %d",	__FUNCTION__,	ret);
		}
		else if(strchr(number,	','))
		{
			ret = SMS_massSend(number,messageBody,encode_type,time_formated,list_start,list_last,1,0);
		}
		if(send_from_draft_id && strncmp(send_from_draft_id,	"",	1))
		{
			Duster_module_Printf(3,	"%s send_from_draft_id is %s",	__FUNCTION__,	send_from_draft_id);
			SMS_delete_list(send_from_draft_id);
			send_from_draft_id = NULL;
		}
		memset(result_Str,'\0',3);
		if(ret<0)
			sprintf(result_Str,"%d",2);
		else
			sprintf(result_Str,"%d",3);

		Duster_module_Printf(3,	"%s	result_Str is %s",	__FUNCTION__,	result_Str);
		strcpy(p_sms_cmd_status_result,result_Str);
		strcpy(p_sms_cmd,"4");
		*p_sms_nv_send_num = totalLocalSentNum;
	}
	else
	{
		strcpy(p_sms_cmd_status_result,"2");
		strcpy(p_sms_cmd,"5");
	}
	if(time_formated)
		sms_free(time_formated);

	totalLocalSentNum_LongSMS = SMS_calc_Long(LocalSendSMS_list_start,LocalSendSMS_list_last);
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);	
	psm_commit_other__(handle_SMS1);
       sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);

	Duster_module_Printf(3,"leave %s",__FUNCTION__);	
}

void Sms_json_data_saveSms(char* p_contacts,char* p_content,char* p_encode_type,char* p_sms_time,char* p_edit_draft_str,char* p_sms_cmd_status_result,char* p_sms_cmd)
{
	char	 	 *number=NULL,*sms_time=NULL,*messageBody=NULL,*id=NULL,*encode_type=NULL,*location=NULL,*time_formated = NULL;
	s_MrvFormvars     **list_start = NULL,**list_last=NULL;
	int 	      ret = 0;
	char result_Str[3] = {0};
	char			*edit_draft_str = NULL;
	UINT8			edit_draft_id = 0;
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	number      = p_contacts;
	messageBody = p_content;
	encode_type = p_encode_type;
	sms_time    = p_sms_time;
	Duster_module_Printf(3,"%s number is %s,encode_type is %s,sms_time is %s",__FUNCTION__,number,encode_type,sms_time);
	if(number&&messageBody&&encode_type&&sms_time)
	{
		time_formated = MrvCgiUnescapeSpecialChars(sms_time);
		{

			list_start = &LocalDraftSMS_list_start;
			list_last  = &LocalDraftSMS_list_last;
			edit_draft_str = p_edit_draft_str;
			if(edit_draft_str && strncmp(edit_draft_str,	"",	1))
			{
				edit_draft_id = atoi(edit_draft_str+4);
				edit_draft_str = NULL;
			}

			ret=SMS_Save(number,messageBody,encode_type,sms_time,1,0,list_start,list_last,	edit_draft_id);
			memset(result_Str,'\0',3);
			if(ret<0)
				sprintf(result_Str,"%d",2);
			else
				sprintf(result_Str,"%d",3);
			strcpy(p_sms_cmd_status_result,result_Str);
			strcpy(p_sms_cmd,"5");
		}
	}
	else
	{
		strcpy(p_sms_cmd_status_result,"2");
		strcpy(p_sms_cmd,"5");
	}

	if(time_formated)
		sms_free(time_formated);		
	psm_commit_other__(handle_SMS1);	
       sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
	Duster_module_Printf(3,"leave %s",__FUNCTION__);	
}

void Sms_json_data_deleteSms(char* p_delete_message_id,
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	char  	       tmp[8]= {0};
	
	char *str2 = NULL;
	int ret = 0;
	char result_Str[3] = {0};
	str2 = p_delete_message_id;
	if(str2)
	{
		Duster_module_Printf(3,"%s delete_sms id is %s",__FUNCTION__,str2);
		ret = SMS_delete_list(str2);
		memset(result_Str,	'\0',	3);
		if(ret<0)
			sprintf(result_Str,"%d",2);
		else
			sprintf(result_Str,"%d",3);
	}
	else
	{
		memset(result_Str,	'\0',	3);
		sprintf(result_Str,"%d",2);
	}
	strcpy(p_sms_cmd_status_result,result_Str);
	strcpy(p_sms_cmd,"5");

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalDraftNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_draftbox_num",	  tmp,	handle_SMS1);
			
	gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
	UIRefresh_Sms_Change();
	psm_commit_other__(handle_SMS1);
	sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;	
	 Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);
}

void Sms_json_data_deleteAllSms(char* p_delete_all_sms_type,
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	char *str2 = NULL;
	int ret = 0;
	char result_Str[3] = {0};
	str2 = p_delete_all_sms_type;
	if(str2 && strncmp(str2,	"",	1))
	{
		if(strcasecmp(str2,	"INBOX") == 0)
		{
			SMS_clear_list_raw_data(&LocalSMS_list_start,	&LocalSMS_list_last,	1);
			gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
			totalLocalSMSNum = 0;
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_rev_num",	"0",	handle_SMS1);
			UIRefresh_Sms_Change();
		}
		else if(strcasecmp(str2,	"SENTBOX") == 0)
		{
			SMS_clear_list_raw_data(&LocalSendSMS_list_start,	&LocalSendSMS_list_last,	2);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_send_num",	"0",	handle_SMS1);			
			totalLocalSentNum = 0;
			UIRefresh_Sms_Change();
		}
		else if(strcasecmp(str2,	"DRAFTBOX") == 0)
		{
			SMS_clear_list_raw_data(&LocalDraftSMS_list_start,	&LocalDraftSMS_list_last,	3);
			psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	"sms_nv_draftbox_num",	"0",	handle_SMS1);			
			totalLocalDraftNum = 0;
			UIRefresh_Sms_Change();
		}
		else if(strcasecmp(str2,	"SIMSMS") == 0)
		{
			SMS_clear_list_raw_data(&SimSMS_list_start,	&SimSMS_list_last,	1);
			gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
			totalSimSmsNum = 0;
			UIRefresh_Sms_Change();
		}		
		psm_commit_other__(handle_SMS1);
		strcpy(p_sms_cmd_status_result,"3");
		strcpy(p_sms_cmd,"6");
	}	
	
	sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;		
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);
}
void Sms_json_data_copyMoveSms(char* p_message_flag,char* p_mv_cp_id,
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd,char* p_mv_cp_sms_result)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	char  	       tmp[8]= {0};	
	
	char *str = NULL;
	char *str2 = NULL;
	int ret = 0;
	UINT8		  mem_to=0,MV_CP_FLAG=0;	
	char result_Str[3] = {0};

	str = p_message_flag;
	if(!strcasecmp(str ,   "MOVE_SMS_TO_LOCAL"))
	{
		mem_to     = 1;
		MV_CP_FLAG = 1;
	}
	else if(!strcasecmp(str , "COPY_SMS_TO_SIM"))
	{
		mem_to 	 = 0;
		MV_CP_FLAG = 0;
	}
	else if(!strcasecmp(str , "MOVE_SMS_TO_SIM"))
	{
		mem_to     = 0;
		MV_CP_FLAG = 1;
	}
	else if(!strcasecmp(str , "COPY_SMS_TO_LOCAL"))
	{
		mem_to     = 1;
		MV_CP_FLAG = 0;
	}
	str2 = p_mv_cp_id;
	if(str2)
	{
		Duster_module_Printf(3,"%s, mv_cp_id is %s",__FUNCTION__,str2);
		ret = SMS_MV_CP( str2,mem_to,MV_CP_FLAG);
		memset(result_Str,'\0',3);
		if(ret < 0)
			sprintf(result_Str,"%d",2);
		else if(ret == 2)
			sprintf(result_Str,"%d",9);//parftfailed
		else
			sprintf(result_Str,"%d",3);
		strcpy(p_sms_cmd_status_result,result_Str);
		strcpy(p_sms_cmd,"8");
	}
	else
	{
		Duster_module_Printf(3,"%s,can't get mv_cp_id",__FUNCTION__);		
		strcpy(p_mv_cp_sms_result,"2");	
	}
	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSMSNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_rev_num", tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_rev_num", tmp,	handle_SMS1);

	memset(tmp,   '\0',	  8);
	sprintf(tmp,  "%d",   totalLocalSentNum);
	psm_set_wrapper(PSM_MOD_SMS,  "sms_capacity_info",	  "sms_nv_send_num",	  tmp);
	psmfile_set_wrapper(PSM_MOD_SMS,	"sms_capacity_info",	  "sms_nv_send_num",	  tmp,	handle_SMS1);

	gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
	UIRefresh_Sms_Change();
	psm_commit_other__(handle_SMS1);
	sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;	
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;
	
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",
	                     __FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);
}

void Sms_json_data_setMessageRead(char* p_read_message_id,
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd,char* p_set_read_result)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;	
	char *str2 = NULL;
	int ret = 0;
	char result_Str[3] = {0};
	str2 = p_read_message_id;
	if(str2)
	{
		Duster_module_Printf(3,"%s set_msg_read id is %s", __FUNCTION__, str2);
		ret=SMS_read_list(str2);
		if(ret<0)
			sprintf(result_Str,"%d",2);
		else
			sprintf(result_Str,"%d",3);
			strcpy(p_sms_cmd_status_result,result_Str);
			strcpy(p_sms_cmd,"6");
	}
	else
		strcpy(p_set_read_result,"2");
	gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
	UIRefresh_Sms_Change();
	psm_commit_other__(handle_SMS1);
	sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;	
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);	
}

void Sms_json_data_setAllMessageRead(
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;	
	int ret = 0;
	CPUartLogPrintf("%s SMS SET ALL READ",	__FUNCTION__);
	SMS_read_By_list(SimSMS_list_start,	SimSMS_list_last);
	SMS_read_By_list(LocalSMS_list_start,	LocalSMS_list_last);
	strcpy(p_sms_cmd_status_result,"3");
	strcpy(p_sms_cmd,"6");
	psm_commit__();
	psm_commit_other__(handle_SMS1);
	gDialer2UIPara.unreadLocalSMSNum = 0;
	UIRefresh_Sms_Change();
	sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;
	 Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);	
}

/*========================================================
	Get the last recved unread msg, and set to read status
	Input:
		storage : 0 --SIM
				  1 -- device
		smsNumber: the sms number
		timeStamp: 15,06,22,16,07,56,+8 length: 24
		smsContent: UTF-8 length: 1200; format: EF 80 80
	Return:
		O -- Get fail
		1 -- OK
	Notice: If the buffer length is not enough, Crash will happen.
=========================================================*/
unsigned int get_unread_json_sms_record(char * sms_json_data, UINT8 storage,UINT8 tags)
{
	UINT16	unread = 0;
	char 			*str = NULL;
	char			 SMS_index[12] = {0}, Item_index_buf[12] = {0};
	char			 tmp[20] = {0};
	UINT32			 total_sms_num = 0;
	OSA_STATUS		 osa_status = 0;
	UINT32			 localSMS_skipNum = 0,shownLocalSMS = 0,total_page_num = 0;
	s_MrvFormvars 		*list_start=NULL,*list_last=NULL;
	char			 LongSMS_index[100] = {0};
	char			 tmpSubject[400];
	char			*p1 = NULL,*p2 = NULL,*p3 = NULL,*p4 = NULL,*p5 = NULL,*p6 = NULL,*tmp_str = NULL;
	char 			*ucs_char = NULL,	*buff_SMSSubject = NULL;
	UINT16			 ghandle = 0;
	int 		     ret     = 0;
	MrvXMLElement 	*pMessage_list = NULL,*pNewTtemEle = NULL,*pnewChild = NULL;
	UINT8			 Item_index = 1;
	char 			 *find_name = NULL,	*add_name = NULL,	*add_name_char = NULL;
	int 			  name_len = 0;
	int 			  nIndex = 0;
	UINT8			  code_type = 255;
	char 			 *p7 = NULL, *p8 = NULL, *p9 = NULL, *p10 = NULL, *p11 = NULL, *p12 = NULL, *p13 = NULL;
	UINT8			  class_type = 255;
	s_MrvFormvars *list_node = NULL;
	SMS_Node * sms_data  = NULL;
	
	char *sms_str = NULL;
	char indexbuf[8] = {0};
	Duster_module_Printf(3,"enter :%s",__FUNCTION__);	
	if(storage == 0)
	{
		list_start = SimSMS_list_start;
		list_last = SimSMS_list_last;
	}
	else if(storage == 1)
	{
		list_start = LocalSMS_list_start;
		list_last = LocalSMS_list_last;
	}
	else
	{
		return 0;
	}

	unread = SMS_calc_unread_List(list_start,	list_last);
	Duster_module_Printf(3,"%s:unread:%d",__FUNCTION__,unread);	
	if(unread == 0)
		return 0;
	
	buff_SMSSubject      =  sms_malloc(MAX_LSMS_SEGMENT_NUM<<9);
	DUSTER_ASSERT(buff_SMSSubject);
	memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);
	
	list_node = list_start;
	while(list_node)
	{
		sms_data = (SMS_Node*)list_node->value;
		if(sms_data->isLongSMS && sms_data->LSMSinfo && sms_data->LSMSinfo->LSMSflag == START)
		{
			if(1)
			{
				Duster_module_Printf(3,"%s  isRecvAll:%d,totalSegment:%d,receivedNum:%d",__FUNCTION__,sms_data->LSMSinfo->isRecvAll,sms_data->LSMSinfo->totalSegment,sms_data->LSMSinfo->receivedNum);			         
				if(!sms_data->LSMSinfo->isRecvAll && (sms_data->LSMSinfo->totalSegment <= MAX_LSMS_SEGMENT_NUM) && (sms_data->LSMSinfo->receivedNum <= MAX_LSMS_SEGMENT_NUM) )
				{
					list_node = list_node->next;
					while(list_node)
					{
						sms_data=(SMS_Node*)list_node->value;
						if(sms_data->LSMSinfo->LSMSflag == END)
						{
							break;
						}
						list_node=list_node->next;
					}
					list_node=list_node->next;
					continue;
				}
			}
			
			memset(LongSMS_index,	'\0',	100);
			memset(buff_SMSSubject,	'\0',	MAX_LSMS_SEGMENT_NUM<<9);
			if(sms_data->sms_status == UNREAD)
			{
				//set to read
				while(list_node)
				{
					sms_data = (SMS_Node*)list_node->value;
					if(storage == 0)
					{
						memset(indexbuf,	0,	sizeof(indexbuf));
						snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"SRCV",	sms_data->psm_location);
						sms_str = psm_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf);
					}
					else
					{
						memset(indexbuf,	0,	sizeof(indexbuf));
						snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"LRCV",	sms_data->psm_location);
						sms_str = psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf,	handle_SMS1);
					}
					Duster_module_Printf(3,"%s:long sms 0debug sms_str:%s",__FUNCTION__,sms_str);	       

					if(sms_str)
					{
				

						p1 = strchr(sms_str, DELIMTER);
						p2 = strchr(p1+1,DELIMTER);
						p3 = strchr(p2+1,DELIMTER);
						p4 = strchr(p3+1,DELIMTER);
						p5 = strchr(p4+1,DELIMTER);
						p6 = strchr(p5+1,DELIMTER);

						p7 = strchr(p6+1,DELIMTER );
						p8 = strchr(p7+1, DELIMTER);
						p9 = strchr(p8+1, DELIMTER);
						p10 = strchr(p9+1, DELIMTER);
						p11 = strchr(p10+1, DELIMTER);
						p12 = strchr(p11+1, DELIMTER);
						p13 = strchr(p12+1, DELIMTER);
					       Duster_module_Printf(3,"%s:sms_str p1:0x%x,p2:0x%x,p3:0x%x,p4:0x%x,p5:0x%x,p6:0x%x",__FUNCTION__,p1,p2,p3,p4,p5,p6);	       
						Duster_module_Printf(3,"%s:sms_str p7:0x%x,p8:0x%x,p9:0x%x,p10:0x%x,p11:0x%x,p12:0x%x",__FUNCTION__,p7,p8,p9,p10,p11,p12);	    					
						if(p1&&p2&&p3&&p4&&p5&&p6)
						{

							Duster_module_Printf(3,"%s:long sms 1debug",__FUNCTION__);	       

							strcat(LongSMS_index, indexbuf);
							strcat(LongSMS_index,",");
							memset(tmpSubject,    '\0',  400);
							if((p4-p3-1)>400)
							{
								Duster_module_Printf(3,"%s content too long",__FUNCTION__);
								DUSTER_ASSERT(0);
							}
							if(sms_data->LSMSinfo->LSMSflag == START)
							{
								memset( tmp, '\0', 	20);
								DUSTER_ASSERT(( p3 - p2 -1) <	20);
								memcpy(tmp,	p2 + 1,	p3 - p2 -1);
								code_type = atoi(tmp);
							}
							Duster_module_Printf(3,"%s:long sms 2debug",__FUNCTION__);	       

							memcpy(buff_SMSSubject+strlen(buff_SMSSubject),	p3+1,	p4-p3-1);
							if(sms_data->LSMSinfo->LSMSflag!=END)
							{
								if(sms_str)
								{
									sms_free(sms_str);
									sms_str = NULL;
								}
							}
							else if(sms_data->LSMSinfo->LSMSflag == END)
							{
								str_replaceChar(buff_SMSSubject,SEMI_REPLACE, ';');
								str_replaceChar(buff_SMSSubject,EQUAL_REPLACE,'=');

								ucs_char = utf8_to_unicode_char(buff_SMSSubject);
								DUSTER_ASSERT(ucs_char);
								strcat(sms_json_data,"{\"index\":\"");
								strcat(sms_json_data,LongSMS_index);
								strcat(sms_json_data,"\",");
							
								/*from*/
								tmp_str = sms_malloc(p2-p1);
								DUSTER_ASSERT(tmp_str);
								memset(tmp_str,	'\0',	p2-p1);
								memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/

								/*search match name in Phonebook*/
								find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);
                                                        Duster_module_Printf(3,"%s:long sms 3debug",__FUNCTION__);	     
								/*name is xml is consisted as : name;number*/
								if(tmp_str)
								{
									name_len = strlen(find_name) + strlen(tmp_str) + 1; /* 1 octs for ' ; '*/
								}
								add_name = sms_malloc(name_len + 1);
								DUSTER_ASSERT(add_name);
								memset(add_name,	'\0',	name_len + 1);
								if(find_name)
								{
									strcat(add_name,	find_name);
								}
								strcat(add_name,	";");
								strcat(add_name,	tmp_str);
								sms_free(tmp_str);
								add_name_char = utf8_to_unicode_char(add_name);
								Duster_module_Printf(3,"%s:long sms 4debug,add_name:%s,add_name_schar:%s",__FUNCTION__,add_name,add_name_char);	     
								if(add_name)
								{
									sms_free(add_name);
									add_name = NULL;
								}
								strcat(sms_json_data,"\"from\":\"");
								strcat(sms_json_data,add_name_char);
								strcat(sms_json_data,"\",");
								if(add_name_char)
								{
									sms_free(add_name_char);
									add_name_char = NULL;
								}

								Duster_module_Printf(3,"%s:long sms 5debug,ucs_char:%s",__FUNCTION__,ucs_char);	     
								//subject
								strcat(sms_json_data,"\"subject\":\"");
								strcat(sms_json_data,ucs_char);
								strcat(sms_json_data,"\",");
                                                 	sms_free(ucs_char);
							       Duster_module_Printf(3,"%s:long sms 6debug",__FUNCTION__);	     
								   
								/*sms_time*/
								tmp_str = sms_malloc(p5-p4);
								DUSTER_ASSERT(tmp_str);
								memset(tmp_str, '\0',	p5-p4);
								memcpy(tmp_str, p4+1,	p5-p4-1);
								strcat(sms_json_data,"\"received\":\"");
								strcat(sms_json_data,tmp_str);
								strcat(sms_json_data,"\",");
								Duster_module_Printf(3,"%s:long sms 7debug,tmp_str:%s",__FUNCTION__,tmp_str);	     	
								if(tmp_str)
								{
                                                 		sms_free(tmp_str);  
									tmp_str=NULL;					
								}
								/*sms_status*/
								tmp_str = sms_malloc(p6-p5);
								DUSTER_ASSERT(tmp_str);
								memset(tmp_str, '\0',	p6-p5);
								memcpy(tmp_str, p5+1,	p6-p5-1);
								strcat(sms_json_data,"\"status\":\"");
								strcat(sms_json_data,tmp_str);
								strcat(sms_json_data,"\",");
								Duster_module_Printf(3,"%s:long sms 8debug,tmp_str:%s",__FUNCTION__,tmp_str);	     	
								if(tmp_str)
								{
                                                 		sms_free(tmp_str);  
									tmp_str=NULL;					
								}

								Duster_module_Printf(3,	"%s code_type is %d",	__FUNCTION__,	code_type);
								//message_type
								strcat(sms_json_data,"\"message_type\":\"");
								if((code_type != 3) && (code_type != 4) && (code_type != 6) )
								{
									strcat(sms_json_data,"0");    /*SMS*/
								}
								else if(code_type == 3)
								{
									strcat(sms_json_data,"1"); 	/*MMS*/
								}
								else if(code_type == 4)
								{
									strcat(sms_json_data,"2");	/*WAP-PUSH*/
								}
								else if(code_type == 6)
								{
									strcat(sms_json_data,"3");	/*VOICE MAIL*/
								}
								Duster_module_Printf(3,"%s:long sms 9debug",__FUNCTION__);	     	
								/*class_type*/ 
								if(tags == 12)
								{
									if(p11 && p12)
									{
										strcat(sms_json_data,"\",");					
										memset( tmp, '\0', 	20);
										DUSTER_ASSERT(( p12 - p11 -1) <	20);
										memcpy(tmp,	p11 + 1,	p12 - p11 -1);
										strcat(sms_json_data,"\"class_type\":\"");
										strcat(sms_json_data,tmp);
									}

									if(p12 && p13)
									{
										strcat(sms_json_data,"\",");					
										memset( tmp, '\0', 	20);
										DUSTER_ASSERT(( p13 - p12 -1) <	20);
										memcpy(tmp,	p12 + 1,	p13 - p12 -1);
										strcat(sms_json_data,"\"message_time_index\":\"");
										strcat(sms_json_data,tmp);							
									}
								}
								/*class_type*/
								strcat(sms_json_data,"\"}");     
								if(sms_str)
								{
									sms_free(sms_str);
									sms_str = NULL;
								}
								Duster_module_Printf(3,"%s:long sms 10debug",__FUNCTION__);	  
								break;
							}
							//SMS_read_By_index(sms_data->psm_location,	list_start, list_last);	
						}
						else
						{
							if(sms_str)
							{
								sms_free(sms_str);
								sms_str = NULL;
							}	
							return	0;
						}
					}
					list_node = list_node->next;
				}
				break;
			}
			else
			{
				while(list_node)
				{
					sms_data = (SMS_Node*)list_node->value;
					if(sms_data->LSMSinfo)
					{
						if(sms_data->LSMSinfo->LSMSflag == END)
							break;
					}
					else
						break;
					list_node = list_node->next;
				}
			}
		}
		else if(!sms_data->isLongSMS)
		{
		       Duster_module_Printf(3,"%s:sms_data->sms_status:%d",__FUNCTION__,sms_data->sms_status);	       
			if(sms_data->sms_status == UNREAD)
			{
				//Set to read
				if(storage == 0)
				{
					memset(indexbuf,	0,	sizeof(indexbuf));
					snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"SRCV",	sms_data->psm_location);
					sms_str = psm_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf);
				}
				else
				{
					memset(indexbuf,	0,	sizeof(indexbuf));
					snprintf(indexbuf,	sizeof(indexbuf),	"%s%d",	"LRCV",	sms_data->psm_location);
					sms_str = psmfile_get_wrapper(PSM_MOD_SMS,	NULL,	indexbuf,	handle_SMS1);
				}
				Duster_module_Printf(3,"%s:sms_str:%d",__FUNCTION__,sms_str);	       
				if(sms_str)
				{

					p1 = strchr(sms_str, DELIMTER);
					p2 = strchr(p1+1,DELIMTER);
					p3 = strchr(p2+1,DELIMTER);
					p4 = strchr(p3+1,DELIMTER);
					p5 = strchr(p4+1,DELIMTER);
					p6 = strchr(p5+1,DELIMTER);

					p7 = strchr(p6+1,DELIMTER );
					p8 = strchr(p7+1, DELIMTER);
					p9 = strchr(p8+1, DELIMTER);
					p10 = strchr(p9+1, DELIMTER);
					p11 = strchr(p10+1, DELIMTER);
					p12 = strchr(p11+1, DELIMTER);
					p13 = strchr(p12+1, DELIMTER);
						
					if(p1&&p2&&p3&&p4&&p5&&p6)
					{
					       Duster_module_Printf(3,"%s:sms_str p1:0x%x,p2:0x%x,p3:0x%x,p4:0x%x,p5:0x%x,p6:0x%x",__FUNCTION__,p1,p2,p3,p4,p5,p6);	       
						Duster_module_Printf(3,"%s:sms_str p7:0x%x,p8:0x%x,p9:0x%x,p10:0x%x,p11:0x%x,p12:0x%x",__FUNCTION__,p7,p8,p9,p10,p11,p12);	         
						tmp_str = sms_malloc(p1 - sms_str +1);
						DUSTER_ASSERT(tmp_str);
						memset(tmp_str,	'\0',	p1 - sms_str +1);
						memcpy(tmp_str,   sms_str, p1-sms_str);/*index*/
						strcat(sms_json_data,"{\"index\":\"");
						strcat(sms_json_data,tmp_str);
						strcat(sms_json_data,"\",");
                                   	sms_free(tmp_str);
						tmp_str = sms_malloc(p2-p1);
						DUSTER_ASSERT(tmp_str);
						memset(tmp_str,	'\0',	p2-p1);
						memcpy(tmp_str, p1+1,p2-p1-1);/*srcAddr*/
                                          Duster_module_Printf(3,"%s:1debug",__FUNCTION__);	       
						/*search match name in Phonebook*/
						find_name = SMS_Find_Name_From_PB(tmp_str,	PB_Local_List_Start,	PB_Sim_List_Start);

						/*name is xml is consisted as : name;number*/
						if(tmp_str)
						{
							name_len = strlen(find_name) + strlen(tmp_str) + 1;	/* 1 octs for ' ; '*/
						}

						add_name = sms_malloc(name_len + 1);
						DUSTER_ASSERT(add_name);
						memset(add_name,	'\0',	name_len + 1);
						if(find_name)
						{
							strcat(add_name,	find_name);
						}
						strcat(add_name,	";");
						strcat(add_name,	tmp_str);
						sms_free(tmp_str);

						add_name_char = utf8_to_unicode_char(add_name);
						sms_free(add_name);
						strcat(sms_json_data,"\"from\":\"");
						strcat(sms_json_data,add_name_char);
						strcat(sms_json_data,"\",");
						sms_free(add_name_char);

					
						memset(tmpSubject,      '\0',    400 );
						memset(buff_SMSSubject, '\0',    MAX_LSMS_SEGMENT_NUM<<9);
						if((p4-p3-1)>400)
						{
							Duster_module_Printf(3,"%s subject too long",__FUNCTION__);
							DUSTER_ASSERT(0);
						}
						memcpy(tmpSubject,      p3+1, p4-p3-1);
						tmpSubject[399] = '\0';
						str_replaceChar(tmpSubject, SEMI_REPLACE,  ';');
						str_replaceChar(tmpSubject, EQUAL_REPLACE, '=');
						ucs_char = utf8_to_unicode_char(tmpSubject);
						DUSTER_ASSERT(ucs_char);
						strcat(sms_json_data,"\"subject\":\"");
						strcat(sms_json_data,ucs_char);
						strcat(sms_json_data,"\",");
                                   	sms_free(ucs_char);


						/*sms_time*/
						tmp_str = sms_malloc(p5-p4);
						DUSTER_ASSERT(tmp_str);
						memset(tmp_str,	'\0',	p5-p4);
						memcpy(tmp_str,	p4+1,	p5-p4-1);
						strcat(sms_json_data,"\"received\":\"");
						strcat(sms_json_data,tmp_str);
						strcat(sms_json_data,"\",");
						sms_free(tmp_str);

						/*sms_status*/
						tmp_str = sms_malloc(p6-p5);
						DUSTER_ASSERT(tmp_str);
						memset(tmp_str,	'\0',	p6-p5);
						memcpy(tmp_str,	p5+1,	p6-p5-1);
						strcat(sms_json_data,"\"status\":\"");
						strcat(sms_json_data,tmp_str);
						strcat(sms_json_data,"\",");
						sms_free(tmp_str);

                                   	/*message_type*/ 
						memset( tmp, '\0', 	20);
						DUSTER_ASSERT(( p3 - p2 -1) <	20);
						memcpy(tmp,	p2 + 1,	p3 - p2 -1);
						code_type = atoi(tmp);
						strcat(sms_json_data,"\"message_type\":\"");
						if((code_type != 3) && (code_type != 4) && (code_type != 6) )
						{
							strcat(sms_json_data,"0");    /*SMS*/
						}
						else if(code_type == 3)
						{
							strcat(sms_json_data,"1"); 	/*MMS*/
						}
						else if(code_type == 4)
						{
							strcat(sms_json_data,"2");	/*WAP-PUSH*/
						}
						else if(code_type == 6)
						{
							strcat(sms_json_data,"3");	/*VOICE MAIL*/
						}

						/*class_type*/
						if(tags == 12)
						{
							if(p11 && p12)
							{
								strcat(sms_json_data,"\",");					
								memset( tmp, '\0', 	20);
								DUSTER_ASSERT(( p12 - p11 -1) <	20);
								memcpy(tmp,	p11 + 1,	p12 - p11 -1);
								strcat(sms_json_data,"\"class_type\":\"");
								strcat(sms_json_data,tmp);
							}

							if(p12 && p13)
							{
								strcat(sms_json_data,"\",");					
								memset( tmp, '\0', 	20);
								DUSTER_ASSERT(( p13 - p12 -1) <	20);
								memcpy(tmp,	p12 + 1,	p13 - p12 -1);
								strcat(sms_json_data,"\"message_time_index\":\"");
								strcat(sms_json_data,tmp);							
							}
						}
                                          strcat(sms_json_data,"\"}");     
						if(sms_str)
						{
							sms_free(sms_str);
							sms_str = NULL;
						}	

					}
					if(sms_str)
					{
						sms_free(sms_str);
						sms_str = NULL;
					}
					//SMS_read_By_index(sms_data->psm_location,	list_start, list_last);
				}
				break;
			}
		}
		list_node = list_node->next;
	}
	if(buff_SMSSubject)
	{
		sms_free(buff_SMSSubject);	
		buff_SMSSubject = NULL;
	}	
	Duster_module_Printf(3,"leave :%s",__FUNCTION__);	
	return 1;
}
void Sms_json_data_getUnreadSmsRecord(char* sms_json_data,
	int* p_sms_nv_rev_total,int* p_sms_nv_send_total,int* p_sms_nv_draftbox_total,int* p_sms_sim_total,
	int* p_sms_nv_rev_num,int* p_sms_nv_send_num,int* p_sms_nv_draftbox_num,int* p_sms_sim_num,
	int* p_sms_nv_rev_long_num,int* p_sms_nv_send_long_num,int* p_sms_sim_long_num,
	char* p_sms_cmd_status_result,char* p_sms_cmd)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	char *str2 = NULL;
	int ret = 0;
	char result_Str[3] = {0};
	Duster_module_Printf(3,"enter :%s",__FUNCTION__);	
	
	ret=get_unread_json_sms_record(sms_json_data, 1,12);	
	if(ret==0)
		sprintf(result_Str,"%d",2);
	else
		sprintf(result_Str,"%d",3);
	
	strcpy(p_sms_cmd_status_result,result_Str);
	strcpy(p_sms_cmd,"6");

	gDialer2UIPara.unreadLocalSMSNum = SMS_calc_unread();
	UIRefresh_Sms_Change();
	//psm_commit_other__(handle_SMS1);
	//sms_set_get_all_num(&total_SIMSMS_Long_num,&total_unread_msg_num,&total_complete_single);
	total_SIMSMS_Long_num= SMS_calc_Long(SimSMS_list_start, SimSMS_list_last);
       *p_sms_nv_rev_total = SupportSMSNum;
	*p_sms_nv_send_total = MaxSentSMSNum;
	*p_sms_nv_draftbox_total= MaxDraftSMSNum;
	*p_sms_sim_total = gMem1TotalNum;
       *p_sms_nv_rev_num = totalLocalSMSNum;
	*p_sms_nv_send_num = totalLocalSentNum;
	*p_sms_nv_draftbox_num = totalLocalDraftNum;
       *p_sms_sim_num = totalSimSmsNum;	
	*p_sms_nv_rev_long_num = totalLocalNum_LongSMS;
	*p_sms_nv_send_long_num = totalLocalSentNum_LongSMS;
	*p_sms_sim_long_num = total_SIMSMS_Long_num;
	
	Duster_module_Printf(3,"%s,	totalLocalSMSNum is %d,totalLocalSentNum is %d,totalLocalDraftNum is %d,total_SIMSMS_Long_num is %d",__FUNCTION__,totalLocalSMSNum,totalLocalSentNum,totalLocalDraftNum,	total_SIMSMS_Long_num);	
}

void Sms_json_data_getSmsReport(char* p_sms_report_status_list)
{
	UINT16			total_SIMSMS_Long_num = 0;
	UINT16			total_unread_msg_num = 0;
	UINT16			total_complete_single = 0;
	char *str2 = NULL;
	int ret = 0;
	char result_Str[3] = {0};
	str2 = psm_get_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list");
	Duster_module_Printf(3,"%s,	str2:%s",__FUNCTION__,str2);	
	if(str2)
	{
		strcpy(p_sms_report_status_list,str2);
		Duster_module_Printf(3,"%s,	p_sms_report_status_list:%s",__FUNCTION__,p_sms_report_status_list);	
		psm_set_wrapper(PSM_MOD_SMS,	NULL,	"sms_report_status_list",	"");
	}
	Duster_module_Printf(3,"%s,	exit",__FUNCTION__);	
}
void Sms_json_data_getMessageCenter(char* p_sms_center,char* p_status_report,char* p_save_time)
{
	char *str2 = NULL;
	char *smsc_Str = NULL;
	Duster_module_Printf(3,"%s,	enter",__FUNCTION__);	
	if(p_sms_center)
	{
		if((smsc_init_done == 0) && SMS_FlgRef)
		{
	       	if(0 == SMSReadyFlag)   //added by notion ggj 20160108
	       	{
	       		Duster_module_Printf(3,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
				getSMSCenter();
				smsc_init_done = 1;
	       	}
		}
		smsc_Str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_center");
		if(smsc_Str)
		{
			strcpy(p_sms_center,smsc_Str);
			Duster_module_Printf(3,"%s,	smsc_Str:%s",__FUNCTION__,p_sms_center);			
			duster_free(smsc_Str);
		}
	}

	if(p_status_report)
	{
		str2=psm_get_wrapper(PSM_MOD_SMS,"sms_setting","status_report"); //set status_report
  		if(str2)
		{
			strcpy(p_status_report,str2);
			Duster_module_Printf(3,"%s,status_report:%s",__FUNCTION__,p_status_report);		
			duster_free(str2);
		}		
	}

	if(p_save_time)
	{
		str2 =psm_get_wrapper(PSM_MOD_SMS,"sms_setting","save_time");//default relative TP-VP is 143 0x8F
  		if(str2)
		{
			strcpy(p_save_time,str2);
			Duster_module_Printf(3,"%s,save_time:%s",__FUNCTION__,p_save_time);		
			duster_free(str2);
		}
	}
	Duster_module_Printf(3,"%s,	exit",__FUNCTION__);	
}

void Sms_json_data_setMessageCenter(char* p_sms_center,char* p_status_report,char* p_save_time,char* p_sms_cmd_status_result,char* p_cmd)
{
	char *str2 = NULL;
	char *smsc_Str = NULL;
	int ret = 0;
	CommandATMsg  *sendSMSMsg = NULL;
	OSA_STATUS	   osa_status;	
	UINT32  	  flag_value = 0;	
	char  *save_location_str = NULL;
	char		   *sms_over_cs_str = NULL;
       UINT8	      set_location = 0;
	int				sms_over_cs_set = 0;

	Duster_module_Printf(3,"%s,	enter",__FUNCTION__);	
	Duster_module_Printf(3,"%s,	smsc_Str:%s,status_report:%s,save_time:%s",__FUNCTION__,p_sms_center,p_status_report,p_save_time);	
	psm_set_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_center",p_sms_center);
	smsc_Str = p_sms_center;
	if((smsc_Str)&&(0==SMSReadyFlag)) 
	{
	       Duster_module_Printf(3,"%s,SMSReadyFlag = %d,smsc_init_done=%d",__FUNCTION__,SMSReadyFlag,smsc_init_done);
		ret = getSMSCenter();
		if(strcmp(smsc_Str , csca_resp_str) && strcmp(smsc_Str,	DEFAULT_SMSC_ADDR))//add proction not to change SMSC address by unkonwn reason
		{
			Duster_module_Printf(3,"%s,old smsc is %s,new smsc is %s",__FUNCTION__, csca_resp_str, smsc_Str);
			sendSMSMsg=(CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
			sendSMSMsg->atcmd=sms_malloc(DIALER_MSG_SIZE);
			sendSMSMsg->ok_fmt=NULL;
			sendSMSMsg->err_fmt=sms_malloc(15);
			if(!sendSMSMsg || !sendSMSMsg->atcmd|| !sendSMSMsg->err_fmt)
			{
				Duster_module_Printf(3,"%s sms_malloc fail DUSTER_ASSERT",__FUNCTION__);
				DUSTER_ASSERT(0);
			}
			memset(sendSMSMsg->atcmd,'\0',DIALER_MSG_SIZE);
			sprintf(sendSMSMsg->atcmd,"AT+CSCA=\"%s\",145\r",smsc_Str);
			sendSMSMsg->ok_flag=1;
			memset(sendSMSMsg->err_fmt,	'\0',	15);
			sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
			sendSMSMsg->callback_func=&CSCA_SET_CALLBACK;
			sendSMSMsg->ATCMD_TYPE=TEL_EXT_GET_CMD;
			osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
			DUSTER_ASSERT(osa_status==OS_SUCCESS);
			flag_value=0;
			osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CSCA_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
			
			if(flag_value == CSCA_SET_CMD)
			{
				strcpy(p_sms_cmd_status_result,"3");
				memset(csca_resp_str,	'\0',	DIALER_MSG_SIZE);
				memcpy(csca_resp_str,smsc_Str,strlen(smsc_Str));
				psm_set_wrapper(PSM_MOD_SMS,"sms_setting","sms_center",smsc_Str);
			}
			else
				strcpy(p_sms_cmd_status_result,"2");
				
			strcpy(p_cmd,"2");

			if(sendSMSMsg)
			{
				if(sendSMSMsg->atcmd)
					sms_free(sendSMSMsg->atcmd);
				if(sendSMSMsg->ok_fmt)
					sms_free(sendSMSMsg->ok_fmt);
				if(sendSMSMsg->err_fmt)
					sms_free(sendSMSMsg->err_fmt);
				sms_free(sendSMSMsg);
			}

		}
	}

			save_location_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"save_location");
			if(save_location_str&&strncmp(save_location_str,	"",	1))
			{
				set_location = ConvertStrToInteger(save_location_str);
				Duster_module_Printf(3,	"%s save_location_str is %d",	__FUNCTION__,	save_location_str);
				if(set_location != save_location)
				{
					save_location = set_location;
					sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
					DUSTER_ASSERT(sendSMSMsg);
					memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));
					sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
					DUSTER_ASSERT(sendSMSMsg->atcmd);
					memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
					sendSMSMsg->callback_func=&CNMI_SET_CALLBACK;
					sendSMSMsg->ok_flag	=	0;
					sendSMSMsg->err_fmt = sms_malloc(15);
					DUSTER_ASSERT(sendSMSMsg->err_fmt);
					memset(sendSMSMsg->err_fmt,	'\0',	15);
					sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
					sendSMSMsg->ok_fmt=NULL;
					sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
					if(set_location == 1)
						sprintf(sendSMSMsg->atcmd,"at+cnmi=0,2,0,1,0\r");
					else if(set_location == 0)
						sprintf(sendSMSMsg->atcmd,"at+cnmi=0,1,0,1,0\r");

					osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					flag_value=0;
					osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CNMI_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
					DUSTER_ASSERT(osa_status==OS_SUCCESS);

					switch(flag_value)
					{
					case CMS_ERROR:
						Duster_module_Printf(3,"%s AT+CNMI failed",__FUNCTION__);
						strcpy(p_sms_cmd_status_result,"2");; //sucess
						break;
					case CNMI_SET_CMD:
						strcpy(p_sms_cmd_status_result,"3");//sucess
						break;
					}

					strcpy(p_cmd,"3");

					if(sendSMSMsg)
					{
						if(sendSMSMsg->atcmd)
							sms_free(sendSMSMsg->atcmd);
						if(sendSMSMsg->ok_fmt)
							sms_free(sendSMSMsg->ok_fmt);
						if(sendSMSMsg->err_fmt)
							sms_free(sendSMSMsg->err_fmt);
						sms_free(sendSMSMsg);
					}

				}
			}
			if(save_location_str)
				sms_free(save_location_str);
			sms_over_cs_str = psm_get_wrapper(PSM_MOD_SMS,	"sms_setting",	"sms_over_cs");
			if(sms_over_cs_str&&strncmp(sms_over_cs_str,	"",	1))
			{
				sms_over_cs_set = atoi(sms_over_cs_str);
				Duster_module_Printf(3,	"%s statoc sms_over_cs is%d, set is %d",	__FUNCTION__, sms_over_cs	,sms_over_cs_set);
				if(sms_over_cs != sms_over_cs_set)
				{
					if((sms_over_cs_set >=0) &&(sms_over_cs_set<= 1))
					{
						sendSMSMsg = (CommandATMsg*)sms_malloc(sizeof(CommandATMsg));
						DUSTER_ASSERT(sendSMSMsg);
						memset(sendSMSMsg,	'\0',	sizeof(CommandATMsg));
						sendSMSMsg->atcmd = sms_malloc(DIALER_CMD_MSG_SIZE);
						DUSTER_ASSERT(sendSMSMsg->atcmd);
						memset(sendSMSMsg->atcmd,	'\0',	DIALER_CMD_MSG_SIZE);
						sendSMSMsg->callback_func=&CGSMS_SET_CALLBACK;
						sendSMSMsg->ok_flag=0;
						sendSMSMsg->err_fmt = sms_malloc(15);
						DUSTER_ASSERT(sendSMSMsg->err_fmt);
						memset(sendSMSMsg->err_fmt,	'\0',	15);
						sprintf(sendSMSMsg->err_fmt,"+CMS ERROR");
						sendSMSMsg->ok_fmt=NULL;
						sendSMSMsg->ATCMD_TYPE=TEL_EXT_SET_CMD;
						if(sms_over_cs_set == 1)
						{
							sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	0);
						}
						else if(sms_over_cs_set == 0)
						{
							sprintf(sendSMSMsg->atcmd,	"AT+CGSMS=%d\r",	1);
						}

						osa_status = OSAMsgQSend(gSendATMsgQ, sizeof(CommandATMsg), (UINT8 *)sendSMSMsg, OSA_SUSPEND);
						DUSTER_ASSERT(osa_status==OS_SUCCESS);

						flag_value=0;
						osa_status = OSAFlagWait(SMS_FlgRef, CMS_ERROR|CGSMS_SET_CMD,OSA_FLAG_OR_CLEAR,&flag_value,SMS_TIMEOUT);
						//DUSTER_ASSERT(osa_status==OS_SUCCESS);

						switch(flag_value)
						{
						case CMS_ERROR:
							Duster_module_Printf(3,"%s AT+CGSMS failed",__FUNCTION__);
							strcpy(p_sms_cmd_status_result,"2");//fail
							break;
						case CGSMS_SET_CMD:
							strcpy(p_sms_cmd_status_result,"3");//sucess
							break;
						}

						if(sendSMSMsg)
						{
							if(sendSMSMsg->atcmd)
								sms_free(sendSMSMsg->atcmd);
							if(sendSMSMsg->ok_fmt)
								sms_free(sendSMSMsg->ok_fmt);
							if(sendSMSMsg->err_fmt)
								sms_free(sendSMSMsg->err_fmt);
							sms_free(sendSMSMsg);
						}
						sms_over_cs = sms_over_cs_set;
					}
				}
			}
			if(sms_over_cs_str)
			{
				sms_free(sms_over_cs_str);
				sms_over_cs_str = NULL;
			}
       if(p_status_report)
       {
		psm_set_wrapper(PSM_MOD_SMS,"sms_setting","status_report",p_status_report); //set status_report	
       }
	if(p_save_time)   
	{
		psm_set_wrapper(PSM_MOD_SMS,"sms_setting","save_time",p_save_time);//default relative TP-VP is 143 0x8F	
	}
	psm_commit__();		
	Duster_module_Printf(3,"%s,	exit",__FUNCTION__);	
}
#endif

// NOTION_ENABLE_KSC5601_UCS2_CONVERT should be added to address Korean SMS issue
#ifdef NOTION_ENABLE_KSC5601_UCS2_CONVERT

/*added by notion ggj 20160601 for Ksc5601 code to ucs2*/
static const unsigned short ksc5601_2uni_page21[1115] = {
  /* 0x21 */
  0x3000, 0x3001, 0x3002, 0x00b7, 0x2025, 0x2026, 0x00a8, 0x3003,
  0x00ad, 0x2015, 0x2225, 0xff3c, 0x223c, 0x2018, 0x2019, 0x201c,
  0x201d, 0x3014, 0x3015, 0x3008, 0x3009, 0x300a, 0x300b, 0x300c,
  0x300d, 0x300e, 0x300f, 0x3010, 0x3011, 0x00b1, 0x00d7, 0x00f7,
  0x2260, 0x2264, 0x2265, 0x221e, 0x2234, 0x00b0, 0x2032, 0x2033,
  0x2103, 0x212b, 0xffe0, 0xffe1, 0xffe5, 0x2642, 0x2640, 0x2220,
  0x22a5, 0x2312, 0x2202, 0x2207, 0x2261, 0x2252, 0x00a7, 0x203b,
  0x2606, 0x2605, 0x25cb, 0x25cf, 0x25ce, 0x25c7, 0x25c6, 0x25a1,
  0x25a0, 0x25b3, 0x25b2, 0x25bd, 0x25bc, 0x2192, 0x2190, 0x2191,
  0x2193, 0x2194, 0x3013, 0x226a, 0x226b, 0x221a, 0x223d, 0x221d,
  0x2235, 0x222b, 0x222c, 0x2208, 0x220b, 0x2286, 0x2287, 0x2282,
  0x2283, 0x222a, 0x2229, 0x2227, 0x2228, 0xffe2,
  /* 0x22 */
  0x21d2, 0x21d4, 0x2200, 0x2203, 0x00b4, 0xff5e, 0x02c7, 0x02d8,
  0x02dd, 0x02da, 0x02d9, 0x00b8, 0x02db, 0x00a1, 0x00bf, 0x02d0,
  0x222e, 0x2211, 0x220f, 0x00a4, 0x2109, 0x2030, 0x25c1, 0x25c0,
  0x25b7, 0x25b6, 0x2664, 0x2660, 0x2661, 0x2665, 0x2667, 0x2663,
  0x2299, 0x25c8, 0x25a3, 0x25d0, 0x25d1, 0x2592, 0x25a4, 0x25a5,
  0x25a8, 0x25a7, 0x25a6, 0x25a9, 0x2668, 0x260f, 0x260e, 0x261c,
  0x261e, 0x00b6, 0x2020, 0x2021, 0x2195, 0x2197, 0x2199, 0x2196,
  0x2198, 0x266d, 0x2669, 0x266a, 0x266c, 0x327f, 0x321c, 0x2116,
  0x33c7, 0x2122, 0x33c2, 0x33d8, 0x2121, 0x20ac, 0x00ae, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x23 */
  0xff01, 0xff02, 0xff03, 0xff04, 0xff05, 0xff06, 0xff07, 0xff08,
  0xff09, 0xff0a, 0xff0b, 0xff0c, 0xff0d, 0xff0e, 0xff0f, 0xff10,
  0xff11, 0xff12, 0xff13, 0xff14, 0xff15, 0xff16, 0xff17, 0xff18,
  0xff19, 0xff1a, 0xff1b, 0xff1c, 0xff1d, 0xff1e, 0xff1f, 0xff20,
  0xff21, 0xff22, 0xff23, 0xff24, 0xff25, 0xff26, 0xff27, 0xff28,
  0xff29, 0xff2a, 0xff2b, 0xff2c, 0xff2d, 0xff2e, 0xff2f, 0xff30,
  0xff31, 0xff32, 0xff33, 0xff34, 0xff35, 0xff36, 0xff37, 0xff38,
  0xff39, 0xff3a, 0xff3b, 0xffe6, 0xff3d, 0xff3e, 0xff3f, 0xff40,
  0xff41, 0xff42, 0xff43, 0xff44, 0xff45, 0xff46, 0xff47, 0xff48,
  0xff49, 0xff4a, 0xff4b, 0xff4c, 0xff4d, 0xff4e, 0xff4f, 0xff50,
  0xff51, 0xff52, 0xff53, 0xff54, 0xff55, 0xff56, 0xff57, 0xff58,
  0xff59, 0xff5a, 0xff5b, 0xff5c, 0xff5d, 0xffe3,
  /* 0x24 */
  0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3138,
  0x3139, 0x313a, 0x313b, 0x313c, 0x313d, 0x313e, 0x313f, 0x3140,
  0x3141, 0x3142, 0x3143, 0x3144, 0x3145, 0x3146, 0x3147, 0x3148,
  0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e, 0x314f, 0x3150,
  0x3151, 0x3152, 0x3153, 0x3154, 0x3155, 0x3156, 0x3157, 0x3158,
  0x3159, 0x315a, 0x315b, 0x315c, 0x315d, 0x315e, 0x315f, 0x3160,
  0x3161, 0x3162, 0x3163, 0x3164, 0x3165, 0x3166, 0x3167, 0x3168,
  0x3169, 0x316a, 0x316b, 0x316c, 0x316d, 0x316e, 0x316f, 0x3170,
  0x3171, 0x3172, 0x3173, 0x3174, 0x3175, 0x3176, 0x3177, 0x3178,
  0x3179, 0x317a, 0x317b, 0x317c, 0x317d, 0x317e, 0x317f, 0x3180,
  0x3181, 0x3182, 0x3183, 0x3184, 0x3185, 0x3186, 0x3187, 0x3188,
  0x3189, 0x318a, 0x318b, 0x318c, 0x318d, 0x318e,
  /* 0x25 */
  0x2170, 0x2171, 0x2172, 0x2173, 0x2174, 0x2175, 0x2176, 0x2177,
  0x2178, 0x2179, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0x2160,
  0x2161, 0x2162, 0x2163, 0x2164, 0x2165, 0x2166, 0x2167, 0x2168,
  0x2169, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397, 0x0398,
  0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f, 0x03a0,
  0x03a1, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7, 0x03a8, 0x03a9,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7, 0x03b8,
  0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf, 0x03c0,
  0x03c1, 0x03c3, 0x03c4, 0x03c5, 0x03c6, 0x03c7, 0x03c8, 0x03c9,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x26 */
  0x2500, 0x2502, 0x250c, 0x2510, 0x2518, 0x2514, 0x251c, 0x252c,
  0x2524, 0x2534, 0x253c, 0x2501, 0x2503, 0x250f, 0x2513, 0x251b,
  0x2517, 0x2523, 0x2533, 0x252b, 0x253b, 0x254b, 0x2520, 0x252f,
  0x2528, 0x2537, 0x253f, 0x251d, 0x2530, 0x2525, 0x2538, 0x2542,
  0x2512, 0x2511, 0x251a, 0x2519, 0x2516, 0x2515, 0x250e, 0x250d,
  0x251e, 0x251f, 0x2521, 0x2522, 0x2526, 0x2527, 0x2529, 0x252a,
  0x252d, 0x252e, 0x2531, 0x2532, 0x2535, 0x2536, 0x2539, 0x253a,
  0x253d, 0x253e, 0x2540, 0x2541, 0x2543, 0x2544, 0x2545, 0x2546,
  0x2547, 0x2548, 0x2549, 0x254a, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x27 */
  0x3395, 0x3396, 0x3397, 0x2113, 0x3398, 0x33c4, 0x33a3, 0x33a4,
  0x33a5, 0x33a6, 0x3399, 0x339a, 0x339b, 0x339c, 0x339d, 0x339e,
  0x339f, 0x33a0, 0x33a1, 0x33a2, 0x33ca, 0x338d, 0x338e, 0x338f,
  0x33cf, 0x3388, 0x3389, 0x33c8, 0x33a7, 0x33a8, 0x33b0, 0x33b1,
  0x33b2, 0x33b3, 0x33b4, 0x33b5, 0x33b6, 0x33b7, 0x33b8, 0x33b9,
  0x3380, 0x3381, 0x3382, 0x3383, 0x3384, 0x33ba, 0x33bb, 0x33bc,
  0x33bd, 0x33be, 0x33bf, 0x3390, 0x3391, 0x3392, 0x3393, 0x3394,
  0x2126, 0x33c0, 0x33c1, 0x338a, 0x338b, 0x338c, 0x33d6, 0x33c5,
  0x33ad, 0x33ae, 0x33af, 0x33db, 0x33a9, 0x33aa, 0x33ab, 0x33ac,
  0x33dd, 0x33d0, 0x33d3, 0x33c3, 0x33c9, 0x33dc, 0x33c6, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x28 */
  0x00c6, 0x00d0, 0x00aa, 0x0126, 0xfffd, 0x0132, 0xfffd, 0x013f,
  0x0141, 0x00d8, 0x0152, 0x00ba, 0x00de, 0x0166, 0x014a, 0xfffd,
  0x3260, 0x3261, 0x3262, 0x3263, 0x3264, 0x3265, 0x3266, 0x3267,
  0x3268, 0x3269, 0x326a, 0x326b, 0x326c, 0x326d, 0x326e, 0x326f,
  0x3270, 0x3271, 0x3272, 0x3273, 0x3274, 0x3275, 0x3276, 0x3277,
  0x3278, 0x3279, 0x327a, 0x327b, 0x24d0, 0x24d1, 0x24d2, 0x24d3,
  0x24d4, 0x24d5, 0x24d6, 0x24d7, 0x24d8, 0x24d9, 0x24da, 0x24db,
  0x24dc, 0x24dd, 0x24de, 0x24df, 0x24e0, 0x24e1, 0x24e2, 0x24e3,
  0x24e4, 0x24e5, 0x24e6, 0x24e7, 0x24e8, 0x24e9, 0x2460, 0x2461,
  0x2462, 0x2463, 0x2464, 0x2465, 0x2466, 0x2467, 0x2468, 0x2469,
  0x246a, 0x246b, 0x246c, 0x246d, 0x246e, 0x00bd, 0x2153, 0x2154,
  0x00bc, 0x00be, 0x215b, 0x215c, 0x215d, 0x215e,
  /* 0x29 */
  0x00e6, 0x0111, 0x00f0, 0x0127, 0x0131, 0x0133, 0x0138, 0x0140,
  0x0142, 0x00f8, 0x0153, 0x00df, 0x00fe, 0x0167, 0x014b, 0x0149,
  0x3200, 0x3201, 0x3202, 0x3203, 0x3204, 0x3205, 0x3206, 0x3207,
  0x3208, 0x3209, 0x320a, 0x320b, 0x320c, 0x320d, 0x320e, 0x320f,
  0x3210, 0x3211, 0x3212, 0x3213, 0x3214, 0x3215, 0x3216, 0x3217,
  0x3218, 0x3219, 0x321a, 0x321b, 0x249c, 0x249d, 0x249e, 0x249f,
  0x24a0, 0x24a1, 0x24a2, 0x24a3, 0x24a4, 0x24a5, 0x24a6, 0x24a7,
  0x24a8, 0x24a9, 0x24aa, 0x24ab, 0x24ac, 0x24ad, 0x24ae, 0x24af,
  0x24b0, 0x24b1, 0x24b2, 0x24b3, 0x24b4, 0x24b5, 0x2474, 0x2475,
  0x2476, 0x2477, 0x2478, 0x2479, 0x247a, 0x247b, 0x247c, 0x247d,
  0x247e, 0x247f, 0x2480, 0x2481, 0x2482, 0x00b9, 0x00b2, 0x00b3,
  0x2074, 0x207f, 0x2081, 0x2082, 0x2083, 0x2084,
  /* 0x2a */
  0x3041, 0x3042, 0x3043, 0x3044, 0x3045, 0x3046, 0x3047, 0x3048,
  0x3049, 0x304a, 0x304b, 0x304c, 0x304d, 0x304e, 0x304f, 0x3050,
  0x3051, 0x3052, 0x3053, 0x3054, 0x3055, 0x3056, 0x3057, 0x3058,
  0x3059, 0x305a, 0x305b, 0x305c, 0x305d, 0x305e, 0x305f, 0x3060,
  0x3061, 0x3062, 0x3063, 0x3064, 0x3065, 0x3066, 0x3067, 0x3068,
  0x3069, 0x306a, 0x306b, 0x306c, 0x306d, 0x306e, 0x306f, 0x3070,
  0x3071, 0x3072, 0x3073, 0x3074, 0x3075, 0x3076, 0x3077, 0x3078,
  0x3079, 0x307a, 0x307b, 0x307c, 0x307d, 0x307e, 0x307f, 0x3080,
  0x3081, 0x3082, 0x3083, 0x3084, 0x3085, 0x3086, 0x3087, 0x3088,
  0x3089, 0x308a, 0x308b, 0x308c, 0x308d, 0x308e, 0x308f, 0x3090,
  0x3091, 0x3092, 0x3093, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x2b */
  0x30a1, 0x30a2, 0x30a3, 0x30a4, 0x30a5, 0x30a6, 0x30a7, 0x30a8,
  0x30a9, 0x30aa, 0x30ab, 0x30ac, 0x30ad, 0x30ae, 0x30af, 0x30b0,
  0x30b1, 0x30b2, 0x30b3, 0x30b4, 0x30b5, 0x30b6, 0x30b7, 0x30b8,
  0x30b9, 0x30ba, 0x30bb, 0x30bc, 0x30bd, 0x30be, 0x30bf, 0x30c0,
  0x30c1, 0x30c2, 0x30c3, 0x30c4, 0x30c5, 0x30c6, 0x30c7, 0x30c8,
  0x30c9, 0x30ca, 0x30cb, 0x30cc, 0x30cd, 0x30ce, 0x30cf, 0x30d0,
  0x30d1, 0x30d2, 0x30d3, 0x30d4, 0x30d5, 0x30d6, 0x30d7, 0x30d8,
  0x30d9, 0x30da, 0x30db, 0x30dc, 0x30dd, 0x30de, 0x30df, 0x30e0,
  0x30e1, 0x30e2, 0x30e3, 0x30e4, 0x30e5, 0x30e6, 0x30e7, 0x30e8,
  0x30e9, 0x30ea, 0x30eb, 0x30ec, 0x30ed, 0x30ee, 0x30ef, 0x30f0,
  0x30f1, 0x30f2, 0x30f3, 0x30f4, 0x30f5, 0x30f6, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  /* 0x2c */
  0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0401, 0x0416,
  0x0417, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
  0x041f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426,
  0x0427, 0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e,
  0x042f, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd, 0xfffd,
  0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0451, 0x0436,
  0x0437, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
  0x043f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446,
  0x0447, 0x0448, 0x0449, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e,
  0x044f,
};
static const unsigned short ksc5601_2uni_page30[2350] = {
  /* 0x30 */
  0xac00, 0xac01, 0xac04, 0xac07, 0xac08, 0xac09, 0xac0a, 0xac10,
  0xac11, 0xac12, 0xac13, 0xac14, 0xac15, 0xac16, 0xac17, 0xac19,
  0xac1a, 0xac1b, 0xac1c, 0xac1d, 0xac20, 0xac24, 0xac2c, 0xac2d,
  0xac2f, 0xac30, 0xac31, 0xac38, 0xac39, 0xac3c, 0xac40, 0xac4b,
  0xac4d, 0xac54, 0xac58, 0xac5c, 0xac70, 0xac71, 0xac74, 0xac77,
  0xac78, 0xac7a, 0xac80, 0xac81, 0xac83, 0xac84, 0xac85, 0xac86,
  0xac89, 0xac8a, 0xac8b, 0xac8c, 0xac90, 0xac94, 0xac9c, 0xac9d,
  0xac9f, 0xaca0, 0xaca1, 0xaca8, 0xaca9, 0xacaa, 0xacac, 0xacaf,
  0xacb0, 0xacb8, 0xacb9, 0xacbb, 0xacbc, 0xacbd, 0xacc1, 0xacc4,
  0xacc8, 0xaccc, 0xacd5, 0xacd7, 0xace0, 0xace1, 0xace4, 0xace7,
  0xace8, 0xacea, 0xacec, 0xacef, 0xacf0, 0xacf1, 0xacf3, 0xacf5,
  0xacf6, 0xacfc, 0xacfd, 0xad00, 0xad04, 0xad06,
  /* 0x31 */
  0xad0c, 0xad0d, 0xad0f, 0xad11, 0xad18, 0xad1c, 0xad20, 0xad29,
  0xad2c, 0xad2d, 0xad34, 0xad35, 0xad38, 0xad3c, 0xad44, 0xad45,
  0xad47, 0xad49, 0xad50, 0xad54, 0xad58, 0xad61, 0xad63, 0xad6c,
  0xad6d, 0xad70, 0xad73, 0xad74, 0xad75, 0xad76, 0xad7b, 0xad7c,
  0xad7d, 0xad7f, 0xad81, 0xad82, 0xad88, 0xad89, 0xad8c, 0xad90,
  0xad9c, 0xad9d, 0xada4, 0xadb7, 0xadc0, 0xadc1, 0xadc4, 0xadc8,
  0xadd0, 0xadd1, 0xadd3, 0xaddc, 0xade0, 0xade4, 0xadf8, 0xadf9,
  0xadfc, 0xadff, 0xae00, 0xae01, 0xae08, 0xae09, 0xae0b, 0xae0d,
  0xae14, 0xae30, 0xae31, 0xae34, 0xae37, 0xae38, 0xae3a, 0xae40,
  0xae41, 0xae43, 0xae45, 0xae46, 0xae4a, 0xae4c, 0xae4d, 0xae4e,
  0xae50, 0xae54, 0xae56, 0xae5c, 0xae5d, 0xae5f, 0xae60, 0xae61,
  0xae65, 0xae68, 0xae69, 0xae6c, 0xae70, 0xae78,
  /* 0x32 */
  0xae79, 0xae7b, 0xae7c, 0xae7d, 0xae84, 0xae85, 0xae8c, 0xaebc,
  0xaebd, 0xaebe, 0xaec0, 0xaec4, 0xaecc, 0xaecd, 0xaecf, 0xaed0,
  0xaed1, 0xaed8, 0xaed9, 0xaedc, 0xaee8, 0xaeeb, 0xaeed, 0xaef4,
  0xaef8, 0xaefc, 0xaf07, 0xaf08, 0xaf0d, 0xaf10, 0xaf2c, 0xaf2d,
  0xaf30, 0xaf32, 0xaf34, 0xaf3c, 0xaf3d, 0xaf3f, 0xaf41, 0xaf42,
  0xaf43, 0xaf48, 0xaf49, 0xaf50, 0xaf5c, 0xaf5d, 0xaf64, 0xaf65,
  0xaf79, 0xaf80, 0xaf84, 0xaf88, 0xaf90, 0xaf91, 0xaf95, 0xaf9c,
  0xafb8, 0xafb9, 0xafbc, 0xafc0, 0xafc7, 0xafc8, 0xafc9, 0xafcb,
  0xafcd, 0xafce, 0xafd4, 0xafdc, 0xafe8, 0xafe9, 0xaff0, 0xaff1,
  0xaff4, 0xaff8, 0xb000, 0xb001, 0xb004, 0xb00c, 0xb010, 0xb014,
  0xb01c, 0xb01d, 0xb028, 0xb044, 0xb045, 0xb048, 0xb04a, 0xb04c,
  0xb04e, 0xb053, 0xb054, 0xb055, 0xb057, 0xb059,
  /* 0x33 */
  0xb05d, 0xb07c, 0xb07d, 0xb080, 0xb084, 0xb08c, 0xb08d, 0xb08f,
  0xb091, 0xb098, 0xb099, 0xb09a, 0xb09c, 0xb09f, 0xb0a0, 0xb0a1,
  0xb0a2, 0xb0a8, 0xb0a9, 0xb0ab, 0xb0ac, 0xb0ad, 0xb0ae, 0xb0af,
  0xb0b1, 0xb0b3, 0xb0b4, 0xb0b5, 0xb0b8, 0xb0bc, 0xb0c4, 0xb0c5,
  0xb0c7, 0xb0c8, 0xb0c9, 0xb0d0, 0xb0d1, 0xb0d4, 0xb0d8, 0xb0e0,
  0xb0e5, 0xb108, 0xb109, 0xb10b, 0xb10c, 0xb110, 0xb112, 0xb113,
  0xb118, 0xb119, 0xb11b, 0xb11c, 0xb11d, 0xb123, 0xb124, 0xb125,
  0xb128, 0xb12c, 0xb134, 0xb135, 0xb137, 0xb138, 0xb139, 0xb140,
  0xb141, 0xb144, 0xb148, 0xb150, 0xb151, 0xb154, 0xb155, 0xb158,
  0xb15c, 0xb160, 0xb178, 0xb179, 0xb17c, 0xb180, 0xb182, 0xb188,
  0xb189, 0xb18b, 0xb18d, 0xb192, 0xb193, 0xb194, 0xb198, 0xb19c,
  0xb1a8, 0xb1cc, 0xb1d0, 0xb1d4, 0xb1dc, 0xb1dd,
  /* 0x34 */
  0xb1df, 0xb1e8, 0xb1e9, 0xb1ec, 0xb1f0, 0xb1f9, 0xb1fb, 0xb1fd,
  0xb204, 0xb205, 0xb208, 0xb20b, 0xb20c, 0xb214, 0xb215, 0xb217,
  0xb219, 0xb220, 0xb234, 0xb23c, 0xb258, 0xb25c, 0xb260, 0xb268,
  0xb269, 0xb274, 0xb275, 0xb27c, 0xb284, 0xb285, 0xb289, 0xb290,
  0xb291, 0xb294, 0xb298, 0xb299, 0xb29a, 0xb2a0, 0xb2a1, 0xb2a3,
  0xb2a5, 0xb2a6, 0xb2aa, 0xb2ac, 0xb2b0, 0xb2b4, 0xb2c8, 0xb2c9,
  0xb2cc, 0xb2d0, 0xb2d2, 0xb2d8, 0xb2d9, 0xb2db, 0xb2dd, 0xb2e2,
  0xb2e4, 0xb2e5, 0xb2e6, 0xb2e8, 0xb2eb, 0xb2ec, 0xb2ed, 0xb2ee,
  0xb2ef, 0xb2f3, 0xb2f4, 0xb2f5, 0xb2f7, 0xb2f8, 0xb2f9, 0xb2fa,
  0xb2fb, 0xb2ff, 0xb300, 0xb301, 0xb304, 0xb308, 0xb310, 0xb311,
  0xb313, 0xb314, 0xb315, 0xb31c, 0xb354, 0xb355, 0xb356, 0xb358,
  0xb35b, 0xb35c, 0xb35e, 0xb35f, 0xb364, 0xb365,
  /* 0x35 */
  0xb367, 0xb369, 0xb36b, 0xb36e, 0xb370, 0xb371, 0xb374, 0xb378,
  0xb380, 0xb381, 0xb383, 0xb384, 0xb385, 0xb38c, 0xb390, 0xb394,
  0xb3a0, 0xb3a1, 0xb3a8, 0xb3ac, 0xb3c4, 0xb3c5, 0xb3c8, 0xb3cb,
  0xb3cc, 0xb3ce, 0xb3d0, 0xb3d4, 0xb3d5, 0xb3d7, 0xb3d9, 0xb3db,
  0xb3dd, 0xb3e0, 0xb3e4, 0xb3e8, 0xb3fc, 0xb410, 0xb418, 0xb41c,
  0xb420, 0xb428, 0xb429, 0xb42b, 0xb434, 0xb450, 0xb451, 0xb454,
  0xb458, 0xb460, 0xb461, 0xb463, 0xb465, 0xb46c, 0xb480, 0xb488,
  0xb49d, 0xb4a4, 0xb4a8, 0xb4ac, 0xb4b5, 0xb4b7, 0xb4b9, 0xb4c0,
  0xb4c4, 0xb4c8, 0xb4d0, 0xb4d5, 0xb4dc, 0xb4dd, 0xb4e0, 0xb4e3,
  0xb4e4, 0xb4e6, 0xb4ec, 0xb4ed, 0xb4ef, 0xb4f1, 0xb4f8, 0xb514,
  0xb515, 0xb518, 0xb51b, 0xb51c, 0xb524, 0xb525, 0xb527, 0xb528,
  0xb529, 0xb52a, 0xb530, 0xb531, 0xb534, 0xb538,
  /* 0x36 */
  0xb540, 0xb541, 0xb543, 0xb544, 0xb545, 0xb54b, 0xb54c, 0xb54d,
  0xb550, 0xb554, 0xb55c, 0xb55d, 0xb55f, 0xb560, 0xb561, 0xb5a0,
  0xb5a1, 0xb5a4, 0xb5a8, 0xb5aa, 0xb5ab, 0xb5b0, 0xb5b1, 0xb5b3,
  0xb5b4, 0xb5b5, 0xb5bb, 0xb5bc, 0xb5bd, 0xb5c0, 0xb5c4, 0xb5cc,
  0xb5cd, 0xb5cf, 0xb5d0, 0xb5d1, 0xb5d8, 0xb5ec, 0xb610, 0xb611,
  0xb614, 0xb618, 0xb625, 0xb62c, 0xb634, 0xb648, 0xb664, 0xb668,
  0xb69c, 0xb69d, 0xb6a0, 0xb6a4, 0xb6ab, 0xb6ac, 0xb6b1, 0xb6d4,
  0xb6f0, 0xb6f4, 0xb6f8, 0xb700, 0xb701, 0xb705, 0xb728, 0xb729,
  0xb72c, 0xb72f, 0xb730, 0xb738, 0xb739, 0xb73b, 0xb744, 0xb748,
  0xb74c, 0xb754, 0xb755, 0xb760, 0xb764, 0xb768, 0xb770, 0xb771,
  0xb773, 0xb775, 0xb77c, 0xb77d, 0xb780, 0xb784, 0xb78c, 0xb78d,
  0xb78f, 0xb790, 0xb791, 0xb792, 0xb796, 0xb797,
  /* 0x37 */
  0xb798, 0xb799, 0xb79c, 0xb7a0, 0xb7a8, 0xb7a9, 0xb7ab, 0xb7ac,
  0xb7ad, 0xb7b4, 0xb7b5, 0xb7b8, 0xb7c7, 0xb7c9, 0xb7ec, 0xb7ed,
  0xb7f0, 0xb7f4, 0xb7fc, 0xb7fd, 0xb7ff, 0xb800, 0xb801, 0xb807,
  0xb808, 0xb809, 0xb80c, 0xb810, 0xb818, 0xb819, 0xb81b, 0xb81d,
  0xb824, 0xb825, 0xb828, 0xb82c, 0xb834, 0xb835, 0xb837, 0xb838,
  0xb839, 0xb840, 0xb844, 0xb851, 0xb853, 0xb85c, 0xb85d, 0xb860,
  0xb864, 0xb86c, 0xb86d, 0xb86f, 0xb871, 0xb878, 0xb87c, 0xb88d,
  0xb8a8, 0xb8b0, 0xb8b4, 0xb8b8, 0xb8c0, 0xb8c1, 0xb8c3, 0xb8c5,
  0xb8cc, 0xb8d0, 0xb8d4, 0xb8dd, 0xb8df, 0xb8e1, 0xb8e8, 0xb8e9,
  0xb8ec, 0xb8f0, 0xb8f8, 0xb8f9, 0xb8fb, 0xb8fd, 0xb904, 0xb918,
  0xb920, 0xb93c, 0xb93d, 0xb940, 0xb944, 0xb94c, 0xb94f, 0xb951,
  0xb958, 0xb959, 0xb95c, 0xb960, 0xb968, 0xb969,
  /* 0x38 */
  0xb96b, 0xb96d, 0xb974, 0xb975, 0xb978, 0xb97c, 0xb984, 0xb985,
  0xb987, 0xb989, 0xb98a, 0xb98d, 0xb98e, 0xb9ac, 0xb9ad, 0xb9b0,
  0xb9b4, 0xb9bc, 0xb9bd, 0xb9bf, 0xb9c1, 0xb9c8, 0xb9c9, 0xb9cc,
  0xb9ce, 0xb9cf, 0xb9d0, 0xb9d1, 0xb9d2, 0xb9d8, 0xb9d9, 0xb9db,
  0xb9dd, 0xb9de, 0xb9e1, 0xb9e3, 0xb9e4, 0xb9e5, 0xb9e8, 0xb9ec,
  0xb9f4, 0xb9f5, 0xb9f7, 0xb9f8, 0xb9f9, 0xb9fa, 0xba00, 0xba01,
  0xba08, 0xba15, 0xba38, 0xba39, 0xba3c, 0xba40, 0xba42, 0xba48,
  0xba49, 0xba4b, 0xba4d, 0xba4e, 0xba53, 0xba54, 0xba55, 0xba58,
  0xba5c, 0xba64, 0xba65, 0xba67, 0xba68, 0xba69, 0xba70, 0xba71,
  0xba74, 0xba78, 0xba83, 0xba84, 0xba85, 0xba87, 0xba8c, 0xbaa8,
  0xbaa9, 0xbaab, 0xbaac, 0xbab0, 0xbab2, 0xbab8, 0xbab9, 0xbabb,
  0xbabd, 0xbac4, 0xbac8, 0xbad8, 0xbad9, 0xbafc,
  /* 0x39 */
  0xbb00, 0xbb04, 0xbb0d, 0xbb0f, 0xbb11, 0xbb18, 0xbb1c, 0xbb20,
  0xbb29, 0xbb2b, 0xbb34, 0xbb35, 0xbb36, 0xbb38, 0xbb3b, 0xbb3c,
  0xbb3d, 0xbb3e, 0xbb44, 0xbb45, 0xbb47, 0xbb49, 0xbb4d, 0xbb4f,
  0xbb50, 0xbb54, 0xbb58, 0xbb61, 0xbb63, 0xbb6c, 0xbb88, 0xbb8c,
  0xbb90, 0xbba4, 0xbba8, 0xbbac, 0xbbb4, 0xbbb7, 0xbbc0, 0xbbc4,
  0xbbc8, 0xbbd0, 0xbbd3, 0xbbf8, 0xbbf9, 0xbbfc, 0xbbff, 0xbc00,
  0xbc02, 0xbc08, 0xbc09, 0xbc0b, 0xbc0c, 0xbc0d, 0xbc0f, 0xbc11,
  0xbc14, 0xbc15, 0xbc16, 0xbc17, 0xbc18, 0xbc1b, 0xbc1c, 0xbc1d,
  0xbc1e, 0xbc1f, 0xbc24, 0xbc25, 0xbc27, 0xbc29, 0xbc2d, 0xbc30,
  0xbc31, 0xbc34, 0xbc38, 0xbc40, 0xbc41, 0xbc43, 0xbc44, 0xbc45,
  0xbc49, 0xbc4c, 0xbc4d, 0xbc50, 0xbc5d, 0xbc84, 0xbc85, 0xbc88,
  0xbc8b, 0xbc8c, 0xbc8e, 0xbc94, 0xbc95, 0xbc97,
  /* 0x3a */
  0xbc99, 0xbc9a, 0xbca0, 0xbca1, 0xbca4, 0xbca7, 0xbca8, 0xbcb0,
  0xbcb1, 0xbcb3, 0xbcb4, 0xbcb5, 0xbcbc, 0xbcbd, 0xbcc0, 0xbcc4,
  0xbccd, 0xbccf, 0xbcd0, 0xbcd1, 0xbcd5, 0xbcd8, 0xbcdc, 0xbcf4,
  0xbcf5, 0xbcf6, 0xbcf8, 0xbcfc, 0xbd04, 0xbd05, 0xbd07, 0xbd09,
  0xbd10, 0xbd14, 0xbd24, 0xbd2c, 0xbd40, 0xbd48, 0xbd49, 0xbd4c,
  0xbd50, 0xbd58, 0xbd59, 0xbd64, 0xbd68, 0xbd80, 0xbd81, 0xbd84,
  0xbd87, 0xbd88, 0xbd89, 0xbd8a, 0xbd90, 0xbd91, 0xbd93, 0xbd95,
  0xbd99, 0xbd9a, 0xbd9c, 0xbda4, 0xbdb0, 0xbdb8, 0xbdd4, 0xbdd5,
  0xbdd8, 0xbddc, 0xbde9, 0xbdf0, 0xbdf4, 0xbdf8, 0xbe00, 0xbe03,
  0xbe05, 0xbe0c, 0xbe0d, 0xbe10, 0xbe14, 0xbe1c, 0xbe1d, 0xbe1f,
  0xbe44, 0xbe45, 0xbe48, 0xbe4c, 0xbe4e, 0xbe54, 0xbe55, 0xbe57,
  0xbe59, 0xbe5a, 0xbe5b, 0xbe60, 0xbe61, 0xbe64,
  /* 0x3b */
  0xbe68, 0xbe6a, 0xbe70, 0xbe71, 0xbe73, 0xbe74, 0xbe75, 0xbe7b,
  0xbe7c, 0xbe7d, 0xbe80, 0xbe84, 0xbe8c, 0xbe8d, 0xbe8f, 0xbe90,
  0xbe91, 0xbe98, 0xbe99, 0xbea8, 0xbed0, 0xbed1, 0xbed4, 0xbed7,
  0xbed8, 0xbee0, 0xbee3, 0xbee4, 0xbee5, 0xbeec, 0xbf01, 0xbf08,
  0xbf09, 0xbf18, 0xbf19, 0xbf1b, 0xbf1c, 0xbf1d, 0xbf40, 0xbf41,
  0xbf44, 0xbf48, 0xbf50, 0xbf51, 0xbf55, 0xbf94, 0xbfb0, 0xbfc5,
  0xbfcc, 0xbfcd, 0xbfd0, 0xbfd4, 0xbfdc, 0xbfdf, 0xbfe1, 0xc03c,
  0xc051, 0xc058, 0xc05c, 0xc060, 0xc068, 0xc069, 0xc090, 0xc091,
  0xc094, 0xc098, 0xc0a0, 0xc0a1, 0xc0a3, 0xc0a5, 0xc0ac, 0xc0ad,
  0xc0af, 0xc0b0, 0xc0b3, 0xc0b4, 0xc0b5, 0xc0b6, 0xc0bc, 0xc0bd,
  0xc0bf, 0xc0c0, 0xc0c1, 0xc0c5, 0xc0c8, 0xc0c9, 0xc0cc, 0xc0d0,
  0xc0d8, 0xc0d9, 0xc0db, 0xc0dc, 0xc0dd, 0xc0e4,
  /* 0x3c */
  0xc0e5, 0xc0e8, 0xc0ec, 0xc0f4, 0xc0f5, 0xc0f7, 0xc0f9, 0xc100,
  0xc104, 0xc108, 0xc110, 0xc115, 0xc11c, 0xc11d, 0xc11e, 0xc11f,
  0xc120, 0xc123, 0xc124, 0xc126, 0xc127, 0xc12c, 0xc12d, 0xc12f,
  0xc130, 0xc131, 0xc136, 0xc138, 0xc139, 0xc13c, 0xc140, 0xc148,
  0xc149, 0xc14b, 0xc14c, 0xc14d, 0xc154, 0xc155, 0xc158, 0xc15c,
  0xc164, 0xc165, 0xc167, 0xc168, 0xc169, 0xc170, 0xc174, 0xc178,
  0xc185, 0xc18c, 0xc18d, 0xc18e, 0xc190, 0xc194, 0xc196, 0xc19c,
  0xc19d, 0xc19f, 0xc1a1, 0xc1a5, 0xc1a8, 0xc1a9, 0xc1ac, 0xc1b0,
  0xc1bd, 0xc1c4, 0xc1c8, 0xc1cc, 0xc1d4, 0xc1d7, 0xc1d8, 0xc1e0,
  0xc1e4, 0xc1e8, 0xc1f0, 0xc1f1, 0xc1f3, 0xc1fc, 0xc1fd, 0xc200,
  0xc204, 0xc20c, 0xc20d, 0xc20f, 0xc211, 0xc218, 0xc219, 0xc21c,
  0xc21f, 0xc220, 0xc228, 0xc229, 0xc22b, 0xc22d,
  /* 0x3d */
  0xc22f, 0xc231, 0xc232, 0xc234, 0xc248, 0xc250, 0xc251, 0xc254,
  0xc258, 0xc260, 0xc265, 0xc26c, 0xc26d, 0xc270, 0xc274, 0xc27c,
  0xc27d, 0xc27f, 0xc281, 0xc288, 0xc289, 0xc290, 0xc298, 0xc29b,
  0xc29d, 0xc2a4, 0xc2a5, 0xc2a8, 0xc2ac, 0xc2ad, 0xc2b4, 0xc2b5,
  0xc2b7, 0xc2b9, 0xc2dc, 0xc2dd, 0xc2e0, 0xc2e3, 0xc2e4, 0xc2eb,
  0xc2ec, 0xc2ed, 0xc2ef, 0xc2f1, 0xc2f6, 0xc2f8, 0xc2f9, 0xc2fb,
  0xc2fc, 0xc300, 0xc308, 0xc309, 0xc30c, 0xc30d, 0xc313, 0xc314,
  0xc315, 0xc318, 0xc31c, 0xc324, 0xc325, 0xc328, 0xc329, 0xc345,
  0xc368, 0xc369, 0xc36c, 0xc370, 0xc372, 0xc378, 0xc379, 0xc37c,
  0xc37d, 0xc384, 0xc388, 0xc38c, 0xc3c0, 0xc3d8, 0xc3d9, 0xc3dc,
  0xc3df, 0xc3e0, 0xc3e2, 0xc3e8, 0xc3e9, 0xc3ed, 0xc3f4, 0xc3f5,
  0xc3f8, 0xc408, 0xc410, 0xc424, 0xc42c, 0xc430,
  /* 0x3e */
  0xc434, 0xc43c, 0xc43d, 0xc448, 0xc464, 0xc465, 0xc468, 0xc46c,
  0xc474, 0xc475, 0xc479, 0xc480, 0xc494, 0xc49c, 0xc4b8, 0xc4bc,
  0xc4e9, 0xc4f0, 0xc4f1, 0xc4f4, 0xc4f8, 0xc4fa, 0xc4ff, 0xc500,
  0xc501, 0xc50c, 0xc510, 0xc514, 0xc51c, 0xc528, 0xc529, 0xc52c,
  0xc530, 0xc538, 0xc539, 0xc53b, 0xc53d, 0xc544, 0xc545, 0xc548,
  0xc549, 0xc54a, 0xc54c, 0xc54d, 0xc54e, 0xc553, 0xc554, 0xc555,
  0xc557, 0xc558, 0xc559, 0xc55d, 0xc55e, 0xc560, 0xc561, 0xc564,
  0xc568, 0xc570, 0xc571, 0xc573, 0xc574, 0xc575, 0xc57c, 0xc57d,
  0xc580, 0xc584, 0xc587, 0xc58c, 0xc58d, 0xc58f, 0xc591, 0xc595,
  0xc597, 0xc598, 0xc59c, 0xc5a0, 0xc5a9, 0xc5b4, 0xc5b5, 0xc5b8,
  0xc5b9, 0xc5bb, 0xc5bc, 0xc5bd, 0xc5be, 0xc5c4, 0xc5c5, 0xc5c6,
  0xc5c7, 0xc5c8, 0xc5c9, 0xc5ca, 0xc5cc, 0xc5ce,
  /* 0x3f */
  0xc5d0, 0xc5d1, 0xc5d4, 0xc5d8, 0xc5e0, 0xc5e1, 0xc5e3, 0xc5e5,
  0xc5ec, 0xc5ed, 0xc5ee, 0xc5f0, 0xc5f4, 0xc5f6, 0xc5f7, 0xc5fc,
  0xc5fd, 0xc5fe, 0xc5ff, 0xc600, 0xc601, 0xc605, 0xc606, 0xc607,
  0xc608, 0xc60c, 0xc610, 0xc618, 0xc619, 0xc61b, 0xc61c, 0xc624,
  0xc625, 0xc628, 0xc62c, 0xc62d, 0xc62e, 0xc630, 0xc633, 0xc634,
  0xc635, 0xc637, 0xc639, 0xc63b, 0xc640, 0xc641, 0xc644, 0xc648,
  0xc650, 0xc651, 0xc653, 0xc654, 0xc655, 0xc65c, 0xc65d, 0xc660,
  0xc66c, 0xc66f, 0xc671, 0xc678, 0xc679, 0xc67c, 0xc680, 0xc688,
  0xc689, 0xc68b, 0xc68d, 0xc694, 0xc695, 0xc698, 0xc69c, 0xc6a4,
  0xc6a5, 0xc6a7, 0xc6a9, 0xc6b0, 0xc6b1, 0xc6b4, 0xc6b8, 0xc6b9,
  0xc6ba, 0xc6c0, 0xc6c1, 0xc6c3, 0xc6c5, 0xc6cc, 0xc6cd, 0xc6d0,
  0xc6d4, 0xc6dc, 0xc6dd, 0xc6e0, 0xc6e1, 0xc6e8,
  /* 0x40 */
  0xc6e9, 0xc6ec, 0xc6f0, 0xc6f8, 0xc6f9, 0xc6fd, 0xc704, 0xc705,
  0xc708, 0xc70c, 0xc714, 0xc715, 0xc717, 0xc719, 0xc720, 0xc721,
  0xc724, 0xc728, 0xc730, 0xc731, 0xc733, 0xc735, 0xc737, 0xc73c,
  0xc73d, 0xc740, 0xc744, 0xc74a, 0xc74c, 0xc74d, 0xc74f, 0xc751,
  0xc752, 0xc753, 0xc754, 0xc755, 0xc756, 0xc757, 0xc758, 0xc75c,
  0xc760, 0xc768, 0xc76b, 0xc774, 0xc775, 0xc778, 0xc77c, 0xc77d,
  0xc77e, 0xc783, 0xc784, 0xc785, 0xc787, 0xc788, 0xc789, 0xc78a,
  0xc78e, 0xc790, 0xc791, 0xc794, 0xc796, 0xc797, 0xc798, 0xc79a,
  0xc7a0, 0xc7a1, 0xc7a3, 0xc7a4, 0xc7a5, 0xc7a6, 0xc7ac, 0xc7ad,
  0xc7b0, 0xc7b4, 0xc7bc, 0xc7bd, 0xc7bf, 0xc7c0, 0xc7c1, 0xc7c8,
  0xc7c9, 0xc7cc, 0xc7ce, 0xc7d0, 0xc7d8, 0xc7dd, 0xc7e4, 0xc7e8,
  0xc7ec, 0xc800, 0xc801, 0xc804, 0xc808, 0xc80a,
  /* 0x41 */
  0xc810, 0xc811, 0xc813, 0xc815, 0xc816, 0xc81c, 0xc81d, 0xc820,
  0xc824, 0xc82c, 0xc82d, 0xc82f, 0xc831, 0xc838, 0xc83c, 0xc840,
  0xc848, 0xc849, 0xc84c, 0xc84d, 0xc854, 0xc870, 0xc871, 0xc874,
  0xc878, 0xc87a, 0xc880, 0xc881, 0xc883, 0xc885, 0xc886, 0xc887,
  0xc88b, 0xc88c, 0xc88d, 0xc894, 0xc89d, 0xc89f, 0xc8a1, 0xc8a8,
  0xc8bc, 0xc8bd, 0xc8c4, 0xc8c8, 0xc8cc, 0xc8d4, 0xc8d5, 0xc8d7,
  0xc8d9, 0xc8e0, 0xc8e1, 0xc8e4, 0xc8f5, 0xc8fc, 0xc8fd, 0xc900,
  0xc904, 0xc905, 0xc906, 0xc90c, 0xc90d, 0xc90f, 0xc911, 0xc918,
  0xc92c, 0xc934, 0xc950, 0xc951, 0xc954, 0xc958, 0xc960, 0xc961,
  0xc963, 0xc96c, 0xc970, 0xc974, 0xc97c, 0xc988, 0xc989, 0xc98c,
  0xc990, 0xc998, 0xc999, 0xc99b, 0xc99d, 0xc9c0, 0xc9c1, 0xc9c4,
  0xc9c7, 0xc9c8, 0xc9ca, 0xc9d0, 0xc9d1, 0xc9d3,
  /* 0x42 */
  0xc9d5, 0xc9d6, 0xc9d9, 0xc9da, 0xc9dc, 0xc9dd, 0xc9e0, 0xc9e2,
  0xc9e4, 0xc9e7, 0xc9ec, 0xc9ed, 0xc9ef, 0xc9f0, 0xc9f1, 0xc9f8,
  0xc9f9, 0xc9fc, 0xca00, 0xca08, 0xca09, 0xca0b, 0xca0c, 0xca0d,
  0xca14, 0xca18, 0xca29, 0xca4c, 0xca4d, 0xca50, 0xca54, 0xca5c,
  0xca5d, 0xca5f, 0xca60, 0xca61, 0xca68, 0xca7d, 0xca84, 0xca98,
  0xcabc, 0xcabd, 0xcac0, 0xcac4, 0xcacc, 0xcacd, 0xcacf, 0xcad1,
  0xcad3, 0xcad8, 0xcad9, 0xcae0, 0xcaec, 0xcaf4, 0xcb08, 0xcb10,
  0xcb14, 0xcb18, 0xcb20, 0xcb21, 0xcb41, 0xcb48, 0xcb49, 0xcb4c,
  0xcb50, 0xcb58, 0xcb59, 0xcb5d, 0xcb64, 0xcb78, 0xcb79, 0xcb9c,
  0xcbb8, 0xcbd4, 0xcbe4, 0xcbe7, 0xcbe9, 0xcc0c, 0xcc0d, 0xcc10,
  0xcc14, 0xcc1c, 0xcc1d, 0xcc21, 0xcc22, 0xcc27, 0xcc28, 0xcc29,
  0xcc2c, 0xcc2e, 0xcc30, 0xcc38, 0xcc39, 0xcc3b,
  /* 0x43 */
  0xcc3c, 0xcc3d, 0xcc3e, 0xcc44, 0xcc45, 0xcc48, 0xcc4c, 0xcc54,
  0xcc55, 0xcc57, 0xcc58, 0xcc59, 0xcc60, 0xcc64, 0xcc66, 0xcc68,
  0xcc70, 0xcc75, 0xcc98, 0xcc99, 0xcc9c, 0xcca0, 0xcca8, 0xcca9,
  0xccab, 0xccac, 0xccad, 0xccb4, 0xccb5, 0xccb8, 0xccbc, 0xccc4,
  0xccc5, 0xccc7, 0xccc9, 0xccd0, 0xccd4, 0xcce4, 0xccec, 0xccf0,
  0xcd01, 0xcd08, 0xcd09, 0xcd0c, 0xcd10, 0xcd18, 0xcd19, 0xcd1b,
  0xcd1d, 0xcd24, 0xcd28, 0xcd2c, 0xcd39, 0xcd5c, 0xcd60, 0xcd64,
  0xcd6c, 0xcd6d, 0xcd6f, 0xcd71, 0xcd78, 0xcd88, 0xcd94, 0xcd95,
  0xcd98, 0xcd9c, 0xcda4, 0xcda5, 0xcda7, 0xcda9, 0xcdb0, 0xcdc4,
  0xcdcc, 0xcdd0, 0xcde8, 0xcdec, 0xcdf0, 0xcdf8, 0xcdf9, 0xcdfb,
  0xcdfd, 0xce04, 0xce08, 0xce0c, 0xce14, 0xce19, 0xce20, 0xce21,
  0xce24, 0xce28, 0xce30, 0xce31, 0xce33, 0xce35,
  /* 0x44 */
  0xce58, 0xce59, 0xce5c, 0xce5f, 0xce60, 0xce61, 0xce68, 0xce69,
  0xce6b, 0xce6d, 0xce74, 0xce75, 0xce78, 0xce7c, 0xce84, 0xce85,
  0xce87, 0xce89, 0xce90, 0xce91, 0xce94, 0xce98, 0xcea0, 0xcea1,
  0xcea3, 0xcea4, 0xcea5, 0xceac, 0xcead, 0xcec1, 0xcee4, 0xcee5,
  0xcee8, 0xceeb, 0xceec, 0xcef4, 0xcef5, 0xcef7, 0xcef8, 0xcef9,
  0xcf00, 0xcf01, 0xcf04, 0xcf08, 0xcf10, 0xcf11, 0xcf13, 0xcf15,
  0xcf1c, 0xcf20, 0xcf24, 0xcf2c, 0xcf2d, 0xcf2f, 0xcf30, 0xcf31,
  0xcf38, 0xcf54, 0xcf55, 0xcf58, 0xcf5c, 0xcf64, 0xcf65, 0xcf67,
  0xcf69, 0xcf70, 0xcf71, 0xcf74, 0xcf78, 0xcf80, 0xcf85, 0xcf8c,
  0xcfa1, 0xcfa8, 0xcfb0, 0xcfc4, 0xcfe0, 0xcfe1, 0xcfe4, 0xcfe8,
  0xcff0, 0xcff1, 0xcff3, 0xcff5, 0xcffc, 0xd000, 0xd004, 0xd011,
  0xd018, 0xd02d, 0xd034, 0xd035, 0xd038, 0xd03c,
  /* 0x45 */
  0xd044, 0xd045, 0xd047, 0xd049, 0xd050, 0xd054, 0xd058, 0xd060,
  0xd06c, 0xd06d, 0xd070, 0xd074, 0xd07c, 0xd07d, 0xd081, 0xd0a4,
  0xd0a5, 0xd0a8, 0xd0ac, 0xd0b4, 0xd0b5, 0xd0b7, 0xd0b9, 0xd0c0,
  0xd0c1, 0xd0c4, 0xd0c8, 0xd0c9, 0xd0d0, 0xd0d1, 0xd0d3, 0xd0d4,
  0xd0d5, 0xd0dc, 0xd0dd, 0xd0e0, 0xd0e4, 0xd0ec, 0xd0ed, 0xd0ef,
  0xd0f0, 0xd0f1, 0xd0f8, 0xd10d, 0xd130, 0xd131, 0xd134, 0xd138,
  0xd13a, 0xd140, 0xd141, 0xd143, 0xd144, 0xd145, 0xd14c, 0xd14d,
  0xd150, 0xd154, 0xd15c, 0xd15d, 0xd15f, 0xd161, 0xd168, 0xd16c,
  0xd17c, 0xd184, 0xd188, 0xd1a0, 0xd1a1, 0xd1a4, 0xd1a8, 0xd1b0,
  0xd1b1, 0xd1b3, 0xd1b5, 0xd1ba, 0xd1bc, 0xd1c0, 0xd1d8, 0xd1f4,
  0xd1f8, 0xd207, 0xd209, 0xd210, 0xd22c, 0xd22d, 0xd230, 0xd234,
  0xd23c, 0xd23d, 0xd23f, 0xd241, 0xd248, 0xd25c,
  /* 0x46 */
  0xd264, 0xd280, 0xd281, 0xd284, 0xd288, 0xd290, 0xd291, 0xd295,
  0xd29c, 0xd2a0, 0xd2a4, 0xd2ac, 0xd2b1, 0xd2b8, 0xd2b9, 0xd2bc,
  0xd2bf, 0xd2c0, 0xd2c2, 0xd2c8, 0xd2c9, 0xd2cb, 0xd2d4, 0xd2d8,
  0xd2dc, 0xd2e4, 0xd2e5, 0xd2f0, 0xd2f1, 0xd2f4, 0xd2f8, 0xd300,
  0xd301, 0xd303, 0xd305, 0xd30c, 0xd30d, 0xd30e, 0xd310, 0xd314,
  0xd316, 0xd31c, 0xd31d, 0xd31f, 0xd320, 0xd321, 0xd325, 0xd328,
  0xd329, 0xd32c, 0xd330, 0xd338, 0xd339, 0xd33b, 0xd33c, 0xd33d,
  0xd344, 0xd345, 0xd37c, 0xd37d, 0xd380, 0xd384, 0xd38c, 0xd38d,
  0xd38f, 0xd390, 0xd391, 0xd398, 0xd399, 0xd39c, 0xd3a0, 0xd3a8,
  0xd3a9, 0xd3ab, 0xd3ad, 0xd3b4, 0xd3b8, 0xd3bc, 0xd3c4, 0xd3c5,
  0xd3c8, 0xd3c9, 0xd3d0, 0xd3d8, 0xd3e1, 0xd3e3, 0xd3ec, 0xd3ed,
  0xd3f0, 0xd3f4, 0xd3fc, 0xd3fd, 0xd3ff, 0xd401,
  /* 0x47 */
  0xd408, 0xd41d, 0xd440, 0xd444, 0xd45c, 0xd460, 0xd464, 0xd46d,
  0xd46f, 0xd478, 0xd479, 0xd47c, 0xd47f, 0xd480, 0xd482, 0xd488,
  0xd489, 0xd48b, 0xd48d, 0xd494, 0xd4a9, 0xd4cc, 0xd4d0, 0xd4d4,
  0xd4dc, 0xd4df, 0xd4e8, 0xd4ec, 0xd4f0, 0xd4f8, 0xd4fb, 0xd4fd,
  0xd504, 0xd508, 0xd50c, 0xd514, 0xd515, 0xd517, 0xd53c, 0xd53d,
  0xd540, 0xd544, 0xd54c, 0xd54d, 0xd54f, 0xd551, 0xd558, 0xd559,
  0xd55c, 0xd560, 0xd565, 0xd568, 0xd569, 0xd56b, 0xd56d, 0xd574,
  0xd575, 0xd578, 0xd57c, 0xd584, 0xd585, 0xd587, 0xd588, 0xd589,
  0xd590, 0xd5a5, 0xd5c8, 0xd5c9, 0xd5cc, 0xd5d0, 0xd5d2, 0xd5d8,
  0xd5d9, 0xd5db, 0xd5dd, 0xd5e4, 0xd5e5, 0xd5e8, 0xd5ec, 0xd5f4,
  0xd5f5, 0xd5f7, 0xd5f9, 0xd600, 0xd601, 0xd604, 0xd608, 0xd610,
  0xd611, 0xd613, 0xd614, 0xd615, 0xd61c, 0xd620,
  /* 0x48 */
  0xd624, 0xd62d, 0xd638, 0xd639, 0xd63c, 0xd640, 0xd645, 0xd648,
  0xd649, 0xd64b, 0xd64d, 0xd651, 0xd654, 0xd655, 0xd658, 0xd65c,
  0xd667, 0xd669, 0xd670, 0xd671, 0xd674, 0xd683, 0xd685, 0xd68c,
  0xd68d, 0xd690, 0xd694, 0xd69d, 0xd69f, 0xd6a1, 0xd6a8, 0xd6ac,
  0xd6b0, 0xd6b9, 0xd6bb, 0xd6c4, 0xd6c5, 0xd6c8, 0xd6cc, 0xd6d1,
  0xd6d4, 0xd6d7, 0xd6d9, 0xd6e0, 0xd6e4, 0xd6e8, 0xd6f0, 0xd6f5,
  0xd6fc, 0xd6fd, 0xd700, 0xd704, 0xd711, 0xd718, 0xd719, 0xd71c,
  0xd720, 0xd728, 0xd729, 0xd72b, 0xd72d, 0xd734, 0xd735, 0xd738,
  0xd73c, 0xd744, 0xd747, 0xd749, 0xd750, 0xd751, 0xd754, 0xd756,
  0xd757, 0xd758, 0xd759, 0xd760, 0xd761, 0xd763, 0xd765, 0xd769,
  0xd76c, 0xd770, 0xd774, 0xd77c, 0xd77d, 0xd781, 0xd788, 0xd789,
  0xd78c, 0xd790, 0xd798, 0xd799, 0xd79b, 0xd79d,
};
static const unsigned short ksc5601_2uni_page4a[4888] = {
  /* 0x4a */
  0x4f3d, 0x4f73, 0x5047, 0x50f9, 0x52a0, 0x53ef, 0x5475, 0x54e5,
  0x5609, 0x5ac1, 0x5bb6, 0x6687, 0x67b6, 0x67b7, 0x67ef, 0x6b4c,
  0x73c2, 0x75c2, 0x7a3c, 0x82db, 0x8304, 0x8857, 0x8888, 0x8a36,
  0x8cc8, 0x8dcf, 0x8efb, 0x8fe6, 0x99d5, 0x523b, 0x5374, 0x5404,
  0x606a, 0x6164, 0x6bbc, 0x73cf, 0x811a, 0x89ba, 0x89d2, 0x95a3,
  0x4f83, 0x520a, 0x58be, 0x5978, 0x59e6, 0x5e72, 0x5e79, 0x61c7,
  0x63c0, 0x6746, 0x67ec, 0x687f, 0x6f97, 0x764e, 0x770b, 0x78f5,
  0x7a08, 0x7aff, 0x7c21, 0x809d, 0x826e, 0x8271, 0x8aeb, 0x9593,
  0x4e6b, 0x559d, 0x66f7, 0x6e34, 0x78a3, 0x7aed, 0x845b, 0x8910,
  0x874e, 0x97a8, 0x52d8, 0x574e, 0x582a, 0x5d4c, 0x611f, 0x61be,
  0x6221, 0x6562, 0x67d1, 0x6a44, 0x6e1b, 0x7518, 0x75b3, 0x76e3,
  0x77b0, 0x7d3a, 0x90af, 0x9451, 0x9452, 0x9f95,
  /* 0x4b */
  0x5323, 0x5cac, 0x7532, 0x80db, 0x9240, 0x9598, 0x525b, 0x5808,
  0x59dc, 0x5ca1, 0x5d17, 0x5eb7, 0x5f3a, 0x5f4a, 0x6177, 0x6c5f,
  0x757a, 0x7586, 0x7ce0, 0x7d73, 0x7db1, 0x7f8c, 0x8154, 0x8221,
  0x8591, 0x8941, 0x8b1b, 0x92fc, 0x964d, 0x9c47, 0x4ecb, 0x4ef7,
  0x500b, 0x51f1, 0x584f, 0x6137, 0x613e, 0x6168, 0x6539, 0x69ea,
  0x6f11, 0x75a5, 0x7686, 0x76d6, 0x7b87, 0x82a5, 0x84cb, 0xf900,
  0x93a7, 0x958b, 0x5580, 0x5ba2, 0x5751, 0xf901, 0x7cb3, 0x7fb9,
  0x91b5, 0x5028, 0x53bb, 0x5c45, 0x5de8, 0x62d2, 0x636e, 0x64da,
  0x64e7, 0x6e20, 0x70ac, 0x795b, 0x8ddd, 0x8e1e, 0xf902, 0x907d,
  0x9245, 0x92f8, 0x4e7e, 0x4ef6, 0x5065, 0x5dfe, 0x5efa, 0x6106,
  0x6957, 0x8171, 0x8654, 0x8e47, 0x9375, 0x9a2b, 0x4e5e, 0x5091,
  0x6770, 0x6840, 0x5109, 0x528d, 0x5292, 0x6aa2,
  /* 0x4c */
  0x77bc, 0x9210, 0x9ed4, 0x52ab, 0x602f, 0x8ff2, 0x5048, 0x61a9,
  0x63ed, 0x64ca, 0x683c, 0x6a84, 0x6fc0, 0x8188, 0x89a1, 0x9694,
  0x5805, 0x727d, 0x72ac, 0x7504, 0x7d79, 0x7e6d, 0x80a9, 0x898b,
  0x8b74, 0x9063, 0x9d51, 0x6289, 0x6c7a, 0x6f54, 0x7d50, 0x7f3a,
  0x8a23, 0x517c, 0x614a, 0x7b9d, 0x8b19, 0x9257, 0x938c, 0x4eac,
  0x4fd3, 0x501e, 0x50be, 0x5106, 0x52c1, 0x52cd, 0x537f, 0x5770,
  0x5883, 0x5e9a, 0x5f91, 0x6176, 0x61ac, 0x64ce, 0x656c, 0x666f,
  0x66bb, 0x66f4, 0x6897, 0x6d87, 0x7085, 0x70f1, 0x749f, 0x74a5,
  0x74ca, 0x75d9, 0x786c, 0x78ec, 0x7adf, 0x7af6, 0x7d45, 0x7d93,
  0x8015, 0x803f, 0x811b, 0x8396, 0x8b66, 0x8f15, 0x9015, 0x93e1,
  0x9803, 0x9838, 0x9a5a, 0x9be8, 0x4fc2, 0x5553, 0x583a, 0x5951,
  0x5b63, 0x5c46, 0x60b8, 0x6212, 0x6842, 0x68b0,
  /* 0x4d */
  0x68e8, 0x6eaa, 0x754c, 0x7678, 0x78ce, 0x7a3d, 0x7cfb, 0x7e6b,
  0x7e7c, 0x8a08, 0x8aa1, 0x8c3f, 0x968e, 0x9dc4, 0x53e4, 0x53e9,
  0x544a, 0x5471, 0x56fa, 0x59d1, 0x5b64, 0x5c3b, 0x5eab, 0x62f7,
  0x6537, 0x6545, 0x6572, 0x66a0, 0x67af, 0x69c1, 0x6cbd, 0x75fc,
  0x7690, 0x777e, 0x7a3f, 0x7f94, 0x8003, 0x80a1, 0x818f, 0x82e6,
  0x82fd, 0x83f0, 0x85c1, 0x8831, 0x88b4, 0x8aa5, 0xf903, 0x8f9c,
  0x932e, 0x96c7, 0x9867, 0x9ad8, 0x9f13, 0x54ed, 0x659b, 0x66f2,
  0x688f, 0x7a40, 0x8c37, 0x9d60, 0x56f0, 0x5764, 0x5d11, 0x6606,
  0x68b1, 0x68cd, 0x6efe, 0x7428, 0x889e, 0x9be4, 0x6c68, 0xf904,
  0x9aa8, 0x4f9b, 0x516c, 0x5171, 0x529f, 0x5b54, 0x5de5, 0x6050,
  0x606d, 0x62f1, 0x63a7, 0x653b, 0x73d9, 0x7a7a, 0x86a3, 0x8ca2,
  0x978f, 0x4e32, 0x5be1, 0x6208, 0x679c, 0x74dc,
  /* 0x4e */
  0x79d1, 0x83d3, 0x8a87, 0x8ab2, 0x8de8, 0x904e, 0x934b, 0x9846,
  0x5ed3, 0x69e8, 0x85ff, 0x90ed, 0xf905, 0x51a0, 0x5b98, 0x5bec,
  0x6163, 0x68fa, 0x6b3e, 0x704c, 0x742f, 0x74d8, 0x7ba1, 0x7f50,
  0x83c5, 0x89c0, 0x8cab, 0x95dc, 0x9928, 0x522e, 0x605d, 0x62ec,
  0x9002, 0x4f8a, 0x5149, 0x5321, 0x58d9, 0x5ee3, 0x66e0, 0x6d38,
  0x709a, 0x72c2, 0x73d6, 0x7b50, 0x80f1, 0x945b, 0x5366, 0x639b,
  0x7f6b, 0x4e56, 0x5080, 0x584a, 0x58de, 0x602a, 0x6127, 0x62d0,
  0x69d0, 0x9b41, 0x5b8f, 0x7d18, 0x80b1, 0x8f5f, 0x4ea4, 0x50d1,
  0x54ac, 0x55ac, 0x5b0c, 0x5da0, 0x5de7, 0x652a, 0x654e, 0x6821,
  0x6a4b, 0x72e1, 0x768e, 0x77ef, 0x7d5e, 0x7ff9, 0x81a0, 0x854e,
  0x86df, 0x8f03, 0x8f4e, 0x90ca, 0x9903, 0x9a55, 0x9bab, 0x4e18,
  0x4e45, 0x4e5d, 0x4ec7, 0x4ff1, 0x5177, 0x52fe,
  /* 0x4f */
  0x5340, 0x53e3, 0x53e5, 0x548e, 0x5614, 0x5775, 0x57a2, 0x5bc7,
  0x5d87, 0x5ed0, 0x61fc, 0x62d8, 0x6551, 0x67b8, 0x67e9, 0x69cb,
  0x6b50, 0x6bc6, 0x6bec, 0x6c42, 0x6e9d, 0x7078, 0x72d7, 0x7396,
  0x7403, 0x77bf, 0x77e9, 0x7a76, 0x7d7f, 0x8009, 0x81fc, 0x8205,
  0x820a, 0x82df, 0x8862, 0x8b33, 0x8cfc, 0x8ec0, 0x9011, 0x90b1,
  0x9264, 0x92b6, 0x99d2, 0x9a45, 0x9ce9, 0x9dd7, 0x9f9c, 0x570b,
  0x5c40, 0x83ca, 0x97a0, 0x97ab, 0x9eb4, 0x541b, 0x7a98, 0x7fa4,
  0x88d9, 0x8ecd, 0x90e1, 0x5800, 0x5c48, 0x6398, 0x7a9f, 0x5bae,
  0x5f13, 0x7a79, 0x7aae, 0x828e, 0x8eac, 0x5026, 0x5238, 0x52f8,
  0x5377, 0x5708, 0x62f3, 0x6372, 0x6b0a, 0x6dc3, 0x7737, 0x53a5,
  0x7357, 0x8568, 0x8e76, 0x95d5, 0x673a, 0x6ac3, 0x6f70, 0x8a6d,
  0x8ecc, 0x994b, 0xf906, 0x6677, 0x6b78, 0x8cb4,
  /* 0x50 */
  0x9b3c, 0xf907, 0x53eb, 0x572d, 0x594e, 0x63c6, 0x69fb, 0x73ea,
  0x7845, 0x7aba, 0x7ac5, 0x7cfe, 0x8475, 0x898f, 0x8d73, 0x9035,
  0x95a8, 0x52fb, 0x5747, 0x7547, 0x7b60, 0x83cc, 0x921e, 0xf908,
  0x6a58, 0x514b, 0x524b, 0x5287, 0x621f, 0x68d8, 0x6975, 0x9699,
  0x50c5, 0x52a4, 0x52e4, 0x61c3, 0x65a4, 0x6839, 0x69ff, 0x747e,
  0x7b4b, 0x82b9, 0x83eb, 0x89b2, 0x8b39, 0x8fd1, 0x9949, 0xf909,
  0x4eca, 0x5997, 0x64d2, 0x6611, 0x6a8e, 0x7434, 0x7981, 0x79bd,
  0x82a9, 0x887e, 0x887f, 0x895f, 0xf90a, 0x9326, 0x4f0b, 0x53ca,
  0x6025, 0x6271, 0x6c72, 0x7d1a, 0x7d66, 0x4e98, 0x5162, 0x77dc,
  0x80af, 0x4f01, 0x4f0e, 0x5176, 0x5180, 0x55dc, 0x5668, 0x573b,
  0x57fa, 0x57fc, 0x5914, 0x5947, 0x5993, 0x5bc4, 0x5c90, 0x5d0e,
  0x5df1, 0x5e7e, 0x5fcc, 0x6280, 0x65d7, 0x65e3,
  /* 0x51 */
  0x671e, 0x671f, 0x675e, 0x68cb, 0x68c4, 0x6a5f, 0x6b3a, 0x6c23,
  0x6c7d, 0x6c82, 0x6dc7, 0x7398, 0x7426, 0x742a, 0x7482, 0x74a3,
  0x7578, 0x757f, 0x7881, 0x78ef, 0x7941, 0x7947, 0x7948, 0x797a,
  0x7b95, 0x7d00, 0x7dba, 0x7f88, 0x8006, 0x802d, 0x808c, 0x8a18,
  0x8b4f, 0x8c48, 0x8d77, 0x9321, 0x9324, 0x98e2, 0x9951, 0x9a0e,
  0x9a0f, 0x9a65, 0x9e92, 0x7dca, 0x4f76, 0x5409, 0x62ee, 0x6854,
  0x91d1, 0x55ab, 0x513a, 0xf90b, 0xf90c, 0x5a1c, 0x61e6, 0xf90d,
  0x62cf, 0x62ff, 0xf90e, 0xf90f, 0xf910, 0xf911, 0xf912, 0xf913,
  0x90a3, 0xf914, 0xf915, 0xf916, 0xf917, 0xf918, 0x8afe, 0xf919,
  0xf91a, 0xf91b, 0xf91c, 0x6696, 0xf91d, 0x7156, 0xf91e, 0xf91f,
  0x96e3, 0xf920, 0x634f, 0x637a, 0x5357, 0xf921, 0x678f, 0x6960,
  0x6e73, 0xf922, 0x7537, 0xf923, 0xf924, 0xf925,
  /* 0x52 */
  0x7d0d, 0xf926, 0xf927, 0x8872, 0x56ca, 0x5a18, 0xf928, 0xf929,
  0xf92a, 0xf92b, 0xf92c, 0x4e43, 0xf92d, 0x5167, 0x5948, 0x67f0,
  0x8010, 0xf92e, 0x5973, 0x5e74, 0x649a, 0x79ca, 0x5ff5, 0x606c,
  0x62c8, 0x637b, 0x5be7, 0x5bd7, 0x52aa, 0xf92f, 0x5974, 0x5f29,
  0x6012, 0xf930, 0xf931, 0xf932, 0x7459, 0xf933, 0xf934, 0xf935,
  0xf936, 0xf937, 0xf938, 0x99d1, 0xf939, 0xf93a, 0xf93b, 0xf93c,
  0xf93d, 0xf93e, 0xf93f, 0xf940, 0xf941, 0xf942, 0xf943, 0x6fc3,
  0xf944, 0xf945, 0x81bf, 0x8fb2, 0x60f1, 0xf946, 0xf947, 0x8166,
  0xf948, 0xf949, 0x5c3f, 0xf94a, 0xf94b, 0xf94c, 0xf94d, 0xf94e,
  0xf94f, 0xf950, 0xf951, 0x5ae9, 0x8a25, 0x677b, 0x7d10, 0xf952,
  0xf953, 0xf954, 0xf955, 0xf956, 0xf957, 0x80fd, 0xf958, 0xf959,
  0x5c3c, 0x6ce5, 0x533f, 0x6eba, 0x591a, 0x8336,
  /* 0x53 */
  0x4e39, 0x4eb6, 0x4f46, 0x55ae, 0x5718, 0x58c7, 0x5f56, 0x65b7,
  0x65e6, 0x6a80, 0x6bb5, 0x6e4d, 0x77ed, 0x7aef, 0x7c1e, 0x7dde,
  0x86cb, 0x8892, 0x9132, 0x935b, 0x64bb, 0x6fbe, 0x737a, 0x75b8,
  0x9054, 0x5556, 0x574d, 0x61ba, 0x64d4, 0x66c7, 0x6de1, 0x6e5b,
  0x6f6d, 0x6fb9, 0x75f0, 0x8043, 0x81bd, 0x8541, 0x8983, 0x8ac7,
  0x8b5a, 0x931f, 0x6c93, 0x7553, 0x7b54, 0x8e0f, 0x905d, 0x5510,
  0x5802, 0x5858, 0x5e62, 0x6207, 0x649e, 0x68e0, 0x7576, 0x7cd6,
  0x87b3, 0x9ee8, 0x4ee3, 0x5788, 0x576e, 0x5927, 0x5c0d, 0x5cb1,
  0x5e36, 0x5f85, 0x6234, 0x64e1, 0x73b3, 0x81fa, 0x888b, 0x8cb8,
  0x968a, 0x9edb, 0x5b85, 0x5fb7, 0x60b3, 0x5012, 0x5200, 0x5230,
  0x5716, 0x5835, 0x5857, 0x5c0e, 0x5c60, 0x5cf6, 0x5d8b, 0x5ea6,
  0x5f92, 0x60bc, 0x6311, 0x6389, 0x6417, 0x6843,
  /* 0x54 */
  0x68f9, 0x6ac2, 0x6dd8, 0x6e21, 0x6ed4, 0x6fe4, 0x71fe, 0x76dc,
  0x7779, 0x79b1, 0x7a3b, 0x8404, 0x89a9, 0x8ced, 0x8df3, 0x8e48,
  0x9003, 0x9014, 0x9053, 0x90fd, 0x934d, 0x9676, 0x97dc, 0x6bd2,
  0x7006, 0x7258, 0x72a2, 0x7368, 0x7763, 0x79bf, 0x7be4, 0x7e9b,
  0x8b80, 0x58a9, 0x60c7, 0x6566, 0x65fd, 0x66be, 0x6c8c, 0x711e,
  0x71c9, 0x8c5a, 0x9813, 0x4e6d, 0x7a81, 0x4edd, 0x51ac, 0x51cd,
  0x52d5, 0x540c, 0x61a7, 0x6771, 0x6850, 0x68df, 0x6d1e, 0x6f7c,
  0x75bc, 0x77b3, 0x7ae5, 0x80f4, 0x8463, 0x9285, 0x515c, 0x6597,
  0x675c, 0x6793, 0x75d8, 0x7ac7, 0x8373, 0xf95a, 0x8c46, 0x9017,
  0x982d, 0x5c6f, 0x81c0, 0x829a, 0x9041, 0x906f, 0x920d, 0x5f97,
  0x5d9d, 0x6a59, 0x71c8, 0x767b, 0x7b49, 0x85e4, 0x8b04, 0x9127,
  0x9a30, 0x5587, 0x61f6, 0xf95b, 0x7669, 0x7f85,
  /* 0x55 */
  0x863f, 0x87ba, 0x88f8, 0x908f, 0xf95c, 0x6d1b, 0x70d9, 0x73de,
  0x7d61, 0x843d, 0xf95d, 0x916a, 0x99f1, 0xf95e, 0x4e82, 0x5375,
  0x6b04, 0x6b12, 0x703e, 0x721b, 0x862d, 0x9e1e, 0x524c, 0x8fa3,
  0x5d50, 0x64e5, 0x652c, 0x6b16, 0x6feb, 0x7c43, 0x7e9c, 0x85cd,
  0x8964, 0x89bd, 0x62c9, 0x81d8, 0x881f, 0x5eca, 0x6717, 0x6d6a,
  0x72fc, 0x7405, 0x746f, 0x8782, 0x90de, 0x4f86, 0x5d0d, 0x5fa0,
  0x840a, 0x51b7, 0x63a0, 0x7565, 0x4eae, 0x5006, 0x5169, 0x51c9,
  0x6881, 0x6a11, 0x7cae, 0x7cb1, 0x7ce7, 0x826f, 0x8ad2, 0x8f1b,
  0x91cf, 0x4fb6, 0x5137, 0x52f5, 0x5442, 0x5eec, 0x616e, 0x623e,
  0x65c5, 0x6ada, 0x6ffe, 0x792a, 0x85dc, 0x8823, 0x95ad, 0x9a62,
  0x9a6a, 0x9e97, 0x9ece, 0x529b, 0x66c6, 0x6b77, 0x701d, 0x792b,
  0x8f62, 0x9742, 0x6190, 0x6200, 0x6523, 0x6f23,
  /* 0x56 */
  0x7149, 0x7489, 0x7df4, 0x806f, 0x84ee, 0x8f26, 0x9023, 0x934a,
  0x51bd, 0x5217, 0x52a3, 0x6d0c, 0x70c8, 0x88c2, 0x5ec9, 0x6582,
  0x6bae, 0x6fc2, 0x7c3e, 0x7375, 0x4ee4, 0x4f36, 0x56f9, 0xf95f,
  0x5cba, 0x5dba, 0x601c, 0x73b2, 0x7b2d, 0x7f9a, 0x7fce, 0x8046,
  0x901e, 0x9234, 0x96f6, 0x9748, 0x9818, 0x9f61, 0x4f8b, 0x6fa7,
  0x79ae, 0x91b4, 0x96b7, 0x52de, 0xf960, 0x6488, 0x64c4, 0x6ad3,
  0x6f5e, 0x7018, 0x7210, 0x76e7, 0x8001, 0x8606, 0x865c, 0x8def,
  0x8f05, 0x9732, 0x9b6f, 0x9dfa, 0x9e75, 0x788c, 0x797f, 0x7da0,
  0x83c9, 0x9304, 0x9e7f, 0x9e93, 0x8ad6, 0x58df, 0x5f04, 0x6727,
  0x7027, 0x74cf, 0x7c60, 0x807e, 0x5121, 0x7028, 0x7262, 0x78ca,
  0x8cc2, 0x8cda, 0x8cf4, 0x96f7, 0x4e86, 0x50da, 0x5bee, 0x5ed6,
  0x6599, 0x71ce, 0x7642, 0x77ad, 0x804a, 0x84fc,
  /* 0x57 */
  0x907c, 0x9b27, 0x9f8d, 0x58d8, 0x5a41, 0x5c62, 0x6a13, 0x6dda,
  0x6f0f, 0x763b, 0x7d2f, 0x7e37, 0x851e, 0x8938, 0x93e4, 0x964b,
  0x5289, 0x65d2, 0x67f3, 0x69b4, 0x6d41, 0x6e9c, 0x700f, 0x7409,
  0x7460, 0x7559, 0x7624, 0x786b, 0x8b2c, 0x985e, 0x516d, 0x622e,
  0x9678, 0x4f96, 0x502b, 0x5d19, 0x6dea, 0x7db8, 0x8f2a, 0x5f8b,
  0x6144, 0x6817, 0xf961, 0x9686, 0x52d2, 0x808b, 0x51dc, 0x51cc,
  0x695e, 0x7a1c, 0x7dbe, 0x83f1, 0x9675, 0x4fda, 0x5229, 0x5398,
  0x540f, 0x550e, 0x5c65, 0x60a7, 0x674e, 0x68a8, 0x6d6c, 0x7281,
  0x72f8, 0x7406, 0x7483, 0xf962, 0x75e2, 0x7c6c, 0x7f79, 0x7fb8,
  0x8389, 0x88cf, 0x88e1, 0x91cc, 0x91d0, 0x96e2, 0x9bc9, 0x541d,
  0x6f7e, 0x71d0, 0x7498, 0x85fa, 0x8eaa, 0x96a3, 0x9c57, 0x9e9f,
  0x6797, 0x6dcb, 0x7433, 0x81e8, 0x9716, 0x782c,
  /* 0x58 */
  0x7acb, 0x7b20, 0x7c92, 0x6469, 0x746a, 0x75f2, 0x78bc, 0x78e8,
  0x99ac, 0x9b54, 0x9ebb, 0x5bde, 0x5e55, 0x6f20, 0x819c, 0x83ab,
  0x9088, 0x4e07, 0x534d, 0x5a29, 0x5dd2, 0x5f4e, 0x6162, 0x633d,
  0x6669, 0x66fc, 0x6eff, 0x6f2b, 0x7063, 0x779e, 0x842c, 0x8513,
  0x883b, 0x8f13, 0x9945, 0x9c3b, 0x551c, 0x62b9, 0x672b, 0x6cab,
  0x8309, 0x896a, 0x977a, 0x4ea1, 0x5984, 0x5fd8, 0x5fd9, 0x671b,
  0x7db2, 0x7f54, 0x8292, 0x832b, 0x83bd, 0x8f1e, 0x9099, 0x57cb,
  0x59b9, 0x5a92, 0x5bd0, 0x6627, 0x679a, 0x6885, 0x6bcf, 0x7164,
  0x7f75, 0x8cb7, 0x8ce3, 0x9081, 0x9b45, 0x8108, 0x8c8a, 0x964c,
  0x9a40, 0x9ea5, 0x5b5f, 0x6c13, 0x731b, 0x76f2, 0x76df, 0x840c,
  0x51aa, 0x8993, 0x514d, 0x5195, 0x52c9, 0x68c9, 0x6c94, 0x7704,
  0x7720, 0x7dbf, 0x7dec, 0x9762, 0x9eb5, 0x6ec5,
  /* 0x59 */
  0x8511, 0x51a5, 0x540d, 0x547d, 0x660e, 0x669d, 0x6927, 0x6e9f,
  0x76bf, 0x7791, 0x8317, 0x84c2, 0x879f, 0x9169, 0x9298, 0x9cf4,
  0x8882, 0x4fae, 0x5192, 0x52df, 0x59c6, 0x5e3d, 0x6155, 0x6478,
  0x6479, 0x66ae, 0x67d0, 0x6a21, 0x6bcd, 0x6bdb, 0x725f, 0x7261,
  0x7441, 0x7738, 0x77db, 0x8017, 0x82bc, 0x8305, 0x8b00, 0x8b28,
  0x8c8c, 0x6728, 0x6c90, 0x7267, 0x76ee, 0x7766, 0x7a46, 0x9da9,
  0x6b7f, 0x6c92, 0x5922, 0x6726, 0x8499, 0x536f, 0x5893, 0x5999,
  0x5edf, 0x63cf, 0x6634, 0x6773, 0x6e3a, 0x732b, 0x7ad7, 0x82d7,
  0x9328, 0x52d9, 0x5deb, 0x61ae, 0x61cb, 0x620a, 0x62c7, 0x64ab,
  0x65e0, 0x6959, 0x6b66, 0x6bcb, 0x7121, 0x73f7, 0x755d, 0x7e46,
  0x821e, 0x8302, 0x856a, 0x8aa3, 0x8cbf, 0x9727, 0x9d61, 0x58a8,
  0x9ed8, 0x5011, 0x520e, 0x543b, 0x554f, 0x6587,
  /* 0x5a */
  0x6c76, 0x7d0a, 0x7d0b, 0x805e, 0x868a, 0x9580, 0x96ef, 0x52ff,
  0x6c95, 0x7269, 0x5473, 0x5a9a, 0x5c3e, 0x5d4b, 0x5f4c, 0x5fae,
  0x672a, 0x68b6, 0x6963, 0x6e3c, 0x6e44, 0x7709, 0x7c73, 0x7f8e,
  0x8587, 0x8b0e, 0x8ff7, 0x9761, 0x9ef4, 0x5cb7, 0x60b6, 0x610d,
  0x61ab, 0x654f, 0x65fb, 0x65fc, 0x6c11, 0x6cef, 0x739f, 0x73c9,
  0x7de1, 0x9594, 0x5bc6, 0x871c, 0x8b10, 0x525d, 0x535a, 0x62cd,
  0x640f, 0x64b2, 0x6734, 0x6a38, 0x6cca, 0x73c0, 0x749e, 0x7b94,
  0x7c95, 0x7e1b, 0x818a, 0x8236, 0x8584, 0x8feb, 0x96f9, 0x99c1,
  0x4f34, 0x534a, 0x53cd, 0x53db, 0x62cc, 0x642c, 0x6500, 0x6591,
  0x69c3, 0x6cee, 0x6f58, 0x73ed, 0x7554, 0x7622, 0x76e4, 0x76fc,
  0x78d0, 0x78fb, 0x792c, 0x7d46, 0x822c, 0x87e0, 0x8fd4, 0x9812,
  0x98ef, 0x52c3, 0x62d4, 0x64a5, 0x6e24, 0x6f51,
  /* 0x5b */
  0x767c, 0x8dcb, 0x91b1, 0x9262, 0x9aee, 0x9b43, 0x5023, 0x508d,
  0x574a, 0x59a8, 0x5c28, 0x5e47, 0x5f77, 0x623f, 0x653e, 0x65b9,
  0x65c1, 0x6609, 0x678b, 0x699c, 0x6ec2, 0x78c5, 0x7d21, 0x80aa,
  0x8180, 0x822b, 0x82b3, 0x84a1, 0x868c, 0x8a2a, 0x8b17, 0x90a6,
  0x9632, 0x9f90, 0x500d, 0x4ff3, 0xf963, 0x57f9, 0x5f98, 0x62dc,
  0x6392, 0x676f, 0x6e43, 0x7119, 0x76c3, 0x80cc, 0x80da, 0x88f4,
  0x88f5, 0x8919, 0x8ce0, 0x8f29, 0x914d, 0x966a, 0x4f2f, 0x4f70,
  0x5e1b, 0x67cf, 0x6822, 0x767d, 0x767e, 0x9b44, 0x5e61, 0x6a0a,
  0x7169, 0x71d4, 0x756a, 0xf964, 0x7e41, 0x8543, 0x85e9, 0x98dc,
  0x4f10, 0x7b4f, 0x7f70, 0x95a5, 0x51e1, 0x5e06, 0x68b5, 0x6c3e,
  0x6c4e, 0x6cdb, 0x72af, 0x7bc4, 0x8303, 0x6cd5, 0x743a, 0x50fb,
  0x5288, 0x58c1, 0x64d8, 0x6a97, 0x74a7, 0x7656,
  /* 0x5c */
  0x78a7, 0x8617, 0x95e2, 0x9739, 0xf965, 0x535e, 0x5f01, 0x8b8a,
  0x8fa8, 0x8faf, 0x908a, 0x5225, 0x77a5, 0x9c49, 0x9f08, 0x4e19,
  0x5002, 0x5175, 0x5c5b, 0x5e77, 0x661e, 0x663a, 0x67c4, 0x68c5,
  0x70b3, 0x7501, 0x75c5, 0x79c9, 0x7add, 0x8f27, 0x9920, 0x9a08,
  0x4fdd, 0x5821, 0x5831, 0x5bf6, 0x666e, 0x6b65, 0x6d11, 0x6e7a,
  0x6f7d, 0x73e4, 0x752b, 0x83e9, 0x88dc, 0x8913, 0x8b5c, 0x8f14,
  0x4f0f, 0x50d5, 0x5310, 0x535c, 0x5b93, 0x5fa9, 0x670d, 0x798f,
  0x8179, 0x832f, 0x8514, 0x8907, 0x8986, 0x8f39, 0x8f3b, 0x99a5,
  0x9c12, 0x672c, 0x4e76, 0x4ff8, 0x5949, 0x5c01, 0x5cef, 0x5cf0,
  0x6367, 0x68d2, 0x70fd, 0x71a2, 0x742b, 0x7e2b, 0x84ec, 0x8702,
  0x9022, 0x92d2, 0x9cf3, 0x4e0d, 0x4ed8, 0x4fef, 0x5085, 0x5256,
  0x526f, 0x5426, 0x5490, 0x57e0, 0x592b, 0x5a66,
  /* 0x5d */
  0x5b5a, 0x5b75, 0x5bcc, 0x5e9c, 0xf966, 0x6276, 0x6577, 0x65a7,
  0x6d6e, 0x6ea5, 0x7236, 0x7b26, 0x7c3f, 0x7f36, 0x8150, 0x8151,
  0x819a, 0x8240, 0x8299, 0x83a9, 0x8a03, 0x8ca0, 0x8ce6, 0x8cfb,
  0x8d74, 0x8dba, 0x90e8, 0x91dc, 0x961c, 0x9644, 0x99d9, 0x9ce7,
  0x5317, 0x5206, 0x5429, 0x5674, 0x58b3, 0x5954, 0x596e, 0x5fff,
  0x61a4, 0x626e, 0x6610, 0x6c7e, 0x711a, 0x76c6, 0x7c89, 0x7cde,
  0x7d1b, 0x82ac, 0x8cc1, 0x96f0, 0xf967, 0x4f5b, 0x5f17, 0x5f7f,
  0x62c2, 0x5d29, 0x670b, 0x68da, 0x787c, 0x7e43, 0x9d6c, 0x4e15,
  0x5099, 0x5315, 0x532a, 0x5351, 0x5983, 0x5a62, 0x5e87, 0x60b2,
  0x618a, 0x6249, 0x6279, 0x6590, 0x6787, 0x69a7, 0x6bd4, 0x6bd6,
  0x6bd7, 0x6bd8, 0x6cb8, 0xf968, 0x7435, 0x75fa, 0x7812, 0x7891,
  0x79d5, 0x79d8, 0x7c83, 0x7dcb, 0x7fe1, 0x80a5,
  /* 0x5e */
  0x813e, 0x81c2, 0x83f2, 0x871a, 0x88e8, 0x8ab9, 0x8b6c, 0x8cbb,
  0x9119, 0x975e, 0x98db, 0x9f3b, 0x56ac, 0x5b2a, 0x5f6c, 0x658c,
  0x6ab3, 0x6baf, 0x6d5c, 0x6ff1, 0x7015, 0x725d, 0x73ad, 0x8ca7,
  0x8cd3, 0x983b, 0x6191, 0x6c37, 0x8058, 0x9a01, 0x4e4d, 0x4e8b,
  0x4e9b, 0x4ed5, 0x4f3a, 0x4f3c, 0x4f7f, 0x4fdf, 0x50ff, 0x53f2,
  0x53f8, 0x5506, 0x55e3, 0x56db, 0x58eb, 0x5962, 0x5a11, 0x5beb,
  0x5bfa, 0x5c04, 0x5df3, 0x5e2b, 0x5f99, 0x601d, 0x6368, 0x659c,
  0x65af, 0x67f6, 0x67fb, 0x68ad, 0x6b7b, 0x6c99, 0x6cd7, 0x6e23,
  0x7009, 0x7345, 0x7802, 0x793e, 0x7940, 0x7960, 0x79c1, 0x7be9,
  0x7d17, 0x7d72, 0x8086, 0x820d, 0x838e, 0x84d1, 0x86c7, 0x88df,
  0x8a50, 0x8a5e, 0x8b1d, 0x8cdc, 0x8d66, 0x8fad, 0x90aa, 0x98fc,
  0x99df, 0x9e9d, 0x524a, 0xf969, 0x6714, 0xf96a,
  /* 0x5f */
  0x5098, 0x522a, 0x5c71, 0x6563, 0x6c55, 0x73ca, 0x7523, 0x759d,
  0x7b97, 0x849c, 0x9178, 0x9730, 0x4e77, 0x6492, 0x6bba, 0x715e,
  0x85a9, 0x4e09, 0xf96b, 0x6749, 0x68ee, 0x6e17, 0x829f, 0x8518,
  0x886b, 0x63f7, 0x6f81, 0x9212, 0x98af, 0x4e0a, 0x50b7, 0x50cf,
  0x511f, 0x5546, 0x55aa, 0x5617, 0x5b40, 0x5c19, 0x5ce0, 0x5e38,
  0x5e8a, 0x5ea0, 0x5ec2, 0x60f3, 0x6851, 0x6a61, 0x6e58, 0x723d,
  0x7240, 0x72c0, 0x76f8, 0x7965, 0x7bb1, 0x7fd4, 0x88f3, 0x89f4,
  0x8a73, 0x8c61, 0x8cde, 0x971c, 0x585e, 0x74bd, 0x8cfd, 0x55c7,
  0xf96c, 0x7a61, 0x7d22, 0x8272, 0x7272, 0x751f, 0x7525, 0xf96d,
  0x7b19, 0x5885, 0x58fb, 0x5dbc, 0x5e8f, 0x5eb6, 0x5f90, 0x6055,
  0x6292, 0x637f, 0x654d, 0x6691, 0x66d9, 0x66f8, 0x6816, 0x68f2,
  0x7280, 0x745e, 0x7b6e, 0x7d6e, 0x7dd6, 0x7f72,
  /* 0x60 */
  0x80e5, 0x8212, 0x85af, 0x897f, 0x8a93, 0x901d, 0x92e4, 0x9ecd,
  0x9f20, 0x5915, 0x596d, 0x5e2d, 0x60dc, 0x6614, 0x6673, 0x6790,
  0x6c50, 0x6dc5, 0x6f5f, 0x77f3, 0x78a9, 0x84c6, 0x91cb, 0x932b,
  0x4ed9, 0x50ca, 0x5148, 0x5584, 0x5b0b, 0x5ba3, 0x6247, 0x657e,
  0x65cb, 0x6e32, 0x717d, 0x7401, 0x7444, 0x7487, 0x74bf, 0x766c,
  0x79aa, 0x7dda, 0x7e55, 0x7fa8, 0x817a, 0x81b3, 0x8239, 0x861a,
  0x87ec, 0x8a75, 0x8de3, 0x9078, 0x9291, 0x9425, 0x994d, 0x9bae,
  0x5368, 0x5c51, 0x6954, 0x6cc4, 0x6d29, 0x6e2b, 0x820c, 0x859b,
  0x893b, 0x8a2d, 0x8aaa, 0x96ea, 0x9f67, 0x5261, 0x66b9, 0x6bb2,
  0x7e96, 0x87fe, 0x8d0d, 0x9583, 0x965d, 0x651d, 0x6d89, 0x71ee,
  0xf96e, 0x57ce, 0x59d3, 0x5bac, 0x6027, 0x60fa, 0x6210, 0x661f,
  0x665f, 0x7329, 0x73f9, 0x76db, 0x7701, 0x7b6c,
  /* 0x61 */
  0x8056, 0x8072, 0x8165, 0x8aa0, 0x9192, 0x4e16, 0x52e2, 0x6b72,
  0x6d17, 0x7a05, 0x7b39, 0x7d30, 0xf96f, 0x8cb0, 0x53ec, 0x562f,
  0x5851, 0x5bb5, 0x5c0f, 0x5c11, 0x5de2, 0x6240, 0x6383, 0x6414,
  0x662d, 0x68b3, 0x6cbc, 0x6d88, 0x6eaf, 0x701f, 0x70a4, 0x71d2,
  0x7526, 0x758f, 0x758e, 0x7619, 0x7b11, 0x7be0, 0x7c2b, 0x7d20,
  0x7d39, 0x852c, 0x856d, 0x8607, 0x8a34, 0x900d, 0x9061, 0x90b5,
  0x92b7, 0x97f6, 0x9a37, 0x4fd7, 0x5c6c, 0x675f, 0x6d91, 0x7c9f,
  0x7e8c, 0x8b16, 0x8d16, 0x901f, 0x5b6b, 0x5dfd, 0x640d, 0x84c0,
  0x905c, 0x98e1, 0x7387, 0x5b8b, 0x609a, 0x677e, 0x6dde, 0x8a1f,
  0x8aa6, 0x9001, 0x980c, 0x5237, 0xf970, 0x7051, 0x788e, 0x9396,
  0x8870, 0x91d7, 0x4fee, 0x53d7, 0x55fd, 0x56da, 0x5782, 0x58fd,
  0x5ac2, 0x5b88, 0x5cab, 0x5cc0, 0x5e25, 0x6101,
  /* 0x62 */
  0x620d, 0x624b, 0x6388, 0x641c, 0x6536, 0x6578, 0x6a39, 0x6b8a,
  0x6c34, 0x6d19, 0x6f31, 0x71e7, 0x72e9, 0x7378, 0x7407, 0x74b2,
  0x7626, 0x7761, 0x79c0, 0x7a57, 0x7aea, 0x7cb9, 0x7d8f, 0x7dac,
  0x7e61, 0x7f9e, 0x8129, 0x8331, 0x8490, 0x84da, 0x85ea, 0x8896,
  0x8ab0, 0x8b90, 0x8f38, 0x9042, 0x9083, 0x916c, 0x9296, 0x92b9,
  0x968b, 0x96a7, 0x96a8, 0x96d6, 0x9700, 0x9808, 0x9996, 0x9ad3,
  0x9b1a, 0x53d4, 0x587e, 0x5919, 0x5b70, 0x5bbf, 0x6dd1, 0x6f5a,
  0x719f, 0x7421, 0x74b9, 0x8085, 0x83fd, 0x5de1, 0x5f87, 0x5faa,
  0x6042, 0x65ec, 0x6812, 0x696f, 0x6a53, 0x6b89, 0x6d35, 0x6df3,
  0x73e3, 0x76fe, 0x77ac, 0x7b4d, 0x7d14, 0x8123, 0x821c, 0x8340,
  0x84f4, 0x8563, 0x8a62, 0x8ac4, 0x9187, 0x931e, 0x9806, 0x99b4,
  0x620c, 0x8853, 0x8ff0, 0x9265, 0x5d07, 0x5d27,
  /* 0x63 */
  0x5d69, 0x745f, 0x819d, 0x8768, 0x6fd5, 0x62fe, 0x7fd2, 0x8936,
  0x8972, 0x4e1e, 0x4e58, 0x50e7, 0x52dd, 0x5347, 0x627f, 0x6607,
  0x7e69, 0x8805, 0x965e, 0x4f8d, 0x5319, 0x5636, 0x59cb, 0x5aa4,
  0x5c38, 0x5c4e, 0x5c4d, 0x5e02, 0x5f11, 0x6043, 0x65bd, 0x662f,
  0x6642, 0x67be, 0x67f4, 0x731c, 0x77e2, 0x793a, 0x7fc5, 0x8494,
  0x84cd, 0x8996, 0x8a66, 0x8a69, 0x8ae1, 0x8c55, 0x8c7a, 0x57f4,
  0x5bd4, 0x5f0f, 0x606f, 0x62ed, 0x690d, 0x6b96, 0x6e5c, 0x7184,
  0x7bd2, 0x8755, 0x8b58, 0x8efe, 0x98df, 0x98fe, 0x4f38, 0x4f81,
  0x4fe1, 0x547b, 0x5a20, 0x5bb8, 0x613c, 0x65b0, 0x6668, 0x71fc,
  0x7533, 0x795e, 0x7d33, 0x814e, 0x81e3, 0x8398, 0x85aa, 0x85ce,
  0x8703, 0x8a0a, 0x8eab, 0x8f9b, 0xf971, 0x8fc5, 0x5931, 0x5ba4,
  0x5be6, 0x6089, 0x5be9, 0x5c0b, 0x5fc3, 0x6c81,
  /* 0x64 */
  0xf972, 0x6df1, 0x700b, 0x751a, 0x82af, 0x8af6, 0x4ec0, 0x5341,
  0xf973, 0x96d9, 0x6c0f, 0x4e9e, 0x4fc4, 0x5152, 0x555e, 0x5a25,
  0x5ce8, 0x6211, 0x7259, 0x82bd, 0x83aa, 0x86fe, 0x8859, 0x8a1d,
  0x963f, 0x96c5, 0x9913, 0x9d09, 0x9d5d, 0x580a, 0x5cb3, 0x5dbd,
  0x5e44, 0x60e1, 0x6115, 0x63e1, 0x6a02, 0x6e25, 0x9102, 0x9354,
  0x984e, 0x9c10, 0x9f77, 0x5b89, 0x5cb8, 0x6309, 0x664f, 0x6848,
  0x773c, 0x96c1, 0x978d, 0x9854, 0x9b9f, 0x65a1, 0x8b01, 0x8ecb,
  0x95bc, 0x5535, 0x5ca9, 0x5dd6, 0x5eb5, 0x6697, 0x764c, 0x83f4,
  0x95c7, 0x58d3, 0x62bc, 0x72ce, 0x9d28, 0x4ef0, 0x592e, 0x600f,
  0x663b, 0x6b83, 0x79e7, 0x9d26, 0x5393, 0x54c0, 0x57c3, 0x5d16,
  0x611b, 0x66d6, 0x6daf, 0x788d, 0x827e, 0x9698, 0x9744, 0x5384,
  0x627c, 0x6396, 0x6db2, 0x7e0a, 0x814b, 0x984d,
  /* 0x65 */
  0x6afb, 0x7f4c, 0x9daf, 0x9e1a, 0x4e5f, 0x503b, 0x51b6, 0x591c,
  0x60f9, 0x63f6, 0x6930, 0x723a, 0x8036, 0xf974, 0x91ce, 0x5f31,
  0xf975, 0xf976, 0x7d04, 0x82e5, 0x846f, 0x84bb, 0x85e5, 0x8e8d,
  0xf977, 0x4f6f, 0xf978, 0xf979, 0x58e4, 0x5b43, 0x6059, 0x63da,
  0x6518, 0x656d, 0x6698, 0xf97a, 0x694a, 0x6a23, 0x6d0b, 0x7001,
  0x716c, 0x75d2, 0x760d, 0x79b3, 0x7a70, 0xf97b, 0x7f8a, 0xf97c,
  0x8944, 0xf97d, 0x8b93, 0x91c0, 0x967d, 0xf97e, 0x990a, 0x5704,
  0x5fa1, 0x65bc, 0x6f01, 0x7600, 0x79a6, 0x8a9e, 0x99ad, 0x9b5a,
  0x9f6c, 0x5104, 0x61b6, 0x6291, 0x6a8d, 0x81c6, 0x5043, 0x5830,
  0x5f66, 0x7109, 0x8a00, 0x8afa, 0x5b7c, 0x8616, 0x4ffa, 0x513c,
  0x56b4, 0x5944, 0x63a9, 0x6df9, 0x5daa, 0x696d, 0x5186, 0x4e88,
  0x4f59, 0xf97f, 0xf980, 0xf981, 0x5982, 0xf982,
  /* 0x66 */
  0xf983, 0x6b5f, 0x6c5d, 0xf984, 0x74b5, 0x7916, 0xf985, 0x8207,
  0x8245, 0x8339, 0x8f3f, 0x8f5d, 0xf986, 0x9918, 0xf987, 0xf988,
  0xf989, 0x4ea6, 0xf98a, 0x57df, 0x5f79, 0x6613, 0xf98b, 0xf98c,
  0x75ab, 0x7e79, 0x8b6f, 0xf98d, 0x9006, 0x9a5b, 0x56a5, 0x5827,
  0x59f8, 0x5a1f, 0x5bb4, 0xf98e, 0x5ef6, 0xf98f, 0xf990, 0x6350,
  0x633b, 0xf991, 0x693d, 0x6c87, 0x6cbf, 0x6d8e, 0x6d93, 0x6df5,
  0x6f14, 0xf992, 0x70df, 0x7136, 0x7159, 0xf993, 0x71c3, 0x71d5,
  0xf994, 0x784f, 0x786f, 0xf995, 0x7b75, 0x7de3, 0xf996, 0x7e2f,
  0xf997, 0x884d, 0x8edf, 0xf998, 0xf999, 0xf99a, 0x925b, 0xf99b,
  0x9cf6, 0xf99c, 0xf99d, 0xf99e, 0x6085, 0x6d85, 0xf99f, 0x71b1,
  0xf9a0, 0xf9a1, 0x95b1, 0x53ad, 0xf9a2, 0xf9a3, 0xf9a4, 0x67d3,
  0xf9a5, 0x708e, 0x7130, 0x7430, 0x8276, 0x82d2,
  /* 0x67 */
  0xf9a6, 0x95bb, 0x9ae5, 0x9e7d, 0x66c4, 0xf9a7, 0x71c1, 0x8449,
  0xf9a8, 0xf9a9, 0x584b, 0xf9aa, 0xf9ab, 0x5db8, 0x5f71, 0xf9ac,
  0x6620, 0x668e, 0x6979, 0x69ae, 0x6c38, 0x6cf3, 0x6e36, 0x6f41,
  0x6fda, 0x701b, 0x702f, 0x7150, 0x71df, 0x7370, 0xf9ad, 0x745b,
  0xf9ae, 0x74d4, 0x76c8, 0x7a4e, 0x7e93, 0xf9af, 0xf9b0, 0x82f1,
  0x8a60, 0x8fce, 0xf9b1, 0x9348, 0xf9b2, 0x9719, 0xf9b3, 0xf9b4,
  0x4e42, 0x502a, 0xf9b5, 0x5208, 0x53e1, 0x66f3, 0x6c6d, 0x6fca,
  0x730a, 0x777f, 0x7a62, 0x82ae, 0x85dd, 0x8602, 0xf9b6, 0x88d4,
  0x8a63, 0x8b7d, 0x8c6b, 0xf9b7, 0x92b3, 0xf9b8, 0x9713, 0x9810,
  0x4e94, 0x4f0d, 0x4fc9, 0x50b2, 0x5348, 0x543e, 0x5433, 0x55da,
  0x5862, 0x58ba, 0x5967, 0x5a1b, 0x5be4, 0x609f, 0xf9b9, 0x61ca,
  0x6556, 0x65ff, 0x6664, 0x68a7, 0x6c5a, 0x6fb3,
  /* 0x68 */
  0x70cf, 0x71ac, 0x7352, 0x7b7d, 0x8708, 0x8aa4, 0x9c32, 0x9f07,
  0x5c4b, 0x6c83, 0x7344, 0x7389, 0x923a, 0x6eab, 0x7465, 0x761f,
  0x7a69, 0x7e15, 0x860a, 0x5140, 0x58c5, 0x64c1, 0x74ee, 0x7515,
  0x7670, 0x7fc1, 0x9095, 0x96cd, 0x9954, 0x6e26, 0x74e6, 0x7aa9,
  0x7aaa, 0x81e5, 0x86d9, 0x8778, 0x8a1b, 0x5a49, 0x5b8c, 0x5b9b,
  0x68a1, 0x6900, 0x6d63, 0x73a9, 0x7413, 0x742c, 0x7897, 0x7de9,
  0x7feb, 0x8118, 0x8155, 0x839e, 0x8c4c, 0x962e, 0x9811, 0x66f0,
  0x5f80, 0x65fa, 0x6789, 0x6c6a, 0x738b, 0x502d, 0x5a03, 0x6b6a,
  0x77ee, 0x5916, 0x5d6c, 0x5dcd, 0x7325, 0x754f, 0xf9ba, 0xf9bb,
  0x50e5, 0x51f9, 0x582f, 0x592d, 0x5996, 0x59da, 0x5be5, 0xf9bc,
  0xf9bd, 0x5da2, 0x62d7, 0x6416, 0x6493, 0x64fe, 0xf9be, 0x66dc,
  0xf9bf, 0x6a48, 0xf9c0, 0x71ff, 0x7464, 0xf9c1,
  /* 0x69 */
  0x7a88, 0x7aaf, 0x7e47, 0x7e5e, 0x8000, 0x8170, 0xf9c2, 0x87ef,
  0x8981, 0x8b20, 0x9059, 0xf9c3, 0x9080, 0x9952, 0x617e, 0x6b32,
  0x6d74, 0x7e1f, 0x8925, 0x8fb1, 0x4fd1, 0x50ad, 0x5197, 0x52c7,
  0x57c7, 0x5889, 0x5bb9, 0x5eb8, 0x6142, 0x6995, 0x6d8c, 0x6e67,
  0x6eb6, 0x7194, 0x7462, 0x7528, 0x752c, 0x8073, 0x8338, 0x84c9,
  0x8e0a, 0x9394, 0x93de, 0xf9c4, 0x4e8e, 0x4f51, 0x5076, 0x512a,
  0x53c8, 0x53cb, 0x53f3, 0x5b87, 0x5bd3, 0x5c24, 0x611a, 0x6182,
  0x65f4, 0x725b, 0x7397, 0x7440, 0x76c2, 0x7950, 0x7991, 0x79b9,
  0x7d06, 0x7fbd, 0x828b, 0x85d5, 0x865e, 0x8fc2, 0x9047, 0x90f5,
  0x91ea, 0x9685, 0x96e8, 0x96e9, 0x52d6, 0x5f67, 0x65ed, 0x6631,
  0x682f, 0x715c, 0x7a36, 0x90c1, 0x980a, 0x4e91, 0xf9c5, 0x6a52,
  0x6b9e, 0x6f90, 0x7189, 0x8018, 0x82b8, 0x8553,
  /* 0x6a */
  0x904b, 0x9695, 0x96f2, 0x97fb, 0x851a, 0x9b31, 0x4e90, 0x718a,
  0x96c4, 0x5143, 0x539f, 0x54e1, 0x5713, 0x5712, 0x57a3, 0x5a9b,
  0x5ac4, 0x5bc3, 0x6028, 0x613f, 0x63f4, 0x6c85, 0x6d39, 0x6e72,
  0x6e90, 0x7230, 0x733f, 0x7457, 0x82d1, 0x8881, 0x8f45, 0x9060,
  0xf9c6, 0x9662, 0x9858, 0x9d1b, 0x6708, 0x8d8a, 0x925e, 0x4f4d,
  0x5049, 0x50de, 0x5371, 0x570d, 0x59d4, 0x5a01, 0x5c09, 0x6170,
  0x6690, 0x6e2d, 0x7232, 0x744b, 0x7def, 0x80c3, 0x840e, 0x8466,
  0x853f, 0x875f, 0x885b, 0x8918, 0x8b02, 0x9055, 0x97cb, 0x9b4f,
  0x4e73, 0x4f91, 0x5112, 0x516a, 0xf9c7, 0x552f, 0x55a9, 0x5b7a,
  0x5ba5, 0x5e7c, 0x5e7d, 0x5ebe, 0x60a0, 0x60df, 0x6108, 0x6109,
  0x63c4, 0x6538, 0x6709, 0xf9c8, 0x67d4, 0x67da, 0xf9c9, 0x6961,
  0x6962, 0x6cb9, 0x6d27, 0xf9ca, 0x6e38, 0xf9cb,
  /* 0x6b */
  0x6fe1, 0x7336, 0x7337, 0xf9cc, 0x745c, 0x7531, 0xf9cd, 0x7652,
  0xf9ce, 0xf9cf, 0x7dad, 0x81fe, 0x8438, 0x88d5, 0x8a98, 0x8adb,
  0x8aed, 0x8e30, 0x8e42, 0x904a, 0x903e, 0x907a, 0x9149, 0x91c9,
  0x936e, 0xf9d0, 0xf9d1, 0x5809, 0xf9d2, 0x6bd3, 0x8089, 0x80b2,
  0xf9d3, 0xf9d4, 0x5141, 0x596b, 0x5c39, 0xf9d5, 0xf9d6, 0x6f64,
  0x73a7, 0x80e4, 0x8d07, 0xf9d7, 0x9217, 0x958f, 0xf9d8, 0xf9d9,
  0xf9da, 0xf9db, 0x807f, 0x620e, 0x701c, 0x7d68, 0x878d, 0xf9dc,
  0x57a0, 0x6069, 0x6147, 0x6bb7, 0x8abe, 0x9280, 0x96b1, 0x4e59,
  0x541f, 0x6deb, 0x852d, 0x9670, 0x97f3, 0x98ee, 0x63d6, 0x6ce3,
  0x9091, 0x51dd, 0x61c9, 0x81ba, 0x9df9, 0x4f9d, 0x501a, 0x5100,
  0x5b9c, 0x610f, 0x61ff, 0x64ec, 0x6905, 0x6bc5, 0x7591, 0x77e3,
  0x7fa9, 0x8264, 0x858f, 0x87fb, 0x8863, 0x8abc,
  /* 0x6c */
  0x8b70, 0x91ab, 0x4e8c, 0x4ee5, 0x4f0a, 0xf9dd, 0xf9de, 0x5937,
  0x59e8, 0xf9df, 0x5df2, 0x5f1b, 0x5f5b, 0x6021, 0xf9e0, 0xf9e1,
  0xf9e2, 0xf9e3, 0x723e, 0x73e5, 0xf9e4, 0x7570, 0x75cd, 0xf9e5,
  0x79fb, 0xf9e6, 0x800c, 0x8033, 0x8084, 0x82e1, 0x8351, 0xf9e7,
  0xf9e8, 0x8cbd, 0x8cb3, 0x9087, 0xf9e9, 0xf9ea, 0x98f4, 0x990c,
  0xf9eb, 0xf9ec, 0x7037, 0x76ca, 0x7fca, 0x7fcc, 0x7ffc, 0x8b1a,
  0x4eba, 0x4ec1, 0x5203, 0x5370, 0xf9ed, 0x54bd, 0x56e0, 0x59fb,
  0x5bc5, 0x5f15, 0x5fcd, 0x6e6e, 0xf9ee, 0xf9ef, 0x7d6a, 0x8335,
  0xf9f0, 0x8693, 0x8a8d, 0xf9f1, 0x976d, 0x9777, 0xf9f2, 0xf9f3,
  0x4e00, 0x4f5a, 0x4f7e, 0x58f9, 0x65e5, 0x6ea2, 0x9038, 0x93b0,
  0x99b9, 0x4efb, 0x58ec, 0x598a, 0x59d9, 0x6041, 0xf9f4, 0xf9f5,
  0x7a14, 0xf9f6, 0x834f, 0x8cc3, 0x5165, 0x5344,
  /* 0x6d */
  0xf9f7, 0xf9f8, 0xf9f9, 0x4ecd, 0x5269, 0x5b55, 0x82bf, 0x4ed4,
  0x523a, 0x54a8, 0x59c9, 0x59ff, 0x5b50, 0x5b57, 0x5b5c, 0x6063,
  0x6148, 0x6ecb, 0x7099, 0x716e, 0x7386, 0x74f7, 0x75b5, 0x78c1,
  0x7d2b, 0x8005, 0x81ea, 0x8328, 0x8517, 0x85c9, 0x8aee, 0x8cc7,
  0x96cc, 0x4f5c, 0x52fa, 0x56bc, 0x65ab, 0x6628, 0x707c, 0x70b8,
  0x7235, 0x7dbd, 0x828d, 0x914c, 0x96c0, 0x9d72, 0x5b71, 0x68e7,
  0x6b98, 0x6f7a, 0x76de, 0x5c91, 0x66ab, 0x6f5b, 0x7bb4, 0x7c2a,
  0x8836, 0x96dc, 0x4e08, 0x4ed7, 0x5320, 0x5834, 0x58bb, 0x58ef,
  0x596c, 0x5c07, 0x5e33, 0x5e84, 0x5f35, 0x638c, 0x66b2, 0x6756,
  0x6a1f, 0x6aa3, 0x6b0c, 0x6f3f, 0x7246, 0xf9fa, 0x7350, 0x748b,
  0x7ae0, 0x7ca7, 0x8178, 0x81df, 0x81e7, 0x838a, 0x846c, 0x8523,
  0x8594, 0x85cf, 0x88dd, 0x8d13, 0x91ac, 0x9577,
  /* 0x6e */
  0x969c, 0x518d, 0x54c9, 0x5728, 0x5bb0, 0x624d, 0x6750, 0x683d,
  0x6893, 0x6e3d, 0x6ed3, 0x707d, 0x7e21, 0x88c1, 0x8ca1, 0x8f09,
  0x9f4b, 0x9f4e, 0x722d, 0x7b8f, 0x8acd, 0x931a, 0x4f47, 0x4f4e,
  0x5132, 0x5480, 0x59d0, 0x5e95, 0x62b5, 0x6775, 0x696e, 0x6a17,
  0x6cae, 0x6e1a, 0x72d9, 0x732a, 0x75bd, 0x7bb8, 0x7d35, 0x82e7,
  0x83f9, 0x8457, 0x85f7, 0x8a5b, 0x8caf, 0x8e87, 0x9019, 0x90b8,
  0x96ce, 0x9f5f, 0x52e3, 0x540a, 0x5ae1, 0x5bc2, 0x6458, 0x6575,
  0x6ef4, 0x72c4, 0xf9fb, 0x7684, 0x7a4d, 0x7b1b, 0x7c4d, 0x7e3e,
  0x7fdf, 0x837b, 0x8b2b, 0x8cca, 0x8d64, 0x8de1, 0x8e5f, 0x8fea,
  0x8ff9, 0x9069, 0x93d1, 0x4f43, 0x4f7a, 0x50b3, 0x5168, 0x5178,
  0x524d, 0x526a, 0x5861, 0x587c, 0x5960, 0x5c08, 0x5c55, 0x5edb,
  0x609b, 0x6230, 0x6813, 0x6bbf, 0x6c08, 0x6fb1,
  /* 0x6f */
  0x714e, 0x7420, 0x7530, 0x7538, 0x7551, 0x7672, 0x7b4c, 0x7b8b,
  0x7bad, 0x7bc6, 0x7e8f, 0x8a6e, 0x8f3e, 0x8f49, 0x923f, 0x9293,
  0x9322, 0x942b, 0x96fb, 0x985a, 0x986b, 0x991e, 0x5207, 0x622a,
  0x6298, 0x6d59, 0x7664, 0x7aca, 0x7bc0, 0x7d76, 0x5360, 0x5cbe,
  0x5e97, 0x6f38, 0x70b9, 0x7c98, 0x9711, 0x9b8e, 0x9ede, 0x63a5,
  0x647a, 0x8776, 0x4e01, 0x4e95, 0x4ead, 0x505c, 0x5075, 0x5448,
  0x59c3, 0x5b9a, 0x5e40, 0x5ead, 0x5ef7, 0x5f81, 0x60c5, 0x633a,
  0x653f, 0x6574, 0x65cc, 0x6676, 0x6678, 0x67fe, 0x6968, 0x6a89,
  0x6b63, 0x6c40, 0x6dc0, 0x6de8, 0x6e1f, 0x6e5e, 0x701e, 0x70a1,
  0x738e, 0x73fd, 0x753a, 0x775b, 0x7887, 0x798e, 0x7a0b, 0x7a7d,
  0x7cbe, 0x7d8e, 0x8247, 0x8a02, 0x8aea, 0x8c9e, 0x912d, 0x914a,
  0x91d8, 0x9266, 0x92cc, 0x9320, 0x9706, 0x9756,
  /* 0x70 */
  0x975c, 0x9802, 0x9f0e, 0x5236, 0x5291, 0x557c, 0x5824, 0x5e1d,
  0x5f1f, 0x608c, 0x63d0, 0x68af, 0x6fdf, 0x796d, 0x7b2c, 0x81cd,
  0x85ba, 0x88fd, 0x8af8, 0x8e44, 0x918d, 0x9664, 0x969b, 0x973d,
  0x984c, 0x9f4a, 0x4fce, 0x5146, 0x51cb, 0x52a9, 0x5632, 0x5f14,
  0x5f6b, 0x63aa, 0x64cd, 0x65e9, 0x6641, 0x66fa, 0x66f9, 0x671d,
  0x689d, 0x68d7, 0x69fd, 0x6f15, 0x6f6e, 0x7167, 0x71e5, 0x722a,
  0x74aa, 0x773a, 0x7956, 0x795a, 0x79df, 0x7a20, 0x7a95, 0x7c97,
  0x7cdf, 0x7d44, 0x7e70, 0x8087, 0x85fb, 0x86a4, 0x8a54, 0x8abf,
  0x8d99, 0x8e81, 0x9020, 0x906d, 0x91e3, 0x963b, 0x96d5, 0x9ce5,
  0x65cf, 0x7c07, 0x8db3, 0x93c3, 0x5b58, 0x5c0a, 0x5352, 0x62d9,
  0x731d, 0x5027, 0x5b97, 0x5f9e, 0x60b0, 0x616b, 0x68d5, 0x6dd9,
  0x742e, 0x7a2e, 0x7d42, 0x7d9c, 0x7e31, 0x816b,
  /* 0x71 */
  0x8e2a, 0x8e35, 0x937e, 0x9418, 0x4f50, 0x5750, 0x5de6, 0x5ea7,
  0x632b, 0x7f6a, 0x4e3b, 0x4f4f, 0x4f8f, 0x505a, 0x59dd, 0x80c4,
  0x546a, 0x5468, 0x55fe, 0x594f, 0x5b99, 0x5dde, 0x5eda, 0x665d,
  0x6731, 0x67f1, 0x682a, 0x6ce8, 0x6d32, 0x6e4a, 0x6f8d, 0x70b7,
  0x73e0, 0x7587, 0x7c4c, 0x7d02, 0x7d2c, 0x7da2, 0x821f, 0x86db,
  0x8a3b, 0x8a85, 0x8d70, 0x8e8a, 0x8f33, 0x9031, 0x914e, 0x9152,
  0x9444, 0x99d0, 0x7af9, 0x7ca5, 0x4fca, 0x5101, 0x51c6, 0x57c8,
  0x5bef, 0x5cfb, 0x6659, 0x6a3d, 0x6d5a, 0x6e96, 0x6fec, 0x710c,
  0x756f, 0x7ae3, 0x8822, 0x9021, 0x9075, 0x96cb, 0x99ff, 0x8301,
  0x4e2d, 0x4ef2, 0x8846, 0x91cd, 0x537d, 0x6adb, 0x696b, 0x6c41,
  0x847a, 0x589e, 0x618e, 0x66fe, 0x62ef, 0x70dd, 0x7511, 0x75c7,
  0x7e52, 0x84b8, 0x8b49, 0x8d08, 0x4e4b, 0x53ea,
  /* 0x72 */
  0x54ab, 0x5730, 0x5740, 0x5fd7, 0x6301, 0x6307, 0x646f, 0x652f,
  0x65e8, 0x667a, 0x679d, 0x67b3, 0x6b62, 0x6c60, 0x6c9a, 0x6f2c,
  0x77e5, 0x7825, 0x7949, 0x7957, 0x7d19, 0x80a2, 0x8102, 0x81f3,
  0x829d, 0x82b7, 0x8718, 0x8a8c, 0xf9fc, 0x8d04, 0x8dbe, 0x9072,
  0x76f4, 0x7a19, 0x7a37, 0x7e54, 0x8077, 0x5507, 0x55d4, 0x5875,
  0x632f, 0x6422, 0x6649, 0x664b, 0x686d, 0x699b, 0x6b84, 0x6d25,
  0x6eb1, 0x73cd, 0x7468, 0x74a1, 0x755b, 0x75b9, 0x76e1, 0x771e,
  0x778b, 0x79e6, 0x7e09, 0x7e1d, 0x81fb, 0x852f, 0x8897, 0x8a3a,
  0x8cd1, 0x8eeb, 0x8fb0, 0x9032, 0x93ad, 0x9663, 0x9673, 0x9707,
  0x4f84, 0x53f1, 0x59ea, 0x5ac9, 0x5e19, 0x684e, 0x74c6, 0x75be,
  0x79e9, 0x7a92, 0x81a3, 0x86ed, 0x8cea, 0x8dcc, 0x8fed, 0x659f,
  0x6715, 0xf9fd, 0x57f7, 0x6f57, 0x7ddd, 0x8f2f,
  /* 0x73 */
  0x93f6, 0x96c6, 0x5fb5, 0x61f2, 0x6f84, 0x4e14, 0x4f98, 0x501f,
  0x53c9, 0x55df, 0x5d6f, 0x5dee, 0x6b21, 0x6b64, 0x78cb, 0x7b9a,
  0xf9fe, 0x8e49, 0x8eca, 0x906e, 0x6349, 0x643e, 0x7740, 0x7a84,
  0x932f, 0x947f, 0x9f6a, 0x64b0, 0x6faf, 0x71e6, 0x74a8, 0x74da,
  0x7ac4, 0x7c12, 0x7e82, 0x7cb2, 0x7e98, 0x8b9a, 0x8d0a, 0x947d,
  0x9910, 0x994c, 0x5239, 0x5bdf, 0x64e6, 0x672d, 0x7d2e, 0x50ed,
  0x53c3, 0x5879, 0x6158, 0x6159, 0x61fa, 0x65ac, 0x7ad9, 0x8b92,
  0x8b96, 0x5009, 0x5021, 0x5275, 0x5531, 0x5a3c, 0x5ee0, 0x5f70,
  0x6134, 0x655e, 0x660c, 0x6636, 0x66a2, 0x69cd, 0x6ec4, 0x6f32,
  0x7316, 0x7621, 0x7a93, 0x8139, 0x8259, 0x83d6, 0x84bc, 0x50b5,
  0x57f0, 0x5bc0, 0x5be8, 0x5f69, 0x63a1, 0x7826, 0x7db5, 0x83dc,
  0x8521, 0x91c7, 0x91f5, 0x518a, 0x67f5, 0x7b56,
  /* 0x74 */
  0x8cac, 0x51c4, 0x59bb, 0x60bd, 0x8655, 0x501c, 0xf9ff, 0x5254,
  0x5c3a, 0x617d, 0x621a, 0x62d3, 0x64f2, 0x65a5, 0x6ecc, 0x7620,
  0x810a, 0x8e60, 0x965f, 0x96bb, 0x4edf, 0x5343, 0x5598, 0x5929,
  0x5ddd, 0x64c5, 0x6cc9, 0x6dfa, 0x7394, 0x7a7f, 0x821b, 0x85a6,
  0x8ce4, 0x8e10, 0x9077, 0x91e7, 0x95e1, 0x9621, 0x97c6, 0x51f8,
  0x54f2, 0x5586, 0x5fb9, 0x64a4, 0x6f88, 0x7db4, 0x8f1f, 0x8f4d,
  0x9435, 0x50c9, 0x5c16, 0x6cbe, 0x6dfb, 0x751b, 0x77bb, 0x7c3d,
  0x7c64, 0x8a79, 0x8ac2, 0x581e, 0x59be, 0x5e16, 0x6377, 0x7252,
  0x758a, 0x776b, 0x8adc, 0x8cbc, 0x8f12, 0x5ef3, 0x6674, 0x6df8,
  0x807d, 0x83c1, 0x8acb, 0x9751, 0x9bd6, 0xfa00, 0x5243, 0x66ff,
  0x6d95, 0x6eef, 0x7de0, 0x8ae6, 0x902e, 0x905e, 0x9ad4, 0x521d,
  0x527f, 0x54e8, 0x6194, 0x6284, 0x62db, 0x68a2,
  /* 0x75 */
  0x6912, 0x695a, 0x6a35, 0x7092, 0x7126, 0x785d, 0x7901, 0x790e,
  0x79d2, 0x7a0d, 0x8096, 0x8278, 0x82d5, 0x8349, 0x8549, 0x8c82,
  0x8d85, 0x9162, 0x918b, 0x91ae, 0x4fc3, 0x56d1, 0x71ed, 0x77d7,
  0x8700, 0x89f8, 0x5bf8, 0x5fd6, 0x6751, 0x90a8, 0x53e2, 0x585a,
  0x5bf5, 0x60a4, 0x6181, 0x6460, 0x7e3d, 0x8070, 0x8525, 0x9283,
  0x64ae, 0x50ac, 0x5d14, 0x6700, 0x589c, 0x62bd, 0x63a8, 0x690e,
  0x6978, 0x6a1e, 0x6e6b, 0x76ba, 0x79cb, 0x82bb, 0x8429, 0x8acf,
  0x8da8, 0x8ffd, 0x9112, 0x914b, 0x919c, 0x9310, 0x9318, 0x939a,
  0x96db, 0x9a36, 0x9c0d, 0x4e11, 0x755c, 0x795d, 0x7afa, 0x7b51,
  0x7bc9, 0x7e2e, 0x84c4, 0x8e59, 0x8e74, 0x8ef8, 0x9010, 0x6625,
  0x693f, 0x7443, 0x51fa, 0x672e, 0x9edc, 0x5145, 0x5fe0, 0x6c96,
  0x87f2, 0x885d, 0x8877, 0x60b4, 0x81b5, 0x8403,
  /* 0x76 */
  0x8d05, 0x53d6, 0x5439, 0x5634, 0x5a36, 0x5c31, 0x708a, 0x7fe0,
  0x805a, 0x8106, 0x81ed, 0x8da3, 0x9189, 0x9a5f, 0x9df2, 0x5074,
  0x4ec4, 0x53a0, 0x60fb, 0x6e2c, 0x5c64, 0x4f88, 0x5024, 0x55e4,
  0x5cd9, 0x5e5f, 0x6065, 0x6894, 0x6cbb, 0x6dc4, 0x71be, 0x75d4,
  0x75f4, 0x7661, 0x7a1a, 0x7a49, 0x7dc7, 0x7dfb, 0x7f6e, 0x81f4,
  0x86a9, 0x8f1c, 0x96c9, 0x99b3, 0x9f52, 0x5247, 0x52c5, 0x98ed,
  0x89aa, 0x4e03, 0x67d2, 0x6f06, 0x4fb5, 0x5be2, 0x6795, 0x6c88,
  0x6d78, 0x741b, 0x7827, 0x91dd, 0x937c, 0x87c4, 0x79e4, 0x7a31,
  0x5feb, 0x4ed6, 0x54a4, 0x553e, 0x58ae, 0x59a5, 0x60f0, 0x6253,
  0x62d6, 0x6736, 0x6955, 0x8235, 0x9640, 0x99b1, 0x99dd, 0x502c,
  0x5353, 0x5544, 0x577c, 0xfa01, 0x6258, 0xfa02, 0x64e2, 0x666b,
  0x67dd, 0x6fc1, 0x6fef, 0x7422, 0x7438, 0x8a17,
  /* 0x77 */
  0x9438, 0x5451, 0x5606, 0x5766, 0x5f48, 0x619a, 0x6b4e, 0x7058,
  0x70ad, 0x7dbb, 0x8a95, 0x596a, 0x812b, 0x63a2, 0x7708, 0x803d,
  0x8caa, 0x5854, 0x642d, 0x69bb, 0x5b95, 0x5e11, 0x6e6f, 0xfa03,
  0x8569, 0x514c, 0x53f0, 0x592a, 0x6020, 0x614b, 0x6b86, 0x6c70,
  0x6cf0, 0x7b1e, 0x80ce, 0x82d4, 0x8dc6, 0x90b0, 0x98b1, 0xfa04,
  0x64c7, 0x6fa4, 0x6491, 0x6504, 0x514e, 0x5410, 0x571f, 0x8a0e,
  0x615f, 0x6876, 0xfa05, 0x75db, 0x7b52, 0x7d71, 0x901a, 0x5806,
  0x69cc, 0x817f, 0x892a, 0x9000, 0x9839, 0x5078, 0x5957, 0x59ac,
  0x6295, 0x900f, 0x9b2a, 0x615d, 0x7279, 0x95d6, 0x5761, 0x5a46,
  0x5df4, 0x628a, 0x64ad, 0x64fa, 0x6777, 0x6ce2, 0x6d3e, 0x722c,
  0x7436, 0x7834, 0x7f77, 0x82ad, 0x8ddb, 0x9817, 0x5224, 0x5742,
  0x677f, 0x7248, 0x74e3, 0x8ca9, 0x8fa6, 0x9211,
  /* 0x78 */
  0x962a, 0x516b, 0x53ed, 0x634c, 0x4f69, 0x5504, 0x6096, 0x6557,
  0x6c9b, 0x6d7f, 0x724c, 0x72fd, 0x7a17, 0x8987, 0x8c9d, 0x5f6d,
  0x6f8e, 0x70f9, 0x81a8, 0x610e, 0x4fbf, 0x504f, 0x6241, 0x7247,
  0x7bc7, 0x7de8, 0x7fe9, 0x904d, 0x97ad, 0x9a19, 0x8cb6, 0x576a,
  0x5e73, 0x67b0, 0x840d, 0x8a55, 0x5420, 0x5b16, 0x5e63, 0x5ee2,
  0x5f0a, 0x6583, 0x80ba, 0x853d, 0x9589, 0x965b, 0x4f48, 0x5305,
  0x530d, 0x530f, 0x5486, 0x54fa, 0x5703, 0x5e03, 0x6016, 0x629b,
  0x62b1, 0x6355, 0xfa06, 0x6ce1, 0x6d66, 0x75b1, 0x7832, 0x80de,
  0x812f, 0x82de, 0x8461, 0x84b2, 0x888d, 0x8912, 0x900b, 0x92ea,
  0x98fd, 0x9b91, 0x5e45, 0x66b4, 0x66dd, 0x7011, 0x7206, 0xfa07,
  0x4ff5, 0x527d, 0x5f6a, 0x6153, 0x6753, 0x6a19, 0x6f02, 0x74e2,
  0x7968, 0x8868, 0x8c79, 0x98c7, 0x98c4, 0x9a43,
  /* 0x79 */
  0x54c1, 0x7a1f, 0x6953, 0x8af7, 0x8c4a, 0x98a8, 0x99ae, 0x5f7c,
  0x62ab, 0x75b2, 0x76ae, 0x88ab, 0x907f, 0x9642, 0x5339, 0x5f3c,
  0x5fc5, 0x6ccc, 0x73cc, 0x7562, 0x758b, 0x7b46, 0x82fe, 0x999d,
  0x4e4f, 0x903c, 0x4e0b, 0x4f55, 0x53a6, 0x590f, 0x5ec8, 0x6630,
  0x6cb3, 0x7455, 0x8377, 0x8766, 0x8cc0, 0x9050, 0x971e, 0x9c15,
  0x58d1, 0x5b78, 0x8650, 0x8b14, 0x9db4, 0x5bd2, 0x6068, 0x608d,
  0x65f1, 0x6c57, 0x6f22, 0x6fa3, 0x701a, 0x7f55, 0x7ff0, 0x9591,
  0x9592, 0x9650, 0x97d3, 0x5272, 0x8f44, 0x51fd, 0x542b, 0x54b8,
  0x5563, 0x558a, 0x6abb, 0x6db5, 0x7dd8, 0x8266, 0x929c, 0x9677,
  0x9e79, 0x5408, 0x54c8, 0x76d2, 0x86e4, 0x95a4, 0x95d4, 0x965c,
  0x4ea2, 0x4f09, 0x59ee, 0x5ae6, 0x5df7, 0x6052, 0x6297, 0x676d,
  0x6841, 0x6c86, 0x6e2f, 0x7f38, 0x809b, 0x822a,
  /* 0x7a */
  0xfa08, 0xfa09, 0x9805, 0x4ea5, 0x5055, 0x54b3, 0x5793, 0x595a,
  0x5b69, 0x5bb3, 0x61c8, 0x6977, 0x6d77, 0x7023, 0x87f9, 0x89e3,
  0x8a72, 0x8ae7, 0x9082, 0x99ed, 0x9ab8, 0x52be, 0x6838, 0x5016,
  0x5e78, 0x674f, 0x8347, 0x884c, 0x4eab, 0x5411, 0x56ae, 0x73e6,
  0x9115, 0x97ff, 0x9909, 0x9957, 0x9999, 0x5653, 0x589f, 0x865b,
  0x8a31, 0x61b2, 0x6af6, 0x737b, 0x8ed2, 0x6b47, 0x96aa, 0x9a57,
  0x5955, 0x7200, 0x8d6b, 0x9769, 0x4fd4, 0x5cf4, 0x5f26, 0x61f8,
  0x665b, 0x6ceb, 0x70ab, 0x7384, 0x73b9, 0x73fe, 0x7729, 0x774d,
  0x7d43, 0x7d62, 0x7e23, 0x8237, 0x8852, 0xfa0a, 0x8ce2, 0x9249,
  0x986f, 0x5b51, 0x7a74, 0x8840, 0x9801, 0x5acc, 0x4fe0, 0x5354,
  0x593e, 0x5cfd, 0x633e, 0x6d79, 0x72f9, 0x8105, 0x8107, 0x83a2,
  0x92cf, 0x9830, 0x4ea8, 0x5144, 0x5211, 0x578b,
  /* 0x7b */
  0x5f62, 0x6cc2, 0x6ece, 0x7005, 0x7050, 0x70af, 0x7192, 0x73e9,
  0x7469, 0x834a, 0x87a2, 0x8861, 0x9008, 0x90a2, 0x93a3, 0x99a8,
  0x516e, 0x5f57, 0x60e0, 0x6167, 0x66b3, 0x8559, 0x8e4a, 0x91af,
  0x978b, 0x4e4e, 0x4e92, 0x547c, 0x58d5, 0x58fa, 0x597d, 0x5cb5,
  0x5f27, 0x6236, 0x6248, 0x660a, 0x6667, 0x6beb, 0x6d69, 0x6dcf,
  0x6e56, 0x6ef8, 0x6f94, 0x6fe0, 0x6fe9, 0x705d, 0x72d0, 0x7425,
  0x745a, 0x74e0, 0x7693, 0x795c, 0x7cca, 0x7e1e, 0x80e1, 0x82a6,
  0x846b, 0x84bf, 0x864e, 0x865f, 0x8774, 0x8b77, 0x8c6a, 0x93ac,
  0x9800, 0x9865, 0x60d1, 0x6216, 0x9177, 0x5a5a, 0x660f, 0x6df7,
  0x6e3e, 0x743f, 0x9b42, 0x5ffd, 0x60da, 0x7b0f, 0x54c4, 0x5f18,
  0x6c5e, 0x6cd3, 0x6d2a, 0x70d8, 0x7d05, 0x8679, 0x8a0c, 0x9d3b,
  0x5316, 0x548c, 0x5b05, 0x6a3a, 0x706b, 0x7575,
  /* 0x7c */
  0x798d, 0x79be, 0x82b1, 0x83ef, 0x8a71, 0x8b41, 0x8ca8, 0x9774,
  0xfa0b, 0x64f4, 0x652b, 0x78ba, 0x78bb, 0x7a6b, 0x4e38, 0x559a,
  0x5950, 0x5ba6, 0x5e7b, 0x60a3, 0x63db, 0x6b61, 0x6665, 0x6853,
  0x6e19, 0x7165, 0x74b0, 0x7d08, 0x9084, 0x9a69, 0x9c25, 0x6d3b,
  0x6ed1, 0x733e, 0x8c41, 0x95ca, 0x51f0, 0x5e4c, 0x5fa8, 0x604d,
  0x60f6, 0x6130, 0x614c, 0x6643, 0x6644, 0x69a5, 0x6cc1, 0x6e5f,
  0x6ec9, 0x6f62, 0x714c, 0x749c, 0x7687, 0x7bc1, 0x7c27, 0x8352,
  0x8757, 0x9051, 0x968d, 0x9ec3, 0x532f, 0x56de, 0x5efb, 0x5f8a,
  0x6062, 0x6094, 0x61f7, 0x6666, 0x6703, 0x6a9c, 0x6dee, 0x6fae,
  0x7070, 0x736a, 0x7e6a, 0x81be, 0x8334, 0x86d4, 0x8aa8, 0x8cc4,
  0x5283, 0x7372, 0x5b96, 0x6a6b, 0x9404, 0x54ee, 0x5686, 0x5b5d,
  0x6548, 0x6585, 0x66c9, 0x689f, 0x6d8d, 0x6dc6,
  /* 0x7d */
  0x723b, 0x80b4, 0x9175, 0x9a4d, 0x4faf, 0x5019, 0x539a, 0x540e,
  0x543c, 0x5589, 0x55c5, 0x5e3f, 0x5f8c, 0x673d, 0x7166, 0x73dd,
  0x9005, 0x52db, 0x52f3, 0x5864, 0x58ce, 0x7104, 0x718f, 0x71fb,
  0x85b0, 0x8a13, 0x6688, 0x85a8, 0x55a7, 0x6684, 0x714a, 0x8431,
  0x5349, 0x5599, 0x6bc1, 0x5f59, 0x5fbd, 0x63ee, 0x6689, 0x7147,
  0x8af1, 0x8f1d, 0x9ebe, 0x4f11, 0x643a, 0x70cb, 0x7566, 0x8667,
  0x6064, 0x8b4e, 0x9df8, 0x5147, 0x51f6, 0x5308, 0x6d36, 0x80f8,
  0x9ed1, 0x6615, 0x6b23, 0x7098, 0x75d5, 0x5403, 0x5c79, 0x7d07,
  0x8a16, 0x6b20, 0x6b3d, 0x6b46, 0x5438, 0x6070, 0x6d3d, 0x7fd5,
  0x8208, 0x50d6, 0x51de, 0x559c, 0x566b, 0x56cd, 0x59ec, 0x5b09,
  0x5e0c, 0x6199, 0x6198, 0x6231, 0x665e, 0x66e6, 0x7199, 0x71b9,
  0x71ba, 0x72a7, 0x79a7, 0x7a00, 0x7fb2, 0x8a70,
};

static int ksc5601_mbtowc (unsigned short *pwc, const unsigned char *s, size_t n)
{
  unsigned char c1 = s[0] - 0x80;
  Duster_module_Printf(1,"%s,c1:0x%x",__FUNCTION__,c1);
  if ((c1 >= 0x21 && c1 <= 0x2c) || (c1 >= 0x30 && c1 <= 0x48) || (c1 >= 0x4a && c1 <= 0x7d)) 
  {
    	if (n >= 2) 
	{
      		unsigned char c2 = s[1] - 0x80;
	       Duster_module_Printf(1,"%s,c2:0x%x",__FUNCTION__,c2);	
      	       if (c2 >= 0x21 && c2 < 0x7f) 
	       {
        		unsigned int i = 94 * (c1 - 0x21) + (c2 - 0x21);
        		unsigned short wc = 0xfffd;
			Duster_module_Printf(1,"%s,i:0x%x",__FUNCTION__,i);		
        		if (i < 1410)
			{
          			if (i < 1115)
            				wc = ksc5601_2uni_page21[i];
        		}
			else if (i < 3854)
			{
          			if (i < 3760)
            				wc = ksc5601_2uni_page30[i-1410];
        		}
			else
        		{
          			if (i < 8742)
            				wc = ksc5601_2uni_page4a[i-3854];
        		}
			Duster_module_Printf(1,"%s,wc:0x%x",__FUNCTION__,wc);	
        		if (wc != 0xfffd)
			{
          			*pwc = wc;
          			return 2;
        		}
      		}
      		return 3;
    	}
    	return 4;
  }
  return 3;
}

int ksc5601_str_to_ucs2(unsigned short *ucs2Str,const unsigned char *ksc5601str,int inlen)
{
       int loop = 0;
	int i = 0;  
	Duster_module_Printf(1,"%s,ksc5601str:%s",__FUNCTION__,ksc5601str);
	Duster_module_Printf(1,"%s,inlen:%d",__FUNCTION__,inlen);
	for(loop = 0;loop<=inlen - 1;loop++)
	{
	      Duster_module_Printf(1,"%s,ksc5601str[%d]:0x%x",__FUNCTION__,loop,ksc5601str[loop]);
		if(ksc5601str[loop] <= 0x80)
		{
			ucs2Str[i] = (unsigned short)(ksc5601str[loop]);
			i++;
		}
		else
		{
			if(0x2 == ksc5601_mbtowc (&(ucs2Str[i]), &(ksc5601str[loop]),0x2))
			{	
				loop++;
				i++;
			}
		}
		Duster_module_Printf(1,"%s,ucs2Str:%s,ucs2Str[%d]:0x%x",__FUNCTION__,ucs2Str,loop,ucs2Str[loop]);
	}
	
	return i*2;
}

#endif

//
