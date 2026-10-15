#include "typing.h"
#include <stdlib.h>
#include <string.h>
static int ascii_key(uint16_t c,int caps,Unit *u) {
    static const uint16_t letters[]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    if(c>='A'&&c<='Z'){u->value=letters[c-'A'];u->shift=!caps;return 1;}
    if(c>='a'&&c<='z'){u->value=letters[c-'a'];u->shift=!!caps;return 1;}
    if(c>='1'&&c<='9'){u->value=c-'1'+2;return 1;}
    if(c=='0'){u->value=0x0b;return 1;}
    if(c==' '){u->value=0x39;return 1;}
    const char *plain="`-=[];'\\,./",*shifted="~_+{}:\"|<>?",*digits="!@#$%^&*()";
    const uint16_t scans[]={0x29,0x0c,0x0d,0x1a,0x1b,0x27,0x28,0x2b,0x33,0x34,0x35};
    for(int i=0;plain[i];i++){
        if(c==(unsigned char)plain[i]){u->value=scans[i];return 1;}
        if(c==(unsigned char)shifted[i]){u->value=scans[i];u->shift=1;return 1;}
    }
    for(int i=0;digits[i];i++)if(c==(unsigned char)digits[i]){u->value=i==9?0x0b:(uint16_t)(i+2);u->shift=1;return 1;}
    return 0;
}
void free_plan(Plan *p){free(p->units);p->units=NULL;p->count=0;}
int build_plan(Plan *p,const uint16_t *text,size_t length,int keyboard,int caps,int enters,size_t *bad){
    if(!length||!text)return BUILD_EMPTY;
    if(enters<0||enters>2)return BUILD_UNSUPPORTED;
    if(length>SIZE_MAX/sizeof(Unit)-(size_t)enters)return BUILD_MEMORY;
    Plan next={calloc(length+(size_t)enters,sizeof(Unit)),0};
    if(!next.units)return BUILD_MEMORY;
    for(size_t i=0;i<length;i++){
        Unit u={0};uint16_t c=text[i];
        if(c=='\r'||c=='\n'){
            if(c=='\r'&&i+1<length&&text[i+1]=='\n')i++;
            u.value=0x1c;
        }else if(c=='\t')u.value=0x0f;
        else if(keyboard){
            if(!ascii_key(c,caps,&u)){if(bad)*bad=i;free_plan(&next);return BUILD_UNSUPPORTED;}
        }else{
            u.unicode=1;u.value=c;
            if(c>=0xd800&&c<=0xdbff&&i+1<length&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)u.second=text[++i];
        }
        next.units[next.count++]=u;
    }
    for(int i=0;i<enters;i++)next.units[next.count++]=(Unit){.value=0x1c};
    free_plan(p);*p=next;return BUILD_OK;
}
int begin_typing(Engine *e,const Plan *p,unsigned hold,unsigned gap,uint64_t now){
    if(e->state==RUNNING||!p->count||hold<5||hold>500||gap<5||gap>1000)return 0;
    memset(e,0,sizeof(*e));e->plan=p;e->hold=hold;e->gap=gap;e->deadline=now+150;e->timeout=now+2500;e->state=RUNNING;return 1;
}
int stop_typing(Engine *e,int reason,Emit emit,void *ctx){
    if(e->state!=RUNNING)return e->state;
    if(e->held){if(!emit(ctx,e->plan->units[e->index],0))reason=SEND_FAILED;e->held=0;}
    return e->state=reason;
}
int tick_typing(Engine *e,uint64_t now,int focus,int clear,int cancel,Emit emit,void *ctx){
    if(e->state!=RUNNING)return e->state;
    if(cancel)return stop_typing(e,CANCELLED,emit,ctx);
    if(!focus)return stop_typing(e,FOCUS_LOST,emit,ctx);
    if(!e->phase){
        if(!clear){if(now>=e->timeout)return stop_typing(e,KEYS_HELD,emit,ctx);e->deadline=now+150;return e->state;}
        if(now<e->deadline)return e->state;
        e->phase=1;
    }else if(!clear)return stop_typing(e,KEYS_HELD,emit,ctx);
    if(now<e->deadline)return e->state;
    if(e->phase==1){
        if(!emit(ctx,e->plan->units[e->index],1))return stop_typing(e,SEND_FAILED,emit,ctx);
        e->held=1;e->phase=2;e->deadline=now+e->hold;
    }else if(e->phase==2){
        if(!emit(ctx,e->plan->units[e->index],0))return stop_typing(e,SEND_FAILED,emit,ctx);
        e->held=0;e->index++;
        if(e->index==e->plan->count)e->state=DONE;
        else{e->phase=1;e->deadline=now+e->gap;}
    }
    return e->state;
}
