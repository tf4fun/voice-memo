#include <assert.h>
#include <stdio.h>
#include "memo_progress.h"
int main(void)
{
    const int sizes[][2]={{56,8},{160,12}};
    for(unsigned i=0;i<2;i++){
        const int w=sizes[i][0],h=sizes[i][1];
        const memo_progress_layout_t p=memo_progress_layout(w,h);
        // Absolute gaps include the track's 1 px border on both axes.
        assert(2*(1+p.x)+p.width==w && 2*(1+p.y)+p.height==h);
        for(unsigned n=0;n<=100;n++)assert(memo_progress_width(w,n,100)<=p.width);
        assert(memo_progress_width(w,0,100)==0);
        assert(memo_progress_width(w,100,100)==p.width);
        assert(memo_progress_width(w,UINT32_MAX,UINT32_MAX)==p.width);
        assert(memo_progress_width(w,200,100)==p.width);
    }
    assert(memo_progress_width(56,5000,10000)==26);
    assert(memo_progress_width(56,1,0)==0);
    assert(memo_progress_width(4,100,100)==0);
    puts("memo progress centred border/inset geometry and bounded fraction: PASS");
}
