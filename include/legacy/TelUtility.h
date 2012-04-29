/*
 * libslp-tapi
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Kyeongchul Kim <kyeongchul.kim@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @open
* @ingroup		TelephonyAPI
* @addtogroup	COMMON_TAPI	COMMON
* @{
*
* @file TelUtility.h

     @brief This file serves as a "C" header file defines structures for Utility Services. \n
      It contains a sample set of constants, enums, structs that would be required by applications.
 */

#ifndef _TEL_UTILITY_H_
#define _TEL_UTILITY_H_
/*==================================================================================================
                                         INCLUDE FILES
==================================================================================================*/
#ifdef __cplusplus
extern "C"
{
#endif

/*==================================================================================================
                                           CONSTANTS
==================================================================================================*/


/*==================================================================================================
                                            MACROS
==================================================================================================*/
#define INVALID_REQUEST_ID	-1    /**< Invalid RequestId Value */


/*==================================================================================================
                                             ENUMS
==================================================================================================*/
/**
* @enum TapiResult_t
* Below enumerations are the return codes of TAPI API's
*/
typedef enum
{
	TAPI_API_SUCCESS						=  0,	/**<No Error occurred */
	TAPI_API_INVALID_INPUT					= -1,	/**<input values are not correct in TAPI Library */
	TAPI_API_INVALID_PTR			= -2,	/**<invalid pointer */
	TAPI_API_NOT_SUPPORTED			= -3,	/**<The feature corresponding to requested API is not supported. This may be due to market/network/vendor reasons such as the feature is not available in the network. */
	TAPI_API_DEPRICATED						= -4,   /**<This API is deprecated and will be so in future also */
	TAPI_API_SYSTEM_OUT_OF_MEM				= -5,   /**<Out of memory */
	TAPI_API_SYSTEM_RPC_LINK_DOWN			= -6,   /**<RPC link down */
	TAPI_API_SERVICE_NOT_READY				= -7,   /**<Phone was powered on, but yet to receive the power up completed notification */
	TAPI_API_SERVER_FAILURE		= -8,	/**<error occurred in Telephony server  */
	TAPI_API_OEM_PLUGIN_FAILURE	= -9,	/**<Plug-in layer failure */
	TAPI_API_TRANSPORT_LAYER_FAILURE		= -10,	/**<Transport layer Failure*/
	TAPI_API_INVALID_DATA_LEN				= -11,  /**<Invalid data length */
	TAPI_API_REQUEST_MAX_IN_PROGRESS		= -12,  /**<Maximum number of API Request for the same service are already in progress */
	TAPI_API_OFFLINE_MODE_ERROR			= -13,	/**<OEM Telephony Provider is in Offline mode. */
	TAPI_EVENT_CLASS_UNKNOWN				= -14,  /**<Event class specified is not present in Event Class list. - 20*/
	TAPI_EVENT_UNKNOWN				= -15,	/**<Event specified is not present in TAPI Unsolicited Event list. */
	TAPI_REGISTRATION_OP_FAILED				= -16,	/**<Callback Registration/De-registration failed */
	TAPI_API_OPERATION_FAILED				= -17,	/**<API operation failed*/
	TAPI_API_INVALID_OPERATION				= -18,	/**<API Invalid Operation */

	/*SAMSUNG specif*/
	TAPI_API_SYSTEM_RPC_LINK_NOT_EST		= -100,	/**< RPC link down */
	TAPI_API_API_NOT_SUPPORTED				= -101,	/**<API not supported */
	TAPI_API_SERVER_LAYER_FAILURE			= -102,	/**< Server layer failure*/

	/*	CALL */
	TAPI_API_INVALID_CALL_ID				= -200,	/**< Invalid call ID*/
	TAPI_API_CALL_CTXT_OVERFLOW				= -201,	/**<Call context overflow */
	TAPI_API_COULD_NOT_GET_CALL_CTXT		= -202,	/**< Could not get call  context*/
	TAPI_API_CTXT_SEARCH_RET_NON_CALL_CTXT  = -203,	/**< Context search returned non-call context*/
	TAPI_API_COULD_NOT_DESTROY_CTXT			= -204, /**< could not destroy context*/
	TAPI_API_INVALID_LINE_ID				= -205, /**< invalid line ID*/
	TAPI_API_INVALID_CALL_HANDLE			= -206, /**<Invalid call handle  - 35*/
	TAPI_API_INVALID_CALL_STATE				= -207, /**<Invalid call state- Newly added. Need to raise a CR for this */
	TAPI_API_CALL_PRE_COND_FAILED			= -208,	/**<Pre condition like MO call can not be established now.*/
	TAPI_API_CALL_SAME_REQ_PENDING			= -209,	/**<  Can not accept same request multiple times  */

	/*	POWER	*/
	TAPI_API_MODEM_POWERED_OFF				= -300, /**<The Modem is powered off */
	TAPI_API_MODEM_ALREADY_ON				= -301, /**<Modem already on */
	TAPI_API_MODEM_ALREADY_OFF				= -302,	/**<Modem already off */

	/* NETTEXT */
	TAPI_API_NETTEXT_DEVICE_NOT_READY		= -400, /**<Nettext device not ready */
	TAPI_API_NETTEXT_SCADDR_NOT_SET			= -401, /**<Nettext SCA address not set */
	TAPI_API_NETTEXT_INVALID_DATA_LEN		= -402, /**<Nettext Invalid data length */
	TAPI_NETTEXT_SCADDRESS_NOT_SET			= -403, /**<Nettext SCA address not set*/

	/* SIM  */
	TAPI_API_SIM_BUSY						= -500, /**<SIM is busy  */
	TAPI_API_SIM_CARD_PERMANENT_ERROR		= -501, /**<SIM error/blocked state */
	TAPI_API_SIM_NOT_INITIALIZED			= -502, /**<SIM has not initialized yet (waiting for PIN verification, etc) */
	TAPI_API_SIM_NOT_FOUND					= -503, /**<SIM is not present / removed */
	TAPI_API_SIM_SIM_SESSION_FULL			= -504,	/**< SIM session full*/
	TAPI_API_SIM_INVALID_CARD_TYPE			= -505,	/**< SIM Invalid Application ID*/
	TAPI_API_SIM_INVALID_SESSION			= -506,	/**<SIM Invalid Session */
	TAPI_API_SIM_FILE_TYPE_WRONG			= -507, /**<SIM file type wrong */
	TAPI_API_SIM_FILE_ENCODE_FAIL			= -508,	/**<SIM file encode fail */
	TAPI_API_SIM_FILE_DECODE_FAIL			= -509,	/**< SIM file decode fail*/
	TAPI_API_SIM_FILE_INVALID_ENCODE_LEN	= -510,	/**< SIM invalid encode length*/
	TAPI_API_SIM_INVALID_RECORD_NUM		= -511,	/**<SIM Invalid record number */
	TAPI_API_SIM_CASE_NOT_HANDLED			= -512,	/**< SIM case not handled*/
	TAPI_API_SIM_OEM_UNKNOWN_SIM_CARD		= -513, /**<SIM OEM unknown SIM card */
	TAPI_API_SIM_SEC_UKNOWN_PIN_TYPE		= -514,	/**<SIM unknown pin type */
	TAPI_API_SIM_SEC_INVALID_PIN_TYPE		= -515,	/**<SIM invalid pin type */
	TAPI_API_SIM_SEC_LOCK_PERS_ENABLED		= -516,	/**<SIM Lock Personalization status(PN/PU/PP/PC) */
	TAPI_API_SIM_PB_INVALID_STORAGE_TYPE	= -517,	/**<SIM phonebook invalid storage type */

	/* SAT  */
	TAPI_API_SAT_INVALID_COMMAND_ID			= -600,	/**<Command Number Invalid	*/
	TAPI_API_SAT_COMMAND_TYPE_MISMATCH		= -601,	/**<	Command Type Mismatch	*/
	TAPI_API_SAT_EVENT_NOT_REQUIRED_BY_USIM = -602,	/**< Event Not Requested by USIM*/

	/* Network */
	TAPI_API_NETWORK_INVALID_CTXT			= -700,	/**< Network invalid context*/

	/*Misc */
	TAPI_API_MISC_RETURN_NULL				= -800,	/**< MISC return NULL*/
	TAPI_API_MISC_VALIDITY_ERROR			= -801,	/**< MISC validity error*/
	TAPI_API_MISC_INPUTPARM_ERROR			= -802, /**< MISC input parameter error*/
	TAPI_API_MISC_OUTPARAM_NULL				= -803,	/**< MISC output parameter null*/

} TapiResult_t;

/*==================================================================================================
                                 STRUCTURES AND OTHER TYPEDEFS
==================================================================================================*/


/*==================================================================================================
                                     FUNCTION PROTOTYPES
==================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif //_TEL_UTILITY_H_

/**
* @}
*/

