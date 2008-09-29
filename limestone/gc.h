#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H

#include <apr_pools.h>
#include <http_core.h>
#include "dav_repos.h"

int dav_repos_garbage_collector(apr_pool_t *p, dav_repos_db *db);

#endif /* GARBAGE_COLLECTOR_H */
