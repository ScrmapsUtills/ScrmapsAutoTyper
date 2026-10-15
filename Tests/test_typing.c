#include "typing.h"
#include <assert.h>
#include <uchar.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {int downs,ups,fail;Unit last;} Log;
static int emit(void *context,Unit u,int down){Log *l=context;if(l->fail)return 0;if(down)l->downs++;else l->ups++;l->last=u;return 1;}
int main(void){
    Plan p={0};size_t bad=0;
    const char16_t text[]=u" AbC-123 =+! 한글\tX\r\n끝😀 ";
    assert(build_plan(&p,(const uint16_t*)text,sizeof(text)/2-1,0,0,0,&bad)==BUILD_OK);
    assert(p.units[0].value==' '&&p.units[0].unicode);
    assert(p.units[1].value=='A'&&p.units[2].value=='b'&&p.units[3].value=='C');
    int tabs=0,returns=0,korean=0,pairs=0;
    for(size_t i=0;i<p.count;i++){
        Unit u=p.units[i];tabs+=!u.unicode&&u.value==0x0f;returns+=!u.unicode&&u.value==0x1c;
        korean+=u.value==0xd55c||u.value==0xae00||u.value==0xb05d;pairs+=u.second!=0;
    }
    assert(tabs==1&&returns==1&&korean==3&&pairs==1);
    assert(p.units[p.count-1].value==' '); /* no trimming or extra Enter */
    size_t n=p.count;
    for(int enters=0;enters<3;enters++){
        assert(build_plan(&p,(const uint16_t*)text,sizeof(text)/2-1,0,0,enters,&bad)==BUILD_OK);
        assert(p.count==n+(size_t)enters);
        for(int j=0;j<enters;j++)assert(!p.units[p.count-1-j].unicode&&p.units[p.count-1-j].value==0x1c);
    }
    uint16_t ascii[95];for(int i=0;i<95;i++)ascii[i]=(uint16_t)(32+i);
    assert(build_plan(&p,ascii,95,1,0,0,&bad)==BUILD_OK&&p.count==95);
    assert(p.units['A'-32].shift&&!p.units['a'-32].shift);
    assert(p.units['!'-32].shift&&p.units['!'-32].value==2);
    assert(build_plan(&p,ascii,95,1,1,0,&bad)==BUILD_OK);
    assert(!p.units['A'-32].shift&&p.units['a'-32].shift);
    assert(build_plan(&p,(const uint16_t*)u"abc한",4,1,0,0,&bad)==BUILD_UNSUPPORTED&&bad==3);
    assert(p.count==95); /* failed preflight must not replace the old plan */
    uint16_t *long_text=malloc(200000*sizeof(uint16_t));assert(long_text);
    for(size_t i=0;i<200000;i++)long_text[i]=(uint16_t)('a'+i%26);
    assert(build_plan(&p,long_text,200000,0,0,0,&bad)==BUILD_OK&&p.count==200000);free(long_text);
    assert(build_plan(&p,(const uint16_t*)u"x",1,0,0,0,&bad)==BUILD_OK&&p.count==1);
    assert(build_plan(&p,NULL,0,0,0,0,&bad)==BUILD_EMPTY);
    assert(build_plan(&p,(const uint16_t*)u"Aa09!",5,1,0,0,&bad)==BUILD_OK);
    for(unsigned step=7;step<230;step+=17){
        Engine e={0};Log l={0};assert(begin_typing(&e,&p,80,120,0));
        assert(!begin_typing(&e,&p,80,120,0));
        for(uint64_t now=0;now<10000&&e.state==RUNNING;now+=step)tick_typing(&e,now,1,1,0,emit,&l);
        assert(e.state==DONE&&l.downs==5&&l.ups==5&&!e.held);
    }
    for(int mode=0;mode<3;mode++)for(uint64_t at=160;at<1400;at+=19){
        Engine e={0};Log l={0};begin_typing(&e,&p,80,120,0);
        for(uint64_t now=0;now<at&&e.state==RUNNING;now+=5)tick_typing(&e,now,1,1,0,emit,&l);
        if(e.state==RUNNING){int before=l.downs;tick_typing(&e,at,mode!=0,mode!=2,mode==1,emit,&l);
            assert(e.state==FOCUS_LOST||e.state==CANCELLED||e.state==KEYS_HELD);assert(l.ups==l.downs&&!e.held);
            tick_typing(&e,at+10000,1,1,0,emit,&l);assert(l.downs==before);
        }
    }
    {Engine e={0};Log l={.fail=1};begin_typing(&e,&p,80,120,0);tick_typing(&e,160,1,1,0,emit,&l);assert(e.state==SEND_FAILED);}
    free_plan(&p);
    puts("PASS: arbitrary lengths (1 to 200,000 units), case, spaces, punctuation, Korean, emoji,");
    puts("CRLF, tabs, optional Enter 0/1/2, all 95 printable ASCII keyboard mappings, Caps Lock,");
    puts("timer jitter, cancellation/focus/modifier interruption, release, failed preflight and send.");
    return 0;
}
