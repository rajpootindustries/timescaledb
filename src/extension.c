#include <postgres.h>
#include <access/xact.h>
#include <access/transam.h>
#include <commands/event_trigger.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <utils/inval.h>

#include "compat-msvc-enter.h"	/* To label externs in extension.h and
								 * miscadmin.h correctly */
#include <commands/extension.h>
#include <miscadmin.h>
#include "compat-msvc-exit.h"

#include <access/relscan.h>
#include <catalog/indexing.h>
#include <catalog/pg_extension.h>
#include <utils/builtins.h>
#include <utils/fmgroids.h>

#include "catalog.h"
#include "extension.h"
#include "guc.h"
#include "version.h"
#include "extension_utils.c"

#define EXTENSION_PROXY_TABLE "cache_inval_extension"

static Oid	extension_proxy_oid = InvalidOid;

/*
 * ExtensionState tracks the state of extension metadata in the backend.
 *
 * Since we want to cache extension metadata to speed up common checks (e.g.,
 * check for presence of the extension itself), we also need to track the
 * extension state to know when the metadata is valid.
 *
 * We use a proxy_table to be notified of extension drops/creates. Namely,
 * we rely on the fact that postgres will internally create RelCacheInvalidation
 * events when any tables are created or dropped. We rely on the following properties
 * of Postgres's dependency managment:
 *	* The proxy table will be created before the extension itself.
 *	* The proxy table will be dropped before the extension itself.
 */

static enum ExtensionState extstate = EXTENSION_STATE_UNKNOWN;

void
extension_check_version(const char *so_version)
{
	char	   *sql_version;

	if (!IsNormalProcessingMode() || !IsTransactionState())
		return;

	sql_version = extension_version();

	if (strcmp(sql_version, so_version) != 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Mismatched timescaledb version. Shared object file %s, SQL %s", so_version, sql_version)));
	}
}

/* Sets a new state, returning whether the state has changed */
static bool
extension_set_state(enum ExtensionState newstate)
{
	if (newstate == extstate)
	{
		return false;
	}
	switch (newstate)
	{
		case EXTENSION_STATE_TRANSITIONING:
		case EXTENSION_STATE_UNKNOWN:
			break;
		case EXTENSION_STATE_CREATED:
			extension_check_version(TIMESCALEDB_VERSION_MOD);
			extension_proxy_oid = get_relname_relid(EXTENSION_PROXY_TABLE, get_namespace_oid(CACHE_SCHEMA_NAME, false));
			catalog_reset();
			break;
		case EXTENSION_STATE_NOT_INSTALLED:
			extension_proxy_oid = InvalidOid;
			catalog_reset();
			break;
	}
	extstate = newstate;
	return true;
}

/* Updates the state based on the current state, returning whether there had been a change. */
static bool
extension_update_state()
{
	return extension_set_state(extension_current_state());
}

/*
 *	Called upon all Relcache invalidate events.
 *	Returns whether or not to invalidate the entire extension.
 */
bool
extension_invalidate(Oid relid)
{
	switch (extstate)
	{
		case EXTENSION_STATE_NOT_INSTALLED:
			/* This event may mean we just added the proxy table */
		case EXTENSION_STATE_UNKNOWN:
			/* Can we recompute the state now? */
		case EXTENSION_STATE_TRANSITIONING:
			/* Has the create/drop extension finished? */
			extension_update_state();
			return false;
		case EXTENSION_STATE_CREATED:

			/*
			 * Here we know the proxy table oid so only listen to potential
			 * drops on that oid. Note that an invalid oid passed in the
			 * invalidation event applies to all tables.
			 */
			if (extension_proxy_oid == relid || !OidIsValid(relid))
			{
				extension_update_state();
				if (EXTENSION_STATE_CREATED != extstate)
				{
					/*
					 * note this state may be UNKNOWN but should be
					 * conservative
					 */
					return true;
				}
			}
			return false;
		default:
			elog(ERROR, "unknown state: %d", extstate);
			return false;
	}
}

bool
extension_is_loaded(void)
{
	/* when restoring deactivate extension */
	if (guc_restoring)
		return false;

	if (EXTENSION_STATE_UNKNOWN == extstate || EXTENSION_STATE_TRANSITIONING == extstate)
	{
		/* status may have updated without a relcache invalidate event */
		extension_update_state();
	}

	switch (extstate)
	{
		case EXTENSION_STATE_CREATED:
			return true;
		case EXTENSION_STATE_NOT_INSTALLED:
		case EXTENSION_STATE_UNKNOWN:
		case EXTENSION_STATE_TRANSITIONING:

			/*
			 * Turn off extension during upgrade scripts. This is necessary so
			 * that, for example, the catalog does not go looking for things
			 * that aren't yet there.
			 */
			return false;
		default:
			elog(ERROR, "unknown state: %d", extstate);
			return false;
	}
}
