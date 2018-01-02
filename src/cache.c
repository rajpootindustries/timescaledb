#include <postgres.h>
#include <access/xact.h>

#include "cache.h"

/* List of pinned caches. A cache occurs once in this list for every pin
 * taken */
static List *pinned_caches = NIL;

void
cache_init(Cache *cache)
{
	if (cache->htab != NULL)
	{
		elog(ERROR, "Cache %s is already initialized", cache->name);
		return;
	}

	/*
	 * The cache object should have been created in its own context so that
	 * cache_destroy can just delete the context to free everything.
	 */
	Assert(MemoryContextContains(cache_memory_ctx(cache), cache));

	cache->htab = hash_create(cache->name, cache->numelements,
							  &cache->hctl, cache->flags);
	cache->refcount = 1;
	cache->release_on_commit = true;
}

static void
cache_destroy(Cache *cache)
{
	if (cache->refcount > 0)
	{
		/* will be destroyed later */
		return;
	}

	if (cache->pre_destroy_hook != NULL)
		cache->pre_destroy_hook(cache);

	hash_destroy(cache->htab);
	MemoryContextDelete(cache->hctl.hcxt);
}

void
cache_invalidate(Cache *cache)
{
	if (cache == NULL)
		return;
	cache->refcount--;
	cache_destroy(cache);
}

/*
 * Pinning is needed if any items returned by the cache may need to survive
 * invalidation events (i.e. AcceptInvalidationMessages() may be called).
 *
 * Invalidation messages may be processed on any internal function that takes a
 * lock (e.g. heap_open).
 *
 * Each call to cache_pin MUST BE paired with a call to cache_release.
 *
 */
extern Cache *
cache_pin(Cache *cache)
{
	MemoryContext old = MemoryContextSwitchTo(CacheMemoryContext);

	pinned_caches = lappend(pinned_caches, cache);
	MemoryContextSwitchTo(old);
	cache->refcount++;
	return cache;
}

extern int
cache_release(Cache *cache)
{
	int			refcount = cache->refcount - 1;

	Assert(cache->refcount > 0);
	cache->refcount--;
	pinned_caches = list_delete_ptr(pinned_caches, cache);
	cache_destroy(cache);

	return refcount;
}


MemoryContext
cache_memory_ctx(Cache *cache)
{
	return cache->hctl.hcxt;
}

MemoryContext
cache_switch_to_memory_context(Cache *cache)
{
	return MemoryContextSwitchTo(cache->hctl.hcxt);
}

void *
cache_fetch(Cache *cache, CacheQuery *query)
{
	bool		found;
	HASHACTION	action = cache->create_entry == NULL ? HASH_FIND : HASH_ENTER;

	if (cache->htab == NULL)
	{
		elog(ERROR, "Hash %s not initialized", cache->name);
	}

	query->result = hash_search(cache->htab, cache->get_key(query), action, &found);

	if (found)
	{
		cache->stats.hits++;

		if (cache->update_entry != NULL)
		{
			MemoryContext old = cache_switch_to_memory_context(cache);

			query->result = cache->update_entry(cache, query);
			MemoryContextSwitchTo(old);
		}
	}
	else
	{
		cache->stats.misses++;

		if (cache->create_entry != NULL)
		{
			MemoryContext old = cache_switch_to_memory_context(cache);

			query->result = cache->create_entry(cache, query);
			MemoryContextSwitchTo(old);
			cache->stats.numelements++;
		}
	}

	return query->result;
}

bool
cache_remove(Cache *cache, void *key)
{
	bool		found;

	hash_search(cache->htab, key, HASH_REMOVE, &found);

	if (found)
		cache->stats.numelements--;

	return found;
}


static void
relase_all_pinned_caches()
{
	ListCell   *lc;

	/*
	 * release once for every occurence of a cache in the pinned caches list.
	 * On abort, release irrespective of cache->release_on_commit.
	 */
	foreach(lc, pinned_caches)
	{
		Cache	   *cache = lfirst(lc);

		cache->refcount--;
		cache_destroy(cache);
	}
	list_free(pinned_caches);
	pinned_caches = NIL;
}

/*
 * Transaction end callback that cleans up any pinned caches. This is a
 * safeguard that protects against indefinitely pinned caches (memory leaks)
 * that may occur if a transaction ends (normally or abnormally) while a pin is
 * held. Without this, a cache_pin() call always needs to be paired with a
 * cache_release() call and wrapped in a PG_TRY() block to capture and handle
 * any exceptions that occur.
 *
 * Note that this checks that cache_release() is always called by the end
 * of a non-aborted transaction unless cache->release_on_commit is set to true.
 * */
static void
cache_xact_end(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_ABORT:
		case XACT_EVENT_PARALLEL_ABORT:
			relase_all_pinned_caches();
		default:
			{
				ListCell   *lc;

				/*
				 * Only caches left should be marked as non-released
				 */
				foreach(lc, pinned_caches)
				{
					Cache	   *cache = lfirst(lc);

					/*
					 * This assert makes sure that that we don't have a cache
					 * leak when running with debugging
					 */
					Assert(!cache->release_on_commit);

					/*
					 * This may still happen in production, in which case,
					 * release
					 */
					if (cache->release_on_commit)
						cache_release(cache);
				}
			}
			break;
	}
}

static void
cache_subxact_abort(SubXactEvent event, SubTransactionId mySubid,
					SubTransactionId parentSubid, void *arg)
{
	/*
	 * Note that cache->release_on_commit is irrelevant here since can't have
	 * cross-commit operations in subtxns
	 */

	/*
	 * In subtxns, caches should have already been released, unless an abort
	 * happened
	 */
	Assert(SUBXACT_EVENT_ABORT_SUB == event || list_length(pinned_caches) == 0);
	relase_all_pinned_caches();
}


void
_cache_init(void)
{
	RegisterXactCallback(cache_xact_end, NULL);
	RegisterSubXactCallback(cache_subxact_abort, NULL);
}

void
_cache_fini(void)
{
	UnregisterXactCallback(cache_xact_end, NULL);
	UnregisterSubXactCallback(cache_subxact_abort, NULL);
}
