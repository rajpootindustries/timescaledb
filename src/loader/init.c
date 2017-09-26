#include <postgres.h>
#include <pg_config.h>
#include <access/xact.h>
#include <commands/extension.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <utils/inval.h>
#include <parser/analyze.h>
#include <nodes/print.h>


#include "extension.h"

/*
 * Some notes on design:
 *
 * We do not check for the installation of the extension upon loading the extension and instead rely on a hook for two reasons:
 * 1) We probably can't
 *	- The shared_preload_libraries is called in PostmasterMain which is way before InitPostgres is called.
 *			(Note: This happens even before the fork of the backend)
 *	-- This means we cannot query for the existance of the extension yet because the caches are initialized in InitPostgres.
 * 2) We actually don't want to load the extension in two cases:
 *	  a) We are upgrading the extension.
 *	  b) We set the guc timescaledb.disable_load.
 *
 *
 */

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define GUC_DISABLE_LOAD_NAME "timescaledb.disable_load"

extern void _PG_init(void);
extern void _PG_fini(void);

bool		guc_disable_load = false;
post_parse_analyze_hook_type prev_post_parse_analyze_hook;

static void
inval_cache_callback(Datum arg, Oid relid)
{
	if (guc_disable_load)
		return;
	extension_check();
}

static void
post_analyze_hook(ParseState *pstate, Query *query)
{
	if (guc_disable_load)
		return;

	/* Don't do a load if setting timescaledb.disable_load or doing an update */
	if (query->commandType == CMD_UTILITY)
	{
		if (IsA(query->utilityStmt, VariableSetStmt))
		{
			VariableSetStmt *stmt = (VariableSetStmt *) query->utilityStmt;

			if (strcmp(stmt->name, GUC_DISABLE_LOAD_NAME) == 0)
			{
				return;
			}
		}
		else if (IsA(query->utilityStmt, AlterExtensionStmt))
		{
			AlterExtensionStmt *stmt = (AlterExtensionStmt *) query->utilityStmt;

			if (strcmp(stmt->extname, EXTENSION_NAME) == 0)
			{
				if (extension_loaded())
				{
					ereport(ERROR,
							(errmsg("Cannot update the extension after the old version has already been loaded"),
							 errhint("You should start a new session and execute ALTER EXTENSION as the first command")));
				}

				return;
			}
		}
	}
	extension_check();

	if (prev_post_parse_analyze_hook != NULL)
	{
		prev_post_parse_analyze_hook(pstate, query);
	}
}

void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		/* cannot use GUC variable here since extension not yet loaded */
		char	   *allow_install_without_preload = GetConfigOptionByName("timescaledb.allow_install_without_preload", NULL, true);

		if (allow_install_without_preload == NULL ||
			strcmp(allow_install_without_preload, "on") != 0)
		{
			char	   *config_file = GetConfigOptionByName("config_file", NULL, false);

			ereport(ERROR,
					(errmsg("The timescaledb library is not preloaded"),
					 errhint("Please preload the timescaledb library via shared_preload_libraries.\n\n"
					 "This can be done by editing the config file at: %1$s\n"
							 "and adding 'timescaledb' to the list in the shared_preload_libraries config.\n"
							 "	# Modify postgresql.conf:\n	shared_preload_libraries = 'timescaledb'\n\n"
							 "Another way to do this, if not preloading other libraries, is with the command:\n"
							 "	echo \"shared_preload_libraries = 'timescaledb'\" >> %1$s \n\n"
							 "(Will require a database restart.)\n\n"
							 "If you REALLY know what you are doing and would like to load the library without preloading, you can disable this check with: \n"
							 "	SET timescaledb.allow_install_without_preload = 'on';", config_file)));
			return;
		}
	}
	elog(INFO, "timescaledb loaded");

	/* This is a safety-valve variable to prevent loading the full extension */
	DefineCustomBoolVariable(GUC_DISABLE_LOAD_NAME, "Disable the loading of the actual extension",
							 NULL,
							 &guc_disable_load,
							 false,
							 PGC_USERSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	/* cannot check for extension here since not inside a transaction yet */

	CacheRegisterRelcacheCallback(inval_cache_callback, PointerGetDatum(NULL));

	/*
	 * using the post_parse_analyze_hook since it's the earliest available
	 * hook
	 */
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = post_analyze_hook;
}

void
_PG_fini(void)
{
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
	/* No way to unregister relcache callback */
}
