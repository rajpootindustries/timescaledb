#include <postgres.h>
#include <access/xact.h>
#include <commands/extension.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <miscadmin.h>

#include "extension.h"
#include "load.h"

#define EXTENSION_PROXY_TABLE "cache_inval_extension"
#define CACHE_SCHEMA_NAME "_timescaledb_cache"

static bool loaded = false;

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
		Oid			extension_oid = get_extension_oid(EXTENSION_NAME, true);

		if (!OidIsValid(extension_oid))
		{
			/* be conservative */
			return true;
		}

		if (extension_oid == CurrentExtensionObject)
			return true;
	}
	return false;
}

void		inline
extension_check()
{
	if (!loaded)
	{
		if (IsNormalProcessingMode() && IsTransactionState() && !extension_is_transitioning() && proxy_table_exists() && extension_exists())
		{
			load_extension();
			loaded = true;
		}
	}
}

bool		inline
extension_loaded()
{
	return loaded;
}
