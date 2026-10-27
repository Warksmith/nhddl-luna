/* Appearance stays on one screen: categories, focused controls, fixed actions. */
enum { AP_FOCUS_CATEGORIES,AP_FOCUS_CONTROLS,AP_FOCUS_ACTIONS,AP_FOCUS_EXIT };
static float apX(float x) {return x*gsGlobal->Width/640;}
static float apY(float y) {return y*gsGlobal->Height/480;}
static uint64_t apInk(int r,int g,int b,float opacity) {
  if(getGlassColorPreset()==GLASS_COLOR_CYBER)
    return glassPresetColor(r,g,b,(int)(128*fmaxf(0,fminf(1,opacity))));
  return GS_SETREG_RGBA(r,g,b,(int)(128*fmaxf(0,fminf(1,opacity))));
}
static void apRect(float x,float y,float width,float height,uint64_t color) {
  gsKit_prim_sprite(gsGlobal,apX(x),apY(y),apX(x+width),apY(y+height),5,color);
}
static void apFocusFrame(float x,float y,float width,float height,float opacity) {
  uint64_t glow=apInk(95,220,255,opacity*.18f);
  uint64_t edge=apInk(95,220,255,opacity*.9f);
  // Broad, steady edges remain legible in interlaced output.
  apRect(x-2,y-2,width+4,2,glow);
  apRect(x-2,y+height,width+4,2,glow);
  apRect(x-2,y,2,height,glow);
  apRect(x+width,y,2,height,glow);
  apRect(x,y,width,2,edge);
  apRect(x,y+height-2,width,2,edge);
  apRect(x,y+2,2,height-4,edge);
  apRect(x+width-2,y+2,2,height-4,edge);
}
static void apFocusRow(float x,float y,float width,float height,float opacity) {
  apRect(x,y,width,height,apInk(24,83,115,opacity*.88f));
  apFocusFrame(x,y,width,height,opacity*.8f);
  apRect(x,y,3,height,apInk(170,239,255,opacity));
}
static void apGlassPanel(float x,float y,float width,float height,float opacity) {
  // Use the same preset-aware glass as the rest of LUNA. The actual panels
  // are the live color feedback; no separate sample palette is necessary.
  drawGlassPanelWithFillAlpha((int)roundf(apX(x)),(int)roundf(apY(y)),
      (int)roundf(apX(x+width)),(int)roundf(apY(y+height)),5,
      (int)(128*fmaxf(0,fminf(1,opacity))));
}
static void apLine(float x,float y,float width,uint64_t color,const char *text) {
  text=lunaText(text);
  float natural=getLineWidth(text),scale=natural>0?fminf(1,apX(width)/natural):1;
  drawTextLineScaledCrisp((int)roundf(apX(x)),(int)roundf(apY(y)),(int)roundf(apX(x+width)),
      7,scale,color,text);
}
static const int appearanceWorkspaceCategories[]={AP_BACKGROUND,AP_VIEWS,AP_COLORS,AP_TEXT,AP_SOUND};
#define AP_WORKSPACE_CATEGORIES 5
static int appearanceWorkspaceCategoryIndex(const AppearanceEditor *s) {
  for(int i=0;i<AP_WORKSPACE_CATEGORIES;i++)if(appearanceWorkspaceCategories[i]==s->category)return i;
  return 0;
}
static int appearanceWorkspaceEffects(const AppearanceEditor *s) {
  return s->pending.background==LIBRARY_BACKGROUND_ORBS?4+ORB_SHAPE_PLAYTIME:
    s->pending.background==LIBRARY_BACKGROUND_SPACE_FLIGHT?2:0;
}
static int appearanceWorkspaceRows(AppearanceEditor *s) {
  return s->category==AP_BACKGROUND?1+appearanceWorkspaceEffects(s):appearanceRowCount(s);
}
static const char *appearanceWorkspaceRowText(AppearanceEditor *s,int row,int value) {
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  const char *text=value?appearanceRowValue(s,row):appearanceRowLabel(s,row);
  s->category=category;return text;
}
static const char *appearanceWorkspaceDescription(AppearanceEditor *s,int row) {
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  const char *text=appearanceRowDescription(s,row);s->category=category;return text;
}
static void appearanceWorkspaceAdjust(AppearanceEditor *s,int row,int direction) {
  int category=s->category;
  if(category==AP_BACKGROUND&&row>0) {s->category=AP_EFFECTS;row--;}
  appearanceAdjust(s,row,direction);s->category=category;
  if(s->rows[category]>=appearanceWorkspaceRows(s))s->rows[category]=0;
}
static void appearanceWorkspaceDraw(void *context,float opacity) {
  AppearanceEditor *s=context;
  uint64_t bright=apInk(235,247,255,opacity),muted=apInk(156,181,205,opacity);
  uint64_t cyan=apInk(95,220,255,opacity);
  apGlassPanel(20,66,224,336,opacity*.82f);
  // Background selection uses the actual full-screen background, not a copy.
  if(s->category!=AP_BACKGROUND)apGlassPanel(260,94,350,252,opacity*.76f);
  if(s->focus==AP_FOCUS_CATEGORIES)apFocusFrame(20,66,224,336,opacity);
  else if(s->focus==AP_FOCUS_CONTROLS) {
    if(s->category!=AP_BACKGROUND)apFocusFrame(260,94,350,252,opacity);
    apRect(260,67,3,23,cyan);
  }
  apLine(32,26,330,bright,"Appearance");
  apLine(32,75,195,s->focus==AP_FOCUS_CATEGORIES?cyan:muted,
      s->focus==AP_FOCUS_CATEGORIES?"Choose a category":"Categories");
  for(int i=0;i<AP_WORKSPACE_CATEGORIES;i++) {
    int category=appearanceWorkspaceCategories[i],active=category==s->category;
    if(active) {
      if(s->focus==AP_FOCUS_CATEGORIES)apFocusRow(28,110+42*i,208,32,opacity);
      else {
        apRect(28,110+42*i,208,32,apInk(29,76,107,opacity*.34f));
        apRect(28,110+42*i,3,32,apInk(95,220,255,opacity*.4f));
      }
    }
    apLine(40,115+42*i,184,active?bright:muted,categories[category]);
  }
  apLine(32,374,196,appearanceDirty(s)?cyan:muted,
      s->loadError?"Settings unavailable":appearanceDirty(s)?"Unsaved changes":"All changes saved");
  apLine(272,68,330,s->focus==AP_FOCUS_CONTROLS?cyan:bright,categories[s->category]);
  int count=appearanceWorkspaceRows(s),row=s->rows[s->category];
  if(row>=count)row=s->rows[s->category]=0;
  if(s->category==AP_BACKGROUND) {
    apGlassPanel(270,101,330,39,opacity*.72f);
    if(s->focus==AP_FOCUS_CONTROLS&&row==0)apFocusRow(270,101,330,39,opacity);
    apLine(281,111,19,cyan,"<");
    apLine(306,111,257,bright,backgrounds[s->pending.background]);
    apLine(576,111,19,cyan,">");
    if(count>1) {
      apLine(280,155,300,muted,s->pending.background==LIBRARY_BACKGROUND_ORBS?"Orb effects":"Space Flight effects");
      int selected=row>0?row-1:0,visible=3;
      int first=selected>=visible?selected-visible+1:0;
      for(int i=first;i<count-1&&i<first+visible;i++) {
        int y=183+(i-first)*50;
        apGlassPanel(270,y-2,330,45,opacity*.72f);
        if(s->focus==AP_FOCUS_CONTROLS&&row==i+1)apFocusRow(270,y-2,330,45,opacity);
        apLine(281,y,306,row==i+1?bright:muted,appearanceWorkspaceRowText(s,i+1,0));
        apLine(294,y+22,290,row==i+1?cyan:muted,appearanceWorkspaceRowText(s,i+1,1));
      }
      if(count-1>visible) {
        char position[32];snprintf(position,sizeof(position),"%d / %d",selected+1,count-1);
        apLine(539,339,65,muted,position);
      }
    }
  } else {
    const int visible=5,first=row>=visible?row-visible+1:0;
    for(int i=first;i<count&&i<first+visible;i++) {
      int y=103+(i-first)*47;
      if(i==row&&s->focus==AP_FOCUS_CONTROLS)
        apFocusRow(270,y-2,330,44,opacity);
      apLine(281,y,306,i==row?bright:muted,appearanceRowLabel(s,i));
      apLine(294,y+21,290,i==row?cyan:muted,appearanceRowValue(s,i));
    }
    if(count>visible) {
      char position[32];snprintf(position,sizeof(position),"%d / %d",row+1,count);
      apLine(539,350,65,muted,position);
    }
    if(count==1&&s->category==AP_TEXT) {
      apLine(293,218,280,bright,"Aa Bb Cc 0123456789");
      apLine(293,260,280,muted,"Game Library / Settings");
    }
  }
  const char *description=s->message?s->message:
    s->focus==AP_FOCUS_ACTIONS?(s->action?"Restore the last saved appearance.":"Save your previewed changes."):
    s->focus==AP_FOCUS_CATEGORIES?appearanceHubDescription(s,s->category):
    s->category==AP_BACKGROUND&&row==0?(count>1?
        "Left / Right chooses a background. Down opens its effects.":
        "Left / Right chooses a background."):
    appearanceWorkspaceDescription(s,row);
  drawTextWindow((int)apX(272),(int)apY(365),(int)apX(610),(int)apY(411),7,
      muted,ALIGN_LEFT,lunaText(description));
  for(int i=0;i<2;i++) {
    int active=s->focus==AP_FOCUS_ACTIONS&&s->action==i;
    if(active)apFocusRow(278+i*165,421,153,29,opacity);
    else apRect(278+i*165,421,153,29,apInk(9,24,41,opacity*.9f));
    apLine(294+i*165,425,126,active?bright:muted,i?"Discard":"Apply");
  }
  if(s->focus==AP_FOCUS_EXIT) {
    // Dim the complete workspace before drawing the active dialog and controls.
    // Keep the veil neutral even when the Cyber preset recolors interface ink.
    gsKit_prim_sprite(gsGlobal,0,0,gsGlobal->Width,gsGlobal->Height,8,
        GS_SETREG_RGBA(0,0,0,(int)(70*fmaxf(0,fminf(1,opacity)))));
    apGlassPanel(148,143,344,186,opacity*.98f);
    apLine(170,159,300,bright,"Unsaved changes");
    for(int i=0;i<3;i++) {
      if(s->exitChoice==i)apFocusRow(161,194+39*i,318,34,opacity);
      apLine(179,201+39*i,280,s->exitChoice==i?bright:muted,
          (const char *const[]){"Apply","Discard","Keep editing"}[i]);
    }
    ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Select")},
        {ICON_CROSS,lunaText("Confirm")},{ICON_CIRCLE,lunaText("Keep editing")}};
    drawPromptBar((int)apX(20),(int)apY(454),(int)apX(620),(int)apY(479),7,
        muted,(PromptBar){NULL,prompts,3});
    return;
  }
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText(s->focus==AP_FOCUS_CONTROLS?"Adjust":"Select")},
      {ICON_CROSS,lunaText(s->focus==AP_FOCUS_CONTROLS?"Change":s->focus>=AP_FOCUS_ACTIONS?"Confirm":"Open")},
      {ICON_CIRCLE,lunaText(s->focus==AP_FOCUS_CATEGORIES?"Close":"Back")},
      {ICON_SQUARE,lunaText("Apply / Discard")}};
  drawPromptBar((int)apX(20),(int)apY(454),(int)apX(620),(int)apY(479),7,muted,(PromptBar){NULL,prompts,4});
}
static int appearanceWorkspaceInput(void *context,int input) {
  AppearanceEditor *s=context;
  if(!input)return 0;
  if(s->focus==AP_FOCUS_EXIT) {
    if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))s->focus=AP_FOCUS_CATEGORIES;
    else if(input&PAD_UP)s->exitChoice=(s->exitChoice+2)%3;
    else if(input&PAD_DOWN)s->exitChoice=(s->exitChoice+1)%3;
    else if(input&PAD_CROSS) {
      if(s->exitChoice==0) {if(!appearanceApply(s))return 1;s->focus=AP_FOCUS_ACTIONS;}
      else if(s->exitChoice==1) {if(!appearanceDiscard(s))return 1;s->focus=AP_FOCUS_ACTIONS;}
      else s->focus=AP_FOCUS_CATEGORIES;
    }
    return 0;
  }
  if(input&PAD_SQUARE) {s->focus=AP_FOCUS_ACTIONS;s->action=0;return 0;}
  if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START)) {
    if(s->focus!=AP_FOCUS_CATEGORIES) {s->focus=AP_FOCUS_CATEGORIES;return 0;}
    if(!appearanceDirty(s))return 1;
    s->focus=AP_FOCUS_EXIT;s->exitChoice=2;return 0;
  }
  if(s->focus==AP_FOCUS_CATEGORIES) {
    if(input&PAD_UP) {
      if(s->category==0) {s->focus=AP_FOCUS_ACTIONS;s->action=0;}
      else {s->category=appearanceWorkspaceCategories[appearanceWorkspaceCategoryIndex(s)-1];s->message=NULL;}
    } else if(input&PAD_DOWN) {
      if(appearanceWorkspaceCategoryIndex(s)==AP_WORKSPACE_CATEGORIES-1) {s->focus=AP_FOCUS_ACTIONS;s->action=0;}
      else {s->category=appearanceWorkspaceCategories[appearanceWorkspaceCategoryIndex(s)+1];s->message=NULL;}
    } else if(input&(PAD_CROSS|PAD_RIGHT))s->focus=AP_FOCUS_CONTROLS;
  } else if(s->focus==AP_FOCUS_CONTROLS) {
    int count=appearanceWorkspaceRows(s),*row=&s->rows[s->category];
    if(*row>=count)*row=0;
    if(input&PAD_UP)*row=(*row+count-1)%count;
    else if(input&PAD_DOWN) {
      if(*row==count-1) {s->focus=AP_FOCUS_ACTIONS;s->action=0;}
      else (*row)++;
    } else if(input&(PAD_LEFT|PAD_RIGHT|PAD_CROSS)) {
      appearanceWorkspaceAdjust(s,*row,input&PAD_LEFT?-1:1);
    }
  } else {
    if(input&(PAD_LEFT|PAD_RIGHT))s->action^=1;
    else if(input&PAD_UP)s->focus=AP_FOCUS_CONTROLS;
    else if(input&PAD_DOWN)s->focus=AP_FOCUS_CATEGORIES;
    else if(input&PAD_CROSS) {if(s->action)appearanceDiscard(s);else appearanceApply(s);}
  }
  return 0;
}
static int appearanceWorkspaceSelection(void *context) {return ((AppearanceEditor *)context)->category;}
