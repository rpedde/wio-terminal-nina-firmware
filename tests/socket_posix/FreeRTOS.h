/* Test-only OS adapter: compile the production backend against BSD sockets. */
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#define portMAX_DELAY 0
#define portTICK_PERIOD_MS 1
