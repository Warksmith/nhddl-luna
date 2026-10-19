// Disc management reuses LUNA's existing browser and on-screen keyboard.
#include "luna_psxcore_discset.h"
#include "luna_psxcore_library.h"

static int ps1DiscConfirm(const char *title,const char *detail) {
  static const char *const actions[]={"Cancel","Continue"};
  return ps1Action(title,lunaText("Discs"),detail,actions,2,0)==1;
}

static int ps1DiscRow(int index,char *name,size_t nameSize,char *detail,size_t detailSize,
    CardArtType *art,void *context) {
  TargetDiscSet *set=context;*art=CARD_ART_NONE;
  if((unsigned)index<set->count) {
    const char *slash=strrchr(set->paths[index],'/');
    snprintf(name,nameSize,"%s",slash?slash+1:set->paths[index]);
    snprintf(detail,detailSize,"%s %d",lunaText("Disc"),index+1);
  } else {
    snprintf(name,nameSize,"%s",lunaText((unsigned)index==set->count?"Add disc":"Save disc set"));detail[0]=0;
  }
  return 1;
}
int uiPs1DiscManage(Target *target) {
  TargetDiscSet *set=calloc(1,sizeof(*set));if(!set)return 0;
  const char *suggestion=lunaPsxDiscSuggest(target,set);
  if(!set->count && !target->discs) {
    snprintf(set->paths[0],sizeof(set->paths[0]),"%s",target->fullPath+6);set->count=1;
  }
  char status[192];snprintf(status,sizeof(status),"%s",target->discs && target->discs->error[0]?
    target->discs->error:suggestion?suggestion:target->discs?
    "Select a disc to reorder or remove it.":"Suggested order. Check the images and confirm every disc is present.");
  int selected=0,result=0;BrowserSelector selector={.selected=-1};
  for(;;) {
    drawBrowserFrameSelected(lunaText("Manage discs"),target->name,lunaText(status),
      PROMPT_TWO(ICON_CROSS,lunaText("Select"),ICON_TRIANGLE,lunaText("Back")),
      (int)set->count+2,selected,browserFirstRow(selected),0,ps1DiscRow,set,&selector);
    int input=readInput();
    if(input&(PAD_CIRCLE|PAD_TRIANGLE))break;
    if(input&PAD_UP)selected=(selected+(int)set->count+1)%((int)set->count+2);
    else if(input&PAD_DOWN)selected=(selected+1)%((int)set->count+2);
    else if(input&PAD_CROSS) {
      if((unsigned)selected<set->count) {
        static const char *const actions[]={"Move earlier","Move later","Remove disc","Revalidate image"};
        int action=ps1Action("Disc order",set->paths[selected],"The first entry is Disc 1.",actions,4,0);
        int other=action==0?selected-1:action==1?selected+1:-1;
        if(other>=0 && (unsigned)other<set->count) {
          char path[256];memcpy(path,set->paths[selected],256);
          memcpy(set->paths[selected],set->paths[other],256);memcpy(set->paths[other],path,256);selected=other;
        } else if(action==3) {
          if(!target->discs || strcmp(set->key,target->discs->key))
            snprintf(status,sizeof(status),"%s","Save the disc set before revalidating its images.");
          else if(ps1DiscConfirm("Revalidate this image?","The previous fingerprint will be preserved. The next launch will read the complete image and record its current content.")) {
            char root[8];snprintf(root,sizeof(root),"%.6s",target->fullPath);
            const char *error=lunaPsxDiscIdentityReset(root,set->key,set->paths[selected]);
            snprintf(status,sizeof(status),"%s",error?error:"The next launch will revalidate this image.");
          }
        } else if(action==2) {
          memmove(set->paths[selected],set->paths[selected+1],(set->count-(unsigned)selected-1)*256);
          memset(set->paths[--set->count],0,256);
        }
      } else if((unsigned)selected==set->count) {
        char path[256];
        if(set->count==4)snprintf(status,sizeof(status),"%s","PSXCore supports at most four discs in a set.");
        else if(ps1PickPath(2,"Add disc",path)) {
          const char *error=NULL;char id[12];
          if(strncmp(path,target->fullPath,6))error="All discs must be on the game's drive.";
          for(unsigned i=0;!error && i<set->count;++i)
            if(!strcasecmp(path+6,set->paths[i]))error="This disc is already in the set.";
          if(!error)error=lunaPsxDiscInfo(path,id);
          if(error)snprintf(status,sizeof(status),"%s",error);
          else {snprintf(set->paths[set->count++],256,"%s",path+6);status[0]=0;}
        }
      } else {
        char playlist[264];
        if(target->discs) {
          if(strlen(target->fullPath)>=sizeof(playlist)) {
            snprintf(status,sizeof(status),"%s","The playlist path is too long.");continue;
          }
          snprintf(playlist,sizeof(playlist),"%s",target->fullPath);
        }
        else {
          char name[128];snprintf(name,sizeof(name),"%.100s",target->name);
          if(!editFileName(lunaText("Disc set name"),name,sizeof(name),sizeof(name)-1))continue;
          const char *slash=strrchr(target->fullPath,'/');
          int n=snprintf(playlist,sizeof(playlist),"%.*s/%s.m3u",(int)(slash-target->fullPath),target->fullPath,name);
          if(n<0 || (size_t)n>=sizeof(playlist)) {snprintf(status,sizeof(status),"%s","The playlist path is too long.");continue;}
        }
        if(!ps1DiscConfirm("Save this disc set?","Check that every disc belongs to this game and the order is correct. All discs will use the same selected memory cards."))continue;
        const char *error=NULL;
        for(unsigned i=0;i<set->count && !error;++i) {
          char path[264],id[12];snprintf(path,sizeof(path),"%.6s%s",target->fullPath,set->paths[i]);
          error=lunaPsxDiscInfo(path,id);
        }
        if(!error)error=lunaPsxPlaylistSave(playlist,set,target->discs!=NULL);
        if(error)snprintf(status,sizeof(status),"%s",error);
        else if(storageRequest(&STORAGE_SETTINGS,target->device->mode))
          snprintf(status,sizeof(status),"%s","Playlist saved. Refresh the library from Storage Devices.");
        else {result=STORAGE_UI_REFRESH;break;}
      }
      selector.selected=-1;
    }
  }
  free(set);return result;
}
