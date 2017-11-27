#ifndef TIMESCALEDB_EXTENSION_H
#define TIMESCALEDB_EXTENSION_H
#include <parser/analyze.h>

#define EXTENSION_NAME "timescaledb"


extern void extension_check(void);
extern bool extension_loaded(void);
extern void call_extension_post_parse_analyze_hook(ParseState *pstate,
									   Query *query);

#endif   /* TIMESCALEDB_EXTENSION_H */
