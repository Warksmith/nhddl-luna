// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/language.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "ui/file_manager.h"
#include "opl_devices.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include "devices/devices.h"
#include "vmc_create.h"
#include "storage.h"
#include "cheat_storage.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OPTIONS_OPEN_DURATION_MS 360
#define OPTIONS_CLOSE_DURATION_MS 180
#define OPTIONS_SELECTOR_GLIDE_DURATION_MS 110
#define OPTIONS_SELECTOR_ROW_SCALE 256

typedef enum {
  OPTIONS_PER_GAME
} OptionsPage;

typedef enum {
  GAME_HUB = -1,
  GAME_MEMORY_CARDS,
  GAME_CORE,
  GAME_COMPATIBILITY,
  GAME_VIDEO,
  GAME_CHEATS,
  GAME_LAUNCH,
  GAME_SECTION_COUNT,
  GAME_VIDEO_OUT,
  GAME_DISCS
} GameSection;

typedef struct {
  OptionsPage page;
  int selectedRow;
  int fromRow;
  int toRow;
  uint32_t startMs;
  int initialized;
} OptionsSelector;


typedef struct {
  Target *target;
  char titleHeader[255];
  ArgumentList *titleArguments;
  LunaGameOptions gameOptions;
  int logoSetting;
  int coreSetting;
  int titleArgumentsChanged;
  int argumentsSaved;
  LunaCheatSettings cheats;
  LunaCheatFile cheatFile;
  int cheatsChanged;
  int cheatLoadResult;
  int cheatFirstRow;
  char cheatStatus[192];
  char cheatPath[PATH_MAX + 1];
  int saveError;
  const char *saveErrorLabel;
  char vmcStatus[192];
#ifdef LUNA_ENABLE_PSXCORE
  LunaPsxVmcSettings ps1Cards;
  int ps1CardsReady;
  int ps1CardsChanged;
  char ps1SetName[64], ps1SlotStatus[2][32];
#endif
  OptionsPage page;
  OptionsSelector selector;
  GameSection gameSection;
  int selectedGameHubRow;
  int selectedGameRow;
  int videoOutOpl;
  int videoOutFirstRow;
} OptionsMenuState;

static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const LunaCheatSettings *cheats);
static int uiVMCPickerLoop(Target *target, ArgumentList *arguments,
                           LunaGameOptions *gameOptions, int slot);

#ifdef LUNA_ENABLE_PSXCORE
static void loadPs1Cards(OptionsMenuState *state) {
  const char *error = uiPs1VmcLoad(state->target, &state->ps1Cards);
  state->ps1CardsReady = error == NULL;
  snprintf(state->vmcStatus, sizeof(state->vmcStatus), "%s", error ? error : "");
  if (error) {
    snprintf(state->ps1SetName, sizeof(state->ps1SetName), "%s", "Unavailable");
    for (int slot = 0; slot < 2; ++slot)
      snprintf(state->ps1SlotStatus[slot], sizeof(state->ps1SlotStatus[slot]), "%s", "Unavailable");
  } else uiPs1VmcDescribe(&state->ps1Cards, state->ps1SetName, state->ps1SlotStatus);
  state->selector.initialized = 0;
}
#endif

static const char *const gameRowLabels[LUNA_GAME_ROW_COUNT] = {
    "IOP: Fast reads", "IOP: Sync reads",
    "EE: Unhook syscalls", "IOP: Emulate DVD-DL",
    "IOP: Fix game buffer overrun", "Launch arguments",
    "VMC slot 1", "VMC slot 2",
    "Video out", "Field flipping",
    "Show PS2 logo", "Debug colors", "Game core",
    "Accurate reads", "Synchronous reads", "Unhook syscalls",
    "Skip videos", "Emulate DVD-DL", "Disable IGR", "Disable IGR",
    "Video out", "Field flipping", "Code Validation", "HDTV Fix"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Review every launch argument, including global overrides.",
    "Assign an existing OPL card image from this drive's VMC folder.",
    "Assign an existing OPL card image to the second slot.",
    "Open the Neutrino and OPL video output modes.",
    "Choose field flipping for a forced video mode.",
    "Choose Inherit to use the Global PS2 logo setting.",
    "Display debug colors while loading.",
    "Inherit or override the default core for this game.",
    "Use OPL's accurate disc-read behavior.",
    "Use OPL's synchronous disc-read method.",
    "Leave EE system calls unhooked in OPL.",
    "Skip PSS and Bink videos in OPL.",
    "Emulate a dual-layer DVD in OPL.",
    "Disable OPL's in-game reset for this title.",
    "Disable Neutrino's in-game return for this title.",
    "Open the Neutrino and OPL video output modes.",
    "Emulate field flipping when OPL forces a video output mode.",
    "Helps with non-boot/black-screen issues. Off by default.",
    "Send low-resolution gameplay as 480i (NTSC) or 576i (PAL) for TVs and HDMI adapters. Off by default."};

static const char *const gameSectionLabels[GAME_SECTION_COUNT] = {
    "Virtual memory cards", "Game core", "Compatibility", "Video", "Cheats", "Launch"};
static const char *const gameSectionDescriptions[GAME_SECTION_COUNT] = {
    "Enable virtual cards by assigning one to a slot.",
    "Choose Default to follow the Global game core setting.",
    "Change the active core's compatibility switches.",
    "Adjust this game's video output.",
    "Select individual codes from CHT/<title ID>.cht on this drive.",
    "Set the startup logo and review launch options."};
static const LunaGameRow gameSectionRows[GAME_SECTION_COUNT][5] = {
    {LUNA_GAME_VMC_SLOT1, LUNA_GAME_VMC_SLOT2},
    {LUNA_GAME_CORE},
    {LUNA_GAME_FAST_READS, LUNA_GAME_SYNC_READS,
     LUNA_GAME_UNHOOK_SYSCALLS, LUNA_GAME_DVD_DL,
     LUNA_GAME_BUFFER_OVERRUN},
    {LUNA_GAME_VIDEO_MODE, LUNA_GAME_FIELD_FLIP},
    {0},
    {LUNA_GAME_PS2_LOGO, LUNA_GAME_LAUNCH_ARGUMENTS,
     LUNA_GAME_DEBUG_COLORS}};
static const LunaGameRow oplCompatRows[LUNA_OPL_COMPAT_COUNT] = {
    LUNA_GAME_OPL_ACCURATE_READS, LUNA_GAME_OPL_SYNC_READS,
    LUNA_GAME_OPL_UNHOOK_SYSCALLS, LUNA_GAME_OPL_SKIP_VIDEOS,
    LUNA_GAME_OPL_DVD_DL, LUNA_GAME_OPL_DISABLE_IGR};
static const LunaGameRow neutrinoCompatRows[LUNA_NEUTRINO_COMPAT_ROW_COUNT] = {
    LUNA_GAME_FAST_READS, LUNA_GAME_SYNC_READS,
    LUNA_GAME_UNHOOK_SYSCALLS, LUNA_GAME_DVD_DL,
    LUNA_GAME_BUFFER_OVERRUN, LUNA_GAME_NEUTRINO_DISABLE_IGR};
static const int gameSectionRowCounts[GAME_SECTION_COUNT] = {4, 1, 6, 2, 3, 3};

static int gameUsesOpl(const OptionsMenuState *state) {
  return lunaOplDevice(state->target->device->mode) != NULL && state->gameOptions.oplCore;
}

static int gameSectionRowCount(const OptionsMenuState *state, GameSection section) {
  if (section == GAME_VIDEO_OUT)
    return lunaGameVideoModeCount(state->videoOutOpl);
  if (section == GAME_COMPATIBILITY)
    return gameUsesOpl(state) ? LUNA_OPL_COMPAT_COUNT :
                               LUNA_NEUTRINO_COMPAT_ROW_COUNT;
  return gameSectionRowCounts[section];
}

static LunaGameRow gameSectionRow(const OptionsMenuState *state,
                                  GameSection section, int row) {
  if (section == GAME_VIDEO && gameUsesOpl(state))
    return row == 0 ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_OPL_FIELD_FLIP;
  if (section == GAME_COMPATIBILITY)
    return gameUsesOpl(state) ? oplCompatRows[row] : neutrinoCompatRows[row];
  return gameSectionRows[section][row];
}


static int optionsHeadingY(void) {
  return headerHeight - getFontLineHeight() + getFontLineHeight() / 4;
}

static int optionsMenuTop(void) {
  return headerHeight + getFontLineHeight() + 8;
}

// Give Options its own scene without carrying library text into the menu.
static void drawOptionsSheet(void) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int panelTop = 78 - getFontLineHeight();
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, 32, panelTop + 2, width - 32,
                    height - footerHeight + 1, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(30, panelTop, width - 30, height - footerHeight + 3, 2);
}

static int optionsTransitionProgress(uint32_t elapsed, uint32_t duration) {
  int progress = elapsed >= duration ? 1000 : (int)(elapsed * 1000U / duration);
  return (int)((int64_t)progress * progress *
               (3000 - 2 * progress) / 1000000);
}

// The leading edge flows first; the trailing edge gathers behind the label.

// A continuous rounded silhouette keeps the liquid connected while it stretches.

static void drawOptionsBlackout(int alpha) {
  if (alpha <= 0)
    return;
  gsGlobal->PrimAlphaEnable = alpha >= 0x80 ? GS_SETTING_OFF : GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0x00, 0x00, 0x00, alpha));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

// Fade the completed Options frame in without sampling a framebuffer that
// double buffering will reuse on the next frame.
static void drawOptionsTransition(int progress, int closing) {
  drawOptionsBlackout((closing ? progress : 1000 - progress) * 0x80 / 1000);
}

static void presentOptionsFrame(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_sync_flip(gsGlobal);
  usleep(1000);
}

static void drawOptionsFooter(int gamePage, int argumentList, int placeholder, const char *actionLabel,
                              int gameDetail) {
  const int top = gsGlobal->Height - footerHeight;
  const int width = gsGlobal->Width;
  const int slot = width / 4;
  if (!placeholder) {
    const ButtonPrompt action[] = {
        {ICON_CROSS, argumentList ? lunaText("Toggle") : actionLabel ? actionLabel : lunaText("Change")}};
    drawPromptBar(8, top, slot, gsGlobal->Height, 0, HeaderTextColor,
                  (PromptBar){NULL, action, 1});
    if (gamePage || argumentList) {
      const ButtonPrompt test[] = {{ICON_SQUARE, lunaText("Test")}};
      drawPromptBar(slot, top, 2 * slot, gsGlobal->Height, 0,
                    HeaderTextColor, (PromptBar){NULL, test, 1});
    }
  }
  const ButtonPrompt back[] = {
      {ICON_TRIANGLE, argumentList || gameDetail ? lunaText("Back") :
                      lunaText("Close")}};
  drawPromptBar(3 * slot, top, width - 8, gsGlobal->Height, 0,
                HeaderTextColor, (PromptBar){NULL, back, 1});
}

static int optionsSelectorProgress(const OptionsSelector *selector, uint32_t now) {
  uint32_t elapsed = now - selector->startMs;
  if (elapsed >= OPTIONS_SELECTOR_GLIDE_DURATION_MS)
    return 1000;
  int progress = (int)(elapsed * 1000ULL / OPTIONS_SELECTOR_GLIDE_DURATION_MS);
  return (int)((int64_t)progress * progress * (3000 - 2 * progress) / 1000000);
}

static int optionsSelectorY(OptionsSelector *selector, OptionsPage page,
                            int selectedRow, int selectedY) {
  const uint32_t now = uiNowMs();
  if (!selector->initialized || selector->page != page) {
    selector->page = page;
    selector->selectedRow = selectedRow;
    selector->fromRow = selectedY * OPTIONS_SELECTOR_ROW_SCALE;
    selector->toRow = selector->fromRow;
    selector->startMs = now;
    selector->initialized = 1;
  } else if (selector->selectedRow != selectedRow) {
    int progress = optionsSelectorProgress(selector, now);
    selector->fromRow += (selector->toRow - selector->fromRow) * progress / 1000;
    selector->toRow = selectedY * OPTIONS_SELECTOR_ROW_SCALE;
    selector->selectedRow = selectedRow;
    selector->startMs = now;
  }
  int progress = optionsSelectorProgress(selector, now);
  int row = selector->fromRow +
            (selector->toRow - selector->fromRow) * progress / 1000;
  return row / OPTIONS_SELECTOR_ROW_SCALE;
}


static void drawOptionsTextRow(int x, int y, int right, int selected,
                               int selectorY, const char *label,
                               const char *value) {
  if (selected)
    drawMenuRowSelector(x, selectorY, right);
  int labelWidth = right - x - 30;
  float valueWidth = value ? getLineWidth(value) : 0;
  if (value)
    labelWidth -= (int)valueWidth + 16;
  if (selected && value)
    drawPSBBNFocusGlow(right - valueWidth - 12, selectorY, right, right - 12);
  drawText(x + 18, y, 0, labelWidth, 0,
           selected ? ColorSelectedText : HeaderTextColor,
           label);
  if (value)
    drawText(right - valueWidth - 12, y, 0, 0, 0,
             selected ? ColorSelectedText : FontMainColor, value);
}

static void drawCompatibilityTabs(const OptionsMenuState *state, int baseX, int y) {
  const int middle = (baseX + gsGlobal->Width) / 2;
  const int neutrinoX = middle - 100;
  const int oplX = middle + 48;
  const int videoOut = state->gameSection == GAME_VIDEO_OUT;
  const int opl = videoOut ? state->videoOutOpl : gameUsesOpl(state);
  const uint64_t disabled = GS_SETREG_RGBA(0x48, 0x4B, 0x50, 0x80);
  int selectedX = opl ? oplX : neutrinoX;
  int selectedRight = selectedX + (int)getLineWidth(opl ? "OPL" : "Neutrino");
  drawPSBBNFocusGlow(selectedX, y, selectedRight + 18, selectedRight);
  drawText(neutrinoX, y, 0, 0, 0, opl ? disabled : ColorSelectedText, "Neutrino");
  drawText(oplX, y, 0, 0, 0, opl ? ColorSelectedText : disabled, "OPL");
  if (videoOut && lunaOplDevice(state->target->device->mode) != NULL) {
    drawText(neutrinoX - 22, y, 0, 0, 0, HeaderTextColor, "<");
    drawText(oplX + 48, y, 0, 0, 0, HeaderTextColor, ">");
  }
}

static void drawVideoOutRows(OptionsMenuState *state, int baseX, int firstY,
                             int rowStep, int menuBottom) {
  drawCompatibilityTabs(state, baseX, firstY);
  const int rowStart = firstY + rowStep;
  const int count = lunaGameVideoModeCount(state->videoOutOpl);
  const int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                       state->gameOptions.videoMode;
  int visible = (menuBottom - getFontLineHeight() - rowStart) / rowStep + 1;
  if (visible < 1)
    visible = 1;
  int first = state->videoOutFirstRow;
  if (state->selectedGameRow < first)
    first = state->selectedGameRow;
  if (state->selectedGameRow >= first + visible)
    first = state->selectedGameRow - visible + 1;
  if (first != state->videoOutFirstRow)
    state->selector.initialized = 0;
  state->videoOutFirstRow = first;
  int selectorY = optionsSelectorY(&state->selector, state->page,
      state->selectedGameRow, rowStart + (state->selectedGameRow - first) * rowStep);
  for (int row = first; row < count && row < first + visible; row++)
    drawOptionsTextRow(baseX, rowStart + (row - first) * rowStep,
                       gsGlobal->Width - baseX, row == state->selectedGameRow,
                       selectorY, lunaGameVideoModeLabel(state->videoOutOpl, row),
                       mode == row ? lunaText("On") : lunaText("Off"));
  char position[20];
  snprintf(position, sizeof(position), "%d/%d", state->selectedGameRow + 1, count);
  // Place the counter on the section heading, clear of the first row's value.
  drawText(gsGlobal->Width - baseX - getLineWidth(position) - 12,
           firstY - getFontLineHeight() - 4, 0, 0, 0, FontMainColor, position);
  drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
                 state->selectedGameRow == 0 ?
                     lunaText("Keep the game's original output. Left/right changes tabs.") :
                     lunaText("Select one output per core. Left/right changes tabs."));
}

static int gameVMCEnabled(ArgumentList *arguments) {
  const char *const names[] = {"mc0", "mc1"};
  for (int slot = 0; slot < 2; slot++) {
    Argument *card = getArgument(arguments, names[slot]);
    if (card != NULL && !card->isDisabled && card->value != NULL &&
        card->value[0] != '\0')
      return 1;
  }
  return 0;
}

static int gameLaunchEnabled(const OptionsMenuState *state) {
  int enabled = state->gameOptions.debugColors;
  for (Argument *argument = state->titleArguments->first;
       argument != NULL; argument = argument->next) {
    const char *name = argument->arg;
    if (!strcmp(name, "logo")) {
      // Inherit is the default; either explicit logo choice is a title override.
      enabled += !argument->isGlobal;
    } else if (strcmp(name, "dbc") && strcmp(name, "gc") &&
               strcmp(name, "gsm") && strcmp(name, "mc0") &&
               strcmp(name, "mc1") && strcmp(name, "luna_neutrino_disable_igr") &&
               strcmp(name, "luna_opl_compat") && strcmp(name, "luna_opl_gsm") &&
               strcmp(name, "luna_opl_field_flip") && strcmp(name, "luna_core") &&
               strcmp(name, "luna_ps1_code_validation") && strcmp(name, "luna_ps1_hdtv_fix") &&
               strcmp(name, "luna_ps1_start_disc")) {
      // Other arguments belong to Launch arguments, including disabled overrides.
      enabled += !argument->isDisabled || !argument->isGlobal;
    }
  }
  return enabled;
}

static void drawOptionsMusicNote(int centerX, int y, uint64_t color) {
  gsKit_prim_line(gsGlobal, centerX - 1, y + 3,
                  centerX - 1, y + 13, 0, color);
  gsKit_prim_line(gsGlobal, centerX - 1, y + 3,
                  centerX + 5, y + 1, 0, color);
  gsKit_prim_sprite(gsGlobal, centerX - 5, y + 12,
                    centerX - 1, y + 15, 0, color);
}

static void drawOptionsSection(int y, const char *title, int musicNotes) {
  const uint64_t color = HeaderTextColor;
  const int titleWidth = (int)getLineWidth(title);
  const int titleX = (gsGlobal->Width - titleWidth) / 2;
  if (musicNotes)
    drawOptionsMusicNote(titleX - 14, y, color);
  else
    drawGlassDiamond(titleX - 14, y + 8, 6, 0, color);
  drawText(titleX, y, 0, 0, 0, color, title);
  if (musicNotes)
    drawOptionsMusicNote(titleX + titleWidth + 14, y, color);
  else
    drawGlassDiamond(titleX + titleWidth + 14, y + 8, 6, 0, color);
}

static const char *gamePS2LogoValue(const OptionsMenuState *state) {
  Argument *logo = getArgument(state->titleArguments, "logo");
  if (logo == NULL || logo->isGlobal)
    return state->logoSetting ? lunaText("Inherit (On)") : lunaText("Inherit (Off)");
  return state->gameOptions.ps2Logo ? lunaText("Override (On)") : lunaText("Override (Off)");
}

static void drawGameVMCRows(OptionsMenuState *state, int baseX,
                            int firstY, int rowStep, int menuBottom) {
  const int enabled = gameVMCEnabled(state->titleArguments);
  const int visibleRows = enabled ? 4 : 3;
  const char *const labels[] = {
      lunaText("VMC slot 1"), lunaText("VMC slot 2"), lunaText("Card assignment"), lunaText("Disable virtual cards")};
  const char *const values[] = {
      state->gameOptions.vmcSlotLabel[0],
      state->gameOptions.vmcSlotLabel[1], NULL, NULL};
  int selectorY = optionsSelectorY(&state->selector, state->page,
      GAME_SECTION_COUNT + GAME_MEMORY_CARDS * 5 + state->selectedGameRow,
      firstY + state->selectedGameRow * rowStep);
  for (int row = 0; row < visibleRows; row++)
    drawOptionsTextRow(baseX, firstY + row * rowStep,
                       gsGlobal->Width - baseX,
                       row == state->selectedGameRow, selectorY,
                       labels[row], values[row]);
  const char *description = state->vmcStatus[0] ? state->vmcStatus :
      state->selectedGameRow == 2 ?
          lunaText("Create cards for this game, choose existing cards or use shared cards.") :
      state->selectedGameRow == 3 ?
          lunaText("Use physical cards in both slots for this game.") :
          lunaText(gameRowDescriptions[LUNA_GAME_VMC_SLOT1 + state->selectedGameRow]);
  drawTextWindow(baseX + 18, menuBottom,
                 gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0,
                 HeaderTextColor, ALIGN_LEFT, description);
}

static void reloadGameCheats(OptionsMenuState *state, int reconcile) {
  lunaCheatFileFree(&state->cheatFile);
  state->cheatLoadResult = lunaCheatsLoad(state->target, &state->cheatFile,
      state->cheatPath, sizeof(state->cheatPath), state->cheatStatus, sizeof(state->cheatStatus));
  state->cheatFirstRow = 0;
  if (state->cheatLoadResult) return;
  int removed = 0;
  for (int i = 0; reconcile && i < state->cheats.count;) {
    int found = 0;
    for (int e = 0; e < state->cheatFile.entryCount; e++)
      if (!state->cheatFile.entries[e].required &&
          state->cheatFile.entries[e].id == state->cheats.ids[i]) found = 1;
    if (found) i++;
    else {
      lunaCheatToggle(&state->cheats, state->cheats.ids[i]);
      removed++;
    }
  }
  if (removed) {
    state->cheatsChanged = 1;
    snprintf(state->cheatStatus, sizeof(state->cheatStatus),
             lunaText("%d changed or missing selections cleared."), removed);
  } else {
    snprintf(state->cheatStatus, sizeof(state->cheatStatus),
             lunaText("%d entries from CHT/%s.cht"), state->cheatFile.entryCount, state->target->id);
  }
}

static void drawGameCheatRows(OptionsMenuState *state, int baseX, int firstY, int bottom) {
  int count = 3 + state->cheatFile.entryCount;
  int step = getFontLineHeight() + getFontLineHeight() / 3;
  int visible = (bottom - firstY) / step;
  if (visible < 1) visible = 1;
  if (state->selectedGameRow >= count) state->selectedGameRow = count - 1;
  if (state->selectedGameRow < state->cheatFirstRow) state->cheatFirstRow = state->selectedGameRow;
  if (state->selectedGameRow >= state->cheatFirstRow + visible)
    state->cheatFirstRow = state->selectedGameRow - visible + 1;
  const int rowRight = gsGlobal->Width - baseX - (count > visible ? 14 : 0);
  for (int row = state->cheatFirstRow; row < count && row < state->cheatFirstRow + visible; row++) {
    const char *label, *value;
    char name[48];
    if (row == 0) { label = lunaText("Enable cheats"); value = state->cheats.enabled ? lunaText("On") : lunaText("Off"); }
    else if (row == 1) { label = lunaText("Clear selections"); value = ">"; }
    else if (row == 2) { label = lunaText("Reload file"); value = ">"; }
    else {
      const LunaCheatEntry *entry = &state->cheatFile.entries[row - 3];
      snprintf(name, sizeof(name), "%.43s%s", entry->name, strlen(entry->name) > 43 ? "..." : "");
      label = name;
      value = entry->required ? lunaText("Required") :
          lunaCheatSelected(&state->cheats, entry->id) ? "[x]" : "[ ]";
    }
    int y = firstY + (row - state->cheatFirstRow) * step;
    drawOptionsTextRow(baseX, y, rowRight,
                       row == state->selectedGameRow, y, label, value);
  }
  drawListScrollbar(gsGlobal->Width - baseX - 6, firstY, firstY + visible * step,
                     count, state->cheatFirstRow, visible, 6);
  char detail[192];
  if (state->selectedGameRow >= 3) {
    const LunaCheatEntry *entry = &state->cheatFile.entries[state->selectedGameRow - 3];
    snprintf(detail, sizeof(detail), "%.110s: %d lines. %s", entry->name, entry->pairCount,
             entry->required ? lunaText("Included with selected cheats.") : lunaText("Toggle this whole code block."));
  } else if (state->cheatLoadResult || state->selectedGameRow == 2) {
    snprintf(detail, sizeof(detail), "%s", state->cheatStatus);
  } else {
    snprintf(detail, sizeof(detail), lunaText("%d selected. %s"), state->cheats.count,
             state->cheats.enabled ? lunaText("Changes save automatically. Square to test.") :
                                     lunaText("Cheats are off; selections are retained."));
  }
  drawTextWindow(baseX + 18, bottom, gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0,
                 state->cheatLoadResult ? ErrorTextColor : HeaderTextColor, ALIGN_LEFT, detail);
}

static void drawTitleOptionsFrame(OptionsMenuState *state,
                                  int transitionProgress, int transitionMode) {
  const int showDirty = transitionMode != 2;
  const int showDescription = !(showDirty && state->saveError);
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();

  drawOptionsSection(optionsHeadingY(),lunaText("Game settings"),0);
  const int menuTop = optionsMenuTop();
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (state->target->platform == TARGET_PS1) {
    if (state->gameSection == GAME_HUB) {
      drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                     HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
      const int contentTop = menuTop + lineHeight + 5;
      const int firstY = contentTop + lineHeight + 4;
      drawOptionsSection(contentTop, lunaText("Game settings"), 0);
      int step = lineHeight + lineHeight / 2;
      int selectorY = optionsSelectorY(&state->selector, state->page,
                                       state->selectedGameHubRow,
                                       firstY + state->selectedGameHubRow * step);
      char compatSummary[24];
      if (state->gameOptions.ps1CodeValidation)
        snprintf(compatSummary, sizeof(compatSummary), lunaText("%d enabled"), 1);
      else
        snprintf(compatSummary, sizeof(compatSummary), "%s", lunaText("Default"));
      char discSummary[24];
      snprintf(discSummary, sizeof(discSummary), "%u %s", state->target->discs ? state->target->discs->count : 1,
               lunaText("discs"));
      for (int row = 0; row < 4; ++row)
        drawOptionsTextRow(baseX, firstY + row * step, gsGlobal->Width - baseX,
                           state->selectedGameHubRow == row, selectorY,
                           lunaText(row == 3 ? "Discs" : row == 2 ? "Video" : row ? "Compatibility" : "Virtual memory cards"),
                           row == 3 ? discSummary : row == 2 ?
                               lunaText(state->gameOptions.ps1HdtvFix ? "On" : "Default") :
                               row ? compatSummary : NULL);
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
                     state->vmcStatus[0] ? lunaText(state->vmcStatus) :
                     lunaText(state->selectedGameHubRow == 3 ? "Choose a starting disc or manage this game's disc set." :
                         state->selectedGameHubRow == 2 ? "Adjust this game's video output." : state->selectedGameHubRow ?
                         "Change this game's compatibility switches." :
                         "Open to select shared memory cards or manage saves."));
    } else if (state->gameSection == GAME_COMPATIBILITY || state->gameSection == GAME_VIDEO) {
      const LunaGameRow option = state->gameSection == GAME_VIDEO ?
          LUNA_GAME_PS1_HDTV_FIX : LUNA_GAME_PS1_CODE_VALIDATION;
      drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                     HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
      const int contentTop = menuTop + lineHeight + 5;
      const int firstY = contentTop + lineHeight + 4;
      drawOptionsSection(contentTop, lunaText(state->gameSection == GAME_VIDEO ? "Video" : "Compatibility"), 0);
      int selectorY = optionsSelectorY(&state->selector, state->page, 0, firstY);
      drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX, 1, selectorY,
                         lunaText(gameRowLabels[option]),
                         lunaGameOptionsValue(&state->gameOptions, option));
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
                     state->vmcStatus[0] ? lunaText(state->vmcStatus) :
                     lunaText(gameRowDescriptions[option]));
    } else if (state->gameSection == GAME_DISCS) {
      const int contentTop = menuTop + lineHeight + 5;
      const int firstY = contentTop + lineHeight + 4;
      const int step = lineHeight + lineHeight / 2;
      drawTextWindow(baseX, menuTop, gsGlobal->Width-baseX, 0, 0,
                     HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
      drawOptionsSection(contentTop, lunaText("Discs"), 0);
      char start[32];
      int disc = state->gameOptions.ps1StartDisc;
      unsigned count = state->target->discs ? state->target->discs->count : 1;
      if (disc < 0 || (unsigned)disc >= count) snprintf(start,sizeof(start),"%s",lunaText("Unavailable"));
      else snprintf(start,sizeof(start),"%s %d / %u",lunaText("Disc"),disc+1,count);
      int selectorY = optionsSelectorY(&state->selector,state->page,state->selectedGameRow,
                                       firstY+state->selectedGameRow*step);
      drawOptionsTextRow(baseX,firstY,gsGlobal->Width-baseX,state->selectedGameRow==0,
                         selectorY,lunaText("Start disc"),start);
      drawOptionsTextRow(baseX,firstY+step,gsGlobal->Width-baseX,state->selectedGameRow==1,
                         selectorY,lunaText("Manage discs"),NULL);
      if (state->target->discs && disc>=0 && (unsigned)disc<count) {
        const char *path=state->target->discs->paths[disc], *slash=strrchr(path,'/');
        drawTextWindow(baseX+18,firstY+step*3,gsGlobal->Width-baseX,0,0,
                       HeaderTextColor,ALIGN_LEFT,slash?slash+1:path);
      }
      drawTextWindow(baseX+18,menuBottom,gsGlobal->Width-baseX,gsGlobal->Height-footerHeight,0,
                     HeaderTextColor,ALIGN_LEFT,state->vmcStatus[0]?lunaText(state->vmcStatus):
                     state->target->discs && state->target->discs->error[0]?lunaText(state->target->discs->error):
                     lunaText("All discs use the same memory cards. Swapping controls are shown before launch."));
    } else {
#ifdef LUNA_ENABLE_PSXCORE
    const char *labels[] = {"Card assignment", "Slot 1", "Slot 2", "Manage cards"};
    char setLabel[28]; snprintf(setLabel, sizeof(setLabel), "%.24s%s", state->ps1SetName,
                               strlen(state->ps1SetName) > 24 ? "..." : "");
    const char *values[] = {setLabel, state->ps1SlotStatus[0], state->ps1SlotStatus[1], NULL};
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    const int contentTop = menuTop + lineHeight + 5;
    const int firstY = contentTop + lineHeight + 4;
    const int available = (menuBottom - firstY - lineHeight) / 3;
    const int step = available < lineHeight + lineHeight / 2 ? available : lineHeight + lineHeight / 2;
    drawOptionsSection(contentTop, lunaText("Virtual memory cards"), 0);
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedGameRow,
                                     firstY + state->selectedGameRow * step);
    for (int row = 0; row < 4; ++row)
      drawOptionsTextRow(baseX, firstY + row * step, gsGlobal->Width - baseX,
          state->selectedGameRow == row, selectorY, lunaText(labels[row]), values[row]);
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
        gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
        state->vmcStatus[0] ? lunaText(state->vmcStatus) :
        lunaText("Games using the same cards share their saved data. Missing default cards are created at launch."));
#else
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    drawOptionsSection(menuTop + lineHeight * 2, "PlayStation", 0);
    drawOptionsTextRow(baseX, menuTop + lineHeight * 4, gsGlobal->Width - baseX,
                       0, 0, "Game core", "PSXCore");
    drawOptionsTextRow(baseX, menuTop + lineHeight * 6, gsGlobal->Width - baseX,
                       0, 0, "Memory cards", "Shared cards");
    drawTextWindow(baseX + 18, menuBottom - lineHeight * 2,
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   HeaderTextColor, ALIGN_LEFT,
                   "LUNA prepares shared memory cards before launch.");
#endif
    }
  } else {
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    const int contentTop = menuTop + lineHeight + 5;
    const int gameStep = lineHeight + lineHeight / 2;
    const int firstY = contentTop + lineHeight + 4;
    if (state->gameSection == GAME_HUB) {
      const int availableStep = (menuBottom - firstY - lineHeight - 8) /
                                (GAME_SECTION_COUNT - 1);
      const int hubStep = availableStep < gameStep ? availableStep : gameStep;
      char compatSummary[24], launchSummary[24];
      int compatEnabled = 0;
      int compatCount = gameUsesOpl(state) ?
          LUNA_OPL_COMPAT_COUNT : LUNA_GAME_COMPAT_COUNT;
      uint8_t compatMask = gameUsesOpl(state) ?
          state->gameOptions.oplCompat : state->gameOptions.compat;
      for (int bit = 0; bit < compatCount; bit++)
        compatEnabled += (compatMask & (1U << bit)) != 0;
      if (!gameUsesOpl(state))
        compatEnabled += state->gameOptions.neutrinoIgrDisabled;
      if (compatEnabled)
        snprintf(compatSummary, sizeof(compatSummary), lunaText("%d enabled"), compatEnabled);
      else
        snprintf(compatSummary, sizeof(compatSummary), lunaText("Default"));
      int launchEnabled = gameLaunchEnabled(state);
      if (launchEnabled)
        snprintf(launchSummary, sizeof(launchSummary), lunaText("%d enabled"), launchEnabled);
      else
        snprintf(launchSummary, sizeof(launchSummary), lunaText("Default"));
      const char *const summaries[GAME_SECTION_COUNT] = {
          gameVMCEnabled(state->titleArguments) ? lunaText("Enabled") : lunaText("Disabled"),
          lunaGameOptionsValue(&state->gameOptions, LUNA_GAME_CORE),
          compatSummary,
          lunaGameOptionsValue(&state->gameOptions,
              gameUsesOpl(state) ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_VIDEO_MODE),
          state->cheats.enabled ? lunaText("Enabled") : lunaText("Disabled"), launchSummary};
      drawOptionsSection(contentTop, lunaText("Game settings"), 0);
      int selectorY = optionsSelectorY(&state->selector, state->page,
          state->selectedGameHubRow, firstY + state->selectedGameHubRow * hubStep);
      for (int row = 0; row < GAME_SECTION_COUNT; row++)
        drawOptionsTextRow(baseX, firstY + row * hubStep,
                           gsGlobal->Width - baseX,
                           row == state->selectedGameHubRow, selectorY,
                           lunaText(gameSectionLabels[row]), summaries[row]);
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                     ALIGN_LEFT, lunaText(gameSectionDescriptions[state->selectedGameHubRow]));
    } else {
      const GameSection section = state->gameSection;
      if (section != GAME_CHEATS && state->selectedGameRow >= gameSectionRowCount(state, section))
        state->selectedGameRow = 0;
      drawOptionsSection(contentTop, section == GAME_VIDEO_OUT ? lunaText("Video out") :
                                                          lunaText(gameSectionLabels[section]), 0);
      if (section == GAME_CHEATS) {
        drawGameCheatRows(state, baseX, firstY, menuBottom);
      } else if (section == GAME_VIDEO_OUT) {
        drawVideoOutRows(state, baseX, firstY, gameStep, menuBottom);
      } else if (section == GAME_MEMORY_CARDS) {
        drawGameVMCRows(state, baseX, firstY, gameStep, menuBottom);
      } else {
        int rowStart = firstY;
        if (section == GAME_COMPATIBILITY) {
          drawCompatibilityTabs(state, baseX, firstY);
          rowStart += gameStep;
        }
        const LunaGameRow selectedRow =
            gameSectionRow(state, section, state->selectedGameRow);
        int selectorY = optionsSelectorY(&state->selector, state->page,
            GAME_SECTION_COUNT + section * 6 + state->selectedGameRow,
            rowStart + state->selectedGameRow * gameStep);
        for (int row = 0; row < gameSectionRowCount(state, section); row++) {
          LunaGameRow option = gameSectionRow(state, section, row);
          drawOptionsTextRow(baseX, rowStart + row * gameStep,
                             gsGlobal->Width - baseX,
                             row == state->selectedGameRow, selectorY,
                             lunaText(gameRowLabels[option]),
                             option == LUNA_GAME_PS2_LOGO ?
                                 gamePS2LogoValue(state) :
                                 lunaGameOptionsValue(&state->gameOptions, option));
        }
        drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                       gsGlobal->Height - footerHeight, 0,
                       HeaderTextColor, ALIGN_LEFT,
                       lunaText(gameRowDescriptions[selectedRow]));
      }
    }
  }
  if (state->page == OPTIONS_PER_GAME && state->target->platform == TARGET_PS1) {
    ButtonPrompt prompts[4];
    int promptCount = 0;
    prompts[promptCount++] = (ButtonPrompt){ICON_CROSS, state->gameSection == GAME_HUB ? lunaText("Open") :
        (state->gameSection == GAME_COMPATIBILITY || state->gameSection == GAME_VIDEO) ? lunaText("Toggle") : lunaText("Select")};
    prompts[promptCount++] = (ButtonPrompt){ICON_SQUARE, lunaText("Launch")};
    prompts[promptCount++] = (ButtonPrompt){ICON_TRIANGLE, lunaText("Back")};
#ifdef LUNA_ENABLE_PSXCORE
    const ButtonPrompt retryPrompts[] = {{ICON_DPAD, lunaText("Move")},
                                        {ICON_CROSS, lunaText("Retry")}, {ICON_TRIANGLE, lunaText("Back")}};
#endif
    drawPromptBar(20, gsGlobal->Height-footerHeight, gsGlobal->Width-20,
                  gsGlobal->Height, 16, HeaderTextColor,
#ifdef LUNA_ENABLE_PSXCORE
                  state->gameSection == GAME_MEMORY_CARDS && !state->ps1CardsReady ? (PromptBar){NULL,retryPrompts,3} :
#endif
                  (PromptBar){NULL,prompts,promptCount});
  } else drawOptionsFooter(1,0,0,
      state->gameSection == GAME_HUB ?
        (state->selectedGameHubRow == GAME_MEMORY_CARDS ?
          (gameVMCEnabled(state->titleArguments) ? lunaText("Manage") : lunaText("Enable")) :
         state->selectedGameHubRow == GAME_CORE ? lunaText("Change") : lunaText("Open")) :
      state->gameSection == GAME_VIDEO_OUT ? lunaText("Select") :
      state->gameSection == GAME_VIDEO && state->selectedGameRow == 0 ? lunaText("Open") :
      state->gameSection == GAME_MEMORY_CARDS ?
        (state->selectedGameRow == 2 ? lunaText("Create") :
         state->selectedGameRow == 3 ? lunaText("Disable") : lunaText("Assign")) : NULL,
      state->gameSection != GAME_HUB);
  if (showDirty && state->saveError) {
    // Give save failures the description area without overlapping its old text.
    gsKit_prim_sprite(gsGlobal, baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
        gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
        GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   state->saveErrorLabel ? state->saveErrorLabel :
                   lunaText("Could not save game settings"));
  }
  if (transitionMode != 0)
    drawOptionsTransition(transitionProgress, transitionMode == 2);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

static int uiVMCPickerLoop(Target *target,ArgumentList *arguments,LunaGameOptions *options,int slot) {
  return uiVmcPs2Slot(target,arguments,options,slot);
}

// Returns 1 for Back, -1 if a test launch unexpectedly returns, or 0 to stay.
static int editGameInput(OptionsMenuState *state, int input) {
  if (state->target->platform == TARGET_PS1) {
    if (input & PAD_TRIANGLE) {
      if (state->gameSection == GAME_HUB) return 1;
      state->gameSection = GAME_HUB;
      state->selectedGameRow = 0;
      state->selector.initialized = 0;
      return 0;
    }
    if (state->gameSection == GAME_HUB) {
      if (input & (PAD_UP | PAD_DOWN)) {
        state->selectedGameHubRow = (state->selectedGameHubRow + ((input & PAD_UP) ? 3 : 1)) % 4;
        state->vmcStatus[0] = 0;
        return 0;
      }
      if (input & (PAD_CROSS | PAD_CIRCLE)) {
        state->gameSection = state->selectedGameHubRow == 3 ? GAME_DISCS :
                             state->selectedGameHubRow == 2 ? GAME_VIDEO :
                             state->selectedGameHubRow ? GAME_COMPATIBILITY : GAME_MEMORY_CARDS;
        state->selectedGameRow = 0;
        state->selector.initialized = 0;
#ifdef LUNA_ENABLE_PSXCORE
        if (state->gameSection == GAME_MEMORY_CARDS && !state->ps1CardsReady) loadPs1Cards(state);
#endif
        return 0;
      }
      if (!(input & PAD_SQUARE)) return 0;
    }

#ifdef LUNA_ENABLE_PSXCORE
    if (input & PAD_SQUARE)
      return uiLaunchPs1Cards(state->target, state->ps1CardsReady ? &state->ps1Cards : NULL,
                              state->titleArguments);
#endif
    if (state->gameSection == GAME_DISCS) {
      if(input & (PAD_UP|PAD_DOWN)) {state->selectedGameRow ^= 1;return 0;}
      if(!state->selectedGameRow && (input & (PAD_CROSS|PAD_CIRCLE|PAD_LEFT|PAD_RIGHT))) {
        unsigned count=state->target->discs?state->target->discs->count:1;
        int disc=state->gameOptions.ps1StartDisc;
        if(count)disc=(disc+(input&PAD_LEFT?(int)count-1:1))%(int)count;
        if(count && lunaGameOptionsSetPs1Disc(&state->gameOptions,state->titleArguments,disc)) {
          state->titleArgumentsChanged=1;state->vmcStatus[0]=0;
        }
      }
#ifdef LUNA_ENABLE_PSXCORE
      else if(state->selectedGameRow && (input & (PAD_CROSS|PAD_CIRCLE))) {

        int result=uiPs1DiscManage(state->target);
        state->selector.initialized=0;
        if(result==STORAGE_UI_REFRESH)return result;
      }
#endif
      return 0;
    }
    if (state->gameSection == GAME_COMPATIBILITY || state->gameSection == GAME_VIDEO) {
      if ((input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) &&
          lunaGameOptionsChange(&state->gameOptions, state->titleArguments,
                                state->gameSection == GAME_VIDEO ? LUNA_GAME_PS1_HDTV_FIX :
                                    LUNA_GAME_PS1_CODE_VALIDATION, 1)) {
        state->titleArgumentsChanged = 1;
        state->saveError = 0;
        state->vmcStatus[0] = 0;
      }
      return 0;
    }
#ifdef LUNA_ENABLE_PSXCORE
    // Navigation never depends on successful storage IO.
    if (input & PAD_UP) { state->selectedGameRow = (state->selectedGameRow + 3) % 4; return 0; }
    if (input & PAD_DOWN) { state->selectedGameRow = (state->selectedGameRow + 1) % 4; return 0; }
    if (!state->ps1CardsReady) {
      if ((input & (PAD_CROSS | PAD_CIRCLE | PAD_START | PAD_SQUARE)) &&
          uiPs1VmcRetry(state->target, state->vmcStatus)) loadPs1Cards(state);
      return 0;
    }
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int changed = 0;
      if (!state->selectedGameRow) changed = uiPs1VmcPolicy(state->target, &state->ps1Cards);
      else {
        if(state->selectedGameRow<=2)changed=uiPs1VmcSlot(state->target,&state->ps1Cards,state->selectedGameRow-1);
        else uiPs1VmcManage(&state->ps1Cards,-1);
      }
      if (changed) {
        state->ps1CardsChanged = 1; state->saveError = 0;
        snprintf(state->vmcStatus, sizeof(state->vmcStatus), "%s", "Card selection updated.");
      }
      uiPs1VmcDescribe(&state->ps1Cards, state->ps1SetName, state->ps1SlotStatus);
      state->selector.initialized = 0;
    }
#else
    if (input & PAD_SQUARE) return uiLaunchTitleWithCheats(state->target,NULL,NULL);
#endif
    return 0;
  }
  if (input & PAD_SQUARE) {
    // Every accepted edit has already been saved.
    return uiLaunchTitleWithCheats(state->target, state->titleArguments, &state->cheats);
  }

  if (state->gameSection == GAME_HUB) {
    if (input & PAD_TRIANGLE)
      return 1;
    if (input & PAD_UP)
      state->selectedGameHubRow = (state->selectedGameHubRow +
                                   GAME_SECTION_COUNT - 1) % GAME_SECTION_COUNT;
    else if (input & PAD_DOWN)
      state->selectedGameHubRow = (state->selectedGameHubRow + 1) % GAME_SECTION_COUNT;
    else if (state->selectedGameHubRow == GAME_CORE &&
             (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT))) {
      if (lunaOplDevice(state->target->device->mode) != NULL &&
          lunaGameOptionsCycleCore(&state->gameOptions, state->titleArguments,
                                   state->coreSetting, (input & PAD_LEFT) ? -1 : 1))
        state->titleArgumentsChanged = 1;
    }
    else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      state->gameSection = (GameSection)state->selectedGameHubRow;
      state->selectedGameRow = 0;
      state->vmcStatus[0] = '\0';
      state->selector.initialized = 0;
      if (state->gameSection == GAME_CHEATS) reloadGameCheats(state, 0);
    }
    return 0;
  }
  if (input & PAD_TRIANGLE) {
    state->gameSection = state->gameSection == GAME_VIDEO_OUT ? GAME_VIDEO : GAME_HUB;
    state->selectedGameRow = 0;
    state->selector.initialized = 0;
    return 0;
  }
  if (state->gameSection == GAME_CHEATS) {
    int count = 3 + state->cheatFile.entryCount;
    if (input & PAD_UP) state->selectedGameRow = (state->selectedGameRow + count - 1) % count;
    else if (input & PAD_DOWN) state->selectedGameRow = (state->selectedGameRow + 1) % count;
    else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int row = state->selectedGameRow;
      if (row == 0 && (!state->cheatLoadResult || state->cheats.enabled)) {
        state->cheats.enabled = !state->cheats.enabled;
        state->cheatsChanged = 1;
      } else if (row == 1) {
        state->cheats.count = 0;
        state->cheatsChanged = 1;
      } else if (row == 2) reloadGameCheats(state, 1);
      else if (row >= 3 && !state->cheatFile.entries[row - 3].required) {
        state->cheatsChanged |= lunaCheatToggle(&state->cheats, state->cheatFile.entries[row - 3].id);
      }
    }
    return 0;
  }
  int count = state->gameSection == GAME_MEMORY_CARDS &&
              !gameVMCEnabled(state->titleArguments) ? 3 :
              gameSectionRowCount(state, state->gameSection);
  if (input & PAD_UP) {
    state->selectedGameRow = (state->selectedGameRow + count - 1) % count;
    return 0;
  }
  if (input & PAD_DOWN) {
    state->selectedGameRow = (state->selectedGameRow + 1) % count;
    return 0;
  }
  if (state->gameSection == GAME_VIDEO_OUT) {
    if ((input & (PAD_LEFT | PAD_RIGHT)) &&
        lunaOplDevice(state->target->device->mode) != NULL) {
      state->videoOutOpl = !state->videoOutOpl;
      int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                     state->gameOptions.videoMode;
      state->selectedGameRow = mode > 0 ? mode : 0;
      state->videoOutFirstRow = 0;
      state->selector.initialized = 0;
    } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int current = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                        state->gameOptions.videoMode;
      int mode = current == state->selectedGameRow ? 0 : state->selectedGameRow;
      if (lunaGameOptionsSetVideoMode(&state->gameOptions, state->titleArguments,
                                     state->videoOutOpl, mode))
        state->titleArgumentsChanged = 1;
    }
    return 0;
  }
  if (state->gameSection == GAME_MEMORY_CARDS &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    if (state->selectedGameRow == 2) {
      if(uiVmcPs2Policy(state->target,state->titleArguments,&state->gameOptions)) {
        state->titleArgumentsChanged=1;state->saveError=0;
        snprintf(state->vmcStatus,sizeof(state->vmcStatus),"%s",lunaText("Card selection updated."));
      }
      return 0;
    }
    if (state->selectedGameRow == 3) {
      if (gameVMCEnabled(state->titleArguments)) {
        if (lunaGameOptionsSetVMCPair(&state->gameOptions,state->titleArguments,"","")) {
          state->titleArgumentsChanged = 1;
          state->selectedGameRow = 0;
          state->selector.initialized = 0;
          snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                   lunaText("Virtual cards disabled."));
        } else {
          snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                   lunaText("Could not disable virtual cards."));
        }
      }
      return 0;
    }
  }
  if (state->gameSection == GAME_MEMORY_CARDS && state->selectedGameRow >= 2)
    return 0;
  if (state->gameSection == GAME_MEMORY_CARDS &&
      (input & (PAD_LEFT | PAD_RIGHT)))
    return 0;
  LunaGameRow selectedRow = gameSectionRow(state, state->gameSection,
                                            state->selectedGameRow);
  if ((selectedRow == LUNA_GAME_VIDEO_MODE || selectedRow == LUNA_GAME_OPL_VIDEO_MODE) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    state->gameSection = GAME_VIDEO_OUT;
    state->videoOutOpl = gameUsesOpl(state);
    int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                   state->gameOptions.videoMode;
    state->selectedGameRow = mode > 0 ? mode : 0;
    state->videoOutFirstRow = 0;
    state->selector.initialized = 0;
  } else if ((selectedRow == LUNA_GAME_VMC_SLOT1 || selectedRow == LUNA_GAME_VMC_SLOT2) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    int slot = selectedRow - LUNA_GAME_VMC_SLOT1;
    if (uiVMCPickerLoop(state->target, state->titleArguments, &state->gameOptions, slot)) {
      state->titleArgumentsChanged = 1;
      snprintf(state->vmcStatus, sizeof(state->vmcStatus),
               lunaText("Card selection updated."));
    }
  } else if (selectedRow == LUNA_GAME_LAUNCH_ARGUMENTS &&
             (input & (PAD_CROSS | PAD_CIRCLE))) {
    int result = uiArgumentListLoop(state->target, state->titleArguments, &state->cheats);
    if (result < 0)
      return -1;
    if (result == 2) {
      state->titleArgumentsChanged = 0;
      state->argumentsSaved = 1;
    }
    else if (result == 1)
      state->titleArgumentsChanged = 1;
    lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    int direction = (input & PAD_LEFT) ? -1 : 1;
    int changed = selectedRow == LUNA_GAME_PS2_LOGO ?
        lunaGameOptionsCyclePS2Logo(&state->gameOptions,
                                    state->titleArguments,
                                    state->logoSetting, direction) :
        lunaGameOptionsChange(&state->gameOptions, state->titleArguments,
                              selectedRow, direction);
    if (changed)
      state->titleArgumentsChanged = 1;
  }
  return 0;
}

// Snapshot only editable values; navigation and canceled dialogs never write.
static ArgumentList *copyOptionsArguments(const ArgumentList *source) {
  ArgumentList *copy = calloc(1, sizeof(*copy));
  if (!copy) return NULL;
  for (const Argument *arg = source->first; arg; arg = arg->next) {
    Argument *item = calloc(1, sizeof(*item));
    if (!item) { freeArgumentList(copy); return NULL; }
    item->arg = arg->arg ? strdup(arg->arg) : NULL;
    item->value = arg->value ? strdup(arg->value) : NULL;
    if ((arg->arg && !item->arg) || (arg->value && !item->value)) {
      free(item->arg); free(item->value); free(item);
      freeArgumentList(copy);
      return NULL;
    }
    item->isDisabled = arg->isDisabled;
    item->isGlobal = arg->isGlobal;
    appendArgument(copy, item);
  }
  return copy;
}

static int sameOptionsArguments(const ArgumentList *a, const ArgumentList *b) {
  const Argument *left = a->first, *right = b->first;
  for (; left && right; left = left->next, right = right->next) {
    if (left->isDisabled != right->isDisabled || left->isGlobal != right->isGlobal ||
        !!left->arg != !!right->arg || !!left->value != !!right->value ||
        (left->arg && strcmp(left->arg, right->arg)) ||
        (left->value && strcmp(left->value, right->value))) return 0;
  }
  return !left && !right;
}

typedef struct {
  ArgumentList *arguments;
  LunaCheatSettings cheats;
#ifdef LUNA_ENABLE_PSXCORE
  LunaPsxVmcSettings cards;
  int cardsReady;
#endif
} GameOptionsSnapshot;

static int handleGameInput(OptionsMenuState *state, int input) {
  if (!(input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)))
    return editGameInput(state, input);
  GameOptionsSnapshot *before = malloc(sizeof(*before));
  if (!before) {
    state->saveError = -ENOMEM;
    state->saveErrorLabel = lunaText("Not enough memory to change settings");
    return 0;
  }
  before->arguments = copyOptionsArguments(state->titleArguments);
  if (!before->arguments) {
    free(before);
    state->saveError = -ENOMEM;
    state->saveErrorLabel = lunaText("Not enough memory to change settings");
    return 0;
  }
  before->cheats = state->cheats;
#ifdef LUNA_ENABLE_PSXCORE
  before->cards = state->ps1Cards;
  before->cardsReady = state->ps1CardsReady;
#endif
  state->argumentsSaved = 0;
  int action = editGameInput(state, input);
  if (action) {
    freeArgumentList(before->arguments);
    free(before);
    return action;
  }
  int argumentsChanged = !sameOptionsArguments(before->arguments, state->titleArguments);
  int cheatsChanged = before->cheats.enabled != state->cheats.enabled ||
      before->cheats.count != state->cheats.count ||
      memcmp(before->cheats.ids, state->cheats.ids,
             (size_t)state->cheats.count * sizeof(state->cheats.ids[0]));
#ifdef LUNA_ENABLE_PSXCORE
  int cardsChanged = before->cardsReady && state->ps1CardsReady &&
      memcmp(&before->cards, &state->ps1Cards, sizeof(before->cards));
#endif
  if (argumentsChanged || cheatsChanged
#ifdef LUNA_ENABLE_PSXCORE
      || cardsChanged
#endif
      ) {
    state->saveError = 0;
    state->saveErrorLabel = NULL;
  }
  if (argumentsChanged && !state->argumentsSaved) {
    int result = state->titleArgumentsChanged ?
        updateTitleLaunchArguments(state->target, state->titleArguments) : -EIO;
    if (result) {
      freeArgumentList(state->titleArguments);
      state->titleArguments = before->arguments;
      before->arguments = NULL;
      lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
      state->saveError = result;
      state->saveErrorLabel = lunaText("Could not save game settings");
      state->vmcStatus[0] = 0;
    }
  }
  if (cheatsChanged) {
    int result = lunaCheatsSaveSettings(state->target, &state->cheats);
    if (result) {
      state->cheats = before->cheats;
      state->saveError = result;
      state->saveErrorLabel = lunaText("Could not save cheat selections");
    }
  }
#ifdef LUNA_ENABLE_PSXCORE
  if (cardsChanged) {
    const char *error = lunaPsxVmcSave(&state->ps1Cards);
    if (error) {
      state->ps1Cards = before->cards;
      state->saveError = -EIO;
      state->saveErrorLabel = error;
      state->vmcStatus[0] = 0;
    }
    uiPs1VmcDescribe(&state->ps1Cards, state->ps1SetName, state->ps1SlotStatus);
  }
  state->ps1CardsChanged = 0;
#endif
  state->titleArgumentsChanged = 0;
  state->cheatsChanged = 0;
  if (before->arguments) freeArgumentList(before->arguments);
  free(before);
  return 0;
}

// Runs settings for the selected game.
// Returns -1 if a test launch unexpectedly returns.
int uiTitleOptionsLoop(Target *target) {
  int res = 0;
  OptionsMenuState state = {
      .target = target,
      .logoSetting = loadPS2LogoEnabled(target),
      .coreSetting = loadGameCoreOpl(target),
      .page = OPTIONS_PER_GAME,
      .gameSection = GAME_HUB};
  snprintf(state.titleHeader, sizeof(state.titleHeader), "%.38s  (%s)",
           target->name, target->id);

  // Load arguments from config files
  state.titleArguments = target->platform == TARGET_PS1 ? calloc(1,sizeof(ArgumentList)) :
                                                        loadLaunchArgumentLists(state.target);
  if (!state.titleArguments) return 0;
  if (target->platform == TARGET_PS1 && getTitleLaunchArguments(state.titleArguments, target)) {
    freeArgumentList(state.titleArguments);
    return 0;
  }
  lunaGameOptionsRead(&state.gameOptions, state.titleArguments);
  if (target->platform != TARGET_PS1 && lunaCheatsLoadSettings(target, &state.cheats)) {
    state.cheatsChanged = 0; // An explicit cheat edit can repair the selection file.
    state.saveError = 1;
    state.saveErrorLabel = lunaText("Could not read cheat settings. Change a cheat option to reset.");
  }
  int input = 0;

  uint32_t transitionStart = uiNowMs();
  int transitionProgress;
  do {
    transitionProgress = optionsTransitionProgress(
        uiNowMs() - transitionStart, OPTIONS_OPEN_DURATION_MS);
    drawTitleOptionsFrame(&state, transitionProgress, 1);
    pollInput();
  } while (transitionProgress < 1000);

  while (1) {
    drawTitleOptionsFrame(&state, 0, 0);

    // Process user inputs
    input = readInput();
    int action = handleGameInput(&state, input);
    if (action != 0) {
      res = action == STORAGE_UI_REFRESH ? action : action < 0 ? -1 : 0;
      goto exit;
    }
  }
exit:
  if (res >= 0) {
    transitionStart = uiNowMs();
    do {
      transitionProgress = optionsTransitionProgress(
          uiNowMs() - transitionStart, OPTIONS_CLOSE_DURATION_MS);
      drawTitleOptionsFrame(&state, transitionProgress, 2);
    } while (transitionProgress < 1000);
  }
  freeArgumentList(state.titleArguments);
  lunaCheatFileFree(&state.cheatFile);
  return res;
}

// LUNA's advanced view of the merged game and global launch arguments.
// Returns -1 after a failed launch, 0 unchanged, or 2 after saved edits.
static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const LunaCheatSettings *cheats) {
  int selectedArgIdx = 0;
  int saveError = 0;
  int changed = 0;
  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet();
    const int baseX = keepoutArea + 10;
    const int lineHeight = getFontLineHeight();
    const int menuTop = optionsMenuTop();
    const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
    const int rowStep = lineHeight + lineHeight / 3;
    const int focusY = menuTop + selectedArgIdx * rowStep;
    const int scrollOffset = focusY + lineHeight > menuBottom
                                 ? focusY + lineHeight - menuBottom : 0;

    drawOptionsSection(optionsHeadingY(),
                       lunaText("Launch arguments"), 0);
    snprintf(lineBuffer, sizeof(lineBuffer), "%.38s  (%s)", target->name, target->id);
    drawTextWindow(baseX, menuTop - lineHeight, gsGlobal->Width - baseX,
                   menuTop, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    if (titleArguments->total == 0)
      drawTextWindow(baseX + 18, menuTop, gsGlobal->Width - baseX, 0, 0,
                     FontMainColor, ALIGN_LEFT, lunaText("No launch arguments set"));

    Argument *argument = titleArguments->first;
    for (int index = 0; argument != NULL; index++, argument = argument->next) {
      int y = menuTop + index * rowStep - scrollOffset;
      if (y < menuTop || y + lineHeight > menuBottom)
        continue;
      const char *value = argument->value != NULL ? argument->value : "";
      snprintf(lineBuffer, sizeof(lineBuffer), "%s%s%s%s",
               argument->isGlobal ? "[G] " : "",
               argument->arg, value[0] ? ": " : "", value);
      drawOptionsTextRow(baseX, y, gsGlobal->Width - baseX,
                         index == selectedArgIdx, y, lineBuffer,
                         argument->isDisabled ? lunaText("Off") : lunaText("On"));
    }
    if (!saveError)
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                     ALIGN_LEFT, lunaText("[G] Inherited from global settings"));
    drawOptionsFooter(1, 1, 0, NULL, 0);
    if (saveError)
      drawTextWindow(baseX, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, ErrorTextColor, ALIGN_HCENTER,
                     lunaText("Could not save game settings"));
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    int input = waitForInput(-1);
    if (input & PAD_SQUARE) {
      if (uiLaunchTitleWithCheats(target, titleArguments, cheats)) return -1;
    } else if (input & PAD_TRIANGLE) {
      return changed ? 2 : 0;
    }
    if (!curArgument)
      continue;
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int oldDisabled = curArgument->isDisabled;
      int oldGlobal = curArgument->isGlobal;
      curArgument->isDisabled = !curArgument->isDisabled;
      if (curArgument->isDisabled)
        curArgument->isGlobal = 0;
      saveError = updateTitleLaunchArguments(target, titleArguments);
      if (saveError) {
        curArgument->isDisabled = oldDisabled;
        curArgument->isGlobal = oldGlobal;
      } else changed = 1;
    } else if (input & PAD_UP) {
      selectedArgIdx = (selectedArgIdx - 1 + titleArguments->total) % titleArguments->total;
      curArgument = (curArgument->prev) ? curArgument->prev : titleArguments->last;
    } else if (input & PAD_DOWN) {
      selectedArgIdx = (selectedArgIdx + 1) % titleArguments->total;
      curArgument = (curArgument->next) ? curArgument->next : titleArguments->first;
    }
  }
}
