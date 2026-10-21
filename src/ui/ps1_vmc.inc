// Uses the same browser rows, controller prompts and keyboard as PS2 VMCs.
#include "psxcore_file.h"
#include <io_common.h>

static int ps1ActionRow(int index, char *name, size_t nameSize, char *detail,
    size_t detailSize, void *context) {
  const char *const *labels = context;
  snprintf(name, nameSize, "%s", lunaText(labels[index])); detail[0] = 0;
  (void)detailSize; return 1;
}
static int ps1Action(const char *title, const char *subtitle, const char *status,
    const char *const *labels, int count, int selected) {
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText(title), subtitle, lunaText(status),
        PROMPT_TWO(ICON_CROSS, lunaText("Select"), ICON_TRIANGLE, lunaText("Back")),
        count, selected, browserFirstRow(selected), ps1ActionRow, (void *)labels, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) return -1;
    if (input & PAD_UP) selected = (selected + count - 1) % count;
    else if (input & PAD_DOWN) selected = (selected + 1) % count;
    else if (input & PAD_CROSS) return selected;
  }
}
static void ps1Message(const char *message) {
  static const char *const back[] = {"Back"};
  ps1Action("PlayStation memory cards", "", message, back, 1, 0);
}
static int ps1Confirm(const char *title, const char *detail) {
  static const char *const actions[] = {"Cancel", "Continue"};
  return ps1Action(title, "PlayStation memory cards", detail, actions, 2, 0) == 1;
}
int uiPs1VmcRetry(Target *target, const char *error) {
  static const char *const actions[] = {"Retry", "Reset card selection", "Back"};
  int action=ps1Action("Memory cards unavailable", "", error, actions, 3, 0);
  if(action==0)return 1;
  if(action!=1 || !ps1Confirm("Reset card selection?","Only this game's selection will be archived. Card images stay unchanged. Choose your shared cards again afterward."))return 0;
  const char *reason=lunaPsxVmcReset(target);
  if(reason)ps1Message(reason);
  return reason==NULL;
}
static void ps1StatusFrame(const char *message, PromptBar footer) {
  int left = keepoutArea + 30, right = gsGlobal->Width - left;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(0);
  drawTextWindow(left, headerHeight - getFontLineHeight(), right, headerHeight + 2,
      0, HeaderTextColor, ALIGN_HCENTER, lunaText("PlayStation memory cards"));
  drawTextWindow(left + 18, headerHeight + 40, right - 18,
      gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT, lunaText(message));
  drawPromptBar(left + 18, gsGlobal->Height - footerHeight, right,
      gsGlobal->Height, 0, HeaderTextColor, footer);
  presentBrowserFrame();
}
static void ps1Busy(const char *message) {
  ps1StatusFrame(message, PROMPT_TEXT(lunaText("Please wait until the operation finishes.")));
}

const char *uiPs1VmcLoad(Target *target,LunaPsxVmcSettings *settings) {
  ps1Busy("Reading memory card settings...");return lunaPsxVmcLoad(target,settings,NULL);
}
int uiPs1VmcPolicy(Target *target,LunaPsxVmcSettings *settings) {
  static const char *const labels[]={"Default shared cards","Choose named cards"};
  int choice=ps1Action("Memory cards",target->name,
      "Games using the same cards share saved data. Both slots use the selected pair.",labels,2,settings->mode);
  if(choice<0)return 0;
  if(choice==LUNA_PS1_CARDS_CUSTOM)return uiPs1VmcPicker(target,settings);
  int changed=settings->mode!=LUNA_PS1_CARDS_DEFAULT;
  settings->mode=LUNA_PS1_CARDS_DEFAULT;memset(settings->key,0,sizeof(settings->key));memset(settings->volume,0,16);return changed;
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
    settings->mode=LUNA_PS1_CARDS_DEFAULT;settings->key[0]=0;memset(settings->volume,0,16);return 1;
  }
  if(!key[0]){ps1Message("Choose a named pair to use cards from another drive.");return 0;}
  if(!ps1Confirm("Use these shared cards?","Other games selecting this pair use the same saved data."))return 0;
  LunaPsxVmcSettings selected=*settings;selected.mode=LUNA_PS1_CARDS_CUSTOM;
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
// A bounded, ordinary storage picker; imports are validated after selection.
static int ps1PickPath(int folder, const char *heading, char out[256]) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserEntry *entries = calloc(BROWSER_MAX_ENTRIES, sizeof(*entries));
  if (!roots || !entries) { free(roots); free(entries); return 0; }
  int rootCount = collectRoots(roots), active = -1, selected = 0, count = 0, truncated = 0, result = 0;
  char path[PATH_MAX + 1] = "", status[128] = "";
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText(heading), active < 0 ? lunaText("Choose a drive") : path,
        status[0] ? status : folder==1 ? lunaText("Square chooses this folder. Cross opens a folder.") :
        folder==2 ? lunaText("Choose a VCD on the game's drive.") : lunaText("Choose a raw 128 KB memory card image."),
        folder==1 && active >= 0 ? PROMPT_TWO(ICON_SQUARE, lunaText("Choose folder"), ICON_TRIANGLE, lunaText("Back")) :
        PROMPT_TWO(ICON_CROSS, lunaText("Open/Select"), ICON_TRIANGLE, lunaText("Back")),
        active < 0 ? rootCount : count, selected, browserFirstRow(selected),
        active < 0 ? rootRow : fileRow, active < 0 ? (void *)roots : (void *)entries, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) {
      if (active < 0) break;
      size_t length = strlen(path); while (length && path[length - 1] == '/') path[--length] = 0;
      char *slash = strrchr(path, '/');
      if (!slash || slash < strchr(path, ':')) { clearEntries(entries, count); count = 0; active = -1; }
      else { *slash = 0; loadDirectory(path, entries, &count, &truncated); }
      selected = 0; selector.selected = -1; status[0] = 0; continue;
    }
    int total = active < 0 ? rootCount : count;
    if (total && (input & PAD_UP)) selected = (selected + total - 1) % total;
    else if (total && (input & PAD_DOWN)) selected = (selected + 1) % total;
    else if (folder==1 && active >= 0 && (input & PAD_SQUARE)) {
      if (strlen(path) >= 256) snprintf(status, sizeof(status), "%s", lunaText("The current path is too long."));
      else { snprintf(out, 256, "%s", path); result = 1; break; }
    } else if (total && (input & PAD_CROSS)) {
      if (active < 0) { active = selected; snprintf(path, sizeof(path), "%s", roots[active].path); }
      else {
        char child[PATH_MAX + 1];
        if (!joinPath(child, sizeof(child), path, entries[selected].name)) continue;
        if (!entries[selected].isDirectory) {
          if (folder==1 || (folder==2 && !np_vcd_filename(child))) continue;
          if (strlen(child) >= 256) { snprintf(status, sizeof(status), "%s", lunaText("The current path is too long.")); continue; }
          snprintf(out, 256, "%s", child); result = 1; break;
        }
        snprintf(path, sizeof(path), "%s", child);
      }
      loadDirectory(path, entries, &count, &truncated); selected = 0; selector.selected = -1;
      snprintf(status, sizeof(status), "%s", truncated ? lunaText("Only the first 512 entries are shown.") : "");
    }
  }
  clearEntries(entries, count); free(entries); free(roots); return result;
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
  static const char *const actions[]={"Export card","Import card","Back up both cards","Recover cards"};
  char title[64],source[256],destination[256],relative[2][NP_FILE_PATH_BYTES];
  const char *error=lunaPsxVmcPaths(base,relative);if(error){ps1Message(error);return;}
  snprintf(title,sizeof(title),"Slot %d - 128 KB",slot+1);
  for(;;) {
    int action=ps1Action("PlayStation memory cards",title,"Import affects every game using this card. A backup is made first.",actions,4,0);if(action<0)return;
    error=NULL;
    if(action==3){ps1Recover(base);continue;}
    if(action==0) {
      if(!ps1PickPath(1,"Export card",destination))continue;
      char name[64];snprintf(name,sizeof(name),"ps1_slot%d.vmc",slot+1);
      if(!editFileName(lunaText("Export filename"),name,sizeof(name),sizeof(name)-1))continue;
      char path[256];if(!joinPath(path,sizeof(path),destination,name)){ps1Message("The export path is too long.");continue;}
      snprintf(source,sizeof(source),"%.6s%s",base,relative[slot]);ps1Busy("Exporting and verifying the card...");error=lunaPsxVmcExport(source,path);
    } else if(action==1) {
      if(!ps1PickPath(0,"Import card",source))continue;
      if(!ps1Confirm("Replace this shared card?","This affects every game using it. Current cards will be backed up first."))continue;
      ps1Busy("Backing up and importing the card...");error=lunaPsxVmcReplace(base,source,slot);
    } else {ps1Busy("Backing up shared cards...");error=lunaPsxVmcBackup(base,destination);}
    ps1Message(error?error:action==0?"Card exported and verified.":action==1?"Card imported. Previous data was backed up.":"Cards backed up and verified.");
  }
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
  char base[128];const char *error=lunaPsxVmcBase(settings,base);if(error){ps1Message(error);return;}ps1ManageBase(base,slot);
}
void uiPs1VmcDescribe(const LunaPsxVmcSettings *s,char name[64],char slots[2][32]) {
  snprintf(name,64,"%s",s->mode==LUNA_PS1_CARDS_DEFAULT?lunaText("Default shared cards"):s->key);
  char base[128],relative[2][NP_FILE_PATH_BYTES],path[288];unsigned char hash[32];iox_stat_t st;
  const char *error=lunaPsxVmcBase(s,base);if(!error)error=lunaPsxVmcPaths(base,relative);
  for(unsigned i=0;i<2;++i) {
    const char *status="Unavailable";
    if(!error) {
      snprintf(path,sizeof(path),"%.6s%s",base,relative[i]);
      status=fileXioGetStat(path,&st)<0?"Missing":lunaPsxVmcCard(path,hash)?"Invalid":"Ready";
      const char *suffix[]={".new",".bak",".recover",".recovery-old"};
      for(unsigned j=0;j<4;++j){snprintf(path,sizeof(path),"%.6s%s%s",base,relative[i],suffix[j]);if(fileXioGetStat(path,&st)>=0)status="Recovery needed";}
      snprintf(path,sizeof(path),"%s/cards.pending",base);if(fileXioGetStat(path,&st)>=0)status="Recovery needed";
    }
    snprintf(slots[i],32,"128 KB - %s",lunaText(status));
  }
}
static int ps1BrowseSets(Target *target,LunaPsxVmcSettings *settings) {
  char root[8];
  while(ps1ChooseRoot(root)) {
    Ps1SetEntry *sets=calloc(128,sizeof(*sets));if(!sets)return 0;
    ps1Busy("Reading shared memory cards...");int count=ps1LoadSets(root,sets),selected=0;BrowserSelector selector={.selected=-1};
    for(;;) {
      drawBrowserFrameSelected(lunaText("PlayStation memory cards"),root,
          target?lunaText("Choose a shared pair. Press Start in game settings to save."):lunaText("Create shared cards, or open a pair to manage its saves."),
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
static void uiVMCPlatformLoop(void) {
  static const char *const platforms[]={"PlayStation","PlayStation 2"};
  for(;;){int selected=ps1Action("Virtual Memory Cards","","Choose a console.",platforms,2,0);if(selected<0)return;
    if(!selected)ps1BrowseSets(NULL,NULL);else uiVMCManagerLoop();}
}
