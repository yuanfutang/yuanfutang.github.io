/* -*- linux-c -*- */
/*******************************************************************************
*               Copyright 2009, Marvell Technology Group Ltd.
*
* THIS CODE CONTAINS CONFIDENTIAL INFORMATION OF MARVELL. NO RIGHTS ARE GRANTED
* HEREIN UNDER ANY PATENT, MASK WORK RIGHT OR COPYRIGHT OF MARVELL OR ANY THIRD
* PARTY. MARVELL RESERVES THE RIGHT AT ITS SOLE DISCRETION TO REQUEST THAT THIS
* CODE BE IMMEDIATELY RETURNED TO MARVELL. THIS CODE IS PROVIDED "AS IS".
* MARVELL MAKES NO WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS
* ACCURACY, COMPLETENESS OR PERFORMANCE. MARVELL COMPRISES MARVELL TECHNOLOGY
* GROUP LTD. (MTGL) AND ITS SUBSIDIARIES, MARVELL INTERNATIONAL LTD. (MIL),
* MARVELL TECHNOLOGY, INC. (MTI), MARVELL SEMICONDUCTOR, INC. (MSI), MARVELL
* ASIA PTE LTD. (MAPL), MARVELL JAPAN K.K. (MJKK), GALILEO TECHNOLOGY LTD. (GTL)
* GALILEO TECHNOLOGY, INC. (GTI) AND RADLAN Computer Communications, LTD.
********************************************************************************
*/
#include <stdio.h>
#include <string.h>
#include "psm.h"
#include "duster_applets.h"
#include "duster.h"
#include "FDI_TYPE.h"
#include "FDI_FILE.h"
#include "mongoose.h"
#include "osa.h"
#include "rdiskfsys.h"
#include "psm_wrapper.h"
#include "MrvXML.h"
#include "common_print.h"
#include "UART.h"

typedef enum MrvXMLParserSetType
{
	noAction = 0,
	setModuleName ,
	setSubspaceName,
	addSubspaceName

} MrvXMLParserSetType;


short duster_action;
short dostuff;
char *module_name;
psm_handle_t *g_handle[TOTAL_PSMFILE_NUM];
static int dirty = 0;

/* 0: Fail. 1: Succeed */
int validation_status = 0;
#define VALIDATION_FAIL 1
#define VALIDATION_SUCCEED 0

#define DLIST_META_SUFFIX     "_meta"
#define DLIST_LIST_SUFFIX     "_list"
#define DLIST_LIST_SUFFIX_LEN (sizeof(DLIST_LIST_SUFFIX) - 1)
extern unsigned long rti_get_current_tick(void);

#define duster_log(fmt,args...)	{Duster_module_Printf(4,fmt, ##args);}
#define duster_critic(fmt,args...)	{Duster_module_Printf(2,fmt, ##args);}
#define duster_err(fmt,args...)	{Duster_module_Printf(2,fmt, ##args);}


#define assert(x) DIAG_ASSERT(x)

#define XML_FILE_SIZE 5120
#define MAX_XML_FILE_SIZE 3*4096
//char * gdusterbuf1[4096];
//char * gdusterbuf2[4096];
static unsigned char buffer_load[XML_FILE_SIZE + 1];
static unsigned char buffer_save[MAX_XML_FILE_SIZE + 1];
//static unsigned char *buffer_save = NULL;
extern BOOL InUpgradeReset;
extern char * g_backup_psm_buf;
extern 	void set_need_ascii_to_html();
extern void clear_need_ascii_to_html();

unsigned char g_upgrade_firmware[256] ={"<?xml version=\"1.0\" encoding=\"US-ASCII\" ?> <RGW><webui_upgrade><upgrade_status /><progress /><upgrade_fail_cause /><backup_status /><backup_progress /><backup_fail_cause /><restore_status /><restore_progress /><restore_fail_cause /></webui_upgrade></RGW>"};
char * duster_strndup(const char *ptr, size_t len)
{
	char *p;

	if ((p = (char *) duster_malloc(len + 1)) != NULL)
	{
		strncpy(p, ptr, len + 1);
	}

	return p;
}

char * strdup(const char *str)
{
	return duster_strndup(str, strlen(str));
}
char * duster_strdup(const char *str)
{
	return duster_strndup(str, strlen(str));
}

static void mrvxml_handleList(MrvXMLElement *pEntry,char *moduleName,char *subspaceName,char *method)
{

	char 		 field_char,record_char ;
	dlist_node_t dlist;
	int 		 nSize = 0,fields = 0;
	char 		*meta  = NULL,*meta_value = NULL, *elementval = NULL;
	dc_args_t 	 dca = {   };
	char 		*newelementval = NULL;


	duster_log("enter %s, moduleName =%s ,method = %s,",__FUNCTION__,	moduleName,	method);
	if(!moduleName)
		goto EXIT;
	if(subspaceName)
		duster_err("%s subspaceName is %s",__FUNCTION__,subspaceName);

	duster_call_module_get_delimiters(moduleName, &field_char, &record_char);
	dl_dlist_init(&dlist);
	nSize = strlen(pEntry->lpszName) + strlen(DLIST_META_SUFFIX) +1;
	meta  = duster_malloc(nSize);
	if(!meta)
		goto EXIT;
	memset(meta,	0,	nSize);
	snprintf(meta, nSize, "%s"DLIST_META_SUFFIX, pEntry->lpszName);
	duster_log("%s meta is %s",__FUNCTION__,meta);
	meta_value = psm_get_wrapper(moduleName,	subspaceName,	meta);
	if(!meta_value||(strcmp(meta_value,	"") == 0))
	{
		duster_err("%s can not get meta_vlaue",__FUNCTION__);
		goto EXIT;
	}
	duster_log("%s ,meta_value is %s",__FUNCTION__,meta_value);


	fields = dl_get_meta_fields(meta_value, &dlist, field_char);

	elementval = psm_get_wrapper(moduleName,	subspaceName,	pEntry->lpszName);
	if(!elementval ||(strcmp(elementval, "") == 0))
	{
		duster_err("%s can not get elementval",__FUNCTION__);
		if(strcmp(method,"get") == 0)
		{
			duster_log("%s goto EXIT",	__FUNCTION__);
			goto EXIT;
		}
	}
	if(elementval)
		duster_err("%s,elementval is %s",__FUNCTION__,elementval);
	dl_variable_to_dlist_alloc(elementval, &dlist, fields, field_char, record_char,moduleName);

	dca.dc_type = DUSTER_CB_ARGS_LIST;
	dca.dc_data.dc_dlist = &dlist;
	dca.dc_root = NULL;

	if(strcmp(method,"get") == 0)
	{
		duster_call_module_post_get_handler(moduleName, DUSTER_CONFIG_GETLIST, &dca);
		duster_critic("%s,before mcbxmllist,pEntry->lpszName is %s,",__FUNCTION__,pEntry->lpszName);
		dl_dlist_to_mrvxmllist(&dlist,pEntry);
	}
	else if((strcmp(method, "set") == 0))
	{
		duster_call_module_pre_set_handler(moduleName, DUSTER_CONFIG_SETLIST, &dca);
		dl_mrvxmllist_dlist(pEntry,&dlist);
		dl_dlist_to_variable(&newelementval, &dlist, field_char, record_char);
		if (newelementval)
		{
		    duster_log("%s, newelementval is %s",	__FUNCTION__,	newelementval);
			psm_set_wrapper(moduleName, subspaceName ,pEntry->lpszName,newelementval);
			dirty = 1;
		}
	}

EXIT:
	if(newelementval)
		duster_free(newelementval);
	if(meta)
		duster_free(meta);
	if(meta_value)
		duster_free(meta_value);
	if(elementval)
		duster_free(elementval);
	dl_free_dlist(&dlist);

	duster_log("leave %s",__FUNCTION__);
	return ;

}

inline int is_node_list(char *name)
{
	unsigned int len;
	/* If the element name ends with the suffix DLIST_LIST_SUFFIX, then it is a list */
	len = strlen(name);

	if (len > DLIST_LIST_SUFFIX_LEN &&
	        strncmp( (name) + len - DLIST_LIST_SUFFIX_LEN, DLIST_LIST_SUFFIX, DLIST_LIST_SUFFIX_LEN) == 0)
		return 1;
	else
		return 0;
}

void mrvxml_Travel(MrvXMLElement *pEntry,char* method,char *moduleName,char* subspaceName)
{
	int nIndex = 0,	nModuleLen = 0,nSubLen = 0;
	MrvXMLParserSetType nSetType = noAction;
	MrvXMLElement * pResult = NULL;
	MrvXMLText 	   *pTextNode = NULL;
	char 	  	   *str = NULL,	*tmpName = NULL;
	char  *retrive_value = NULL;
	
	if(subspaceName)
		duster_log("%s ,subspaceName is %s",__FUNCTION__,subspaceName);
	duster_log("enter %s,method is %s,moduleName is %s",__FUNCTION__,method, moduleName);

	for (; nIndex < pEntry->nSize ; nIndex++)
	{
		switch(pEntry->pEntries[nIndex].type)
		{
		case eNodeElement:
			duster_log("%s nIndex is %d",__FUNCTION__,nIndex);
			pResult = pEntry->pEntries[nIndex].node.pElement;
			if(is_node_list(pResult->lpszName)&&strcmp(method,"default"))
			{
				if(strcmp(pResult->lpszName,	"message_list") == 0
					|| strcmp(pResult->lpszName,	"deny_name_list") == 0
					||strcmp(pResult->lpszName,	"allow_name_list") == 0)
					continue;
				mrvxml_handleList(pResult, moduleName, subspaceName, method);
				break;
			}
			pResult = pEntry->pEntries[nIndex].node.pElement;
			if(!moduleName)
			{
				nModuleLen = strlen(pResult->lpszName) + 1;
				moduleName = duster_malloc(nModuleLen);
				ASSERT(moduleName);
				memset(moduleName,	0	,nModuleLen);
				memcpy(moduleName,	pResult->lpszName,	nModuleLen-1);
				nSetType = setModuleName;
				break;
			}
			else if(moduleName&&!subspaceName)
			{
				duster_log("%s, no subspaceName,pResult->lpszName is %s",__FUNCTION__,pResult->lpszName);
				if( (strcmp(method,"default")==0) ||	(strcmp(method,"set") ==0 ))
				{
					pTextNode = MrvFindOnlyText(pResult);
					if( (MrvCountElement(pResult) == 0)&&pTextNode)
					{
						if(strcmp(method,"default")==0)
						{
							/*do not set the value when there's value in psm during default processing*/
							str = psm_get_wrapper(moduleName,	subspaceName,	pResult->lpszName);
							if(str&&strcmp(str, ""))
							{
								duster_log("%s, default ,pResult->lpszName in psm is %s",__FUNCTION__,str);
								duster_free(str);
								str = NULL;
								nSetType = noAction;
								if(InUpgradeReset)
								{
									duster_log("%s:[InUpgradeReset] default ,pResult->lpszName in psm",__FUNCTION__);
								}
								break;
							}
							else
							{
							#if 0
								if(InUpgradeReset)
								{
									str = psm_get_wrapper_with_variable_name(moduleName,	pResult->lpszName);
									if(str)
										CPUartLogPrintf("%s:[InUpgradeReset] module %s, pResult->lpszName is %s in psm is %s",__FUNCTION__,moduleName,pResult->lpszName,str);
									else
										CPUartLogPrintf("%s:[InUpgradeReset] module %s ,pResult->lpszName %s not in psm",__FUNCTION__,moduleName,pResult->lpszName);
									nSetType = noAction;
									if(str)
									{
										psm_set_wrapper(moduleName, subspaceName,	pResult->lpszName,	str);
										duster_free(str);
										dirty = 1;
										break;
									}
								}
							#endif
								if(str)
								{
									duster_free(str);
									str = NULL;
								}
							}
						}
						duster_log("%s,pResult->lpszName is %s,pTextNode->lpszValue is %s",__FUNCTION__,pResult->lpszName,pTextNode->lpszValue);
						#ifdef SUPPORT_SD_UPGRADE
						if(strcmp(method,"default")==0)
						{
							if(is_upgrade_success())
							{
								retrive_value = retrive_psm_backup_value(g_backup_psm_buf,moduleName,subspaceName,pResult->lpszName);
								if(retrive_value)
								{
									CPUartLogPrintf("%s: %s.%s = %s", __FUNCTION__, moduleName,pResult->lpszName,retrive_value);
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	retrive_value);
									duster_free(retrive_value);
									retrive_value = NULL;
								}
								else
								{
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
								}
							}
							else
								psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						}
						else
						{
							psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						}
						#else
							psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						#endif
						dirty = 1;
						nSetType = noAction;
						break;
					}
					else if(MrvCountElement(pResult) == 0)
					{
						#ifdef SUPPORT_SD_UPGRADE
						if(strcmp(method,"default")==0)
						{
							if(is_upgrade_success())
							{
								retrive_value = retrive_psm_backup_value(g_backup_psm_buf,moduleName,subspaceName,pResult->lpszName);
								if(retrive_value)
								{
									CPUartLogPrintf("%s: %s.%s = %s", __FUNCTION__, moduleName,pResult->lpszName,retrive_value);
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	retrive_value);
									duster_free(retrive_value);
									retrive_value = NULL;
								}
							}
						}
					    #endif
						break;
					}
				}
				else if(strcmp(method ,"get") == 0)
				{
					if(MrvCountElement(pResult) == 0)
					{
						str = psm_get_wrapper(moduleName, subspaceName,   pResult->lpszName);
						pTextNode = MrvFindOnlyText(pResult);

						if(str&&strcmp(str,	""))
						{
							if(pTextNode)
							{
								if(pTextNode->lpszValue)
									duster_free(pTextNode->lpszValue);
								pTextNode->lpszValue = str;
								str = NULL;
							}
							else if(!pTextNode)
							{
								MrvAddText(pResult, MrvStrdup(str, 0), 1);
								duster_free(str);
								str = NULL;
							}
						}
						else
						{
							if(str)
							{
								duster_free(str);
								str = NULL;
							}
						}
						nSetType = noAction;
						break;
					}
				}
				nSubLen 	   = strlen(pResult->lpszName) + 1;
				subspaceName = duster_malloc(nSubLen);
				ASSERT(subspaceName);
				memset(subspaceName,	0,	nSubLen);
				memcpy(subspaceName,	pResult->lpszName,	nSubLen-1);
				nSetType = setSubspaceName;
				mrvxml_Travel(pResult,	  method  ,moduleName,	  subspaceName);
				break;
			}
			else if(moduleName&&subspaceName)
			{
				duster_log("%s, subspaceName %s,pResult->lpszName is %s",__FUNCTION__,subspaceName,pResult->lpszName);
				if( (strcmp(method,"default")==0) ||	(strcmp(method,"set") ==0 ))
				{
					pTextNode = MrvFindOnlyText(pResult);
					if( (MrvCountElement(pResult) == 0)&&pTextNode)
					{
						if(strcmp(method,"default")==0)
						{
							/*do not set the value when there's value in psm during default processing*/
							str = psm_get_wrapper(moduleName,	subspaceName,	pResult->lpszName);
							if(str&&strcmp(str, ""))
							{
								duster_log("%s, default ,pResult->lpszName in psm is %s",__FUNCTION__,str);
								duster_free(str);
								str = NULL;
								nSetType = noAction;
								if(InUpgradeReset)
								{
									duster_log("%s:[InUpgradeReset] default ,pResult->lpszName in psm",__FUNCTION__);
								}
								break;
							}
							else
							{
							#if 0
								if(InUpgradeReset)
								{
									str = psm_get_wrapper_with_variable_name(moduleName,	pResult->lpszName);
									CPUartLogPrintf("%s:[InUpgradeReset] default ,pResult->lpszName in psm is %s",__FUNCTION__,str);
									nSetType = noAction;
									if(str)
									{
										psm_set_wrapper(moduleName, subspaceName,	pResult->lpszName,	str);
										duster_free(str);
										dirty = 1;
										break;
									}

								}
							#endif
								if(str)
								{
									duster_free(str);
									str = NULL;
								}
							}
						}
						duster_log("%s,pResult->lpszName is %s,pTextNode->lpszValue is %s",__FUNCTION__,pResult->lpszName,pTextNode->lpszValue);
						#ifdef SUPPORT_SD_UPGRADE
						if(strcmp(method,"default")==0)
						{
							if(is_upgrade_success())
							{
								retrive_value = retrive_psm_backup_value(g_backup_psm_buf,moduleName,subspaceName,pResult->lpszName);
								if(retrive_value)
								{
									CPUartLogPrintf("%s: %s.%s.%s = %s", __FUNCTION__, moduleName,subspaceName,pResult->lpszName,retrive_value);
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	retrive_value);
									duster_free(retrive_value);
									retrive_value = NULL;
								}
								else
								{
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
								}
							}
							else
								psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						}
						else
						{
							psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						}
						#else
							psm_set_wrapper(moduleName, subspaceName,	pResult->lpszName,	pTextNode->lpszValue);
						#endif
						dirty = 1;
						nSetType = noAction;
						break;
					}
					else if(MrvCountElement(pResult) == 0)
					{
					#if 0
						if(InUpgradeReset)
						{
							CPUartLogPrintf("%s:[InUpgradeReset] module %s variable %s",__FUNCTION__,moduleName,pResult->lpszName);
							str = psm_get_wrapper_with_variable_name(moduleName,	pResult->lpszName);
							if(str)
							    CPUartLogPrintf("%s:[InUpgradeReset] module %s ,pResult->lpszName in psm is %s",__FUNCTION__,moduleName,str);
							else
								CPUartLogPrintf("%s:[InUpgradeReset] module %s ,pResult->lpszName %s not in psm",__FUNCTION__,moduleName,pResult->lpszName);
							if(str)
							{
								psm_set_wrapper(moduleName, subspaceName,	pResult->lpszName,	str);
								duster_free(str);
								dirty = 1;
							}
							nSetType = noAction;
					#endif
					#ifdef SUPPORT_SD_UPGRADE
						if(strcmp(method,"default")==0)
						{
							if(is_upgrade_success())
							{
								retrive_value = retrive_psm_backup_value(g_backup_psm_buf,moduleName,subspaceName,pResult->lpszName);
								if(retrive_value)
								{
									CPUartLogPrintf("%s: %s.%s.%s = %s", __FUNCTION__, moduleName,subspaceName,pResult->lpszName,retrive_value);
									psm_set_wrapper(moduleName,	subspaceName,	pResult->lpszName,	retrive_value);
									duster_free(retrive_value);
									retrive_value = NULL;
								}
							}
						}
					#endif
						break;
					}
				}
				else if(strcmp(method ,"get") == 0)
				{
					if( MrvCountElement(pResult) == 0)
					{
						str = psm_get_wrapper(moduleName, subspaceName,   pResult->lpszName);
						pTextNode = MrvFindOnlyText(pResult);
						if(str&&strcmp(str,	""))
						{
							if(pTextNode)
							{
								if(pTextNode->lpszValue)
									duster_free(pTextNode->lpszValue);
								pTextNode->lpszValue = str;
								str = NULL;
							}
							else if(!pTextNode)
							{
								MrvAddText(pResult, MrvStrdup(str,0), 1);
								duster_free(str);
								str = NULL;
							}
						}
						else
						{	if(str)
							{
								duster_free(str);	
								str = NULL;
							}
						}
						nSetType = noAction;
						break;
					}
				}

				if(tmpName)
					duster_free(tmpName);
				nSubLen = strlen(subspaceName) + strlen(pResult->lpszName) + 3;
				tmpName = duster_malloc(nSubLen);
				ASSERT(tmpName);
				memset(tmpName,	0,	nSubLen);
				sprintf(tmpName,	"%s.%s",	subspaceName,	pResult->lpszName);

				nSetType = addSubspaceName;
				mrvxml_Travel(pResult,	  method  ,moduleName,	  tmpName);
				break;
			}
		case eNodeAttribute:
		case eNodeText:
		case eNodeClear:
		case eNodeEmpty:
			break;
		}
		duster_log("%s,nSetType is %d",__FUNCTION__,nSetType);
		switch(nSetType)
		{
		case setModuleName:
			if(moduleName)
			{
				duster_free(moduleName);
			}
			moduleName = NULL;
			break;
		case setSubspaceName:
			if(subspaceName)
				duster_free(subspaceName);
			subspaceName = NULL;
			break;
		case addSubspaceName:
			if(tmpName)
				duster_free(tmpName);
			tmpName = NULL;
			break;
		case noAction:
			break;
		}
	}

	duster_log("leave %s",__FUNCTION__);

}

int take_action_nucleus_mrvxml(char *fpath, char *fileres,	char *method)
{

	FILE_ID 		fid = NULL, 		 fid1 = NULL;
	int 			read_size = 0,	 nNameLen = 0;
	MrvXMLElement  *root = NULL,*curRoot = NULL, *pChlElement = NULL;
	MrvXMLResults  *result = NULL;
	int 			nIndex = 0,  save_size = 0,	  written = 0,	creat_size = 0;
	char 		   *moduleName = NULL,*subspaceName = NULL, *str = NULL, *malloc_buffer_save = NULL;
	int 			textRow = 0;

	duster_critic("%s:Enter,fpath is %s",__FUNCTION__,fpath);


	if(!strncmp(fpath, "www\\data", 8))
	{
		fid = FDI_fopen(fpath, "rb");///www//data
		if (fid == NULL)
		{
			CPUartLogPrintf("Couldn't read file");
			return -1;
		}
		read_size = FDI_fread(buffer_load, sizeof(char), XML_FILE_SIZE, fid);
		duster_log("%s read_size is %d",	__FUNCTION__,	read_size);
		FDI_fclose(fid);
	}
	else
	{
		if(PlatformIsMinSystem())
		{
			if(strcmp(fpath,"www\\xmldata\\upgrade_firmware.xml") == 0)
			{
				strncpy(buffer_load, g_upgrade_firmware, strlen(g_upgrade_firmware));
				read_size = strlen(g_upgrade_firmware);
			}
			else
			{
				CPUartLogPrintf("NOT support the file %s request",fpath);
				return -1;
			}
		}
		else
		{
			memset(buffer_load, 0, XML_FILE_SIZE + 1);
			if(strcmp(fpath,"www\\xmldata\\upgrade_firmware.xml") == 0)
			{
				strncpy(buffer_load, g_upgrade_firmware, strlen(g_upgrade_firmware));
				read_size = strlen(g_upgrade_firmware);
			}
			else
			{
				fid = Rdisk_fopen(fpath);//open  temp xml file, if set the file is post from client, if get the file is in /www/data/*
				if (fid == NULL)
				{
					CPUartLogPrintf("Couldn't read file");
					return -1;
				}
				read_size = Rdisk_fread(buffer_load, sizeof(char), XML_FILE_SIZE, fid);
				buffer_load[read_size] = '\0';
				duster_err("%c %c %c %c",buffer_load[0],buffer_load[1],buffer_load[2],buffer_load[3]);
				Rdisk_fclose(fid);
			}
		}
	}

	if(read_size <= XML_FILE_SIZE)
		buffer_load[read_size] = '\0';
	else
	{
		CPUartLogPrintf("Wrong size of XML file");
		return -1;
	}
	duster_err("size:%d",read_size);
	duster_err("again %c %c %c %c",buffer_load[0],buffer_load[1],buffer_load[2],buffer_load[3]);
	result = (MrvXMLResults*)duster_malloc(sizeof(MrvXMLResults));
	ASSERT(result != NULL);
	memset(result,	0,	sizeof(MrvXMLResults));

	root = MrvParseXML(buffer_load, result);
	if(!root)
	{
		duster_err("%s ParseXML failed fpath is %s",__FUNCTION__,fpath);
		if(result)/*memory leaks before code 12 byte*/
		{
			duster_free(result);
			result = NULL;
		}
		return -1;
	}

	curRoot = MrvFindElement(root,"xml/RGW");

	if(result)/*memory leaks before code 12 byte*/
	{
		duster_free(result);
		result = NULL;
	}

	if(!curRoot)
	{
		CPUartLogPrintf("%s:load xml string to xml node failed",__FUNCTION__);
		MrvDeleteRoot(root);
		return -1;
	}

	if(!PlatformIsMinSystem())
	{
		psm_open__("default", NULL);
	}

	for (; nIndex < curRoot->nSize ; nIndex++)
	{
		dc_args_t dca = { } ;
		dca.dc_type = DUSTER_CB_ARGS_XML;
		if(curRoot->pEntries[nIndex].type == eNodeElement)
		{
			duster_log("%s nIndex is %d",__FUNCTION__,nIndex);
			pChlElement = curRoot->pEntries[nIndex].node.pElement;
			if(pChlElement->lpszName)
			{
				if(moduleName)
					duster_free(moduleName);
				moduleName = NULL;
				nNameLen = strlen(pChlElement->lpszName);
				moduleName = duster_malloc(nNameLen+1);
				ASSERT(moduleName);
				memset(moduleName,	0,	nNameLen+1);
				memcpy(moduleName,	pChlElement->lpszName, nNameLen);
				dca.dc_root = pChlElement;
			}else
				continue;
			duster_log("%s , module_name pChlElement->lpszname is %s ",__FUNCTION__,	moduleName);
			if(duster_action == DUSTER_PARSE_ACTION_SET && !(dostuff & DUSTER_PARSER_FL_DEFAULTS))
			{
				duster_critic("Enter duster_call_module_pre_set_handler:%s",moduleName);
				duster_call_module_pre_set_handler(moduleName, DUSTER_CONFIG_SET, &dca);
			}
			if (duster_action == DUSTER_PARSE_ACTION_GET)
			{
				duster_critic("Enter duster_call_module_pre_get_handler:%s",moduleName);
				duster_call_module_pre_get_handler(moduleName, DUSTER_CONFIG_GET, &dca);
			}
			set_need_ascii_to_html();
			if(!PlatformIsMinSystem())
			{
				mrvxml_Travel(pChlElement,method,moduleName,NULL);
			}
			clear_need_ascii_to_html();

			if (dirty && !(dostuff & DUSTER_PARSER_FL_DEFAULTS))
			{
				/* Call the module specific post_set handler */
				duster_critic("before call module post_set_handler");
				duster_call_module_post_set_handler(moduleName, DUSTER_CONFIG_SET, &dca);
				if(strcmp(moduleName,	PSM_MOD_SMS) && strcmp(moduleName,	PSM_MOD_PB))
				{
					psm_commit__();
				}
				dirty = 0;
			}

			if (dirty && (dostuff & DUSTER_PARSER_FL_DEFAULTS))
			{
				//psm_commit__();
				dirty = 0;
			}

			if (duster_action == DUSTER_PARSE_ACTION_GET)
			{
				duster_critic("before call module post_get_handler %s",__FUNCTION__,moduleName);
				if(duster_call_module_post_get_handler(moduleName, DUSTER_CONFIG_GET, &dca) == -1)
				{
					duster_critic("call post get handler, get fail status");
				}
			}
		}
		if(moduleName)
			{
			  duster_log("%s ,moduleName is %s",__FUNCTION__,moduleName);
			  duster_free(moduleName);
			}
		moduleName = NULL;
		if(subspaceName)
			{
			  duster_log("%s ,subspaceName is %s",__FUNCTION__,subspaceName);
			  duster_free(subspaceName);
			}
		subspaceName = NULL;
	}

	if (duster_action == DUSTER_PARSE_ACTION_GET && dostuff != DUSTER_PARSER_FL_PRINT)
	{
		save_size = MrvCreateXMLStringR(curRoot, 0, -1);
		CPUartLogPrintf("%s save_size is %d",	__FUNCTION__, save_size);
		if(save_size <= MAX_XML_FILE_SIZE)
		{
			memset(buffer_save,	0,	MAX_XML_FILE_SIZE + 1);
			creat_size = MrvCreateXMLStringR(curRoot, buffer_save, -1);
			CPUartLogPrintf("%s save_size is %d, creat_size is %d",	__FUNCTION__, save_size,	creat_size);
		}
		else
			malloc_buffer_save = MrvCreateXMLString(curRoot,0,&save_size);

		fid1 = Rdisk_fopen(fileres);
		if (fid1 == NULL)
		{
			CPUartLogPrintf("Couldn't create the tmp file");
			if(malloc_buffer_save)
				duster_free(malloc_buffer_save);
			malloc_buffer_save = NULL;
			MrvDeleteRoot(root);
			return  -1;
		}
		duster_log("Write XML date to the tmp file, size is:%d", save_size);
		if(!malloc_buffer_save)
			written = Rdisk_fwrite(buffer_save, sizeof(char), save_size, fid1);
		else
			written = Rdisk_fwrite(malloc_buffer_save, sizeof(char), save_size, fid1);
		duster_log("Write XML date to the tmp file, actual write size is:%d", written);
		assert(written == save_size);
		Rdisk_fclose(fid1);
		duster_log("%s before free buffer_save",__FUNCTION__);
		if(malloc_buffer_save)
			duster_free(malloc_buffer_save);
		malloc_buffer_save = NULL;
		duster_log("%s,after free buffer_save",__FUNCTION__);
	}
	duster_log("%s,before delete root",__FUNCTION__);
	if(root)
		MrvDeleteRoot(root);
	duster_critic("%s:Leave",__FUNCTION__);
	return 0;

}


int  duster_parser_nucleus(char *method, char *filename, char *fileres)
{
	duster_log("Enter duster parser,	method is %s",	method);
	if(method)
	{
		if(!strcmp(method, "get"))
		{
			duster_action = DUSTER_PARSE_ACTION_GET;
			dostuff &= ~DUSTER_PARSER_FL_DEFAULTS;
		}
		else if(!strcmp(method,"restore"))
		{
			duster_action = DUSTER_PARSE_ACTION_SET;
			dostuff |= DUSTER_PARSER_FL_RESTORE;
		}
		else if(!strcmp(method,"set"))
		{
			duster_action = DUSTER_PARSE_ACTION_SET;
			dostuff &= ~DUSTER_PARSER_FL_DEFAULTS;
		}
		else if(!strcmp(method,"default"))
		{
			duster_action = DUSTER_PARSE_ACTION_SET;
			dostuff |= DUSTER_PARSER_FL_DEFAULTS;
		}
	}

	if(filename)
	{
		duster_log("xmlname:%s,method:%s\n", filename, method);
	}
	else
		duster_log("xmlname is missing\n");

	return take_action_nucleus_mrvxml(filename, fileres, method);


}

/*
int mcbtest(char *filename)
{
	FILE_ID fid;
	int read_size;
	MrvXMLElement *root	=	NULL;
	MrvXMLResults *result = NULL;
	int nrow = 0;


  	duster_log("enter %s",__FUNCTION__);

	 fid = Rdisk_fopen(filename);//open  temp xml file, if set the file is post from client, if get the file is in /www/data/*
	    if (fid == NULL)
		{
			CPUartLogPrintf("Couldn't read file");
			goto EXIT;
	    }

	    read_size = Rdisk_fread(buffer_load, sizeof(char), XML_FILE_SIZE, fid);
	    Rdisk_fclose(fid);
		if(read_size <= XML_FILE_SIZE)
	    	buffer_load[read_size] = '\0';
		else
		{
	   		 CPUartLogPrintf("Wrong size of XML file");
	    	return -1;
		}
		duster_log("%s here 1",__FUNCTION__);
		result = (MrvXMLResults*)duster_malloc(sizeof(MrvXMLResults));
		ASSERT(result != NULL);
		memset(result,	0,	sizeof(MrvXMLResults));

		root = MrvParseXML(buffer_load,	result);
		ASSERT(root !=	NULL);

		duster_log("%s,result->nLine is %d,result->nColumn,root->nSize is %d",__FUNCTION__,result->nLine,result->nColumn,root->nSize);

		#ifdef 0
		for (;nIndex < root->nSize && !pResult; nIndex++)
		{
			duster_log("%s	root->pEntries[nIndex].type is %d",	__FUNCTION__,root->pEntries[nIndex].type);
			switch(root->pEntries[nIndex].type)
			{
				case eNodeElement:
					pResult = root->pEntries[nIndex].node.pElement;
					duster_log("%s Element type	root->pEntries[nIndex].lpszName is %s,nsize is %d",__FUNCTION__,pResult->lpszName,pResult->nSize);
				break;
				case eNodeAttribute:
					duster_log("%s	eNodeAttribute	name is %s,value is %s",__FUNCTION__,(root->pEntries[nIndex].node.pAttrib)->lpszName,(root->pEntries[nIndex].node.pAttrib)->lpszValue);
					break;
				case eNodeText:
					duster_log("%s	eNodeText,value is %s",__FUNCTION__,(root->pEntries[nIndex].node.pText)->lpszValue);
					break;
				case eNodeClear:
					duster_log("%s	eNodeClear,",__FUNCTION__,(root->pEntries[nIndex].node.pClear)->lpszOpenTag,(root->pEntries[nIndex].node.pClear)->lpszValue,(root->pEntries[nIndex].node.pClear)->lpszCloseTag);
					break;
				case eNodeEmpty:
					duster_log("%s	eNodeEmpty",__FUNCTION__);

			}
		}
		#endif

		MrvEnumPrintf(root,&nrow);

EXIT:

	duster_log("leave %s",__FUNCTION__);
}
*/
