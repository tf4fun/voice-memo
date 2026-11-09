#pragma once
#include <stddef.h>
typedef struct cJSON {
    const char *key; char *valuestring; double valuedouble; int type;
    struct cJSON *child, *next;
} cJSON;
cJSON *cJSON_Parse(const char *text);
void cJSON_Delete(cJSON *root);
const cJSON *cJSON_GetObjectItem(const cJSON *root, const char *key);
static inline int cJSON_IsString(const cJSON *n) { return n && n->type == 1; }
static inline int cJSON_IsNumber(const cJSON *n) { return n && n->type == 2; }
static inline int cJSON_IsArray(const cJSON *n) { return n && n->type == 3; }
#define cJSON_ArrayForEach(it, array) for ((it) = (array) ? (array)->child : NULL; (it); (it) = (it)->next)
