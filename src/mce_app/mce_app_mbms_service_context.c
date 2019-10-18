  /*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under 
 * the Apache License, Version 2.0  (the "License"); you may not use this file
 * except in compliance with the License.  
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file mce_app_context.c
  \brief
  \author Dincer Beken
  \company Blackned GmbH
  \email: dbeken@blackned.de
*/

#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include <inttypes.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "gcc_diag.h"
#include "dynamic_memory_check.h"
#include "assertions.h"
#include "log.h"
#include "msc.h"
#include "3gpp_requirements_36.413.h"
#include "common_types.h"
#include "conversions.h"
#include "intertask_interface.h"
#include "mme_config.h"
#include "enum_string.h"
#include "timer.h"
#include "mce_app_mbms_service_context.h"
#include "mce_app_defs.h"
#include "mce_app_itti_messaging.h"
#include "common_defs.h"

// todo: think about locking the MCE_APP context or EMM context, which one to lock, why to lock at all? lock seperately?
////------------------------------------------------------------------------------
//int lock_ue_contexts(mbms_service_t * const ue_context) {
//  int rc = RETURNerror;
//  if (ue_context) {
//    struct timeval start_time;
//    gettimeofday(&start_time, NULL);
//    struct timespec wait = {0}; // timed is useful for debug
//    wait.tv_sec=start_time.tv_sec + 5;
//    wait.tv_nsec=start_time.tv_usec*1000;
//    rc = pthread_mutex_timedlock(&ue_context->recmutex, &wait);
//    if (rc) {
//      OAILOG_ERROR (LOG_MCE_APP, "Cannot lock UE context mutex, err=%s\n", strerror(rc));
//#if ASSERT_MUTEX
//      struct timeval end_time;
//      gettimeofday(&end_time, NULL);
//      AssertFatal(!rc, "Cannot lock UE context mutex, err=%s took %ld seconds \n", strerror(rc), end_time.tv_sec - start_time.tv_sec);
//#endif
//    }
//#if DEBUG_MUTEX
//    OAILOG_TRACE (LOG_MCE_APP, "UE context mutex locked, count %d lock %d\n",
//        ue_context->recmutex.__data.__count, ue_context->recmutex.__data.__lock);
//#endif
//  }
//  return rc;
//}
////------------------------------------------------------------------------------
//int unlock_ue_contexts(mbms_service_t * const ue_context) {
//  int rc = RETURNerror;
//  if (ue_context) {
//    rc = pthread_mutex_unlock(&ue_context->recmutex);
//    if (rc) {
//      OAILOG_ERROR (LOG_MCE_APP, "Cannot unlock UE context mutex, err=%s\n", strerror(rc));
//    }
//#if DEBUG_MUTEX
//    OAILOG_TRACE (LOG_MCE_APP, "UE context mutex unlocked, count %d lock %d\n",
//        ue_context->recmutex.__data.__count, ue_context->recmutex.__data.__lock);
//#endif
//  }
//  return rc;
//}
//
/****************************************************************************/
/*******************  L O C A L    D E F I N I T I O N S  *******************/
/****************************************************************************/
static void clear_mbms_service(mbms_service_t * mbms_service);
static void release_mbms_service(mbms_service_t ** mbms_service);
static int mce_insert_mbms_service(mce_mbms_services_t * const mce_mbms_services_p, const struct mbms_service_s *const mbms_service);
static mbmsServiceIndex64_t get_mbms_service_index(const tmgi_t * tmgi, const mbms_service_area_t * const mbms_service_area);
static struct mbms_service_s *mce_mbms_service_exists_mbms_service_index(mce_mbms_services_t * const mce_mbms_services_p, const mbmsServiceIndex64_t mbms_service_index);

////------------------------------------------------------------------------------
//static bstring bearer_state2string(const mce_app_bearer_state_t bearer_state)
//{
//  bstring bsstr = NULL;
//  if  (BEARER_STATE_NULL == bearer_state) {
//    bsstr = bfromcstr("BEARER_STATE_NULL");
//    return bsstr;
//  }
//  bsstr = bfromcstr(" ");
//  if  (BEARER_STATE_SGW_CREATED & bearer_state) bcatcstr(bsstr, "SGW_CREATED ");
//  if  (BEARER_STATE_MME_CREATED & bearer_state) bcatcstr(bsstr, "MME_CREATED ");
//  if  (BEARER_STATE_ENB_CREATED & bearer_state) bcatcstr(bsstr, "ENB_CREATED ");
//  if  (BEARER_STATE_ACTIVE & bearer_state) bcatcstr(bsstr, "ACTIVE");
//  return bsstr;
//}

//------------------------------------------------------------------------------
static mbmsServiceIndex64_t get_mbms_service_index(const tmgi_t * tmgi, const mbms_service_area_t * const mbms_service_area)
{
  if(!tmgi || !tmgi->mbms_service_id)
	  return INVALID_MBMS_SERVICE_INDEX;

  if(!mbms_service_area || !mbms_service_area->num_service_area || !mbms_service_area->serviceArea[0])
	  return INVALID_MBMS_SERVICE_INDEX;

  return tmgi->mbms_service_id + mbms_service_area->serviceArea[0] << 24;
}

//------------------------------------------------------------------------------
struct mbms_service_s *
register_mbms_service(const tmgi_t * const tmgi, const mbms_service_area_t * const mbms_service_area, const teid_t mme_sm_teid){
  OAILOG_FUNC_IN (LOG_MCE_APP);

  // todo: lock the mce_desc
  mbmsServiceIndex64_t mbms_service_index = get_mbms_service_index(tmgi, mbms_service_area);
  if (mbms_service_index == INVALID_MBMS_SERVICE_INDEX) {
    OAILOG_CRITICAL (LOG_MCE_APP, "MBMS Service Index generation failed for TMGI " TMGI_FMT".\n", TMGI_ARG(tmgi));
    OAILOG_FUNC_RETURN(LOG_MCE_APP, NULL);
  }
  /** Check the first element in the list. If it is not empty, reject. */
  mbms_service_t * mbms_service = STAILQ_FIRST(&mce_app_desc.mce_mbms_services_list);
  DevAssert(mbms_service); /**< todo: with locks, it should be guaranteed, that this should exist. */

  mbmsServiceIndex64_t mbms_service_index_first_free = get_mbms_service_index(&mbms_service->privates.fields.tmgi, &mbms_service->privates.fields.mbms_service_area);
  if (mbms_service_index_first_free == INVALID_MBMS_SERVICE_INDEX) {
	OAILOG_ERROR(LOG_MCE_APP, "No free MBMS Bearer Services. Cannot allocate a new one.\n");
    OAILOG_FUNC_RETURN (LOG_MCE_APP, NULL);
  }
  // todo: lock the MBMS_SERVICE !!
  /** Found a free pool: Remove it from the head, add the mbms_service_index and set it to the end. */
  STAILQ_REMOVE_HEAD(&mce_app_desc.mce_mbms_services_list, entries); /**< free_ms is removed. */

  OAILOG_INFO(LOG_MCE_APP, "Clearing received current mbms_service %p.\n", mbms_service);
  clear_mbms_service(mbms_service);
  /** Set the TMGI, MBMS Service ID and the Sm MME TEID. */
  memcpy((void*)&mbms_service->privates.fields.tmgi, tmgi, sizeof(tmgi_t));
  memcpy((void*)&mbms_service->privates.fields.mbms_service_area, mbms_service_area, sizeof(mbms_service_area_t));
  mbms_service->privates.fields.mme_teid_sm = mme_sm_teid;
  // todo: unlock the MBMS service
  /** Add it to the back of the list. */
  STAILQ_INSERT_TAIL(&mce_app_desc.mce_mbms_services_list, mbms_service, entries);

  /** Add the MBMS Service. */
  OAILOG_DEBUG (LOG_MCE_APP, "Allocated new MBMS service with MBMS Service Index " MCE_MBMS_SERVICE_INDEX_FMT " for MBMS Service with TMGI " TMGI_FMT". \n",
		  mbms_service_index, TMGI_ARG(tmgi));
  DevAssert(mce_insert_mbms_service( &mce_app_desc.mbms_services, mbms_service) == 0);
  // todo: unlock mce_desc!
  OAILOG_FUNC_RETURN (LOG_MCE_APP, mbms_service);
}

//------------------------------------------------------------------------------
static struct mbms_service_s                           *
mce_mbms_service_exists_mbms_service_index(
  mce_mbms_services_t * const mce_mbms_services_p, const mbmsServiceIndex64_t mbms_service_index)
{
  struct mbms_service_t                    *mbms_service = NULL;
  hashtable_ts_get (mce_mbms_services_p->mbms_service_index_mbms_service_htbl, (const hash_key_t)mbms_service_index, (void **)&mbms_service);
  return mbms_service;
}

//------------------------------------------------------------------------------
struct mbms_service_s                           *
mce_mbms_service_exists_mbms_service_id(
  mce_mbms_services_t * const mce_mbms_services_p,
  const tmgi_t * const tmgi, const mbms_service_area_t * const mbms_service_area)
{
  struct mbms_service_t                    *mbms_service = NULL;

  mbmsServiceIndex64_t					    mbms_service_index = get_mbms_service_index(tmgi, mbms_service);

  hashtable_ts_get (mce_mbms_services_p->mbms_service_index_mbms_service_htbl, (const hash_key_t)mbms_service_index, (void **)&mbms_service);
  return mbms_service;
}

//------------------------------------------------------------------------------
struct mbms_service_s					        *
mce_mbms_service_exists_sm_teid(mce_mbms_services_t * const mce_mbms_services, const sm_teid_t teid)
{
  hashtable_rc_t                          h_rc = HASH_TABLE_OK;
  uint64_t                                mce_mbms_service_idx64 = 0;

  h_rc = hashtable_uint64_ts_get (mce_mbms_services->tunsm_mbms_service_htbl, (const hash_key_t)teid, &mce_mbms_service_idx64);

  if (HASH_TABLE_OK == h_rc) {
    return mce_mbms_service_exists_mbms_service_index(mce_mbms_services, (mbmsServiceIndex64_t) mce_mbms_service_idx64);
  }
  return NULL;
}

//------------------------------------------------------------------------------
void mce_mbms_service_dump_coll_keys(void)
{
  bstring tmp = bfromcstr(" ");
  btrunc(tmp, 0);

  btrunc(tmp, 0);
  hashtable_uint64_ts_dump_content (mce_app_desc.mce_mbms_service_contexts.tunsm_mbms_service_htbl, tmp);
  OAILOG_TRACE (LOG_MCE_APP,"tunsm_mbms_service_htbl %s\n", bdata(tmp));

  btrunc(tmp, 0);
  hashtable_ts_dump_content(mce_app_desc.mce_mbms_service_contexts.mbms_service_index_mbms_service_htbl, tmp);
  OAILOG_TRACE (LOG_MCE_APP,"mce_mbms_index_mbms_service_htbl %s\n", bdata(tmp));

  bdestroy_wrapper(&tmp);
}

// todo: check the locks here
//------------------------------------------------------------------------------
static int
mce_insert_mbms_service (
  mce_mbms_services_t * const mce_mbms_services_p,
  const struct mbms_service_s *const mbms_service)
{
  hashtable_rc_t                          h_rc = HASH_TABLE_OK;

  OAILOG_FUNC_IN (LOG_MCE_APP);
  DevAssert (mce_mbms_services_p );
  DevAssert (mbms_service );

  mbmsServiceIndex64_t mbms_service_index = get_mbms_service_index(&mbms_service->privates.fields.tmgi, &mbms_service->privates.fields.mbms_service_area);
  DevAssert (mbms_service_index != INVALID_MBMS_SERVICE_INDEX);
  DevAssert (mbms_service->privates.fields.mme_teid_sm != INVALID_TEID);

  h_rc = hashtable_ts_is_key_exists (mce_mbms_services_p->mbms_service_index_mbms_service_htbl, (const hash_key_t)mbms_service_index);

  if (HASH_TABLE_OK == h_rc) {
	  OAILOG_DEBUG (LOG_MCE_APP, "The MBMS Service with TMGI " TMGI_FMT" and MBMS Service Index " MCE_MBMS_SERVICE_INDEX_FMT " is already existing. \n",
			  TMGI_ARG(&mbms_service->privates.fields.tmgi), mbms_service_index);
	  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
  }

  h_rc = hashtable_ts_insert (mce_mbms_services_p->mbms_service_index_mbms_service_htbl,
		  (const hash_key_t)mbms_service_index, (void *)mbms_service);

  if (HASH_TABLE_OK != h_rc) {
	  OAILOG_DEBUG (LOG_MCE_APP, "Error could not register the MBMS Service %p with TMGI " TMGI_FMT" and MBMS Service Index " MCE_MBMS_SERVICE_INDEX_FMT ". \n",
			  mbms_service, TMGI_ARG(&mbms_service->privates.fields.tmgi), mbms_service_index);
	  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
  }

  // filled SM tun id
  if (mbms_service->privates.fields.mme_teid_sm) {
	  h_rc = hashtable_uint64_ts_insert (mce_mbms_services_p->tunsm_mbms_service_htbl,
			  (const hash_key_t)mbms_service->privates.fields.mme_teid_sm,
			  (void *)((uintptr_t)mbms_service_index));

	  if (HASH_TABLE_OK != h_rc) {
		  OAILOG_DEBUG (LOG_MCE_APP, "Error could not register the MBMS Service %p with TMGI "TMGI_FMT " and MBMS Service Index " MCE_MBMS_SERVICE_INDEX_FMT " and SM-MME TEID " TEID_FMT". \n",
				  mbms_service, TMGI_ARG(&mbms_service->privates.fields.tmgi), mbms_service_index, mbms_service->privates.fields.mme_teid_sm);
		  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNerror);
	  }
  }
  /*
   * Updating statistics
   */
  __sync_fetch_and_add (&mce_mbms_services_p->nb_mbms_service_managed, 1);
  __sync_fetch_and_add (&mce_mbms_services_p->nb_mbms_service_since_last_stat, 1);

  OAILOG_FUNC_RETURN (LOG_MCE_APP, RETURNok);
}

//------------------------------------------------------------------------------
void mce_remove_mbms_service(
  mce_mbms_services_t * const mce_mbms_services_p,
  struct mbms_service_s *mbms_service)
{
  unsigned int                           *id = NULL;
  hashtable_rc_t                          hash_rc = HASH_TABLE_OK;
  mbmsServiceIndex64_t 					  mbms_service_idx = INVALID_MBMS_SERVICE_INDEX;

  OAILOG_FUNC_IN (LOG_MCE_APP);
  DevAssert (mce_mbms_services_p);
  DevAssert (mbms_service);

  // filled Sm tun id
  if (mbms_service->privates.fields.mme_teid_sm) {
    hash_rc = hashtable_uint64_ts_remove (mce_mbms_services_p->tunsm_mbms_service_htbl, (const hash_key_t)mbms_service->privates.fields.mme_teid_sm);
    if (HASH_TABLE_OK != hash_rc)
      OAILOG_DEBUG(LOG_MCE_APP, "Service MBMS Service for TMGI "TMGI_FMT" and MME SM_TEID " TEID_FMT "  not in sm collection. \n",
          TMGI_ARG(&mbms_service->privates.fields.tmgi), mbms_service->privates.fields.mme_teid_sm);
  }

  mbms_service_idx = get_mbms_service_index(&mbms_service->privates.fields.tmgi, &mbms_service->privates.fields.mbms_service_area);

  // filled MBMS Service Index
  if (INVALID_MBMS_SERVICE_INDEX != mbms_service_idx) {
    hash_rc = hashtable_ts_remove (mce_mbms_services_p->mbms_service_index_mbms_service_htbl, (const hash_key_t)mbms_service_idx, (void **)&mbms_service);
    if (HASH_TABLE_OK != hash_rc)
      OAILOG_DEBUG(LOG_MCE_APP, "MBMS Service MBMS Service Index " MCE_MBMS_SERVICE_INDEX_FMT " not in MCE MBMS Service Index collection", mbms_service_idx);
  }

  release_mbms_service(&mbms_service);
  // todo: unlock?
  //  unlock_ue_contexts(ue_context);
  /*
   * Updating statistics
   */
  __sync_fetch_and_sub (&mce_mbms_services_p->nb_mbms_service_managed, 1);
  __sync_fetch_and_sub (&mce_mbms_services_p->nb_mbms_service_since_last_stat, 1);

  OAILOG_FUNC_OUT (LOG_MCE_APP);
}

//------------------------------------------------------------------------------
bool
mce_app_dump_mbms_service (
  const hash_key_t keyP,
  void *const mbms_service_pP,
  void *unused_param_pP,
  void** unused_result_pP)
//------------------------------------------------------------------------------
{
  struct mbms_service_s           *const mbms_service = (struct mbms_service_s *)mbms_service_pP;
  uint8_t                                 j = 0;
  if (mbms_service) {
	mbmsServiceIndex64_t mbms_service_idx = get_mbms_service_index(&mbms_service->privates.fields.tmgi, &mbms_service->privates.fields.mbms_service_area);
    bstring                                 bstr_dump = bfromcstralloc(4096, "\n-----------------------MBMS Service context ");
    bformata (bstr_dump, "%p --------------------\n", mbms_service);
    bformata (bstr_dump, "    - MBMS Service Index.: " MCE_MBMS_SERVICE_INDEX_FMT "\n", mbms_service_idx);
    bformata (bstr_dump, "    - MME Sm TEID .......: " TEID_FMT "\n", mbms_service->privates.fields.mme_teid_sm);
    /*
     * Display MBMS Service info only if we know them
     */
    bcatcstr (bstr_dump, "---------------------------------------------------------\n");
    OAILOG_DEBUG(LOG_MCE_APP, "%s\n", bdata(bstr_dump));
    bdestroy_wrapper(&bstr_dump);
    return false;
  }
  return true;
}

//------------------------------------------------------------------------------
void
mce_app_dump_mbms_services(
  const mce_mbms_services_t * const mce_mbms_services_p)
//------------------------------------------------------------------------------
{
  hashtable_uint64_ts_apply_callback_on_elements (mce_mbms_services_p->mbms_service_index_mbms_service_htbl, mce_app_dump_mbms_service, NULL, NULL);
}

////------------------------------------------------------------------------------
//void mce_app_handle_m2ap_enb_deregistered_ind (const itti_m2ap_eNB_deregistered_ind_t * const enb_dereg_ind)
//{
//  for (int ue_idx = 0; ue_idx < enb_dereg_ind->nb_ue_to_deregister; ue_idx++) {
//      mce_app_send_nas_signalling_connection_rel_ind(enb_dereg_ind->mme_ue_s1ap_id[ue_idx]); /**< If any procedures were ongoing, kill them. */
//      _mce_app_handle_s1ap_ue_context_release(enb_dereg_ind->mme_ue_s1ap_id[ue_idx], enb_dereg_ind->enb_ue_s1ap_id[ue_idx], enb_dereg_ind->enb_id, S1AP_SCTP_SHUTDOWN_OR_RESET);
//  }
//}
//
////------------------------------------------------------------------------------
//void
//mce_app_handle_enb_reset_req (itti_s1ap_enb_initiated_reset_req_t * const enb_reset_req)
//{
//
//  MessageDef *message_p;
//  OAILOG_DEBUG (LOG_MCE_APP, " eNB Reset request received. eNB id = %d, reset_type  %d \n ", enb_reset_req->enb_id, enb_reset_req->s1ap_reset_type);
//  DevAssert (enb_reset_req->ue_to_reset_list != NULL);
//  if (enb_reset_req->s1ap_reset_type == RESET_ALL) {
//  // Full Reset. Trigger UE Context release release for all the connected UEs.
//    for (int i = 0; i < enb_reset_req->num_ue; i++) {
//      _mce_app_handle_s1ap_ue_context_release((enb_reset_req->ue_to_reset_list[i].mme_ue_s1ap_id) ? *(enb_reset_req->ue_to_reset_list[i].mme_ue_s1ap_id) : INVALID_MME_UE_S1AP_ID,
//    		  	  	  	  	  	  	  		(enb_reset_req->ue_to_reset_list[i].enb_ue_s1ap_id) ? *(enb_reset_req->ue_to_reset_list[i].enb_ue_s1ap_id) : 0,
//                                            enb_reset_req->enb_id,
//                                            S1AP_SCTP_SHUTDOWN_OR_RESET);
//    }
//
//  } else { // Partial Reset
//    for (int i = 0; i < enb_reset_req->num_ue; i++) {
//      if (enb_reset_req->ue_to_reset_list[i].mme_ue_s1ap_id == NULL &&
//                          enb_reset_req->ue_to_reset_list[i].enb_ue_s1ap_id == NULL)
//        continue;
//      else
//        _mce_app_handle_s1ap_ue_context_release((enb_reset_req->ue_to_reset_list[i].mme_ue_s1ap_id) ? *(enb_reset_req->ue_to_reset_list[i].mme_ue_s1ap_id) : INVALID_MME_UE_S1AP_ID,
//        								    (enb_reset_req->ue_to_reset_list[i].enb_ue_s1ap_id) ? *(enb_reset_req->ue_to_reset_list[i].enb_ue_s1ap_id) : 0,
//                                            enb_reset_req->enb_id,
//                                            S1AP_SCTP_SHUTDOWN_OR_RESET);
//    }
//
//  }
//  // Send Reset Ack to S1AP module
//  message_p = itti_alloc_new_message (TASK_MCE_APP, S1AP_ENB_INITIATED_RESET_ACK);
//  DevAssert (message_p != NULL);
//  memset ((void *)&message_p->ittiMsg.s1ap_enb_initiated_reset_ack, 0, sizeof (itti_s1ap_enb_initiated_reset_ack_t));
//  S1AP_ENB_INITIATED_RESET_ACK (message_p).s1ap_reset_type = enb_reset_req->s1ap_reset_type;
//  S1AP_ENB_INITIATED_RESET_ACK (message_p).sctp_assoc_id = enb_reset_req->sctp_assoc_id;
//  S1AP_ENB_INITIATED_RESET_ACK (message_p).sctp_stream_id = enb_reset_req->sctp_stream_id;
//  S1AP_ENB_INITIATED_RESET_ACK (message_p).num_ue = enb_reset_req->num_ue;
//  /*
//   * Send the same ue_reset_list to S1AP module to be used to construct S1AP Reset Ack message. This would be freed by
//   * S1AP module.
//   */
//  S1AP_ENB_INITIATED_RESET_ACK (message_p).ue_to_reset_list = enb_reset_req->ue_to_reset_list;
//  enb_reset_req->ue_to_reset_list = NULL;
//  itti_send_msg_to_task (TASK_S1AP, INSTANCE_DEFAULT, message_p);
//  OAILOG_DEBUG (LOG_MCE_APP, " Reset Ack sent to S1AP. eNB id = %d, reset_type  %d \n ", enb_reset_req->enb_id, enb_reset_req->s1ap_reset_type);
//  OAILOG_FUNC_OUT (LOG_MCE_APP);
//}

/****************************************************************************/
/*********************  L O C A L    F U N C T I O N S  *********************/
/****************************************************************************/

// todo: MBMS Service should already be locked : any way to check it?!?
//------------------------------------------------------------------------------
static void clear_mbms_service(mbms_service_t * mbms_service) {
	OAILOG_FUNC_IN(LOG_MCE_APP);

	DevAssert(mbms_service != NULL);
	// todo: lock MBMS Service
	OAILOG_INFO(LOG_MCE_APP, "Clearing MBMS Service with TMGI " TMGI_FMT ". \n", TMGI_ARG(&mbms_service->privates.fields.tmgi));
	/**
	 * Clear the bearer contexts.
	 * Just one bearer for all eNBs should exist.
	 * eNB dependencies should be handled in the M2AP layer.
	 */
//	mbms_service->privates.fields.bc_mbms.esm_ebr_context.status   = ESM_EBR_INACTIVE;
//	if(mbms_service->privates.fields.bc_mbms.esm_ebr_context.tft){
//	  OAILOG_INFO(LOG_MCE_APP, "FREEING TFT %p of MBMS Service with TMGI " TMGI_FMT".\n",
//		mbms_service->privates.fields.bc_mbms.esm_ebr_context.tft, TMGI_ARG(&mbms_service->privates.fields.tmgi));
//	  free_traffic_flow_template(&mbms_service->privates.fields.bc_mbms.esm_ebr_context.tft);
//	}
	// todo: check pco
	//  if(bearer_context->esm_ebr_context.pco){
	//    free_protocol_configuration_options(&bearer_context->esm_ebr_context.pco);
	//  }
//	/** Remove the remaining contexts of the bearer context. */
//	memset(&mbms_service->privates.fields.bc_mbms.esm_ebr_context, 0, sizeof(esm_ebr_context_t)); /**< Sets the SM status to ESM_STATUS_INVALID. */
//	memset(&mbms_service->privates.fields.bc_mbms, 0, sizeof(bearer_context_new_t));
//	/** Single bearer expected // NO EBIs & no lists. */

	memset(&mbms_service->privates.fields, 0, sizeof(mbms_service->privates.fields));
	OAILOG_INFO(LOG_MCE_APP, "Successfully cleared MBMS Service for TMGI " TMGI_FMT". \n", TMGI_ARG(&mbms_service->privates.fields.tmgi));
}

//------------------------------------------------------------------------------
static void release_mbms_service(mbms_service_t ** mbms_service) {
	OAILOG_FUNC_IN (LOG_MCE_APP);

	/** Clear the UE context. */
	OAILOG_INFO(LOG_MCE_APP, "Releasing the MBMS Service %p for TMGI " TMGI_FMT".\n",
			*mbms_service, TMGI_ARG(&(*mbms_service)->privates.fields.tmgi));

	mbmsServiceIndex64_t mbms_service_idx = get_mbms_service_index(&(*mbms_service)->privates.fields.tmgi, &(*mbms_service)->privates.fields.mbms_service_area);
	// todo: lock the mce_desc (mbms_service should already be locked)
	/** Remove the mbms_service from the list (probably at the back - must not be at the very end. */
	STAILQ_REMOVE(&mce_app_desc.mce_mbms_services_list, (*mbms_service), mbms_service_s, entries);
	clear_mbms_service(*mbms_service);
	/** Put it into the head. */
	STAILQ_INSERT_HEAD(&mce_app_desc.mce_mbms_services_list, (*mbms_service), entries);
	*mbms_service= NULL;
	// todo: unlock the mce_desc
}

