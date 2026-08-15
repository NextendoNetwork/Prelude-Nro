#ifndef NEXTENDO_TIME_H
#define NEXTENDO_TIME_H

#include <switch.h>

// Synchronizes the Nintendo Switch system time with time.cloudflare.com.
// Returns 0 on success, or a libnx Result code on failure.
Result nextendo_time_sync(void);

#endif // NEXTENDO_TIME_H
