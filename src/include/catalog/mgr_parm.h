
#ifndef MGR_PARM_H
#define MGR_PARM_H

#ifdef BUILD_BKI
#include "catalog/buildbki.h"
#else /* BUILD_BKI */
#include "catalog/genbki.h"
#endif /* BUILD_BKI */


#define ParmRelationId 4928

CATALOG(mgr_parm,4928)
{
	char		parmnodetype;		/* parm type:c/d/g/'*' for all/'#' for datanode and coordinator*/
	NameData	parmname;			/* parm name */
#ifdef CATALOG_VARLEN
	text		parmunit;
	text		parmcontext;		/*backend, user, internal, postmaster, superuser, sighup*/
	text		parmvartype;		/*bool, enum, string, integer, real*/	
	text		parmminval;			/* parm comment */
	text		parmmaxval;
#endif								/* CATALOG_VARLEN */
} FormData_mgr_parm;

/* ----------------
 *		Form_mgr_parm corresponds to a pointer to a tuple with
 *		the format of mgr_parm relation.
 * ----------------
 */
typedef FormData_mgr_parm *Form_mgr_parm;

/* ----------------
 *		compiler constants for mgr_parm
 * ----------------
 */
#define Natts_mgr_parm				7
#define Anum_mgr_parm_nodetype		1
#define Anum_mgr_parm_name			2
#define Anum_mgr_parm_unit			3
#define Anum_mgr_parm_context		4
#define Anum_mgr_parm_vartype		5
#define Anum_mgr_parm_minval		6
#define Anum_mgr_parm_maxval		7
#endif /* MGR_GTM_H */
