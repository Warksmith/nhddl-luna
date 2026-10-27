/* Game identity and categories remain visible while a section is edited. */
typedef struct {
  char label[160], value[160], description[320];
} OptionsWorkspaceRow;

static float optionsWorkspaceOpacity = 1;
static int owX(int x) { return (int)lroundf(x * gsGlobal->Width / 640.0f); }
static int owY(int y) { return (int)lroundf(y * gsGlobal->Height / 448.0f); }
static uint64_t owInk(int r, int g, int b, float alpha) {
  int a = (int)(128 * optionsWorkspaceOpacity * alpha);
  if (getGlassColorPreset() == GLASS_COLOR_CYBER)
    return glassPresetColor(r,g,b,a);
  if (getGlassColorPreset() == GLASS_COLOR_BLACK) {
    int v = r > g ? r : g; if (b > v) v = b;
    return GS_SETREG_RGBA(v,v,v,a);
  }
  return GS_SETREG_RGBA(r,g,b,a);
}
static void owRect(int x, int y, int right, int bottom, uint64_t ink) {
  gsKit_prim_sprite(gsGlobal,x,y,right,bottom,8,ink);
}
static void owLine(int x, int y, int right, const char *text, uint64_t ink) {
  text=text ? text : "";
  float width=getLineDrawWidth(text),scale=width>right-x ? fmaxf(.85f,(right-x)/width) : 1;
  drawTextLineScaledCrisp(x,y,right,10,scale,ink,text);
}
static void owPanel(int x, int y, int right, int bottom, int focused) {
  drawGlassPanelWithOpacity(x,y,right,bottom,5,104,optionsWorkspaceOpacity);
  if (!focused) return;
  uint64_t glow=owInk(95,220,255,.16f), edge=owInk(95,220,255,.85f);
  owRect(x-2,y-2,right+2,y,glow); owRect(x-2,bottom,right+2,bottom+2,glow);
  owRect(x-2,y,x,bottom,glow); owRect(right,y,right+2,bottom,glow);
  owRect(x,y,right,y+2,edge); owRect(x,bottom-2,right,bottom,edge);
  owRect(x,y+2,x+2,bottom-2,edge); owRect(right-2,y+2,right,bottom-2,edge);
}
static void owFocus(int x, int y, int right, int height) {
  owRect(x,y,right,y+height,owInk(24,83,115,.8f));
  owRect(x,y,x+3,y+height,owInk(170,239,255,1));
  owRect(x+3,y,right,y+2,owInk(95,220,255,.42f));
  owRect(x+3,y+height-2,right,y+height,owInk(95,220,255,.25f));
}
static int owCategoryCount(const OptionsMenuState *s) {
  return s->target->platform == TARGET_PS1 ? 4 : GAME_SECTION_COUNT;
}
static GameSection owCategorySection(const OptionsMenuState *s, int row) {
  static const GameSection ps1[] = {GAME_MEMORY_CARDS,GAME_COMPATIBILITY,GAME_VIDEO,GAME_DISCS};
  return s->target->platform == TARGET_PS1 ? ps1[row] : (GameSection)row;
}
static const char *owSectionLabel(GameSection section) {
  if (section == GAME_MEMORY_CARDS) return lunaText("Memory cards");
  if (section == GAME_VIDEO_OUT) return lunaText("Video out");
  if (section == GAME_DISCS) return lunaText("Discs");
  return lunaText(gameSectionLabels[section]);
}
static GameSection owCurrentSection(const OptionsMenuState *s) {
  return s->gameSection == GAME_HUB ? owCategorySection(s,s->selectedGameHubRow) : s->gameSection;
}
static int owRowCount(const OptionsMenuState *s, GameSection section) {
  if (s->target->platform == TARGET_PS1)
    return section == GAME_MEMORY_CARDS ? 4 : section == GAME_DISCS ? 2 : 1;
  if (section == GAME_CHEATS) return 3 + s->cheatFile.entryCount;
  if (section == GAME_MEMORY_CARDS) return gameVMCEnabled(s->titleArguments) ? 4 : 3;
  return gameSectionRowCount(s,section);
}
static void owDescribeRow(OptionsMenuState *s, GameSection section, int row,
                          OptionsWorkspaceRow *out) {
  memset(out,0,sizeof(*out));
  const char *label="", *value="", *help="";
  if (s->target->platform == TARGET_PS1) {
    if (section == GAME_MEMORY_CARDS) {
      static const char *const labels[]={"Card assignment","Slot 1","Slot 2","Manage cards"};
      label=labels[row];
#ifdef LUNA_ENABLE_PSXCORE
      value=!s->ps1CardsReady ? s->vmcStatus[0] ? "Unavailable" : "Open" : row==0 ? s->ps1SetName :
            row<3 ? s->ps1SlotStatus[row-1] : ">";
#endif
      help="Games using the same cards share their saved data. Missing default cards are created at launch.";
    } else if (section == GAME_DISCS) {
      unsigned count=s->target->discs ? s->target->discs->count : 1;
      int disc=s->gameOptions.ps1StartDisc;
      label=row ? "Manage discs" : "Start disc";
      if (row) value=">";
      else if (disc<0 || (unsigned)disc>=count) value="Unavailable";
      else snprintf(out->value,sizeof(out->value),"%s %d / %u",lunaText("Disc"),disc+1,count);
      help=s->target->discs && s->target->discs->error[0] ? s->target->discs->error :
        "All discs use the same memory cards. Swapping controls are shown before launch.";
    } else {
      LunaGameRow option=section==GAME_VIDEO ? LUNA_GAME_PS1_HDTV_FIX : LUNA_GAME_PS1_CODE_VALIDATION;
      label=gameRowLabels[option]; value=lunaGameOptionsValue(&s->gameOptions,option);
      help=gameRowDescriptions[option];
    }
  } else if (section == GAME_MEMORY_CARDS) {
    static const char *const labels[]={"VMC slot 1","VMC slot 2","Card assignment","Disable virtual cards"};
    label=labels[row]; value=row<2 ? s->gameOptions.vmcSlotLabel[row] : ">";
    help=row==2 ? "Create cards for this game, choose existing cards or use shared cards." :
         row==3 ? "Use physical cards in both slots for this game." : gameRowDescriptions[LUNA_GAME_VMC_SLOT1+row];
  } else if (section == GAME_CHEATS) {
    if (row==0) { label="Enable cheats"; value=s->cheats.enabled ? "On" : "Off"; }
    else if (row==1) { label="Clear selections"; value=">"; }
    else if (row==2) { label="Reload file"; value=">"; }
    else {
      const LunaCheatEntry *entry=&s->cheatFile.entries[row-3];
      label=entry->name;
      value=entry->required ? "Required" : lunaCheatSelected(&s->cheats,entry->id) ? "On" : "Off";
      snprintf(out->description,sizeof(out->description),"%s: %d %s. %s",entry->name,entry->pairCount,
          lunaText("lines"),lunaText(entry->required ? "Included with selected cheats." : "Toggle this whole code block."));
    }
    if (s->cheatLoadResult || row==2) help=s->cheatStatus;
    else if (row<3) snprintf(out->description,sizeof(out->description),"%d %s. %s",s->cheats.count,
        lunaText("selected"),lunaText(s->cheats.enabled ? "Changes save automatically. Square to test." :
                                                      "Cheats are off; selections are retained."));
  } else if (section == GAME_VIDEO_OUT) {
    label=lunaGameVideoModeLabel(s->videoOutOpl,row);
    value=(s->videoOutOpl ? s->gameOptions.oplVideoMode : s->gameOptions.videoMode)==row ? "On" : "Off";
    help=row==0 ? "Keep the game's original output. Left/right changes tabs." :
                  "Select one output per core. Left/right changes tabs.";
  } else {
    LunaGameRow option=gameSectionRow(s,section,row);
    label=gameRowLabels[option]; help=gameRowDescriptions[option];
    value=option==LUNA_GAME_PS2_LOGO ? gamePS2LogoValue(s) : lunaGameOptionsValue(&s->gameOptions,option);
  }
  snprintf(out->label,sizeof(out->label),"%s",section==GAME_CHEATS && row>=3 ? label : lunaText(label));
  if (!out->value[0]) snprintf(out->value,sizeof(out->value),"%s",lunaText(value));
  if (help[0]) snprintf(out->description,sizeof(out->description),"%s",lunaText(help));
}
static void owSummary(OptionsMenuState *s, GameSection section, char *out, size_t size) {
  if (section==GAME_MEMORY_CARDS) {
    snprintf(out,size,"%s",lunaText(s->target->platform==TARGET_PS1 ?
#ifdef LUNA_ENABLE_PSXCORE
        s->ps1CardsReady ? s->ps1SetName : s->vmcStatus[0] ? "Unavailable" : "Open to load cards"
#else
        "Shared cards"
#endif
        : gameVMCEnabled(s->titleArguments) ? "Enabled" : "Physical cards"));
  } else if (section==GAME_COMPATIBILITY) {
    int enabled=0;
    if (s->target->platform==TARGET_PS1) enabled=s->gameOptions.ps1CodeValidation!=0;
    else {
      unsigned mask=gameUsesOpl(s) ? s->gameOptions.oplCompat : s->gameOptions.compat;
      for (;mask;mask>>=1) enabled+=mask&1;
      if (!gameUsesOpl(s)) enabled+=s->gameOptions.neutrinoIgrDisabled!=0;
    }
    if (enabled) snprintf(out,size,lunaText("%d enabled"),enabled);
    else snprintf(out,size,"%s",lunaText("Default"));
  } else if (section==GAME_CHEATS) {
    snprintf(out,size,"%s / %d %s",lunaText(s->cheats.enabled ? "On" : "Off"),s->cheats.count,lunaText("selected"));
  } else if (section==GAME_LAUNCH) {
    snprintf(out,size,"%d %s",gameLaunchEnabled(s),lunaText("enabled"));
  } else if (section==GAME_DISCS) {
    snprintf(out,size,"%u %s",s->target->discs ? s->target->discs->count : 1,lunaText("discs"));
  } else if (section==GAME_VIDEO_OUT) {
    snprintf(out,size,"%s",s->videoOutOpl ? "OPL" : "Neutrino");
  } else {
    LunaGameRow option=section==GAME_CORE ? LUNA_GAME_CORE : s->target->platform==TARGET_PS1 ?
        LUNA_GAME_PS1_HDTV_FIX : gameUsesOpl(s) ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_VIDEO_MODE;
    snprintf(out,size,"%s",lunaGameOptionsValue(&s->gameOptions,option));
  }
}
static void drawOptionsWorkspaceContents(OptionsMenuState *s, int transitionProgress, int transitionMode) {
  (void)transitionProgress;
  if (!transitionMode) lunaOptionsRevealTick(&s->transition,uiNowMs());
  float open=s->transition.opacity;
  float shade=s->transition.shade;
  float position=s->transition.position;
  gsKit_clear(gsGlobal,BGColor); gsKit_TexManager_nextFrame(gsGlobal);
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  if (s->scene && s->scene->drawLibrary)
    s->scene->drawLibrary(s->scene->context,position*gsGlobal->Height);
  else drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF); gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  owRect(0,0,gsGlobal->Width,gsGlobal->Height,GS_SETREG_RGBA(0,0,0,(int)(128*shade)));
  optionsWorkspaceOpacity=open;
  int left=owX(24),right=owX(616),split=owX(208),detail=owX(222);
  int line=getFontLineHeight(),top=owY(116);
  int helpTop=gsGlobal->Height-footerHeight-2*line-12,bottom=helpTop-10;
  uint64_t bright=owInk(235,247,255,1),muted=owInk(156,181,205,1),accent=owInk(95,220,255,1);
  owPanel(left,owY(16),right,owY(104),0);
  int titleX=left+14;
  owLine(titleX,owY(25),right-12,lunaText("Game settings"),muted);
  owLine(titleX,owY(49),right-12,s->target->name,bright);
  char identity[192];
  snprintf(identity,sizeof(identity),"%s  /  %s  /  %s",s->target->platform==TARGET_PS1 ? "PlayStation" : "PlayStation 2",
      s->target->id ? s->target->id : "",s->target->platform==TARGET_PS1 ? "PSXCore" : gameUsesOpl(s) ? "OPL" : "Neutrino");
  owLine(titleX,owY(76),right-12,identity,muted);
  int editing=s->gameSection!=GAME_HUB,count=owCategoryCount(s);
  owPanel(left,top,split,bottom,!editing);
  owPanel(detail,top,right,bottom,editing);
  int catStep=(bottom-top-24)/count;
  if (catStep>line+14) catStep=line+14;
  for (int i=0;i<count;i++) {
    int y=top+12+i*catStep,active=i==s->selectedGameHubRow;
    if (active) {
      if (!editing) owFocus(left+8,y-3,split-8,catStep-2);
      else {
        owRect(left+8,y-3,split-8,y+catStep-5,owInk(29,76,107,.28f));
        owRect(left+8,y-3,left+11,y+catStep-5,owInk(95,220,255,.35f));
      }
    }
    owLine(left+18,y,split-12,owSectionLabel(owCategorySection(s,i)),active ? bright : muted);
  }
  GameSection section=owCurrentSection(s);
  char summary[160]; owSummary(s,section,summary,sizeof(summary));
  owLine(detail+14,top+10,right-14,owSectionLabel(section),editing ? accent : bright);
  int rowsTop=top+line+14;
  owLine(detail+14,rowsTop,right-14,summary,muted);
  rowsTop+=line+12;
  int rowCount=owRowCount(s,section);
  int selected=editing ? s->selectedGameRow : s->workspaceRows[section];
  if (selected<0 || selected>=rowCount) selected=0;
  if (editing) s->selectedGameRow=selected;
  int stacked=section==GAME_MEMORY_CARDS || section==GAME_CORE;
  int step=stacked ? 2*line+10 : line+12,visible=(bottom-rowsTop-8)/step;
  if (visible<1) visible=1;
  int first=s->workspaceFirstRow;
  if (selected<first) first=selected;
  if (selected>=first+visible) first=selected-visible+1;
  if (first<0) first=0;
  s->workspaceFirstRow=first;
  if (s->selector.initialized && first!=s->cheatFirstRow) s->selector.initialized=0;
  s->cheatFirstRow=first;
  int selectorY=optionsSelectorY(&s->selector,s->page,section*10000+selected,
      rowsTop+(selected-first)*step);
  for (int i=first;i<rowCount && i<first+visible;i++) {
    OptionsWorkspaceRow row; owDescribeRow(s,section,i,&row);
    int y=rowsTop+(i-first)*step,rowRight=right-14-(rowCount>visible ? 12 : 0);
    if (editing && i==selected) owFocus(detail+8,selectorY-3,rowRight,step-3);
    int valueWidth=row.value[0] ? (int)ceilf(getLineDrawWidth(row.value)) : 0;
    int maxValue=stacked ? rowRight-detail-30 : (rowRight-detail-30)*45/100;
    if (valueWidth>maxValue) valueWidth=maxValue;
    int valueLeft=rowRight-valueWidth-6;
    owLine(detail+17,y,row.value[0] && !stacked ? valueLeft-10 : rowRight-6,row.label,
        editing && i==selected ? bright : muted);
    if (row.value[0]) owLine(stacked ? detail+25 : valueLeft,stacked ? y+line+2 : y,rowRight-6,row.value,
        editing && i==selected ? accent : muted);
  }
  if (rowCount>visible && open>=1) drawListScrollbar(right-17,rowsTop,bottom-8,rowCount,first,visible,11);
  OptionsWorkspaceRow selectedRow; owDescribeRow(s,section,selected,&selectedRow);
  const char *help=s->saveError ? (s->saveErrorLabel ? s->saveErrorLabel : "Could not save game settings") :
      s->vmcStatus[0] ? s->vmcStatus : selectedRow.description;
  if (!editing && section==GAME_CHEATS) help="Select individual codes from CHT/<title ID>.cht on this drive.";
  drawTextWindow(left+8,helpTop,right-8,gsGlobal->Height-footerHeight-4,10,
      s->saveError ? ErrorTextColor : muted,ALIGN_LEFT,lunaText(help));
  if (!s->saveError && s->savedAt && uiNowMs()-s->savedAt<1800)
    owLine(right-owX(86),owY(25),right-12,lunaText("Saved"),accent);
  const char *action=!editing ? "Open" : section==GAME_VIDEO_OUT ? "Select" :
      section==GAME_COMPATIBILITY || (s->target->platform==TARGET_PS1 && section==GAME_VIDEO) ? "Toggle" : "Change";
#ifdef LUNA_ENABLE_PSXCORE
  if (editing && section==GAME_MEMORY_CARDS && s->target->platform==TARGET_PS1 && !s->ps1CardsReady) action="Retry";
#endif
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Select")},{ICON_CROSS,lunaText(action)},
      {ICON_SQUARE,lunaText(s->target->platform==TARGET_PS1 ? "Launch" : "Test")},
      {ICON_TRIANGLE,lunaText(editing ? "Back" : "Close")}};
  if (open>=.999f)
    drawPromptBar(left,gsGlobal->Height-footerHeight,right,gsGlobal->Height,12,muted,(PromptBar){NULL,prompts,4});
  optionsWorkspaceOpacity=1;
  gsKit_set_test(gsGlobal,GS_ZTEST_ON);
}

static void drawTitleOptionsFrame(OptionsMenuState *s, int progress, int mode) {
  drawOptionsWorkspaceContents(s,progress,mode);
  presentOptionsFrame();
}

static void optionsWorkspaceBackdrop(void *context) {
  drawOptionsWorkspaceContents(context,0,0);
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
}

static int optionsWorkspaceInput(OptionsMenuState *s, int input) {
  GameSection before=s->gameSection;
  if (before!=GAME_HUB) s->workspaceRows[before]=s->selectedGameRow;
  if (input & (PAD_L1|PAD_R1)) {
    int delta=(input&PAD_L1) ? -1 : 1,count=owCategoryCount(s);
    s->selectedGameHubRow=(s->selectedGameHubRow+count+delta)%count;
    s->gameSection=GAME_HUB; s->workspaceFirstRow=0; s->selector.initialized=0;
    s->vmcStatus[0]=0;
    return 0;
  }
  if (before==GAME_HUB) {
    if (input & PAD_LEFT) return 0;
    if (input & (PAD_CROSS|PAD_CIRCLE|PAD_RIGHT)) {
      GameSection section=owCategorySection(s,s->selectedGameHubRow);
      if (section==GAME_CORE) { s->gameSection=section; s->vmcStatus[0]=0; }
      else handleGameInput(s,PAD_CROSS);
      s->selectedGameRow=s->workspaceRows[section];
      s->workspaceFirstRow=0; s->selector.initialized=0;
      return 0;
    }
  }
  int result=handleGameInput(s,input);
  if (s->gameSection!=before || s->gameSection==GAME_HUB) {
    s->workspaceFirstRow=0; s->selector.initialized=0;
  }
  return result;
}
