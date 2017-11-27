#include <postgres.h>
#include <access/xact.h>
#include <commands/extension.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <miscadmin.h>
#include <parser/analyze.h>
#include <commands/extension.h>
#include <access/relscan.h>
#include <catalog/pg_extension.h>
#include <utils/fmgroids.h>
#include <utils/builtins.h>
#include <utils/rel.h>
#include <catalog/indexing.h>

#include "extension.h"

#define EXTENSION_PROXY_TABLE "cache_inval_extension"
#define CACHE_SCHEMA_NAME "_timescaledb_cache"
#define MAX_SO_NAME_LEN NAMEDATALEN+NAMEDATALEN+1+1		/* extname+"-"+version */

static bool loaded = false;

/* This is the extension's post_parse_analyze_hook */
static post_parse_analyze_hook_type extension_post_parse_analyze_hook = NULL;


static bool inline
proxy_table_exists()
{
	Oid			nsid = get_namespace_oid(CACHE_SCHEMA_NAME, true);
	Oid			proxy_table = get_relname_relid(EXTENSION_PROXY_TABLE, nsid);

	return OidIsValid(proxy_table);
}

static bool inline
extension_exists()
{
	return OidIsValid(get_extension_oid(EXTENSION_NAME, true));
}

static bool inline
extension_is_transitioning()
{
	/*
	 * Determine whether the extension is being created or upgraded (as a
	 * misnomer creating_extension is true during upgrades)
	 */
	if (creating_extension)
	{
		char	   *current_extension_name = get_extension_name(CurrentExtensionObject);

		if (NULL == current_extension_name)
		{
			elog(ERROR, "Unknown current extension while creating");
		}

		if (strcmp(EXTENSION_NAME, current_extension_name) == 0)
		{
			return true;
		}
	}
	return false;
}

static char *
extension_version(void)
{
	Datum		result;
	Relation	rel;
	SysScanDesc scandesc;
	HeapTuple	tuple;
	ScanKeyData entry[1];
	bool		is_null = true;
	static char *sql_version = NULL;

	rel = heap_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				Anum_pg_extension_extname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(EXTENSION_NAME));

	scandesc = systable_beginscan(rel, ExtensionNameIndexId, true,
								  NULL, 1, entry);

	tuple = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
	{
		result = heap_getattr(tuple, Anum_pg_extension_extversion, RelationGetDescr(rel), &is_null);

		if (!is_null)
		{
			sql_version = strdup(TextDatumGetCString(result));
		}
	}

	systable_endscan(scandesc);
	heap_close(rel, AccessShareLock);

	if (sql_version == NULL)
	{
		elog(ERROR, "Extension not found when getting version");
	}
	return sql_version;
}

static void inline
do_load()
{
	char	   *version = extension_version();
	char		soname[MAX_SO_NAME_LEN];
	post_parse_analyze_hook_type old_hook;


	snprintf(soname, MAX_SO_NAME_LEN, "%s-%s", EXTENSION_NAME, version);
	/*
	 * we need to capture the extension's analyzed post analyze hook, giving
	 * it a NULL as previous
	 */
	old_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = NULL;

	PG_TRY();
	{
		load_file(soname, false);
		loaded = true;
	}
	PG_CATCH();
	{
		/* Assume the extension was loaded to prevent re-loading another .so */
		loaded = true;
		
		extension_post_parse_analyze_hook = post_parse_analyze_hook;
		post_parse_analyze_hook = old_hook;
		PG_RE_THROW();
	}
	PG_END_TRY();
	
	extension_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = old_hook;
}

void		inline
extension_check()
{
	if (!loaded)
	{
		if (!IsNormalProcessingMode() || !IsTransactionState())
			return;

		if (extension_is_transitioning())
		{
			/*
			 * Always load as soon as the extension is transitioning. This is
			 * necessary so that the extension load before any CREATE FUNCTION
			 * calls. Otherwise, the CREATE FUNCTION calls will load the .so
			 * without capturing the post_parse_analyze_hook NOTE: do not
			 * check for proxy_table_exists here. Want to load even before
			 * proxy_table created.
			 */
			do_load();
			return;
		}

		if (proxy_table_exists() && extension_exists())
		{
			/*
			 * proxy_table will not exists when dropping extension -- so that
			 * protects loading while dropping extension. also note
			 * proxy_table_exists using syscache while extension_exists is not
			 * cached TODO: check that syscache has negative entries
			 */
			do_load();
			return;
		}
	}
}

void
call_extension_post_parse_analyze_hook(ParseState *pstate,
									   Query *query)
{
	if (loaded && extension_post_parse_analyze_hook != NULL)
	{
		extension_post_parse_analyze_hook(pstate, query);
	}
}


bool		inline
extension_loaded()
{
	return loaded;
}
