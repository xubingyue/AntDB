#include "postgres.h"

#include "agtm/agtm_msg.h"
#include "agtm/agtm_utils.h"

#define CASE_TYPE_(t)	\
	case t:				\
		return #t

const char *gtm_util_message_name(AGTM_MessageType type)
{
	switch(type)
	{
	CASE_TYPE_(AGTM_MSG_GET_GXID);
	CASE_TYPE_(AGTM_MSG_GET_TIMESTAMP);
	CASE_TYPE_(AGTM_MSG_GXID_LIST);
	CASE_TYPE_(AGTM_MSG_SNAPSHOT_GET);
	CASE_TYPE_(AGTM_MSG_SEQUENCE_GET_NEXT);
	CASE_TYPE_(AGTM_MSG_SEQUENCE_GET_CUR);
	CASE_TYPE_(AGTM_MSG_SEQUENCE_GET_LAST);
	CASE_TYPE_(AGTM_MSG_SEQUENCE_SET_VAL);
	CASE_TYPE_(AGTM_MSG_GET_STATUS);
	CASE_TYPE_(AGTM_MSG_XACT_LOCK_TABLE_WAIT);
	CASE_TYPE_(AGTM_MSG_LOCK_TRANSACTION);
	/* here no default, we need a compiler warning */
	}
	return "Unknown AGTM_MessageType";
}

const char *gtm_util_result_name(AGTM_ResultType type)
{
	switch(type)
	{
	CASE_TYPE_(AGTM_NONE_RESULT);
	CASE_TYPE_(AGTM_GET_GXID_RESULT);
	CASE_TYPE_(AGTM_GET_TIMESTAMP_RESULT);
	CASE_TYPE_(AGTM_GXID_LIST_RESULT);
	CASE_TYPE_(AGTM_SNAPSHOT_GET_RESULT);
	CASE_TYPE_(AGTM_SEQUENCE_GET_NEXT_RESULT);
	CASE_TYPE_(AGTM_MSG_SEQUENCE_GET_CUR_RESULT);
	CASE_TYPE_(AGTM_SEQUENCE_GET_LAST_RESULT);
	CASE_TYPE_(AGTM_SEQUENCE_SET_VAL_RESULT);
	CASE_TYPE_(AGTM_COMPLETE_RESULT);
	/* here no default, we need a compiler warning */
	}
	return "Unknown AGTM_ResultType";
}
