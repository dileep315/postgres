/*-------------------------------------------------------------------------
 *
 * proclang.c
 *	  PostgreSQL PROCEDURAL LANGUAGE support code.
 *
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $Header: /cvsroot/pgsql/src/backend/commands/proclang.c,v 1.30 2002/04/09 20:35:48 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>

#include "access/heapam.h"
#include "catalog/catname.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_language.h"
#include "catalog/pg_proc.h"
#include "commands/proclang.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"


/*
 * Translate the input language name to lower case.
 */
static void
case_translate_language_name(const char *input, char *output)
{
	int			i;

	for (i = 0; i < NAMEDATALEN && input[i]; ++i)
		output[i] = tolower((unsigned char) input[i]);

	output[i] = '\0';
}


/* ---------------------------------------------------------------------
 * CREATE PROCEDURAL LANGUAGE
 * ---------------------------------------------------------------------
 */
void
CreateProceduralLanguage(CreatePLangStmt *stmt)
{
	char		languageName[NAMEDATALEN];
	Oid			procOid;
	Oid			typev[FUNC_MAX_ARGS];
	char		nulls[Natts_pg_language];
	Datum		values[Natts_pg_language];
	Relation	rel;
	HeapTuple	tup;
	TupleDesc	tupDesc;
	int			i;

	/*
	 * Check permission
	 */
	if (!superuser())
		elog(ERROR, "Only users with Postgres superuser privilege are "
			 "permitted to create procedural languages");

	/*
	 * Translate the language name and check that this language doesn't
	 * already exist
	 */
	case_translate_language_name(stmt->plname, languageName);

	if (SearchSysCacheExists(LANGNAME,
							 PointerGetDatum(languageName),
							 0, 0, 0))
		elog(ERROR, "Language %s already exists", languageName);

	/*
	 * Lookup the PL handler function and check that it is of return type
	 * Opaque
	 */
	MemSet(typev, 0, sizeof(typev));
	procOid = LookupFuncName(stmt->plhandler, 0, typev);
	if (!OidIsValid(procOid))
		elog(ERROR, "PL handler function %s() doesn't exist",
			 NameListToString(stmt->plhandler));
	if (get_func_rettype(procOid) != InvalidOid)
		elog(ERROR, "PL handler function %s() isn't of return type Opaque",
			 NameListToString(stmt->plhandler));

	/*
	 * Insert the new language into pg_language
	 */
	for (i = 0; i < Natts_pg_language; i++)
	{
		nulls[i] = ' ';
		values[i] = (Datum) NULL;
	}

	i = 0;
	values[i++] = PointerGetDatum(languageName);
	values[i++] = BoolGetDatum(true);	/* lanispl */
	values[i++] = BoolGetDatum(stmt->pltrusted);
	values[i++] = ObjectIdGetDatum(procOid);
	values[i++] = DirectFunctionCall1(textin,
									  CStringGetDatum(stmt->plcompiler));
	nulls[i] = 'n';				/* lanacl */

	rel = heap_openr(LanguageRelationName, RowExclusiveLock);

	tupDesc = rel->rd_att;
	tup = heap_formtuple(tupDesc, values, nulls);

	heap_insert(rel, tup);

	if (RelationGetForm(rel)->relhasindex)
	{
		Relation	idescs[Num_pg_language_indices];

		CatalogOpenIndices(Num_pg_language_indices, Name_pg_language_indices, idescs);
		CatalogIndexInsert(idescs, Num_pg_language_indices, rel, tup);
		CatalogCloseIndices(Num_pg_language_indices, idescs);
	}

	heap_close(rel, RowExclusiveLock);
}


/* ---------------------------------------------------------------------
 * DROP PROCEDURAL LANGUAGE
 * ---------------------------------------------------------------------
 */
void
DropProceduralLanguage(DropPLangStmt *stmt)
{
	char		languageName[NAMEDATALEN];
	HeapTuple	langTup;
	Relation	rel;

	/*
	 * Check permission
	 */
	if (!superuser())
		elog(ERROR, "Only users with Postgres superuser privilege are "
			 "permitted to drop procedural languages");

	/*
	 * Translate the language name, check that this language exist and is
	 * a PL
	 */
	case_translate_language_name(stmt->plname, languageName);

	rel = heap_openr(LanguageRelationName, RowExclusiveLock);

	langTup = SearchSysCacheCopy(LANGNAME,
								 PointerGetDatum(languageName),
								 0, 0, 0);
	if (!HeapTupleIsValid(langTup))
		elog(ERROR, "Language %s doesn't exist", languageName);

	if (!((Form_pg_language) GETSTRUCT(langTup))->lanispl)
		elog(ERROR, "Language %s isn't a created procedural language",
			 languageName);

	simple_heap_delete(rel, &langTup->t_self);

	heap_freetuple(langTup);
	heap_close(rel, RowExclusiveLock);
}
