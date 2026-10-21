// Original LUNA PSXCore handoff, 2026.
#include "luna_psxcore.h"
#include "devices/devices.h"
#include "ui/ambient.h"
#include "ui/art_cache.h"
#include "ui/handoff.h"
#include "ui/pad.h"
#include "ui/graphics.h"
#include <dmaKit.h>
#include <kernel.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
extern unsigned char luna_psx_stub_start[],luna_psx_stub_end[];
_Static_assert(offsetof(LunaPsxTransfer,argc)==136,"transfer argc offset");
_Static_assert(offsetof(LunaPsxTransfer,argv)==140,"transfer argv offset");
_Static_assert(LUNA_PSX_STAGE+LUNA_PSX_CAPACITY<=LUNA_PSX_CONTROL,"stage boundary");
_Static_assert(LUNA_PSX_CONTROL+sizeof(LunaPsxTransfer)<=LUNA_PSX_STUB,"plan boundary");
static int prepared;

const char *lunaPsxPrepare(const char *bootstrap,unsigned argumentCount,
    const char *const arguments[]) {
    prepared=0;
    uintptr_t stack;asm volatile("move %0,$sp":"=r"(stack));
    // Unlike a linker static-data limit, EndOfHeap bounds all SDK allocations,
    // including audio/art thread stacks. The PSXCore build sets a finite heap.
    if((unsigned)GetMemorySize()<0x02000000u || (uintptr_t)EndOfHeap()>LUNA_PSX_STAGE ||
       stack>=LUNA_PSX_STAGE)return "frontend-memory-reservation";
    LunaPsxTransfer plan;
    const char *error=lunaPsxArgumentsCheck(bootstrap,argumentCount,arguments,&plan);
    if(error)return error;
    FILE *f=fopen(bootstrap,"rb");if(!f)return "bootstrap-open";
    size_t n=fread((void *)LUNA_PSX_STAGE,1,LUNA_PSX_CAPACITY,f);
    int extra=fgetc(f),bad=ferror(f),closed=fclose(f);
    if(extra!=EOF || bad || closed)return "bootstrap-read-size";
    // Refuse an explicit On selection before leaving the library when the
    // installed bootstrap predates the public enable switch.
    for(unsigned i=0;i<argumentCount;++i)if(!strcmp(arguments[i],"--code-validation") || !strcmp(arguments[i],"--disc-set")) {
        const char *option=arguments[i];size_t optionBytes=strlen(option)+1;
        const unsigned char *bytes=(const unsigned char *)LUNA_PSX_STAGE;
        int supported=0;
        for(size_t offset=0;offset+optionBytes<=n;++offset)
            if(!memcmp(bytes+offset,option,optionBytes)){supported=1;break;}
        if(!supported)return !strcmp(option,"--disc-set")?
            "This PSXCore bootstrap does not support disc sets. Update PSXCore.":
            "This PSXCore bootstrap does not support Code Validation On. Update PSXCore.";
    }
    error=lunaPsxBootstrapCheck((void *)LUNA_PSX_STAGE,n,&plan.elf);
    if(error)return error;
    size_t stubBytes=luna_psx_stub_end-luna_psx_stub_start;
    if(!stubBytes || stubBytes>4096)return "transfer-stub-size";
    memcpy((void *)LUNA_PSX_CONTROL,&plan,sizeof(plan));
    memcpy((void *)LUNA_PSX_STUB,luna_psx_stub_start,stubBytes);
    prepared=1;
    return NULL;
}
void lunaPsxExecute(void) {
    if(!prepared)__builtin_trap();
    uiPlayLaunchTransition();
    ambientStop();
    // Finish queued GPU DMA before freeing art pixels; then join all artwork
    // workers before closing frontend I/O and replacing code or thread stacks.
    gsKit_queue_exec(gsGlobal);gsKit_finish();dmaKit_wait_fast();
    artCacheShutdown();closePad();syncDeviceMap();
    FlushCache(0);FlushCache(2);TerminateLibrary();DIntr();
    ((void (*)(LunaPsxTransfer *))LUNA_PSX_STUB)((LunaPsxTransfer *)LUNA_PSX_CONTROL);
    __builtin_trap();
}
