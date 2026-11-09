#include <assert.h>
#include <stdio.h>
#include "memo_handoff.h"
static int suspend_error;
static int events_error, upload_error, provision_error, stop_error, resume_error;
static int events, uploads, released, suspended, starts, stops, resumes;
static int stop_events(void *ctx) { (void)ctx; events++; return events_error; }
static int stop_upload(void *ctx) { (void)ctx; uploads++; return upload_error; }
static void release(void *ctx) { (void)ctx; assert(events && uploads && !events_error && !upload_error); released++; }
static int suspend(void *ctx) { (void)ctx; assert(released); suspended++; return suspend_error; }
static int start(void *ctx) { (void)ctx; assert(suspended); starts++; return provision_error; }
static int stop(void *ctx) { (void)ctx; stops++; return stop_error; }
static int resume(void *ctx) { (void)ctx; assert(!stop_error); resumes++; return resume_error; }
int main(void)
{
    memo_handoff_t handoff={0};
    memo_handoff_ops_t ops={stop_events,stop_upload,release,suspend,start,stop,resume,NULL};
    events_error=1;
    assert(!memo_handoff_begin(&handoff,&ops) && !released && !suspended && !starts);
    assert(events==1 && uploads==1 && resumes==1 && handoff.phase==MEMO_HANDOFF_NORMAL);
    events_error=0;
    assert(memo_handoff_begin(&handoff,&ops) && starts==1);
    assert(!memo_handoff_begin(&handoff,&ops) && starts==1);
    stop_error=1;
    assert(!memo_handoff_end(&handoff,&ops) && handoff.phase==MEMO_HANDOFF_EXITING && resumes==1);
    assert(!memo_handoff_poll(&handoff,&ops) && resumes==1);
    stop_error=0; resume_error=1;
    assert(!memo_handoff_poll(&handoff,&ops) && handoff.phase==MEMO_HANDOFF_RESUMING);
    int stop_count=stops;
    resume_error=0;
    assert(memo_handoff_poll(&handoff,&ops) && stops==stop_count);
    provision_error=1;
    assert(!memo_handoff_begin(&handoff,&ops) && handoff.phase==MEMO_HANDOFF_NORMAL);
    assert(starts==2 && stops==stop_count+1 && resumes==4);
    provision_error=0;suspend_error=1;resume_error=1;
    int started=starts, stopped=stops;
    assert(!memo_handoff_begin(&handoff,&ops) && handoff.phase==MEMO_HANDOFF_RESUMING);
    assert(starts==started && stops==stopped); // BLE never acquired the failed driver.
    assert(!memo_handoff_begin(&handoff,&ops) && starts==started);
    assert(!memo_handoff_poll(&handoff,&ops) && starts==started);
    suspend_error=resume_error=0;
    assert(memo_handoff_poll(&handoff,&ops) && handoff.phase==MEMO_HANDOFF_NORMAL);
    assert(memo_handoff_begin(&handoff,&ops) && starts==started+1);
    assert(memo_handoff_end(&handoff,&ops));
    puts("memo provisioning ownership, rollback, deferred exit and restart: PASS");
}
