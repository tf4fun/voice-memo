#pragma once
#include <stddef.h>
typedef struct { char *valuestring; double valuedouble; } cJSON;
/* Lifecycle tests send comments only. JSON/cache behaviour has separate coverage. */
static inline cJSON *cJSON_Parse(const char *json) { (void)json; return NULL; }
static inline const cJSON *cJSON_GetObjectItem(const cJSON *root, const char *key)
{ (void)root; (void)key; return NULL; }
static inline int cJSON_IsString(const cJSON *item) { return item != NULL; }
static inline int cJSON_IsNumber(const cJSON *item) { return item != NULL; }
static inline void cJSON_Delete(cJSON *item) { (void)item; }
