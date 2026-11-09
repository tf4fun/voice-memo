#include <assert.h>
#include <stdio.h>
#include "memo_browse.h"
int main(void)
{
    for (int count=1;count<=80;count++) {
        int top=0;
        for(int selected=0;selected<count;selected++) {
            top=memo_viewport_top(top,selected,count,5);
            assert(top>=0 && top<=selected && selected<top+5 && top< count);
        }
        for(int selected=count-1;selected>=0;selected--) {
            top=memo_viewport_top(top,selected,count,5);
            assert(top<=selected && selected<top+5);
        }
    }
    assert(memo_viewport_top(20,40,0,5)==0);
    assert(memo_viewport_top(20,40,3,5)==0);
    int selected=23;
    assert(memo_browse_move(&selected,24,1,false,true)==1 && selected==23);
    selected=0;assert(memo_browse_move(&selected,24,-1,true,true)==-1 && selected==0);
    assert(memo_browse_move(&selected,24,1,false,true)==0 && selected==1);
    assert(memo_browse_move(&selected,24,-1,false,true)==0 && selected==0);
    assert(memo_browse_move(&selected,24,-1,false,false)==0 && selected==0);
    memo_rows_layout_t l=memo_rows_layout(240,320),st=memo_settings_layout(240,320);
    assert(l.top+l.pitch*(l.rows-1)+l.row_height<l.footer_y);
    assert(st.top+st.pitch*(st.rows-1)+st.row_height<st.footer_y);
    assert(st.footer_y+st.footer_height<=320-8);
    assert(l.x>=12 && l.x+l.width+3<=240-12);
    const memo_list_columns_t columns=memo_list_columns(l.width);
    assert(columns.time_w==5*8 && columns.duration_w==5*8);
    assert(columns.time_x+columns.time_w+8<=columns.duration_x);
    assert(columns.text_w>=32); // Both CJK characters in the waiting status fit.
    assert(columns.text_x==columns.time_x && columns.text_x+columns.text_w<=l.width-12);
    assert(columns.duration_x+columns.duration_w<=l.width-8);

    assert(!memo_scrollbar_thumb(5,5,0,212,8).visible);
    memo_detail_step_t move=memo_detail_step(23,24,0,1,false,true);
    assert(move.request_page && move.page==1);
    move=memo_detail_step(0,24,0,-1,true,false);
    assert(move.request_page && move.page==-1);
    move=memo_detail_step(0,24,0,-1,false,false);assert(move.index==-1 && !move.request_page);
    move=memo_detail_step(2,10,2,-1,true,true);assert(move.request_page && move.page==-1);
    move=memo_detail_step(2,10,2,-1,false,true);assert(move.index==1 && !move.request_page);
    move=memo_detail_step(1,10,2,1,true,true);assert(move.request_page && move.page==0);
    move=memo_detail_step(0,10,2,1,true,true);assert(move.index==1 && !move.request_page);
    move=memo_detail_step(0,10,2,-1,true,true);assert(move.index==-1 && !move.request_page);
    move=memo_detail_step(6,10,2,1,true,true);assert(move.index==7 && !move.request_page);
    assert(!memo_scrollbar_thumb(0,5,0,212,8).visible);
    assert(!memo_scrollbar_thumb(24,0,0,212,8).visible);
    assert(!memo_scrollbar_thumb(24,5,0,0,8).visible);
    memo_scrollbar_t bar=memo_scrollbar_thumb(24,5,0,212,8);
    assert(bar.visible && bar.offset==0 && bar.length==44);
    bar=memo_scrollbar_thumb(24,5,19,212,8);
    assert(bar.offset==168 && bar.offset+bar.length==212);
    bar=memo_scrollbar_thumb(24,5,10,212,8);
    assert(bar.offset==(212-44)*10/19 && bar.length==44);
    bar=memo_scrollbar_thumb(24,5,99,212,8);
    assert(bar.offset==168);
    bar=memo_scrollbar_thumb(7,5,2,212,8);
    assert(bar.visible && bar.length==151 && bar.offset==61 && bar.offset+bar.length==212);
    bar=memo_scrollbar_thumb(7,5,0,212,8);
    assert(bar.offset==0);
    bar=memo_scrollbar_thumb(200,5,195,10,8);
    assert(bar.length==8 && bar.offset==2 && bar.offset+bar.length==10);
    puts("memo browse selection viewport, page boundaries and round-safe row/footer layout: PASS");
}
