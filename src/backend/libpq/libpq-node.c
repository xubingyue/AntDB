#include "postgres.h"

#include "catalog/pgxc_node.h"
#include "libpq/libpq-fe.h"
#include "libpq/pqcomm.h"
#include "miscadmin.h"
#include "utils/memutils.h"
#include "nodes/pg_list.h"
#include "pgxc/nodemgr.h"
#include "pgxc/pgxcnode.h"
#include "pgxc/poolmgr.h"
#include "utils/hsearch.h"

#ifdef HAVE_POLL_H
#include <poll.h>
#elif defined(HAVE_SYS_POLL_H)
#include <sys/poll.h>
#endif

#include "libpq/libpq-node.h"

typedef struct OidPGconn
{
	Oid oid;
	char type;
	PGconn *conn;
}OidPGconn;

static HTAB *htab_oid_pgconn = NULL;

static void init_htab_oid_pgconn(void);
static List* apply_for_node_use_oid(List *oid_list);
static OidPGconn* insert_pgconn_to_htab(int index, char type, PGconn *conn);
static List* pg_conn_attach_socket(int *fds, Size n);
static void PQNExecFinsh_trouble(PGconn *conn);
static bool PQNExecFinish(PGconn *conn, PQNExecFinishHook_function hook, const void *context);
static int PQNIsConnecting(PGconn *conn);

List *PQNGetConnUseOidList(List *oid_list)
{
	if(htab_oid_pgconn == NULL)
		init_htab_oid_pgconn();
	return apply_for_node_use_oid(oid_list);
}

static void init_htab_oid_pgconn(void)
{
	HASHCTL hctl;
	long size;
	Assert(htab_oid_pgconn == NULL);

	MemSet(&hctl, 0, sizeof(hctl));
	hctl.keysize = sizeof(Oid);
	hctl.entrysize = sizeof(OidPGconn);
	hctl.hash = oid_hash;
	hctl.hcxt = TopMemoryContext;
	size = 16;
	while(size < NumCoords + NumDataNodes)
		size <<= 1;	/* size = size*2 */
	htab_oid_pgconn = hash_create("hash oid to PGconn", size, &hctl
				, HASH_ELEM | HASH_CONTEXT | HASH_FUNCTION);
/*	pg_atexit*/
}

/*
 * save apply for socket to result list,
 * if we has socket for node oid, save PGINVALID_SOCKET in list item
 */
static List* apply_for_node_use_oid(List *oid_list)
{
	List *co_list = NIL;
	List *dn_list = NIL;
	List *result = NIL;
	ListCell *lc, *lc2;
	OidPGconn *op;
	int id;

	foreach(lc, oid_list)
	{
		if((op=hash_search(htab_oid_pgconn, &(lfirst_oid(lc)), HASH_FIND, NULL)) != NULL)
		{
			result = lappend(result, op->conn);
			continue;
		}else
		{
			result = lappend(result, NULL);
		}

		if((id = PGXCNodeGetNodeId(lfirst_oid(lc), PGXC_NODE_DATANODE)) != -1)
		{
			dn_list = lappend_int(dn_list, id);
		}else if((id = PGXCNodeGetNodeId(lfirst_oid(lc), PGXC_NODE_COORDINATOR)) != -1)
		{
			co_list = lappend_int(co_list, id);
		}else
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
				(errmsg("Can not found node for oid '%u'", lfirst_oid(lc)))));
		}
	}
	Assert(list_length(result) == list_length(oid_list));

	if(co_list != NIL || dn_list != NIL)
	{
		List *conns;
		int *fds = PoolManagerGetConnections(dn_list, co_list);
		if(fds == NULL)
		{
			/* this error message copy from pgxcnode.c */
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_RESOURCES),
					 errmsg("Failed to get pooled connections")));
		}

		conns = pg_conn_attach_socket(fds, list_length(dn_list) + list_length(co_list));
		PQNListExecFinish(conns, PQNEFHNormal, NULL);

		foreach(lc,dn_list)
		{
			insert_pgconn_to_htab(lfirst_int(lc), PGXC_NODE_DATANODE, linitial(conns));
			conns = list_delete_first(conns);
		}
		list_free(dn_list);

		foreach(lc, co_list)
		{
			insert_pgconn_to_htab(lfirst_int(lc), PGXC_NODE_COORDINATOR, linitial(conns));
			conns = list_delete_first(conns);
		}
		list_free(co_list);

		Assert(conns == NIL);
	}else
	{
		Assert(list_member_ptr(result, NULL) == false);
		return result;
	}

	forboth(lc, result, lc2, oid_list)
	{
		if(lfirst(lc) == NULL)
		{
			op = hash_search(htab_oid_pgconn, &(lfirst_oid(lc2)), HASH_FIND, NULL);
			Assert(op != NULL);
			lfirst(lc) = op->conn;
		}
	}

	Assert(list_member_ptr(result, NULL) == false);
	return result;
}

static OidPGconn* insert_pgconn_to_htab(int index, char type, PGconn *conn)
{
	OidPGconn *op;
	Oid oid;
	bool found;
	AssertArg(index >= 0 && conn != NULL);

	oid = PGXCNodeGetNodeOid(index, type);
	Assert(OidIsValid(oid));
	op = hash_search(htab_oid_pgconn, &oid, HASH_ENTER, &found);
	Assert(found == false && op->oid == oid);
	op->conn = conn;
	op->type = type;

	return op;
}

static List* pg_conn_attach_socket(int *fds, Size n)
{
	Size i;
	List *list = NIL;

	for(i=0;i<n;++i)
	{
		PGconn *conn = PQbeginAttach(fds[i], NULL, true, PG_PROTOCOL_LATEST);
		if(conn == NULL)
		{
			ListCell *lc;
			PQNListExecFinish(list, PQNEFHNormal, NULL);
			foreach(lc, list)
				PQdetach(lfirst(lc));
			ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
				errmsg("Out of memory")));
		}else
		{
			list = lappend(list, conn);
		}
	}
	return list;
}

bool PQNListExecFinish(List *conn_list, PQNExecFinishHook_function hook, const void *context)
{
	List *list;
	ListCell *lc;
	PGconn *conn;
	struct pollfd *pfds;
	int i,n;
	bool res;

	if(conn_list == NIL)
		return false;

	/* first try got data */
	foreach(lc, conn_list)
	{
		conn = lfirst(lc);
		if(PQNIsConnecting(conn) == 0 &&
			(res = PQNExecFinish(conn, hook, context)) != false)
			return res;
	}

	list = NIL;
	foreach(lc,conn_list)
	{
		conn = lfirst(lc);
		if(PQNIsConnecting(conn) == 0
			&& PQstatus(conn) != CONNECTION_BAD
			&& PQtransactionStatus(conn) != PQTRANS_ACTIVE)
			continue;
		list = lappend(list, conn);
	}
	if(list == NIL)
		return false;

	res = false;
	pfds = palloc(sizeof(pfds[0]) * list_length(list));
	while(list != NIL)
	{
		for(i=0,lc=list_head(list);lc!=NULL;)
		{
			conn = lfirst(lc);
			if((n=PQNIsConnecting(conn)) != 0)
			{
				if(n > 0)
					pfds[i].events = POLLOUT;
				else
					pfds[i].events = POLLIN;
			}else if(PQisCopyInState(conn) && !PQisCopyOutState(conn))
			{
				lc = lnext(lc);
				list = list_delete_ptr(list, conn);
				continue;
			}else
			{
				pfds[i].events = POLLIN;
			}
			pfds[i].fd = PQsocket(conn);
			++i;
			lc = lnext(lc);
		}

re_poll_:
		n = poll(pfds, list_length(list), -1);
		CHECK_FOR_INTERRUPTS();
		if(n < 0)
		{
			if(errno == EINTR)
				goto re_poll_;
			res = (*hook)((void*)context, NULL, PQNHFT_ERROR);
			if(res)
				break;
		}

		/* first consume all socket data */
		for(i=0,lc=list_head(list);lc!=NULL;lc=lnext(lc),++i)
		{
			if(pfds[i].revents != 0)
			{
				conn = lfirst(lc);
				if(PQNIsConnecting(conn))
				{
					PQconnectPoll(conn);
				}else
				{
					PQconsumeInput(conn);
				}
			}
		}

		/* second analyze socket data one by one */
		for(i=0,lc=list_head(list);lc!=NULL;++i)
		{
			if(pfds[i].revents == 0
				|| PQNIsConnecting(lfirst(lc)))
			{
				lc = lnext(lc);
				continue;
			}

			conn = lfirst(lc);
			res = PQNExecFinish(conn, hook, context);
			if(res)
				goto end_loop_;
			if(PQstatus(conn) == CONNECTION_BAD
				|| PQtransactionStatus(conn) != PQTRANS_ACTIVE)
			{
				lc = lnext(lc);
				list = list_delete_ptr(list, conn);
			}else
			{
				lc = lnext(lc);
			}
		}
	}

end_loop_:
	pfree(pfds);
	list_free(list);
	return res;
}

bool PQNEFHNormal(void *context, struct pg_conn *conn, PQNHookFuncType type,...)
{
	if(type ==PQNHFT_ERROR)
		ereport(ERROR, (errmsg("%m")));
	return false;
}

static bool PQNExecFinish(PGconn *conn, PQNExecFinishHook_function hook, const void *context)
{
	const char *buf;
	PGresult *res;
	int n;
	bool hook_res;

re_get_:
	if(PQstatus(conn) == CONNECTION_BAD)
	{
		res = PQgetResult(conn);
		hook_res = (*hook)((void*)context, conn, PQNHFT_RESULT, res);
		PQclear(res);
		if(hook_res)
			return hook_res;
	}else if(PQisCopyOutState(conn))
	{
		n = PQgetCopyDataBuffer(conn, &buf, true);
		if(n > 0)
		{
			hook_res = (*hook)((void*)context, conn, PQNHFT_COPY_OUT_DATA, buf, n);
			if(hook_res)
				return hook_res;
			goto re_get_;
		}else if(n < 0)
		{
			goto re_get_;
		}else if(n == 0)
		{
			return false;
		}
	}else if(PQisCopyInState(conn))
	{
		hook_res = (*hook)((void*)context, conn, PQNHFT_COPY_IN_ONLY);
		if(hook_res)
			return hook_res;
	}else if(PQisBusy(conn) == false)
	{
		res = PQgetResult(conn);
		hook_res = (*hook)((void*)context, conn, PQNHFT_RESULT, res);
		PQclear(res);
		if(hook_res)
			return hook_res;
	}
	return false;
}

/*
 * return 0 for not connectiong
 * <0 for need input
 * >0 for need output
 */
static int PQNIsConnecting(PGconn *conn)
{
	AssertArg(conn);
	switch(PQstatus(conn))
	{
	case CONNECTION_OK:
	case CONNECTION_BAD:
		break;
	case CONNECTION_STARTED:
	case CONNECTION_MADE:
		return 1;
	case CONNECTION_AWAITING_RESPONSE:
	case CONNECTION_AUTH_OK:
		return -1;
	case CONNECTION_SETENV:
		ereport(ERROR, (errcode(ERRCODE_PROTOCOL_VIOLATION),
						errmsg("No support protocol 2.0 version for remote node")));
		break;
	case CONNECTION_SSL_STARTUP:
		return -1;
	case CONNECTION_NEEDED:
		switch(PQconnectPoll(conn))
		{
		case PGRES_POLLING_READING:
			return -1;
		case PGRES_POLLING_WRITING:
			return 1;
		default:
			break;
		}
		break;
	}
	return 0;
}

static void PQNExecFinsh_trouble(PGconn *conn)
{
	PGresult *res;
	for(;;)
	{
		if(PQstatus(conn) == CONNECTION_BAD)
			break;
		if(PQisCopyInState(conn))
			PQputCopyEnd(conn, NULL);
		while(PQisCopyOutState(conn))
		{
			if(PQgetCopyDataBuffer(conn, (const char**)&res, false) < 0)
				break;
		}
		res = PQgetResult(conn);
		if(res)
			PQclear(res);
		else
			break;
	}
}

void PQNReleaseAllConnect(void)
{
	HASH_SEQ_STATUS seq_status;
	OidPGconn *op;
	if(htab_oid_pgconn == NULL || hash_get_num_entries(htab_oid_pgconn) == 0)
		return;

	hash_seq_init(&seq_status, htab_oid_pgconn);
	while((op = hash_seq_search(&seq_status)) != NULL)
	{
		PQNExecFinsh_trouble(op->conn);
		PQdetach(op->conn);
		op->conn = NULL;
	}
	hash_destroy(htab_oid_pgconn);
	htab_oid_pgconn = NULL;
	PoolManagerReleaseConnections(false);
}

void PQNReportResultError(struct pg_result *result, struct pg_conn *conn, int elevel, bool free_result)
{
	AssertArg(result);
	PG_TRY();
	{
		char	   *file_name = PQresultErrorField(result, PG_DIAG_SOURCE_FILE);
		char	   *file_line = PQresultErrorField(result, PG_DIAG_SOURCE_LINE);
		char	   *func_name = PQresultErrorField(result, PG_DIAG_SOURCE_FUNCTION);

		if(errstart(elevel, file_name ? file_name : __FILE__,
			file_line ? atoi(file_line) : __LINE__,
			func_name ? func_name : PG_FUNCNAME_MACRO,
			TEXTDOMAIN))
		{
			const char *str;
			if((str = PQresultErrorField(result, PG_DIAG_SQLSTATE)) != NULL)
				errcode(MAKE_SQLSTATE(str[0], str[1], str[2], str[3], str[4]));
			else
				errcode(ERRCODE_CONNECTION_FAILURE);

			str = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
			if(str == NULL && conn)
				str = PQerrorMessage(conn);
			if(str != NULL)
				errmsg_internal("%s", str);

			str = PQresultErrorField(result, PG_DIAG_NODE_NAME);
			if(str == NULL && conn)
				str = PQparameterStatus(conn, "pgxc_node_name");
			if(str == NULL)
				str = PQNConnectName(conn);
			if(str != NULL);
				errnode(str);

#define GENERIC_ERROR(diag, func)									\
			if((str = PQresultErrorField(result, diag)) != NULL)	\
				func("%s", str)

			GENERIC_ERROR(PG_DIAG_MESSAGE_DETAIL, errdetail_internal);
			GENERIC_ERROR(PG_DIAG_MESSAGE_HINT, errhint);
			GENERIC_ERROR(PG_DIAG_CONTEXT, errcontext);

#undef GENERIC_ERROR
#define GENERIC_ERROR(diag)											\
			if((str = PQresultErrorField(result, diag)) != NULL)	\
				err_generic_string(diag, str)

			GENERIC_ERROR(PG_DIAG_SCHEMA_NAME);
			GENERIC_ERROR(PG_DIAG_TABLE_NAME);
			GENERIC_ERROR(PG_DIAG_COLUMN_NAME);
			GENERIC_ERROR(PG_DIAG_DATATYPE_NAME);
			GENERIC_ERROR(PG_DIAG_CONSTRAINT_NAME);
#undef GENERIC_ERROR

			errfinish(0);
			if (elevel >= ERROR)
				pg_unreachable();
		}
	}PG_CATCH();
	{
		if(free_result)
			PQclear(result);
		PG_RE_THROW();
	}PG_END_TRY();
	if(free_result)
		PQclear(result);
}

extern const char *PQNConnectName(struct pg_conn *conn)
{
	OidPGconn *op;
	HASH_SEQ_STATUS status;
	if(htab_oid_pgconn)
	{
		hash_seq_init(&status, htab_oid_pgconn);
		while((op = hash_seq_search(&status)) != NULL)
		{
			if(op->conn == conn)
			{
				hash_seq_term(&status);
				return PGXCNodeOidGetName(op->oid);
			}
		}
	}
	return NULL;
}