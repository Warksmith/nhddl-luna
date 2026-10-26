// Uses the same browser rows, controller prompts and keyboard as PS2 VMCs.
#include "psxcore_file.h"
#include <io_common.h>

int uiPs1VmcRetry(Target *target, const char *error) {
  static const char *const actions[] = {"Retry", "Reset card selection", "Back"};
  int action=ps1Action("Memory cards unavailable", "", error, actions, 3, 0);
  if(action==0)return 1;
  if(action!=1 || !ps1Confirm("Reset card selection?","Only this game's selection will be archived. Card images stay unchanged. Choose your shared cards again afterward."))return 0;
  const char *reason=lunaPsxVmcReset(target);
  if(reason)ps1Message(reason);
  return reason==NULL;
}
const char *uiPs1VmcLoad(Target *target,LunaPsxVmcSettings *settings) {
  ps1Busy("Reading memory card settings...");return lunaPsxVmcLoad(target,settings,NULL);
}
static int ps1CollectRoots(BrowserRoot *roots) {
  int count = 0;
  for (int i = 0; i < MAX_DEVICES; ++i) {
    char root[8];
    if (!lunaPsxVmcDevice(&deviceModeMap[i], root)) continue;
    char label[64]; snprintf(label, sizeof(label), "%s (%s)",
        deviceModeMap[i].mode == MODE_USB ? "USB" : "ATA HDD", root);
    count = addRoot(roots, count, root, label);
  }
  return count;
}
static int ps1ChooseRoot(char root[8]) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  if (!roots) return 0;
  int count = ps1CollectRoots(roots), selected = 0, result = 0;
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText("PlayStation memory cards"), lunaText("Choose a drive"),
        count ? lunaText("Choose where the card set will be stored.") : lunaText("No supported local drives found."),
        PROMPT_TWO(ICON_CROSS, lunaText("Select"), ICON_TRIANGLE, lunaText("Back")),
        count, selected, 0, rootRow, roots, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) break;
    if (count && (input & PAD_UP)) selected = (selected + count - 1) % count;
    else if (count && (input & PAD_DOWN)) selected = (selected + 1) % count;
    else if (count && (input & PAD_CROSS)) { snprintf(root, 8, "%.6s", roots[selected].path); result = 1; break; }
  }
  free(roots); return result;
}


typedef struct {char key[49],detail[32];} Ps1SetEntry;
static int ps1SetRow(int index,char *name,size_t nameSize,char *detail,size_t detailSize,void *context) {
  Ps1SetEntry *sets=context;
  if(!index){snprintf(name,nameSize,"%s",lunaText("Create named pair"));snprintf(detail,detailSize,"2 x 128 KB");}
  else if(index==1){snprintf(name,nameSize,"%s",lunaText("Default shared cards"));detail[0]=0;}
  else{snprintf(name,nameSize,"%s",sets[index-2].key);snprintf(detail,detailSize,"%s",sets[index-2].detail);}
  return 1;
}
static int ps1LoadSets(const char *root,Ps1SetEntry *sets) {
  char directory[128];snprintf(directory,sizeof(directory),"%s/POPS/CARDS",root);
  DIR *dir=opendir(directory);if(!dir)return 0;int count=0;struct dirent *entry;
  while(count<128 && (entry=readdir(dir))) {
    if(!lunaPsxVmcKeyValid(entry->d_name))continue;
    char base[128],cards[2][NP_FILE_PATH_BYTES],path[288];iox_stat_t st;
    snprintf(base,sizeof(base),"%s/%s",directory,entry->d_name);
    if(fileXioGetStat(base,&st)<0 || !FIO_S_ISDIR(st.mode))continue;
    snprintf(sets[count].key,sizeof(sets[count].key),"%s",entry->d_name);
    const char *error=lunaPsxVmcPaths(base,cards);unsigned char hash[32];
    for(unsigned i=0;i<2 && !error;++i){snprintf(path,sizeof(path),"%s%s",root,cards[i]);error=lunaPsxVmcCard(path,hash);}
    snprintf(sets[count].detail,sizeof(sets[count].detail),"%s",lunaText(error?"Check / recover":"2 x 128 KB"));++count;
  }
  closedir(dir);return count;
}
static int ps1Assign(Target *target,LunaPsxVmcSettings *settings,const char *root,const char *key) {
  (void)target;
  if(!key[0] && !strcmp(root,settings->gameRoot)) {
    settings->mode=LUNA_PS1_CARDS_DEFAULT;settings->key[0]=0;memset(settings->cards,0,sizeof(settings->cards));memset(settings->volume,0,16);return 1;
  }
  if(!key[0]){ps1Message("Choose a named pair to use cards from another drive.");return 0;}
  if(!ps1Confirm("Use these shared cards?","Other games selecting this pair use the same saved data."))return 0;
  LunaPsxVmcSettings selected=*settings;selected.mode=LUNA_PS1_CARDS_CUSTOM;memset(selected.cards,0,sizeof(selected.cards));
  snprintf(selected.key,sizeof(selected.key),"%s",key);
  unsigned char gameVolume[16];const char *error=lunaPsxVmcVolume(settings->gameRoot,gameVolume,1);
  if(!error)error=lunaPsxVmcVolume(root,selected.volume,0);
  if(!error)error=lunaPsxVmcPrepare(&selected,0);
  if(error){ps1Message(error);return 0;}*settings=selected;return 1;
}
static int ps1CreateAt(const char *root,Target *target,LunaPsxVmcSettings *settings) {
  char name[49]="LUNA PS1 Cards",key[49];
  if(!editFileName(lunaText("Card pair name"),name,sizeof(name),sizeof(name)-1))return 0;
  if(!ps1Confirm("Create shared cards?","LUNA will create two formatted 128 KB cards. Existing cards remain unchanged."))return 0;
  ps1Busy("Creating and verifying shared cards...");const char *error=lunaPsxVmcCreate(root,name,key);
  if(error){ps1Message(error);return 0;}
  if(target)return ps1Assign(target,settings,root,key);
  ps1Message("Created two formatted 128 KB cards.");return 0;
}
static void ps1Recover(const char *base) {
  static const char *const slots[]={"Slot 1","Slot 2"};
  static const char *const images[]={"Current card","Previous save (.prev)","Save candidate (.bak)","Staged card (.new)","Recovery copy","Earlier recovery card","Create blank missing card"};
  static const char *const suffix[]={"",".prev",".bak",".new",".recover",".recovery-old",".blank"};
  for(;;) {
    int slot=ps1Action("Recover cards",base,"Recover each affected slot. Every candidate is backed up before any change.",slots,2,0);if(slot<0)return;
    int image=ps1Action("Choose recovery image",slots[slot],"Blank recovery is allowed only for a missing card with no valid saved candidate.",images,7,0);if(image<0)continue;
    if(!ps1Confirm("Recover this card?","LUNA will back up every candidate, restore your choice and clear this slot's interrupted files."))continue;
    ps1Busy("Backing up candidates and recovering the card...");const char *error=lunaPsxVmcRecover(base,slot,suffix[image]);
    ps1Message(error?error:"Slot recovered. Recover the other slot too if it has unfinished files.");
  }
}
static void ps1CardActions(const char *base,int slot) {
  char relative[2][NP_FILE_PATH_BYTES],path[LUNA_VMC_PATH];
  const char *error=lunaPsxVmcPaths(base,relative);if(error){ps1Message(error);return;}
  snprintf(path,sizeof(path),"%.6s%s",base,relative[slot]);vmcCardMenu(LUNA_VMC_PS1,path,NULL,NULL);
}
static void ps1ManageBase(const char *base,int initialSlot) {
  if(initialSlot>=0){ps1CardActions(base,initialSlot);return;}
  static const char *const actions[]={"Slot 1","Slot 2","Create missing cards","Back up both cards","Restore backup","Recover cards"};
  for(;;) {
    int action=ps1Action("PlayStation memory cards",base,"Games selecting these cards share saved data.",actions,6,0);if(action<0)return;
    if(action<2){ps1CardActions(base,action);continue;}
    if(action==5){ps1Recover(base);continue;}
    char path[256];const char *error=NULL;
    if(action==2) {
      LunaPsxVmcSettings s={0};snprintf(s.gameRoot,sizeof(s.gameRoot),"%.6s",base);
      if(strcmp(base+6,"/POPS/ps1emu")) {
        s.mode=LUNA_PS1_CARDS_CUSTOM;snprintf(s.key,sizeof(s.key),"%s",base+18);
        error=lunaPsxVmcVolume(s.gameRoot,s.volume,0);
      }
      if(!error){ps1Busy("Creating only missing cards...");error=lunaPsxVmcPrepare(&s,1);}
    } else if(action==3){ps1Busy("Backing up cards...");error=lunaPsxVmcBackup(base,path);}
    else {
      if(!ps1PickPath(1,"Choose backup folder",path))continue;
      if(!ps1Confirm("Restore both shared cards?","This affects all games selecting this pair. Current cards will be backed up first."))continue;
      ps1Busy("Backing up and restoring both cards...");error=lunaPsxVmcReplace(base,path,-1);
    }
    ps1Message(error?error:action==2?"Both cards are ready.":action==3?"Cards backed up and verified.":"Both cards restored. Previous data was backed up.");
  }
}
void uiPs1VmcManage(const LunaPsxVmcSettings *settings,int slot) {
  if(settings->mode!=LUNA_PS1_CARDS_SLOTS && slot<0) {
    char base[128];const char *error=lunaPsxVmcBase(settings,base);
    if(error)ps1Message(error);else ps1ManageBase(base,-1);return;
  }
  char root[8],relative[2][NP_FILE_PATH_BYTES],path[LUNA_VMC_PATH];
  const char *error=lunaPsxVmcResolve(settings,root,relative);if(error){ps1Message(error);return;}
  if(slot<0){static const char *const slots[]={"Slot 1","Slot 2"};slot=ps1Action("Manage cards","","Choose a slot.",slots,2,0);}
  if(slot<0)return;
  snprintf(path,sizeof(path),"%s%s",root,relative[slot]);vmcCardMenu(LUNA_VMC_PS1,path,NULL,NULL);
}
void uiPs1VmcDescribe(const LunaPsxVmcSettings *s,char name[64],char slots[2][32]) {
  snprintf(name,64,"%s",s->mode==LUNA_PS1_CARDS_DEFAULT?lunaText("Default shared cards"):
      s->mode==LUNA_PS1_CARDS_SLOTS?lunaText("Independent slots"):s->key);
  char root[8],relative[2][NP_FILE_PATH_BYTES],path[LUNA_VMC_PATH];
  const char *error=lunaPsxVmcResolve(s,root,relative);
  for(unsigned i=0;i<2;++i) {
    if(error){snprintf(slots[i],32,"%s",lunaText("Unavailable"));continue;}
    snprintf(path,sizeof(path),"%s%s",root,relative[i]);
    const char *problem=lunaVmcReady(path,LUNA_VMC_PS1);
    const char *label=strrchr(relative[i],'/');label=label?label+1:relative[i];
    snprintf(slots[i],32,"%.18s - %s",label,lunaText(problem?"Check":"Ready"));
  }
}
static int ps1BrowseSets(Target *target,LunaPsxVmcSettings *settings) {
  char root[8];
  while(ps1ChooseRoot(root)) {
    Ps1SetEntry *sets=calloc(128,sizeof(*sets));if(!sets)return 0;
    ps1Busy("Reading shared memory cards...");int count=ps1LoadSets(root,sets),selected=0;BrowserSelector selector={.selected=-1};
    for(;;) {
      drawBrowserFrameSelected(lunaText("PlayStation memory cards"),root,
          target?lunaText("Choose a shared pair. The selection saves automatically."):lunaText("Create shared cards, or open a pair to manage its saves."),
          PROMPT_TWO(ICON_CROSS,target?lunaText("Select/Create"):lunaText("Open/Create"),ICON_TRIANGLE,lunaText("Back")),
          count+2,selected,browserFirstRow(selected),ps1SetRow,sets,&selector);
      int input=readInput();if(input&(PAD_CIRCLE|PAD_TRIANGLE))break;
      if(input&PAD_UP)selected=(selected+count+1)%(count+2);
      else if(input&PAD_DOWN)selected=(selected+1)%(count+2);
      else if(input&PAD_CROSS) {
        int changed=0;
        if(!selected)changed=ps1CreateAt(root,target,settings);
        else if(target)changed=ps1Assign(target,settings,root,selected==1?"":sets[selected-2].key);
        else {
          char base[128];if(selected==1)snprintf(base,sizeof(base),"%s/POPS/ps1emu",root);
          else snprintf(base,sizeof(base),"%s/POPS/CARDS/%s",root,sets[selected-2].key);
          ps1ManageBase(base,-1);
        }
        if(changed){free(sets);return 1;}count=ps1LoadSets(root,sets);if(selected>count+1)selected=count+1;selector.selected=-1;
      }
    }
    free(sets);
  }
  return 0;
}
int uiPs1VmcPicker(Target *target,LunaPsxVmcSettings *settings){return ps1BrowseSets(target,settings);}
