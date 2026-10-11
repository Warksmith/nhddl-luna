// LUNA supplies existing shared cards to the unchanged PSXCore bootstrap.
#include "luna_psxcore.h"
#include "luna_psxcore_library.h"
#include "luna_psxcore_vmc.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>

static int readable(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int byte=fgetc(f),bad=ferror(f),closed=fclose(f);
    return byte!=EOF && !bad && !closed;
}
static int titleError(char *out,size_t size,const char *message) {snprintf(out,size,"%s",message);return -1;}
int lunaPsxPrepareTitleCards(Target *target,const LunaPsxVmcSettings *selection,
    int createCards,char *error,size_t size,int (*progress)(uint64_t,uint64_t)) {
    if(!target || target->platform!=TARGET_PS1 || !target->device ||
       !(STORAGE_SETTINGS.enabled & target->device->mode) ||
       !((target->device->mode==MODE_ATA && target->device->index==0) ||
         (target->device->mode==MODE_USB && target->device->index<=1)))
        return titleError(error,size,"PS1 games require an enabled internal ATA or USB drive.");
    const char *path=target->fullPath;
    LunaPsxVmcSettings settings;const char *reason=lunaPsxVmcLoad(target,&settings,progress);
    if(reason)return titleError(error,size,reason);
    if(selection) {
        if(strcmp(selection->gamePath,settings.gamePath) || strcmp(selection->gameRoot,settings.gameRoot))
            return titleError(error,size,"The selected game changed. Reopen its memory card settings.");
        settings=*selection;
    }
    char id[12];reason=lunaPsxDiscInfo(path,id);
    if(reason){snprintf(error,size,"Cannot prepare PS1 disc: %s",reason);return -1;}
    char asset[192];const char *names[]={"POPS.ELF","IOPRP252.IMG"};
    for(unsigned i=0;i<2;++i) {
        snprintf(asset,sizeof(asset),"%.6s/POPS/%s",path,names[i]);
        if(!readable(asset)){snprintf(error,size,"Missing or unreadable firmware: %s",asset);return -1;}
    }
    if(settings.mode==LUNA_PS1_CARDS_DEFAULT)createCards=1;
    reason=lunaPsxVmcPrepare(&settings,createCards);
    if(reason) {
        if(!strcmp(reason,"Memory cards have not been created yet.")) {
            snprintf(error,size,"This pair needs PS1 memory cards.\nPress Square to create missing cards and launch.\nPress Triangle to return.");return 1;
        }
        return titleError(error,size,reason);
    }
    char base[128],cards[2][NP_FILE_PATH_BYTES],saveRoot[8],discId[33],saveId[33],saveDevice[4],discDevice[4];
    reason=lunaPsxVmcBase(&settings,base);if(reason)return titleError(error,size,reason);
    reason=lunaPsxVmcPaths(base,cards);if(reason)return titleError(error,size,reason);
    snprintf(saveRoot,sizeof(saveRoot),"%.6s",base);
    const char *saveBackend=NULL;
    for(int i=0;i<MAX_DEVICES;++i) {
        char root[8];
        if(lunaPsxVmcDevice(&deviceModeMap[i],root) && !strcmp(root,saveRoot)) {
            saveBackend=deviceModeMap[i].mode==MODE_USB?"usb":"ata";
            snprintf(saveDevice,sizeof(saveDevice),"%u",(unsigned)deviceModeMap[i].index);break;
        }
    }
    if(!saveBackend)return titleError(error,size,"The selected card drive is unavailable.");
    int custom=settings.mode==LUNA_PS1_CARDS_CUSTOM;
    if(custom) {
        unsigned char gameVolume[16];char verified[8];
        reason=lunaPsxVmcVolume(settings.gameRoot,gameVolume,1);if(reason)return titleError(error,size,reason);
        reason=lunaPsxVmcFindVolume(gameVolume,verified);if(reason)return titleError(error,size,reason);
        np_file_volume_id_text(gameVolume,discId);np_file_volume_id_text(settings.volume,saveId);
    }
    char bootstrap[PATH_MAX+1];const char *slash=strrchr(NEUTRINO_ELF_PATH,'/');
    if(!slash || (size_t)(slash-NEUTRINO_ELF_PATH)+sizeof("/psxcore-runtime-bootstrap.elf")>sizeof(bootstrap))
        return titleError(error,size,"Cannot locate PSXCore beside the LUNA runtime.");
    snprintf(bootstrap,sizeof(bootstrap),"%.*s/psxcore-runtime-bootstrap.elf",(int)(slash-NEUTRINO_ELF_PATH),NEUTRINO_ELF_PATH);
    snprintf(discDevice,sizeof(discDevice),"%u",(unsigned)target->device->index);
    const char *args[LUNA_PSX_MAX_ARGS]={"--file",path,"--backend",target->device->mode==MODE_USB?"usb":"ata",
        "--device",discDevice,"--card0",cards[0],"--card1",cards[1]};unsigned count=10;
#ifdef LUNA_EMULATOR_BUILD
    args[count++]="--pcsx2";
#endif
    if(custom) {
        args[count++]="--volume-id";args[count++]=discId;
        args[count++]="--save-volume";args[count++]=saveRoot;
        args[count++]="--save-volume-id";args[count++]=saveId;
        args[count++]="--save-backend";args[count++]=saveBackend;
        args[count++]="--save-device";args[count++]=saveDevice;
    }
    reason=lunaPsxPrepare(bootstrap,count,args);
    if(reason){snprintf(error,size,"Cannot prepare PSXCore: %s",reason);return -1;}
    if(updateLastLaunchedTitle(target->device,target->fullPath))DPRINTF("WARN: Could not record selected PS1 game\n");
    DPRINTF("LUNA PS1 selected=%s cards=sony-shared card0=%s card1=%s\n",path,cards[0],cards[1]);
    return 0;
}
int lunaPsxPrepareTitle(Target *target,int createCards,char *error,size_t size,int (*progress)(uint64_t,uint64_t)) {
    return lunaPsxPrepareTitleCards(target,NULL,createCards,error,size,progress);
}
