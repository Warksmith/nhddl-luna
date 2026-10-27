#include "cube_glass.inc"
static void cubeText(float cx,float y,uint64_t color,const char *text) {
  /* Use the active LUNA font at its native size, just like List view. */
  drawText((int)cubeSX(cx)-getLineWidth(text)/2,(int)cubeSY(y),6,
      (int)cubeSX(635),0,color,text);
}
static uint64_t cubeTextFade(uint64_t color,float opacity) {
  unsigned alpha=(unsigned)((color>>24)&255);
  return (color&~(0xffULL<<24))|((uint64_t)(alpha*cubeClamp(opacity))<<24);
}
void uiCubeSubmenuFocusDraw(int left,int top,int right,int bottom,float opacity) {
  gsKit_prim_sprite(gsGlobal,left,top,right,bottom,6,
      glassPresetColor(47,100,132,(int)(70*opacity)));
  uint64_t accent=getGlassColorPreset()==GLASS_COLOR_BLACK?
      cubeColor(200,200,200,opacity*.75f):glassPresetColor(112,208,255,(int)(96*opacity));
  gsKit_prim_sprite(gsGlobal,left,top,left+2,bottom,7,accent);
}
void uiCubeSubmenuPageDraw(const LunaCubePage *page,int selected,
                          LunaCubePageFocus *focus,float opacity) {
  int left=(int)cubeSX(32),right=(int)cubeSX(506),line=getFontLineHeight();
  int step=line+line/3,top=(int)cubeSY(127),bottom=(int)cubeSY(370);
  int visible=(bottom-top)/step;
  if(visible<1)visible=1;
  int first=selected/visible*visible,y=top+(selected-first)*step;
  uint32_t now=uiNowMs();
  unsigned duration=focus->glideMs>0?(unsigned)focus->glideMs:110;
  float progress=cubeClamp((now-focus->startMs)/(float)duration);
  if(focus->selected<0||focus->first!=first) {
    focus->fromY=focus->toY=y*256;focus->selected=selected;focus->first=first;
  } else if(focus->selected!=selected) {
    focus->fromY+=(int)((focus->toY-focus->fromY)*
        lunaNavEase((int)(progress*1000)))/1000;
    focus->toY=y*256;focus->startMs=now;focus->selected=selected;progress=0;
  }
  int focusY=(focus->fromY+(focus->toY-focus->fromY)*
      lunaNavEase((int)(progress*1000))/1000)/256;
  uint64_t ink=cubeTextFade(HeaderTextColor,opacity);
  uint64_t bright=cubeColor(240,250,255,opacity);
  drawTextWindow(left,(int)cubeSY(22),right,(int)cubeSY(22)+line,6,ink,
                 ALIGN_LEFT,lunaText("System Configuration"));
  drawTextWindow(left,(int)cubeSY(60),right,(int)cubeSY(60)+line,6,
                 cubeColor(232,232,70,opacity),ALIGN_LEFT,lunaText(page->title));
  drawGlassPanelWithOpacity((int)cubeSX(20),(int)cubeSY(97),(int)cubeSX(520),
                            (int)cubeSY(385),5,104,opacity);
  if(page->count>0)uiCubeSubmenuFocusDraw(left-4,focusY-1,right-10,focusY+line+2,opacity);
  for(int row=first;row<page->count&&row<first+visible;row++) {
    const char *label=lunaText(page->label(page->context,row));
    const char *value=page->value?lunaText(page->value(page->context,row)):NULL;
    int valueWidth=value&&value[0]?(int)getLineWidth(value)+12:0;
    int nameRight=right-12-valueWidth,available=nameRight-left-8;
    float natural=getLineWidth(label),scale=natural>0?fminf(1,available/natural):1;
    uint64_t color=row==selected?bright:ink;
    int rowY=top+(row-first)*step;
    if(scale>0)drawTextLineScaledCrisp(left+4,rowY,nameRight,7,scale,color,label);
    if(valueWidth)drawTextWindow(nameRight+8,rowY,right-12,rowY+line,7,color,ALIGN_RIGHT,value);
  }
  if(page->count==0)drawTextWindow(left+4,top,right,top+line,7,ink,
                                  ALIGN_LEFT,lunaText("Nothing to show"));
  if(opacity>=.999f)drawListScrollbar(right-6,top,top+visible*step,
                                     page->count,first,visible,7);
  const char *description=page->description&&page->count>0?
      page->description(page->context,selected):page->status;
  if(description)drawTextWindow(left,(int)cubeSY(393),(int)cubeSX(608),
      (int)cubeSY(425),7,ink,ALIGN_LEFT,lunaText(description));
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Select")},
      {ICON_CROSS,lunaText(page->adjust?"Change":"Select")},{ICON_CIRCLE,lunaText("Back")}};
  drawPromptBar(left,(int)cubeSY(426),(int)cubeSX(608),(int)cubeSY(464),7,ink,
                (PromptBar){NULL,prompts,3});
}
static void cubeDraw(CubeMenu *m,const char *status,int confirmShutdown,const LunaCubePage *page,const LunaCubeWorkspace *workspace,const LunaCubeSubmenu *submenu) {
  static const char *labels[]={"Virtual Memory Cards","Storage Devices","Language","Game Defaults","Exit LUNA","Shutdown"};
  gsKit_clear(gsGlobal,BGColor);gsKit_TexManager_nextFrame(gsGlobal);
  GSTEST savedTest=*gsGlobal->Test;uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  float entry=cubeClamp(m->entry/500);
  float overall=entry*(m->entering?1-cubeClamp((m->exit-1140)/300):1-cubeClamp(m->exit/500));
  float text=overall*(m->entering?1-cubeClamp(m->exit/200):1);
  if(m->submenuOpen)text*=1-cubeSmooth(m->submenuAge/(CUBE_SUBMENU_MS*.6f));
  if(workspace) {
    workspace->draw(workspace->context,text);
    goto present;
  }
  CubeVec camera={0,0,0};
  float submenuProgress=m->submenuOpen?m->submenuAge/CUBE_SUBMENU_MS:0;
  if(m->submenuOpen) {
    CubeVec anchor=cubePosition(m->selected%CUBE_COUNT,m->time);
    float dolly=cubeSmooth(submenuProgress);
    /* Approach the selected crystal, letting it pass the right edge as the
     * submenu arrives. Stop 96 units away to keep its faces safely visible. */
    camera=(CubeVec){(-anchor.x-83)*dolly,(anchor.y-10.56f)*dolly,
        (500+anchor.z-96)*dolly};
  }
  if(m->entering) {
    /* LUNA easing adaptation. The native two-manager blend is not yet
     * recovered; preserve its 1200/1440 ms camera/exit timing landmarks. */
    float t=cubeClamp(m->exit/1200),u=1-powf(1-t,3);
    camera=(CubeVec){m->cameraTarget.x*u,m->cameraTarget.y*u,m->cameraTarget.z*u};
  }
  cubeSceneScale=1;
  cubeSceneOffsetX=cubeSceneOffsetY=0;
  CubeGlass scene[CUBE_COUNT];int order[CUBE_COUNT];
  for(int i=0;i<CUBE_COUNT;i++) {
    float fade=overall;
    if(m->submenuOpen) {
      if(i==m->selected%CUBE_COUNT)
        fade*=1-cubeSmooth((submenuProgress-.58f)/.42f);
      else fade*=1-cubeSmooth(submenuProgress/.45f);
    }
    if(m->entering&&i!=m->selected%CUBE_COUNT)fade*=1-cubeClamp(m->exit/1000);
    cubeGlassBuild(&scene[i],i,m,camera,fade);order[i]=i;
  }
  for(int i=1;i<CUBE_COUNT;i++) {
    int index=order[i],j=i;
    while(j>0&&scene[order[j-1]].depth<scene[index].depth){order[j]=order[j-1];j--;}
    order[j]=index;
  }
  for(int i=0;i<CUBE_COUNT;i++)cubeGlassDraw(&scene[order[i]],camera);
  cubeRestoreGlassState();
  cubeText(60,20,cubeColor(185,210,235,text),"LUNA");
  cubeText(175,page?105:128,cubeColor(232,232,70,text),lunaText(page?page->title:"System Configuration"));
  if(status)cubeText(320,53,cubeColor(215,229,244,text),status);
  const int listTop=(int)cubeSY(page?140:167);
  const int rowStep=getFontLineHeight()+getFontLineHeight()/3;
  const int listCenter=(int)cubeSX(175),rowRight=(int)cubeSX(300);
  int visible=(int)((cubeSY(352)-listTop)/rowStep);
  if(visible<1)visible=1;
  if(visible>m->itemCount)visible=m->itemCount;
  int first=m->selected>=visible?m->selected-visible+1:0;
  const char *selectedLabel=lunaText(page?page->label(page->context,m->selected):labels[m->selected]);
  const int selectedLeft=listCenter-getLineWidth(selectedLabel)/2;
  int textRight=selectedLeft+getLineWidth(selectedLabel);
  if(textRight>rowRight-12)textRight=rowRight-12;
  if(text>.01f)drawPSBBNFocusGlow(selectedLeft,(int)(listTop+rowStep*fmaxf(0,fminf(visible-1,m->row-first))),rowRight,textRight);
  for(int i=first;i<first+visible;i++) {
    float alpha=text*(i==m->selected?1:.65f);
    const char *label=lunaText(page?page->label(page->context,i):labels[i]);
    uint64_t color=i==m->selected?GS_SETREG_RGBA(0xF0,0xFA,0xFF,0x80):HeaderTextColor;
    drawText(listCenter-getLineWidth(label)/2,listTop+rowStep*(i-first),6,rowRight,0,
        cubeTextFade(color,alpha),label);
  }
  if(page&&page->value)cubeText(175,355,cubeColor(240,250,255,text),lunaText(page->value(page->context,m->selected)));
  if(page&&page->description) {
    const char *description=page->description(page->context,m->selected);
    if(description)drawTextWindow((int)cubeSX(25),(int)cubeSY(381),(int)cubeSX(615),(int)cubeSY(424),6,
        cubeColor(190,211,232,text),ALIGN_HCENTER,lunaText(description));
  }
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Select")},{ICON_CROSS,lunaText("Open")},{ICON_CIRCLE,lunaText("Back")}};
  ButtonPrompt confirm[]={{ICON_CROSS,lunaText("Confirm")},{ICON_CIRCLE,lunaText("Cancel")}};
  if(page&&page->adjust)prompts[1].label=lunaText("Change");
  if(confirmShutdown)cubeText(175,365,cubeColor(240,250,255,text),lunaText("Power off the console?"));
  /* Controller artwork has its own alpha convention. Hand the prompt row to
   * the submenu when its contents appear instead of drawing both sets of icons. */
  if(!m->submenuOpen||m->submenuAge<=CUBE_SUBMENU_MS*.28f)
    drawPromptBar((int)cubeSX(155),(int)cubeSY(426),(int)cubeSX(580),(int)cubeSY(456),4,
        cubeColor(185,210,235,text),(PromptBar){NULL,confirmShutdown?confirm:prompts,confirmShutdown?2:3});
  if(m->submenuOpen&&submenu) {
    float opacity=cubeSmooth((m->submenuAge/CUBE_SUBMENU_MS-.28f)/.72f);
    if(opacity>0)submenu->draw(submenu->context,opacity);
  }
present:
  cubeSceneScale=1;cubeSceneOffsetX=cubeSceneOffsetY=0;
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest;gsKit_set_test(gsGlobal,GS_ZTEST_ON);
  gsKit_queue_exec(gsGlobal);gsKit_finish();gsKit_sync_flip(gsGlobal);usleep(1000);
}
