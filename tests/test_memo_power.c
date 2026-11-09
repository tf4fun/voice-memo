#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "memo_power.h"

static char trace[80];
static int count, fault, safety_checks;
static int step(char name) { trace[count++] = name; trace[count] = '\0'; return fault == name; }
static int stop_events(void *p) { (void)p; return step('E'); }
static int stop_upload(void *p) { (void)p; return step('U'); }
static int stop_player(void *p) { (void)p; return step('P'); }
static bool safe(void *p) { (void)p; safety_checks++; step('S'); return fault != 'S' && !(fault == '2' && safety_checks == 2); }
static bool lock(void *p) { (void)p; return !step('L'); }
static int arm(void *p) { (void)p; return step('W'); }
static void unlock(void *p) { (void)p; step('l'); }
static void recover(void *p) { (void)p; step('R'); }
static void network(void *p) { (void)p; step('N'); }
static int buttons(void *p) { (void)p; return step('K'); }
static int battery(void *p) { (void)p; return step('B'); }
static int audio(void *p) { (void)p; return step('A'); }
static int audio_pins(void *p) { (void)p; return step('a'); }
static int i2c(void *p) { (void)p; return step('i'); }
static int display(void *p) { (void)p; return step('D'); }
static void sleep_now(void *p) { (void)p; step('Z'); }
static void restart(void *p) { (void)p; step('X'); }
static const memo_power_ops_t ops = {
    .stop_events=stop_events, .stop_upload=stop_upload, .stop_player=stop_player,
    .safe=safe, .lock_display=lock, .arm_wake=arm, .unlock_display=unlock,
    .recover_workers=recover, .release_network=network, .release_buttons=buttons,
    .battery_sleep=battery, .audio_sleep=audio, .audio_pins=audio_pins,
    .i2c_pins=i2c, .display_sleep=display, .sleep=sleep_now, .restart=restart,
};
static void run(int failure, const char *expected)
{
    count=safety_checks=0; trace[0]='\0'; fault=failure;
    memo_power_shutdown(&ops); assert(strcmp(trace, expected)==0);
}
int main(void)
{
    memo_power_t p={.activity_ms=1000};
    assert(memo_power_poll(&p,30999,30,5,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,31000,30,5,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_key(&p,32000,true,false)); // Wake press never records.
    assert(!p.blank);
    assert(memo_power_key(&p,32500,false,false)); // LONG
    assert(memo_power_key(&p,32600,false,true)); // RELEASE
    assert(memo_power_key(&p,32600,false,false)); // trailing CLICK
    assert(!memo_power_key(&p,33000,true,false)); // New gesture operates normally.
    assert(!memo_power_key(&p,33100,false,true));
    assert(memo_power_poll(&p,633100,30,5,true,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,663099,30,5,false,true)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,663100,30,5,false,true)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,1233100,30,5,false,true)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,1233100,30,5,false,false)==MEMO_POWER_SLEEP);
    assert(memo_power_poll(&p,1233100,0,0,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,1233100,30,0,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,1233100,0,5,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,1,30,5,false,false)==MEMO_POWER_ON); // Clock reset.
    assert(p.activity_ms==1);
    p=(memo_power_t){0};
    assert(memo_power_poll(&p,29999,30,5,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,30000,30,5,false,false)==MEMO_POWER_BLANK);
    assert(p.blank_since_ms==30000);
    assert(memo_power_poll(&p,329999,30,5,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,330000,30,5,false,false)==MEMO_POWER_SLEEP);
    // A shorter sleep setting cannot bypass the display-on interval.
    p=(memo_power_t){0};
    assert(memo_power_poll(&p,179999,180,1,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,180000,180,1,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,239999,180,1,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,240000,180,1,false,false)==MEMO_POWER_SLEEP);
    // Delayed polling starts the blank interval when the display really turns off.
    p=(memo_power_t){0};
    assert(memo_power_poll(&p,900000,30,1,false,false)==MEMO_POWER_BLANK);
    assert(p.blank_since_ms==900000);
    assert(memo_power_poll(&p,959999,30,1,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,960000,30,1,false,false)==MEMO_POWER_SLEEP);
    assert(memo_power_key(&p,961000,true,false) && !p.blank && p.blank_since_ms==0);
    assert(memo_power_poll(&p,991000,30,1,false,false)==MEMO_POWER_BLANK);
    assert(p.blank_since_ms==991000);
    assert(memo_power_poll(&p,1050999,30,1,false,false)==MEMO_POWER_BLANK);
    assert(memo_power_poll(&p,1051000,30,1,false,false)==MEMO_POWER_SLEEP);
    assert(memo_power_poll(&p,1051000,0,1,false,false)==MEMO_POWER_ON);
    assert(memo_power_poll(&p,9999999,0,1,false,false)==MEMO_POWER_ON);
    // Every pre-terminal failure retains inputs/display and requests worker recovery.
    run('E',"EUPR"); run('U',"EUPR"); run('P',"EUPR");
    run('S',"EUPSR"); run('L',"EUPSLR");
    run('2',"EUPSLSlR"); run('W',"EUPSLSWlR");
    run('K',"EUPSLSWNKX");
    run(0,"EUPSLSWNKBAaiDZX");
    // Peripheral faults cannot return to input handling after terminal teardown.
    for (const char *f="BAaiD"; *f; ++f) run(*f,"EUPSLSWNKBAaiDZX");
    puts("memo power idle protection, wake gesture and terminal shutdown faults: PASS");
    return 0;
}
