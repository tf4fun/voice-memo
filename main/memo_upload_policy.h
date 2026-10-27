#pragma once

#include <stdbool.h>
#include <stdint.h>

/* At most three rejected attempts in total. Transport failures do not
 * consume this budget: an offline recording must remain retryable. */
#define MEMO_UPLOAD_REJECTION_ATTEMPTS 3

typedef struct {
    uint8_t rejections;
} memo_upload_policy_t;

static inline bool memo_upload_policy_rejected(memo_upload_policy_t *policy)
{
    if (policy->rejections < MEMO_UPLOAD_REJECTION_ATTEMPTS) policy->rejections++;
    return policy->rejections == MEMO_UPLOAD_REJECTION_ATTEMPTS;
}
