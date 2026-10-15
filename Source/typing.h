#ifndef TYPING_H
#define TYPING_H
#include <stddef.h>
#include <stdint.h>
typedef struct {uint16_t value,second;unsigned char unicode,shift;} Unit;
typedef struct {Unit *units;size_t count;} Plan;
enum { BUILD_OK,BUILD_EMPTY,BUILD_MEMORY,BUILD_UNSUPPORTED };
int build_plan(Plan *p,const uint16_t *text,size_t length,int keyboard,int caps,int enter_count,size_t *bad);
void free_plan(Plan *p);
typedef int (*Emit)(void*,Unit,int);
enum { IDLE,RUNNING,DONE,CANCELLED,FOCUS_LOST,KEYS_HELD,SEND_FAILED };
typedef struct {const Plan *plan;size_t index;int state,phase,held;unsigned hold,gap;uint64_t deadline,timeout;} Engine;
int begin_typing(Engine *e,const Plan *p,unsigned hold,unsigned gap,uint64_t now);
int stop_typing(Engine *e,int reason,Emit emit,void *context);
int tick_typing(Engine *e,uint64_t now,int focus,int clear,int cancel,Emit emit,void *context);
#endif
