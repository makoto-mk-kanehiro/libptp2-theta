/* ptp.c
 *
 * Copyright (C) 2001-2005 Mariusz Woloszyn <emsi@ipartners.pl>
 *
 *  This file is part of libptp2.
 *
 *  libptp2 is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  libptp2 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libptp2; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include "ptp.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define N_(String) (String)
#endif

#define CHECK_PTP_RC(result)	{uint16_t r=(result); if (r!=PTP_RC_OK) return r;}

#define PTP_CNT_INIT(cnt) {memset(&cnt,0,sizeof(cnt));}

static void
ptp_debug (PTPParams *params, const char *format, ...)
{  
        va_list args;

        va_start (args, format);
        if (params->debug_func!=NULL)
                params->debug_func (params->data, format, args);
        else
	{
                vfprintf (stderr, format, args);
		fprintf (stderr,"\n");
		fflush (stderr);
	}
        va_end (args);
}  

static void
ptp_error (PTPParams *params, const char *format, ...)
{  
        va_list args;

        va_start (args, format);
        if (params->error_func!=NULL)
                params->error_func (params->data, format, args);
        else
	{
                vfprintf (stderr, format, args);
		fprintf (stderr,"\n");
		fflush (stderr);
	}
        va_end (args);
}

/* Pack / unpack functions */

#include "ptp-pack.c"

/* send / receive functions */

uint16_t
ptp_usb_sendreq (PTPParams* params, PTPContainer* req)
{
	static uint16_t ret;
	static PTPUSBBulkContainer usbreq;

	PTP_CNT_INIT(usbreq);
	/* build appropriate USB container */
	usbreq.length=htod32(PTP_USB_BULK_REQ_LEN-
		(sizeof(uint32_t)*(5-req->Nparam)));
	usbreq.type=htod16(PTP_USB_CONTAINER_COMMAND);
	usbreq.code=htod16(req->Code);
	usbreq.trans_id=htod32(req->Transaction_ID);
	usbreq.payload.params.param1=htod32(req->Param1);
	usbreq.payload.params.param2=htod32(req->Param2);
	usbreq.payload.params.param3=htod32(req->Param3);
	usbreq.payload.params.param4=htod32(req->Param4);
	usbreq.payload.params.param5=htod32(req->Param5);
	/* send it to responder */
	ret=params->write_func((unsigned char *)&usbreq,
		PTP_USB_BULK_REQ_LEN-(sizeof(uint32_t)*(5-req->Nparam)),
		params->data);
	if (ret!=PTP_RC_OK) {
		ret = PTP_ERROR_IO;
/*		ptp_error (params,
			"PTP: request code 0x%04x sending req error 0x%04x",
			req->Code,ret); */
	}
	return ret;
}

uint16_t
ptp_usb_senddata (PTPParams* params, PTPContainer* ptp,
			unsigned char *data, unsigned int size)
{
	static uint16_t ret;
	size_t ptp_bulk_payload_len;
	static PTPUSBBulkContainer usbdata;

	ptp_bulk_payload_len = params->pktlen_bulk - PTP_USB_BULK_HDR_LEN;
	/* build appropriate USB container */
	usbdata.length=htod32(PTP_USB_BULK_HDR_LEN+size);
	usbdata.type=htod16(PTP_USB_CONTAINER_DATA);
	usbdata.code=htod16(ptp->Code);
	usbdata.trans_id=htod32(ptp->Transaction_ID);
	memcpy(usbdata.payload.data,data,
		(size<ptp_bulk_payload_len)?size:ptp_bulk_payload_len);
	/* send first part of data */
	ret=params->write_func((unsigned char *)&usbdata, PTP_USB_BULK_HDR_LEN+
		((size<ptp_bulk_payload_len)?size:ptp_bulk_payload_len),
		params->data);
	if (ret!=PTP_RC_OK) {
		ret = PTP_ERROR_IO;
/*		ptp_error (params,
		"PTP: request code 0x%04x sending data error 0x%04x",
			ptp->Code,ret);*/
		return ret;
	}
	if (size<=ptp_bulk_payload_len) return ret;
	/* if everything OK send the rest */
	ret=params->write_func (data+ptp_bulk_payload_len,
				size-ptp_bulk_payload_len, params->data);
	if (ret!=PTP_RC_OK) {
		ret = PTP_ERROR_IO;
/*		ptp_error (params,
		"PTP: request code 0x%04x sending data error 0x%04x",
			ptp->Code,ret); */
	}
	return ret;
}

uint16_t
ptp_usb_getdata (PTPParams* params, PTPContainer* ptp,  uint64_t *getlen, 
		unsigned char **data)
{
	static uint16_t ret;
	size_t ptp_bulk_payload_len;
	static PTPUSBBulkContainer usbdata;

	PTP_CNT_INIT(usbdata);

	ptp_bulk_payload_len = params->pktlen_bulk - PTP_USB_BULK_HDR_LEN;
#if 0
	if (*data!=NULL) return PTP_ERROR_BADPARAM;
#endif
	do {
		/* read first(?) part of data */
		ret=params->read_func((unsigned char *)&usbdata,
				params->pktlen_bulk, params->data);
		if (ret!=PTP_RC_OK) {
			ret = PTP_ERROR_IO;
			break;
		} else
		if (dtoh16(usbdata.type)!=PTP_USB_CONTAINER_DATA
			&& dtoh16(usbdata.type)!=PTP_USB_CONTAINER_RESPONSE) {
			ret = PTP_ERROR_DATA_EXPECTED;
			break;
		} else
		if (dtoh16(usbdata.code)!=ptp->Code) {
			ret = dtoh16(usbdata.code);
			break;
		}
		/* evaluate data length */
		if (dtoh32(usbdata.length) == ~0)
			*getlen = ptp->length;
		else
			*getlen=dtoh32(usbdata.length)- PTP_USB_BULK_HDR_LEN;

		/* allocate memory for data if not allocated already */
		if (*data==NULL) *data=calloc(1,*getlen);
		/* copy first part of data to 'data' */
		memcpy(*data,usbdata.payload.data,
			ptp_bulk_payload_len<*getlen?
			ptp_bulk_payload_len:*getlen);
		/* is that all of data? */
		if (*getlen+PTP_USB_BULK_HDR_LEN<= params->pktlen_bulk) break;
		/* if not finaly read the rest of it */
		ret=params->read_func(((unsigned char *)(*data))+
					ptp_bulk_payload_len,
					*getlen-ptp_bulk_payload_len,
					params->data);
		if (ret!=PTP_RC_OK) {
			ret = PTP_ERROR_IO;
			break;
		}
	} while (0);
/*
	if (ret!=PTP_RC_OK) {
		ptp_error (params,
		"PTP: request code 0x%04x getting data error 0x%04x",
			ptp->Code, ret);
	}*/
	return ret;
}

uint16_t
ptp_usb_getresp (PTPParams* params, PTPContainer* resp)
{
	static uint16_t ret;
	static PTPUSBBulkContainer usbresp;

	PTP_CNT_INIT(usbresp);
	/* read response, it should never be longer than sizeof(usbresp) */
	ret=params->read_func((unsigned char *)&usbresp,
				sizeof(usbresp), params->data);

	if (ret!=PTP_RC_OK) {
		ret = PTP_ERROR_IO;
	} else
	if (dtoh16(usbresp.type)!=PTP_USB_CONTAINER_RESPONSE) {
		ret = PTP_ERROR_RESP_EXPECTED;
	} else
	if (dtoh16(usbresp.code)!=resp->Code) {
		ret = dtoh16(usbresp.code);
	}
	if (ret!=PTP_RC_OK) {
/*		ptp_error (params,
		"PTP: request code 0x%04x getting resp error 0x%04x",
			resp->Code, ret);*/
		return ret;
	}
	/* build an appropriate PTPContainer */
	resp->Code=dtoh16(usbresp.code);
	resp->SessionID=params->session_id;
	resp->Transaction_ID=dtoh32(usbresp.trans_id);
	resp->Param1=dtoh32(usbresp.payload.params.param1);
	resp->Param2=dtoh32(usbresp.payload.params.param2);
	resp->Param3=dtoh32(usbresp.payload.params.param3);
	resp->Param4=dtoh32(usbresp.payload.params.param4);
	resp->Param5=dtoh32(usbresp.payload.params.param5);
	return ret;
}

/* major PTP functions */

/* Number of PTP Request phase parameters */
#define PTP_RQ_PARAM0		0x0000	/* zero parameters */
#define PTP_RQ_PARAM1		0x0100	/* one parameter */
#define PTP_RQ_PARAM2		0x0200	/* two parameters */
#define PTP_RQ_PARAM3		0x0300	/* three parameters */
#define PTP_RQ_PARAM4		0x0400	/* four parameters */
#define PTP_RQ_PARAM5		0x0500	/* five parameters */

/**
 * ptp_transaction:
 * params:	PTPParams*
 * 		PTPContainer* ptp	- general ptp container
 * 		uint16_t flags		- lower 8 bits - data phase description
 * 		unsigned int sendlen	- senddata phase data length
 * 		char** data		- send or receive data buffer pointer
 *
 * Performs PTP transaction. ptp is a PTPContainer with appropriate fields
 * filled in (i.e. operation code and parameters). It's up to caller to do
 * so.
 * The flags decide whether the transaction has a data phase and what is its
 * direction (send or receive). 
 * If transaction is sending data the sendlen should contain its length in
 * bytes, otherwise it's ignored.
 * The data should contain an address of a pointer to data going to be sent
 * or is filled with such a pointer address if data are received depending
 * od dataphase direction (send or received) or is beeing ignored (no
 * dataphase).
 * The memory for a pointer should be preserved by the caller, if data are
 * beeing retreived the appropriate amount of memory is beeing allocated
 * (the caller should handle that!).
 *
 * Return values: Some PTP_RC_* code.
 * Upon success PTPContainer* ptp contains PTP Response Phase container with
 * all fields filled in.
 **/
 uint16_t
ptp_transaction (PTPParams* params, PTPContainer* ptp, 
			uint16_t flags, unsigned int sendlen, char** data)
{
	if ((params==NULL) || (ptp==NULL)) 
		return PTP_ERROR_BADPARAM;
	
	ptp->Transaction_ID=params->transaction_id++;
	ptp->SessionID=params->session_id;
	/* send request */
	CHECK_PTP_RC(params->sendreq_func (params, ptp));
	/* is there a dataphase? */
	switch (flags&PTP_DP_DATA_MASK) {
		case PTP_DP_SENDDATA:
			CHECK_PTP_RC(params->senddata_func(params, ptp,
				(unsigned char*)*data, sendlen));
			break;
		case PTP_DP_GETDATA:
			{
			uint64_t getlen;
			CHECK_PTP_RC(params->getdata_func(params, ptp,
				    sendlen?(unsigned int *)(((uint64_t)(&getlen)&0xffffffff00000000)|sendlen):
				    &getlen,
				(unsigned char**)data));
			}
			break;
		case PTP_DP_NODATA:
			break;
		default:
		return PTP_ERROR_BADPARAM;
	}
	/* get response */
	CHECK_PTP_RC(params->getresp_func(params, ptp));
	return PTP_RC_OK;
}

/* Enets handling functions */

/* PTP Events wait for or check mode */
#define PTP_EVENT_CHECK			0x0000	/* waits for */
#define PTP_EVENT_CHECK_FAST		0x0001	/* checks */

#define CHECK_INT(usbevent, size)  {	\
	    switch(wait) {		\
		case PTP_EVENT_CHECK:	\
		     result+=params->check_int_func((unsigned char*)&usbevent+result, \
			    size-result, params->data);    \
		    break;			    \
		case PTP_EVENT_CHECK_FAST:	\
		     result+=params->check_int_fast_func((unsigned char*)&usbevent+result,  \
			    size-result, params->data);    \
		    break;			    \
		default:			    \
		    return PTP_ERROR_BADPARAM;	    \
	    }\
	}
					

static inline uint16_t
ptp_usb_event (PTPParams* params, PTPContainer* event, int wait)
{
	int result=0, size=0;
	static PTPUSBEventContainer usbevent;

	PTP_CNT_INIT(usbevent);

	if ((params==NULL) || (event==NULL)) 
		return PTP_ERROR_BADPARAM;


	CHECK_INT(usbevent, params->pktlen_intr);
	if (result<0)
	    return PTP_ERROR_IO;
	size=dtoh32(usbevent.length);
	while (result<size) {
	    CHECK_INT(usbevent, size);
	    if (result<0)
		return PTP_ERROR_IO;
	}


	/* if we read anything over interrupt endpoint it must be an event */
	/* build an appropriate PTPContainer */
	event->Code=dtoh16(usbevent.code);
	event->SessionID=params->session_id;
	event->Transaction_ID=dtoh32(usbevent.trans_id);
	event->Param1=dtoh32(usbevent.param1);
	event->Param2=dtoh32(usbevent.param2);
	event->Param3=dtoh32(usbevent.param3);

	return PTP_RC_OK;
}

uint16_t
ptp_usb_event_check (PTPParams* params, PTPContainer* event) {

	ptp_debug(params,"PTP: Checking for Event");
	return ptp_usb_event (params, event, PTP_EVENT_CHECK_FAST);
}

uint16_t
ptp_usb_event_wait (PTPParams* params, PTPContainer* event) {

	ptp_debug(params,"PTP: Waiting for Event");
	return ptp_usb_event (params, event, PTP_EVENT_CHECK);
}

/**
 * PTP operation functions
 *
 * all ptp_ functions should take integer parameters
 * in host byte order!
 **/


/**
 * ptp_getdeviceinfo:
 * params:	PTPParams*
 *
 * Gets device info dataset and fills deviceinfo structure.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_getdeviceinfo (PTPParams* params, PTPDeviceInfo* deviceinfo)
{
	uint16_t ret;
	PTPContainer ptp;
	char* di=NULL;

	ptp_debug(params,"PTP: Obtaining DeviceInfo");

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetDeviceInfo;
	ptp.Nparam=0;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &di);
	if (ret == PTP_RC_OK) ptp_unpack_DI(params, di, deviceinfo);
	free(di);
	return ret;
}


/**
 * ptp_opensession:
 * params:	PTPParams*
 * 		session			- session number 
 *
 * Establishes a new session.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_opensession (PTPParams* params, uint32_t session)
{
	uint16_t ret;
	PTPContainer ptp;

	ptp_debug(params,"PTP: Opening session 0x%08x", session);

	/* SessonID field of the operation dataset should always
	   be set to 0 for OpenSession request! */
	params->session_id=0x00000000;
	/* TransactionID should be set to 0 also! */
	params->transaction_id=0x0000000;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_OpenSession;
	ptp.Param1=session;
	ptp.Nparam=1;
	ret=ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
	/* now set the global session id to current session number */
	params->session_id=session;
	return ret;
}

/**
 * ptp_closesession:
 * params:	PTPParams*
 *
 * Closes session.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_closesession (PTPParams* params)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Closing session");

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CloseSession;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_getststorageids:
 * params:	PTPParams*
 *
 * Gets array of StorageiDs and fills the storageids structure.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_getstorageids (PTPParams* params, PTPStorageIDs* storageids)
{
	uint16_t ret;
	PTPContainer ptp;
	char* sids=NULL;

	ptp_debug(params,"PTP: Obtaining StorageIDs");

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetStorageIDs;
	ptp.Nparam=0;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &sids);
	if (ret == PTP_RC_OK) ptp_unpack_SIDs(params, sids, storageids);
	free(sids);
	return ret;
}

/**
 * ptp_getststorageinfo:
 * params:	PTPParams*
 *		storageid		- StorageID
 *
 * Gets StorageInfo dataset of desired storage and fills storageinfo
 * structure.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_getstorageinfo (PTPParams* params, uint32_t storageid,
			PTPStorageInfo* storageinfo)
{
	uint16_t ret;
	PTPContainer ptp;
	char* si=NULL;

	ptp_debug(params,"PTP: Obtaining StorageInfo for storage 0x%08x",
		storageid);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetStorageInfo;
	ptp.Param1=storageid;
	ptp.Nparam=1;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &si);
	if (ret == PTP_RC_OK) ptp_unpack_SI(params, si, storageinfo);
	free(si);
	return ret;
}

/**
 * ptp_getobjecthandles:
 * params:	PTPParams*
 *		storage			- StorageID
 *		objectformatcode	- ObjectFormatCode (optional)
 *		associationOH		- ObjectHandle of Association for
 *					  wich a list of children is desired
 *					  (optional)
 *		objecthandles		- pointer to structute
 *
 * Fills objecthandles with structure returned by device.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_getobjecthandles (PTPParams* params, uint32_t storage,
			uint32_t objectformatcode, uint32_t associationOH,
			PTPObjectHandles* objecthandles)
{
	uint16_t ret;
	PTPContainer ptp;
	char* oh=NULL;

	ptp_debug(params,"PTP: Obtaining ObjectHandles");

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetObjectHandles;
	ptp.Param1=storage;
	ptp.Param2=objectformatcode;
	ptp.Param3=associationOH;
	ptp.Nparam=3;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &oh);
	if (ret == PTP_RC_OK) ptp_unpack_OH(params, oh, objecthandles);
	free(oh);
	return ret;
}

/**
 * ptp_ptp_getobjectinfo:
 * params:	PTPParams*
 *		handle			- object Handler
 *		objectinfo		- pointer to PTPObjectInfo structure
 *
 * Fills objectinfo structure with appropriate data of object given by
 * hander.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_getobjectinfo (PTPParams* params, uint32_t handle,
			PTPObjectInfo* objectinfo)
{
	uint16_t ret;
	PTPContainer ptp;
	char* oi=NULL;

	ptp_debug(params,"PTP: Obtaining ObjectInfo for object 0x%08x",
		handle);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetObjectInfo;
	ptp.Param1=handle;
	ptp.Nparam=1;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &oi);
	if (ret == PTP_RC_OK) ptp_unpack_OI(params, oi, objectinfo);
	free(oi);
	return ret;
}

uint16_t
ptp_getobject (PTPParams* params, uint32_t handle, char** object, uint64_t sz)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Downloading Object 0x%08x",
		handle);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetObject;
	ptp.Param1=handle;
	ptp.Nparam=1;
	ptp.length = sz;
	return ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, object);
}

uint16_t
ptp_getthumb (PTPParams* params, uint32_t handle,  char** object)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Downloading Thumbnail from object 0x%08x",
		handle);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetThumb;
	ptp.Param1=handle;
	ptp.Nparam=1;
	return ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, object);
}

/**
 * ptp_deleteobject:
 * params:	PTPParams*
 *		handle			- object handle
 *		ofc			- object format code (optional)
 * 
 * Deletes desired objects.
 *
 * Return values: Some PTP_RC_* code.
 **/
uint16_t
ptp_deleteobject (PTPParams* params, uint32_t handle,
			uint32_t ofc)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Deleting Object 0x%08x", handle);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_DeleteObject;
	ptp.Param1=handle;
	ptp.Param2=ofc;
	ptp.Nparam=2;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_sendobjectinfo:
 * params:	PTPParams*
 *		uint32_t* store		- destination StorageID on Responder
 *		uint32_t* parenthandle 	- Parent ObjectHandle on responder
 * 		uint32_t* handle	- see Return values
 *		PTPObjectInfo* objectinfo- ObjectInfo that is to be sent
 * 
 * Sends ObjectInfo of file that is to be sent via SendFileObject.
 *
 * Return values: Some PTP_RC_* code.
 * Upon success : uint32_t* store	- Responder StorageID in which
 *					  object will be stored
 *		  uint32_t* parenthandle- Responder Parent ObjectHandle
 *					  in which the object will be stored
 *		  uint32_t* handle	- Responder's reserved ObjectHandle
 *					  for the incoming object
 **/
uint16_t
ptp_sendobjectinfo (PTPParams* params, uint32_t* store, 
			uint32_t* parenthandle, uint32_t* handle,
			PTPObjectInfo* objectinfo)
{
	uint16_t ret;
	PTPContainer ptp;
	char* oidata=NULL;
	uint32_t size;

	ptp_debug(params,"PTP: Sending ObjectInfo; parent object 0x%08x", 
		*parenthandle);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_SendObjectInfo;
	ptp.Param1=*store;
	ptp.Param2=*parenthandle;
	ptp.Nparam=2;
	
	size=ptp_pack_OI(params, objectinfo, &oidata);
	ret=ptp_transaction(params, &ptp, PTP_DP_SENDDATA, size, &oidata); 
	free(oidata);
	*store=ptp.Param1;
	*parenthandle=ptp.Param2;
	*handle=ptp.Param3; 
	return ret;
}

/**
 * ptp_sendobject:
 * params:	PTPParams*
 *		char*	object		- contains the object that is to be sent
 *		uint32_t size		- object size
 *		
 * Sends object to Responder.
 *
 * Return values: Some PTP_RC_* code.
 *
 */
uint16_t
ptp_sendobject (PTPParams* params, char* object, uint32_t size)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Sending Object of size %u", size);

	PTP_CNT_INIT(ptp);

	ptp.Code=PTP_OC_SendObject;
	ptp.Nparam=0;

	return ptp_transaction(params, &ptp, PTP_DP_SENDDATA, size, &object);
}


/**
 * ptp_initiatecapture:
 * params:	PTPParams*
 *		storageid		- destination StorageID on Responder
 *		ofc			- object format code
 * 
 * Causes device to initiate the capture of one or more new data objects
 * according to its current device properties, storing the data into store
 * indicated by storageid. If storageid is 0x00000000, the object(s) will
 * be stored in a store that is determined by the capturing device.
 * The capturing of new data objects is an asynchronous operation.
 *
 * Return values: Some PTP_RC_* code.
 **/

uint16_t
ptp_initiatecapture (PTPParams* params, uint32_t storageid,
			uint32_t ofc)
{
	PTPContainer ptp;

	ptp_debug(params,"PTP: Initiating Capture");

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_InitiateCapture;
	ptp.Param1=storageid;
	ptp.Param2=ofc;
	ptp.Nparam=2;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

uint16_t
ptp_getdevicepropdesc (PTPParams* params, uint16_t propcode, 
			PTPDevicePropDesc* devicepropertydesc)
{
	PTPContainer ptp;
	uint16_t ret;
	char* dpd=NULL;

	ptp_debug(params, "PTP: Obtaining Device Property Description for property 0x%04x", propcode);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetDevicePropDesc;
	ptp.Param1=propcode;
	ptp.Nparam=1;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &dpd);
	if (ret == PTP_RC_OK) ptp_unpack_DPD(params, dpd, devicepropertydesc);
	free(dpd);
	return ret;
}


uint16_t
ptp_getdevicepropvalue (PTPParams* params, uint16_t propcode,
			void** value, uint16_t datatype)
{
	PTPContainer ptp;
	uint16_t ret;
	char* dpv=NULL;

	ptp_debug(params, "PTP: Obtaining Device Property Value for property 0x%04x", propcode);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_GetDevicePropValue;
	ptp.Param1=propcode;
	ptp.Nparam=1;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &dpv);
	if (ret == PTP_RC_OK) ptp_unpack_DPV(params, dpv, value, datatype);
	free(dpv);
	return ret;
}

uint16_t
ptp_setdevicepropvalue (PTPParams* params, uint16_t propcode,
			void* value, uint16_t datatype)
{
	PTPContainer ptp;
	uint16_t ret;
	uint32_t size;
	char* dpv=NULL;

	ptp_debug(params, "PTP: Setting Device Property Value for property 0x%04x", propcode);

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_SetDevicePropValue;
	ptp.Param1=propcode;
	ptp.Nparam=1;
	size=ptp_pack_DPV(params, value, &dpv, datatype);
	ret=ptp_transaction(params, &ptp, PTP_DP_SENDDATA, size, &dpv);
	free(dpv);
	return ret;
}

uint16_t
ptp_sendgenericrequest (PTPParams* params, uint16_t reqcode,
						uint32_t* reqparams, char** data, uint32_t direction, long sendlen)
{
	PTPContainer ptp;
	uint16_t ret=0;
	char *dpv=NULL;
	
	if (direction == PTP_DP_GETDATA)
		*data = NULL;

	ptp_debug(params, "PTP: Sending generic Request 0x%04x", reqcode);

	PTP_CNT_INIT(ptp);
	ptp.Code=reqcode;
	ptp.Nparam=0;
	// carfull here, do not only rely on the nullity of a value,
	// because the next parameter might be not null
	if((ptp.Param1=reqparams[0]) != 0)
		ptp.Nparam = 1;
	if((ptp.Param2=reqparams[1]) != 0)
		ptp.Nparam = 2;
	if((ptp.Param3=reqparams[2]) != 0)
		ptp.Nparam = 3;
	if((ptp.Param4=reqparams[3]) != 0)
		ptp.Nparam = 4;
	if((ptp.Param5=reqparams[4]) != 0)
		ptp.Nparam = 5;

	ret=ptp_transaction(params, &ptp, direction, sendlen, data);
	return ret;
}

uint16_t
ptp_getobjectsizefromproperty(PTPParams *params, uint32_t handle, uint64_t *sz)
{
	uint16_t ret;
	PTPContainer ptp;

	PTP_CNT_INIT(ptp);
	ptp.Code = PTP_OC_MTP_GetObjectPropValue;
	ptp.Param1 = handle;
	ptp.Param2 = PTP_OPC_OBJECTSIZE;
	ptp.Nparam = 2;
	ret = ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &sz);

	return ret;
}

/**
 * ptp_ek_sendfileobjectinfo:
 * params:	PTPParams*
 *		uint32_t* store		- destination StorageID on Responder
 *		uint32_t* parenthandle 	- Parent ObjectHandle on responder
 * 		uint32_t* handle	- see Return values
 *		PTPObjectInfo* objectinfo- ObjectInfo that is to be sent
 * 
 * Sends ObjectInfo of file that is to be sent via SendFileObject.
 *
 * Return values: Some PTP_RC_* code.
 * Upon success : uint32_t* store	- Responder StorageID in which
 *					  object will be stored
 *		  uint32_t* parenthandle- Responder Parent ObjectHandle
 *					  in which the object will be stored
 *		  uint32_t* handle	- Responder's reserved ObjectHandle
 *					  for the incoming object
 **/
uint16_t
ptp_ek_sendfileobjectinfo (PTPParams* params, uint32_t* store, 
			uint32_t* parenthandle, uint32_t* handle,
			PTPObjectInfo* objectinfo)
{
	uint16_t ret;
	PTPContainer ptp;
	char* oidata=NULL;
	uint32_t size;

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_EK_SendFileObjectInfo;
	ptp.Param1=*store;
	ptp.Param2=*parenthandle;
	ptp.Nparam=2;
	
	size=ptp_pack_OI(params, objectinfo, &oidata);
	ret=ptp_transaction(params, &ptp, PTP_DP_SENDDATA, size, &oidata); 
	free(oidata);
	*store=ptp.Param1;
	*parenthandle=ptp.Param2;
	*handle=ptp.Param3; 
	return ret;
}

/**
 * ptp_ek_sendfileobject:
 * params:	PTPParams*
 *		char*	object		- contains the object that is to be sent
 *		uint32_t size		- object size
 *		
 * Sends object to Responder.
 *
 * Return values: Some PTP_RC_* code.
 *
 */
uint16_t
ptp_ek_sendfileobject (PTPParams* params, char* object, uint32_t size)
{
	PTPContainer ptp;

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_EK_SendFileObject;
	ptp.Nparam=0;

	return ptp_transaction(params, &ptp, PTP_DP_SENDDATA, size, &object);
}


/*************************************************************************
 * RICOH THETA extension support
 * 
 * **********************************************************************/

uint16_t
ptp_theta_sleepmode (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_THETA_SleepMode;
	ptp.Nparam=1;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/*************************************************************************
 *
 * Canon PTP extensions support
 *
 * (C) Nikolai Kopanygin 2003
 *
 *************************************************************************/


/**
 * ptp_canon_getobjectsize:
 * params:	PTPParams*
 *		uint32_t handle		- ObjectHandle
 *		uint32_t p2 		- Yet unknown parameter,
 *					  value 0 works.
 * 
 * Gets form the responder the size of the specified object.
 *
 * Return values: Some PTP_RC_* code.
 * Upon success : uint32_t* size	- The object size
 *		  uint32_t  rp2		- Yet unknown parameter
 *
 **/
uint16_t
ptp_canon_getobjectsize (PTPParams* params, uint32_t handle, uint32_t p2, 
			uint32_t* size, uint32_t* rp2) 
{
	uint16_t ret;
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_GetObjectSize;
	ptp.Param1=handle;
	ptp.Param2=p2;
	ptp.Nparam=2;
	ret=ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
	*size=ptp.Param1;
	*rp2=ptp.Param2;
	return ret;
}

/**
 * ptp_canon_startshootingmode:
 * params:	PTPParams*
 * 
 * Starts shooting session. It emits a StorageInfoChanged
 * event via the interrupt pipe and pushes the StorageInfoChanged
 * and CANON_CameraModeChange events onto the event stack
 * (see operation PTP_OC_CANON_CheckEvent).
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_startshootingmode (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_StartShootingMode;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_endshootingmode:
 * params:	PTPParams*
 * 
 * This operation is observed after pressing the Disconnect 
 * button on the Remote Capture app. It emits a StorageInfoChanged 
 * event via the interrupt pipe and pushes the StorageInfoChanged
 * and CANON_CameraModeChange events onto the event stack
 * (see operation PTP_OC_CANON_CheckEvent).
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_endshootingmode (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_EndShootingMode;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_viewfinderon:
 * params:	PTPParams*
 * 
 * Prior to start reading viewfinder images, one  must call this operation.
 * Supposedly, this operation affects the value of the CANON_ViewfinderMode
 * property.
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_viewfinderon (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_ViewfinderOn;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_viewfinderoff:
 * params:	PTPParams*
 * 
 * Before changing the shooting mode, or when one doesn't need to read
 * viewfinder images any more, one must call this operation.
 * Supposedly, this operation affects the value of the CANON_ViewfinderMode
 * property.
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_viewfinderoff (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_ViewfinderOff;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_reflectchanges:
 * params:	PTPParams*
 * 		uint32_t p1 	- Yet unknown parameter,
 * 				  value 7 works
 * 
 * Make viewfinder reflect changes.
 * There is a button for this operation in the Remote Capture app.
 * What it does exactly I don't know. This operation is followed
 * by the CANON_GetChanges(?) operation in the log.
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_reflectchanges (PTPParams* params, uint32_t p1)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_ReflectChanges;
	ptp.Param1=p1;
	ptp.Nparam=1;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}


/**
 * ptp_canon_checkevent:
 * params:	PTPParams*
 * 
 * The camera has a FIFO stack, in which it accumulates events.
 * Partially these events are communicated also via the USB interrupt pipe
 * according to the PTP USB specification, partially not.
 * This operation returns from the device a block of data, empty,
 * if the event stack is empty, or filled with an event's data otherwise.
 * The event is removed from the stack in the latter case.
 * The Remote Capture app sends this command to the camera all the time
 * of connection, filling with it the gaps between other operations. 
 *
 * Return values: Some PTP_RC_* code.
 * Upon success : PTPUSBEventContainer* event	- is filled with the event data
 *						  if any
 *                int *isevent			- returns 1 in case of event
 *						  or 0 otherwise
 **/
uint16_t
ptp_canon_checkevent (PTPParams* params, PTPUSBEventContainer* event, int* isevent)
{
	uint16_t ret;
	PTPContainer ptp;
	char *evdata = NULL;
	
	*isevent=0;
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_CheckEvent;
	ptp.Nparam=0;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &evdata);
	if (evdata!=NULL) {
		if (ret == PTP_RC_OK) {
        		ptp_unpack_EC(params, evdata, event);
    			*isevent=1;
        	}
		free(evdata);
	}
	return ret;
}


/**
 * ptp_canon_focuslock:
 *
 * This operation locks the focus. It is followed by the CANON_GetChanges(?)
 * operation in the log. 
 * It affects the CANON_MacroMode property. 
 *
 * params:	PTPParams*
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_focuslock (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_FocusLock;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_focusunlock:
 *
 * This operation unlocks the focus. It is followed by the CANON_GetChanges(?)
 * operation in the log. 
 * It sets the CANON_MacroMode property value to 1 (where it occurs in the log).
 * 
 * params:	PTPParams*
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_focusunlock (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_FocusUnlock;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_initiatecaptureinmemory:
 * 
 * This operation starts the image capture according to the current camera
 * settings. When the capture has happened, the camera emits a CaptureComplete
 * event via the interrupt pipe and pushes the CANON_RequestObjectTransfer,
 * CANON_DeviceInfoChanged and CaptureComplete events onto the event stack
 * (see operation CANON_CheckEvent). From the CANON_RequestObjectTransfer
 * event's parameter one can learn the just captured image's ObjectHandle.
 * The image is stored in the camera's own RAM.
 * On the next capture the image will be overwritten!
 *
 * params:	PTPParams*
 *
 * Return values: Some PTP_RC_* code.
 *
 **/
uint16_t
ptp_canon_initiatecaptureinmemory (PTPParams* params)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_InitiateCaptureInMemory;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

/**
 * ptp_canon_getpartialobject:
 *
 * This operation is used to read from the device a data 
 * block of an object from a specified offset.
 *
 * params:	PTPParams*
 *      uint32_t handle - the handle of the requested object
 *      uint32_t offset - the offset in bytes from the beginning of the object
 *      uint32_t size - the requested size of data block to read
 *      uint32_t pos - 1 for the first block, 2 - for a block in the middle,
 *                  3 - for the last block
 *
 * Return values: Some PTP_RC_* code.
 *      char **block - the pointer to the block of data read
 *      uint32_t* readnum - the number of bytes read
 *
 **/
uint16_t
ptp_canon_getpartialobject (PTPParams* params, uint32_t handle, 
				uint32_t offset, uint32_t size,
				uint32_t pos, char** block, 
				uint32_t* readnum)
{
	uint16_t ret;
	PTPContainer ptp;
	char *data=NULL;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_GetPartialObject;
	ptp.Param1=handle;
	ptp.Param2=offset;
	ptp.Param3=size;
	ptp.Param4=pos;
	ptp.Nparam=4;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &data);
	if (ret==PTP_RC_OK) {
		*block=data;
		*readnum=ptp.Param1;
	}
	return ret;
}

/**
 * ptp_canon_getviewfinderimage:
 *
 * This operation can be used to read the image which is currently
 * in the camera's viewfinder. The image size is 320x240, format is JPEG.
 * Of course, prior to calling this operation, one must turn the viewfinder
 * on with the CANON_ViewfinderOn command.
 * Invoking this operation many times, one can get live video from the camera!
 * 
 * params:	PTPParams*
 * 
 * Return values: Some PTP_RC_* code.
 *      char **image - the pointer to the read image
 *      unit32_t *size - the size of the image in bytes
 *
 **/
uint16_t
ptp_canon_getviewfinderimage (PTPParams* params, char** image, uint32_t* size)
{
	uint16_t ret;
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_GetViewfinderImage;
	ptp.Nparam=0;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, image);
	if (ret==PTP_RC_OK) *size=ptp.Param1;
	return ret;
}

/**
 * ptp_canon_getchanges:
 *
 * This is an interesting operation, about the effect of which I am not sure.
 * This command is called every time when a device property has been changed 
 * with the SetDevicePropValue operation, and after some other operations.
 * This operation reads the array of Device Properties which have been changed
 * by the previous operation.
 * Probably, this operation is even required to make those changes work.
 *
 * params:	PTPParams*
 * 
 * Return values: Some PTP_RC_* code.
 *      uint16_t** props - the pointer to the array of changed properties
 *      uint32_t* propnum - the number of elements in the *props array
 *
 **/
uint16_t
ptp_canon_getchanges (PTPParams* params, uint16_t** props, uint32_t* propnum)
{
	uint16_t ret;
	PTPContainer ptp;
	char* data=NULL;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_GetChanges;
	ptp.Nparam=0;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &data);
	if (ret == PTP_RC_OK)
        	*propnum=ptp_unpack_uint16_t_array(params,data,0,props);
	free(data);
	return ret;
}

/**
 * ptp_canon_getfolderentries:
 *
 * This command reads a specified object's record in a device's filesystem,
 * or the records of all objects belonging to a specified folder (association).
 *  
 * params:	PTPParams*
 *      uint32_t store - StorageID,
 *      uint32_t p2 - Yet unknown (0 value works OK)
 *      uint32_t parent - Parent Object Handle
 *                      # If Parent Object Handle is 0xffffffff, 
 *                      # the Parent Object is the top level folder.
 *      uint32_t handle - Object Handle
 *                      # If Object Handle is 0, the records of all objects 
 *                      # belonging to the Parent Object are read.
 *                      # If Object Handle is not 0, only the record of this 
 *                      # Object is read.
 *
 * Return values: Some PTP_RC_* code.
 *      PTPCANONFolderEntry** entries - the pointer to the folder entry array
 *      uint32_t* entnum - the number of elements of the array
 *
 **/
uint16_t
ptp_canon_getfolderentries (PTPParams* params, uint32_t store, uint32_t p2, 
			    uint32_t parent, uint32_t handle, 
			    PTPCANONFolderEntry** entries, uint32_t* entnum)
{
	uint16_t ret;
	PTPContainer ptp;
	char *data = NULL;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_CANON_GetFolderEntries;
	ptp.Param1=store;
	ptp.Param2=p2;
	ptp.Param3=parent;
	ptp.Param4=handle;
	ptp.Nparam=4;
	ret=ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &data);
	if (ret == PTP_RC_OK) {
		int i;
		*entnum=ptp.Param1;
		*entries=calloc(*entnum, sizeof(PTPCANONFolderEntry));
		if (*entries!=NULL) {
			for(i=0; i<(*entnum); i++)
				ptp_unpack_Canon_FE(params,
					data+i*PTP_CANON_FolderEntryLen,
					&((*entries)[i]) );
		} else {
			ret=PTP_ERROR_IO; /* Cannot allocate memory */
		}
	}
	free(data);
	return ret;
}


/* Nikon extension code */

uint16_t
ptp_nikon_setcontrolmode (PTPParams* params, uint32_t mode)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_NIKON_SetControlMode;
	ptp.Param1=mode;
	ptp.Nparam=1;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

uint16_t
ptp_nikon_directcapture (PTPParams* params, uint32_t unknown)
{
	PTPContainer ptp;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_NIKON_DirectCapture;
	ptp.Param1=unknown; /* as of yet unknown parameter */
	ptp.Nparam=1;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}

uint16_t
ptp_nikon_checkevent (PTPParams* params, PTPUSBEventContainer** event, uint16_t* evnum)
{
	uint16_t ret;
	PTPContainer ptp;
	char *evdata = NULL;
	
	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_NIKON_CheckEvent;
	ptp.Nparam=0;

	ret = ptp_transaction(params, &ptp, PTP_DP_GETDATA, 0, &evdata);
	if (ret == PTP_RC_OK) ptp_nikon_unpack_EC(params, evdata, event, evnum);
	free (evdata);
	return ret;
}

uint16_t
ptp_nikon_keepalive (PTPParams* params)
{
	
	PTPContainer ptp;

	PTP_CNT_INIT(ptp);
	ptp.Code=PTP_OC_NIKON_KeepAlive;
	ptp.Nparam=0;
	return ptp_transaction(params, &ptp, PTP_DP_NODATA, 0, NULL);
}




/* Non PTP protocol functions */
/* devinfo testing functions */

int
ptp_operation_issupported(PTPParams* params, uint16_t operation)
{
	int i=0;

	for (;i<params->deviceinfo.OperationsSupported_len;i++) {
		if (params->deviceinfo.OperationsSupported[i]==operation)
			return 1;
	}
	return 0;
}


/* ptp structures feeing functions */

void
ptp_free_devicepropdesc(PTPDevicePropDesc* dpd)
{
	uint16_t i;

	free(dpd->FactoryDefaultValue);
	free(dpd->CurrentValue);
	switch (dpd->FormFlag) {
		case PTP_DPFF_Range:
		free (dpd->FORM.Range.MinimumValue);
		free (dpd->FORM.Range.MaximumValue);
		free (dpd->FORM.Range.StepSize);
		break;
		case PTP_DPFF_Enumeration:
		for (i=0;i<dpd->FORM.Enum.NumberOfValues;i++)
			free(dpd->FORM.Enum.SupportedValue[i]);
		free(dpd->FORM.Enum.SupportedValue);
	}
}

/* report PTP errors */

void 
ptp_perror(PTPParams* params, uint16_t error) {

	int i;
	/* PTP error descriptions */
	static struct {
		uint16_t error;
		const char *txt;
	} ptp_errors[] = {
	{PTP_RC_Undefined, 		N_("PTP: Undefined Error")},
	{PTP_RC_OK, 			N_("PTP: OK!")},
	{PTP_RC_GeneralError, 		N_("PTP: General Error")},
	{PTP_RC_SessionNotOpen, 	N_("PTP: Session Not Open")},
	{PTP_RC_InvalidTransactionID, 	N_("PTP: Invalid Transaction ID")},
	{PTP_RC_OperationNotSupported, 	N_("PTP: Operation Not Supported")},
	{PTP_RC_ParameterNotSupported, 	N_("PTP: Parameter Not Supported")},
	{PTP_RC_IncompleteTransfer, 	N_("PTP: Incomplete Transfer")},
	{PTP_RC_InvalidStorageId, 	N_("PTP: Invalid Storage ID")},
	{PTP_RC_InvalidObjectHandle, 	N_("PTP: Invalid Object Handle")},
	{PTP_RC_DevicePropNotSupported, N_("PTP: Device Prop Not Supported")},
	{PTP_RC_InvalidObjectFormatCode, N_("PTP: Invalid Object Format Code")},
	{PTP_RC_StoreFull, 		N_("PTP: Store Full")},
	{PTP_RC_ObjectWriteProtected, 	N_("PTP: Object Write Protected")},
	{PTP_RC_StoreReadOnly, 		N_("PTP: Store Read Only")},
	{PTP_RC_AccessDenied,		N_("PTP: Access Denied")},
	{PTP_RC_NoThumbnailPresent, 	N_("PTP: No Thumbnail Present")},
	{PTP_RC_SelfTestFailed, 	N_("PTP: Self Test Failed")},
	{PTP_RC_PartialDeletion, 	N_("PTP: Partial Deletion")},
	{PTP_RC_StoreNotAvailable, 	N_("PTP: Store Not Available")},
	{PTP_RC_SpecificationByFormatUnsupported,
				N_("PTP: Specification By Format Unsupported")},
	{PTP_RC_NoValidObjectInfo, 	N_("PTP: No Valid Object Info")},
	{PTP_RC_InvalidCodeFormat, 	N_("PTP: Invalid Code Format")},
	{PTP_RC_UnknownVendorCode, 	N_("PTP: Unknown Vendor Code")},
	{PTP_RC_CaptureAlreadyTerminated,
					N_("PTP: Capture Already Terminated")},
	{PTP_RC_DeviceBusy, 		N_("PTP: Device Busy")},
	{PTP_RC_InvalidParentObject, 	N_("PTP: Invalid Parent Object")},
	{PTP_RC_InvalidDevicePropFormat, N_("PTP: Invalid Device Prop Format")},
	{PTP_RC_InvalidDevicePropValue, N_("PTP: Invalid Device Prop Value")},
	{PTP_RC_InvalidParameter, 	N_("PTP: Invalid Parameter")},
	{PTP_RC_SessionAlreadyOpened, 	N_("PTP: Session Already Opened")},
	{PTP_RC_TransactionCanceled, 	N_("PTP: Transaction Canceled")},
	{PTP_RC_SpecificationOfDestinationUnsupported,
			N_("PTP: Specification Of Destination Unsupported")},

	{PTP_ERROR_IO,		  N_("PTP: I/O error")},
	{PTP_ERROR_BADPARAM,	  N_("PTP: Error: bad parameter")},
	{PTP_ERROR_DATA_EXPECTED, N_("PTP: Protocol error: data expected")},
	{PTP_ERROR_RESP_EXPECTED, N_("PTP: Protocol error: response expected")},
	{0, NULL}
	};
	static struct {
		uint16_t error;
		const char *txt;
	} ptp_errors_EK[] = {
	{PTP_RC_EK_FilenameRequired,	N_("PTP EK: Filename Required")},
	{PTP_RC_EK_FilenameConflicts,	N_("PTP EK: Filename Conflicts")},
	{PTP_RC_EK_FilenameInvalid,	N_("PTP EK: Filename Invalid")},
	{0, NULL}
	};
	static struct {
		uint16_t error;
		const char *txt;
	} ptp_errors_NIKON[] = {
	{PTP_RC_NIKON_PropertyReadOnly,	N_("PTP NIKON: Property Read Only")},
	{0, NULL}
	};

	for (i=0; ptp_errors[i].txt!=NULL; i++)
		if (ptp_errors[i].error == error){
			ptp_error(params, ptp_errors[i].txt);
			return;
		}

	/*if (error|PTP_RC_EXTENSION_MASK==PTP_RC_EXTENSION)*/
	switch (params->deviceinfo.VendorExtensionID) {
		case PTP_VENDOR_EASTMAN_KODAK:
			for (i=0; ptp_errors_EK[i].txt!=NULL; i++)
				if (ptp_errors_EK[i].error==error) {
					ptp_error(params, ptp_errors_EK[i].txt);
					return;
				}
			break;
		case PTP_VENDOR_NIKON:
			for (i=0; ptp_errors_NIKON[i].txt!=NULL; i++)
				if (ptp_errors_NIKON[i].error==error) {
					ptp_error(params, ptp_errors_NIKON[i].txt);
					return;
				}
			break;
	}

	ptp_error(params, "PTP: Error 0x%04x", error);
	
}

/* return DataType description */

const char*
ptp_get_datatype_name(PTPParams* params, uint16_t dt)
{
	int i;
	/* Data Types */
	static struct {
		uint16_t dt;
		const char *txt;
	} ptp_datatypes[] = {
		{PTP_DTC_UNDEF,		N_("UndefinedType")},
		{PTP_DTC_INT8,		N_("INT8")},
		{PTP_DTC_UINT8,		N_("UINT8")},
		{PTP_DTC_INT16,		N_("INT16")},
		{PTP_DTC_UINT16,	N_("UINT16")},
		{PTP_DTC_INT32,		N_("INT32")},
		{PTP_DTC_UINT32,	N_("UINT32")},
		{PTP_DTC_INT64,		N_("INT64")},
		{PTP_DTC_UINT64,	N_("UINT64")},
		{PTP_DTC_INT128,	N_("INT128")},
		{PTP_DTC_UINT128,	N_("UINT128")},
		{PTP_DTC_AINT8,		N_("ArrayINT8")},
		{PTP_DTC_AUINT8,	N_("ArrayUINT8")},
		{PTP_DTC_AINT16,	N_("ArrayINT16")},
		{PTP_DTC_AUINT16,	N_("ArrayUINT16")},
		{PTP_DTC_AINT32,	N_("ArrayINT32")},
		{PTP_DTC_AUINT32,	N_("ArrayUINT32")},
		{PTP_DTC_AINT64,	N_("ArrayINT64")},
		{PTP_DTC_AUINT64,	N_("ArrayUINT64")},
		{PTP_DTC_AINT128,	N_("ArrayINT128")},
		{PTP_DTC_AUINT128,	N_("ArrayUINT128")},
		{PTP_DTC_STR,		N_("String")},
		{0,NULL}
	};

	for (i=0; ptp_datatypes[i].txt!=NULL; i++)
		if (ptp_datatypes[i].dt == dt){
			return (ptp_datatypes[i].txt);
		}

	return NULL;
}

/* return ptp operation name */

const char*
ptp_get_operation_name(PTPParams* params, uint16_t oc)
{
	int i;
	/* Operation Codes */
	static struct {
		uint16_t oc;
		const char *txt;
	} ptp_operations[] = {
		{PTP_OC_Undefined,		N_("UndefinedOperation")},
		{PTP_OC_GetDeviceInfo,		N_("GetDeviceInfo")},
		{PTP_OC_OpenSession,		N_("OpenSession")},
		{PTP_OC_CloseSession,		N_("CloseSession")},
		{PTP_OC_GetStorageIDs,		N_("GetStorageIDs")},
		{PTP_OC_GetStorageInfo,		N_("GetStorageInfo")},
		{PTP_OC_GetNumObjects,		N_("GetNumObjects")},
		{PTP_OC_GetObjectHandles,	N_("GetObjectHandles")},
		{PTP_OC_GetObjectInfo,		N_("GetObjectInfo")},
		{PTP_OC_GetObject,		N_("GetObject")},
		{PTP_OC_GetThumb,		N_("GetThumb")},
		{PTP_OC_DeleteObject,		N_("DeleteObject")},
		{PTP_OC_SendObjectInfo,		N_("SendObjectInfo")},
		{PTP_OC_SendObject,		N_("SendObject")},
		{PTP_OC_InitiateCapture,	N_("InitiateCapture")},
		{PTP_OC_FormatStore,		N_("FormatStore")},
		{PTP_OC_ResetDevice,		N_("ResetDevice")},
		{PTP_OC_SelfTest,		N_("SelfTest")},
		{PTP_OC_SetObjectProtection,	N_("SetObjectProtection")},
		{PTP_OC_PowerDown,		N_("PowerDown")},
		{PTP_OC_GetDevicePropDesc,	N_("GetDevicePropDesc")},
		{PTP_OC_GetDevicePropValue,	N_("GetDevicePropValue")},
		{PTP_OC_SetDevicePropValue,	N_("SetDevicePropValue")},
		{PTP_OC_ResetDevicePropValue,	N_("ResetDevicePropValue")},
		{PTP_OC_TerminateOpenCapture,	N_("TerminateOpenCapture")},
		{PTP_OC_MoveObject,		N_("MoveObject")},
		{PTP_OC_CopyObject,		N_("CopyObject")},
		{PTP_OC_GetPartialObject,	N_("GetPartialObject")},
		{PTP_OC_InitiateOpenCapture,	N_("InitiateOpenCapture")},
		{0,NULL}
	};
	static struct {
		uint16_t oc;
		const char *txt;
	} ptp_operations_EK[] = {
		{PTP_OC_EK_SendFileObjectInfo,	N_("EK SendFileObjectInfo")},
		{PTP_OC_EK_SendFileObject,	N_("EK SendFileObject")},
		{0,NULL}
	};
	static struct {
		uint16_t oc;
		const char *txt;
	} ptp_operations_THETA[] = {
		{PTP_OC_THETA_SleepMode,	N_("THETA SleepMode")},
		{0,NULL}
	};


	static struct {
		uint16_t oc;
		const char *txt;
	} ptp_operations_CANON[] = {
		{PTP_OC_CANON_GetObjectSize,	N_("CANON GetObjectSize")},
		{PTP_OC_CANON_StartShootingMode,N_("CANON StartShootingMode")},
		{PTP_OC_CANON_EndShootingMode,	N_("CANON EndShootingMode")},
		{PTP_OC_CANON_ViewfinderOn,	N_("CANON ViewfinderOn")},
		{PTP_OC_CANON_ViewfinderOff,	N_("CANON ViewfinderOff")},
		{PTP_OC_CANON_ReflectChanges,	N_("CANON ReflectChanges")},
		{PTP_OC_CANON_CheckEvent,	N_("CANON CheckEvent")},
		{PTP_OC_CANON_FocusLock,	N_("CANON FocusLock")},
		{PTP_OC_CANON_FocusUnlock,	N_("CANON FocusUnlock")},
		{PTP_OC_CANON_InitiateCaptureInMemory,
					N_("CANON InitiateCaptureInMemory")},
		{PTP_OC_CANON_GetPartialObject,	N_("CANON GetPartialObject")},
		{PTP_OC_CANON_GetViewfinderImage,
					N_("CANON GetViewfinderImage")},
		{PTP_OC_CANON_GetChanges,	N_("CANON GetChanges")},
		{PTP_OC_CANON_GetFolderEntries,	N_("CANON GetFolderEntries")},
		{0,NULL}
	};
	static struct {
		uint16_t oc;
		const char *txt;
	} ptp_operations_NIKON[] = {
		{PTP_OC_NIKON_DirectCapture,	N_("NIKON DirectCapture")},
		{PTP_OC_NIKON_SetControlMode,	N_("NIKON SetControlMode")},
		{PTP_OC_NIKON_CheckEvent,	N_("NIKON Check Event")},
		{PTP_OC_NIKON_KeepAlive,	N_("NIKON Keep Alive (?)")},
		{0,NULL}
	};

	switch (params->deviceinfo.VendorExtensionID) {
		case PTP_VENDOR_EASTMAN_KODAK:
			for (i=0; ptp_operations_EK[i].txt!=NULL; i++)
				if (ptp_operations_EK[i].oc==oc)
					return (ptp_operations_EK[i].txt);
			break;
		case PTP_VENDOR_THETA:
			for (i=0; ptp_operations_THETA[i].txt!=NULL; i++)
				if (ptp_operations_THETA[i].oc==oc)
					return (ptp_operations_THETA[i].txt);
			break;			

		case PTP_VENDOR_CANON:
			for (i=0; ptp_operations_CANON[i].txt!=NULL; i++)
				if (ptp_operations_CANON[i].oc==oc)
					return (ptp_operations_CANON[i].txt);
			break;
		case PTP_VENDOR_NIKON:
			for (i=0; ptp_operations_NIKON[i].txt!=NULL; i++)
				if (ptp_operations_NIKON[i].oc==oc)
					return (ptp_operations_NIKON[i].txt);
			break;
		}
	for (i=0; ptp_operations[i].txt!=NULL; i++)
		if (ptp_operations[i].oc == oc){
			return (ptp_operations[i].txt);
		}

	return NULL;
}

