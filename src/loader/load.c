#include <postgres.h>
#include <commands/extension.h>
#include <access/relscan.h>
#include <catalog/pg_extension.h>
#include <utils/fmgroids.h>
#include <utils/builtins.h>
#include <utils/rel.h>
#include <catalog/indexing.h>

#include "load.h"
#include "extension.h"

#define MAX_SO_NAME_LEN NAMEDATALEN+NAMEDATALEN+1 /* extname+"-"+version*/

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

void
load_extension(void)
{
	char	   *version = extension_version();
	char		soname[MAX_SO_NAME_LEN];

	snprintf(soname, MAX_SO_NAME_LEN, "%s-%s", EXTENSION_NAME, version);

	load_file(soname, false);
}

void
unload_extension(void)
{
}
