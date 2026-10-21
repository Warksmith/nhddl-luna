#include "ui/ambient.h"
#include "common.h"
#include <kernel.h>
#include <loadfile.h>
#include <ps2snd.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AMBIENT_SPU_BASE 0x10000U
#define AMBIENT_SPU_LIMIT 0x1c0000U
#define AMBIENT_UPLOAD_BYTES 16384U
#define AMBIENT_CORE 1
#define AMBIENT_VOICES 3U
#define AMBIENT_VOLUME 0x3fff

extern unsigned char ps2snd_irx[] __attribute__((aligned(16)));
extern uint32_t size_ps2snd_irx;

static int audioDriversLoaded;
static int audioInitialized;
static int enabled;
static uint32_t channelBytes;
static uint32_t sampleFrames;
static uint8_t uploadBuffer[AMBIENT_UPLOAD_BYTES] __attribute__((aligned(64)));

static uint16_t read16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int inspectAudio(FILE *file) {
  uint8_t header[32];
  if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
      memcmp(header, "LADP", 4) || read16(header + 4) != 1 ||
      read16(header + 6) != 2 || read32(header + 8) != 48000 ||
      read32(header + 20) || read32(header + 24) || read32(header + 28))
    return -1;
  sampleFrames = read32(header + 12);
  channelBytes = read32(header + 16);
  if (channelBytes < 64 || (channelBytes & 63) ||
      channelBytes > (AMBIENT_SPU_LIMIT - AMBIENT_SPU_BASE) / 2 ||
      sampleFrames < 56 || sampleFrames % 28 ||
      sampleFrames > (channelBytes / 16) * 28 ||
      ((sampleFrames / 28 * 16 + 63) & ~63U) != channelBytes)
    return -1;
  if (fseek(file, 0, SEEK_END) ||
      ftell(file) != (long)(sizeof(header) + channelBytes * 2) ||
      fseek(file, sizeof(header), SEEK_SET))
    return -1;
  return 0;
}

static void silenceAudio(void) {
  sceSdSetParam(SD_VOICE(AMBIENT_CORE, 0) | SD_VPARAM_VOLL, 0);
  sceSdSetParam(SD_VOICE(AMBIENT_CORE, 1) | SD_VPARAM_VOLR, 0);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_KOFF, AMBIENT_VOICES);
  enabled = 0;
}

static int uploadChannel(FILE *file, int channel) {
  uint32_t blocks = sampleFrames / 28;
  for (uint32_t offset = 0; offset < channelBytes;) {
    uint32_t count = channelBytes - offset;
    if (count > sizeof(uploadBuffer)) count = sizeof(uploadBuffer);
    if (fread(uploadBuffer, 1, count, file) != count) return -1;
    for (uint32_t i = 0; i < count; i += 16) {
      uint32_t block = (offset + i) / 16;
      if (block >= blocks) continue;
      uint8_t flags = block == 0 ? 6 : block == blocks - 1 ? 3 : 2;
      if ((uploadBuffer[i] >> 4) > 4 || (uploadBuffer[i] & 15) > 12 ||
          uploadBuffer[i + 1] != flags ||
          (block == 0 && (uploadBuffer[i] >> 4) != 0))
        return -1;
    }
    // Only temporary EE/IOP buffers; the complete track lives in SPU2 RAM.
    if (sndLoadSample(uploadBuffer,
                      AMBIENT_SPU_BASE + channel * channelBytes + offset,
                      count) != (int)count)
      return -1;
    offset += count;
  }
  return 0;
}

static void startVoices(void) {
  for (int channel = 0; channel < 2; channel++) {
    uint16_t voice = SD_VOICE(AMBIENT_CORE, channel);
    uint32_t address = AMBIENT_SPU_BASE + channel * channelBytes;
    sceSdSetAddr(voice | SD_VADDR_SSA, address);
    sceSdSetAddr(voice | SD_VADDR_LSAX, address);
    sceSdSetParam(voice | SD_VPARAM_VOLL, channel == 0 ? AMBIENT_VOLUME : 0);
    sceSdSetParam(voice | SD_VPARAM_VOLR, channel == 1 ? AMBIENT_VOLUME : 0);
    sceSdSetParam(voice | SD_VPARAM_PITCH, 0x1000);
    sceSdSetParam(voice | SD_VPARAM_ADSR1,
                  SD_SET_ADSR1(SD_ADSR_AR_LINEARi, 0x10, 0, 15));
    sceSdSetParam(voice | SD_VPARAM_ADSR2,
                  SD_SET_ADSR2(SD_ADSR_SR_LINEARi, 0x7f, SD_ADSR_RR_LINEARd, 0));
  }
  // One key-on starts both channels on the same sound-hardware tick.
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_KON, AMBIENT_VOICES);
  enabled = 1;
}

void ambientForgetDrivers(void) {
  audioDriversLoaded = 0;
  audioInitialized = 0;
  enabled = 0;
}

void ambientStart(int shouldPlay) {
  char path[PATH_MAX];
  int iopResult;
  if (audioInitialized) {
    ambientSetEnabled(shouldPlay);
    return;
  }
  if (!shouldPlay) return;
#ifdef LUNA_EMULATOR_BUILD
  strcpy(path, "host:/ambient.adp");
#else
  snprintf(path, sizeof(path), "mc%c:/APP_LUNA/ambient.adp",
           !strncmp(LAUNCHER_OPTIONS.returnPath, "mc1:", 4) ? '1' : '0');
#endif
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return;
  }
  if (inspectAudio(file)) {
    fclose(file);
    return;
  }
  if (!audioDriversLoaded) {
    if (SifLoadModule("rom0:LIBSD", 0, NULL) < 0 ||
        SifExecModuleBuffer(ps2snd_irx, size_ps2snd_irx, 0, NULL,
                            &iopResult) < 0 || iopResult < 0)
      goto fail;
    audioDriversLoaded = 1;
  }
  // Probe with a deadline before the SDK's blocking RPC initializer.
  SifRpcClientData_t probe __attribute__((aligned(64)));
  memset(&probe, 0, sizeof(probe));
  for (int tries = 0; probe.server == NULL && tries < 100; tries++) {
    if (SifBindRpc(&probe, BINDID_PS2SND, 0) < 0) goto fail;
    if (probe.server == NULL) usleep(1000);
  }
  if (probe.server == NULL || sceSdInit(0) < 0) goto fail;
  audioInitialized = 1;
  silenceAudio();
  sceSdSetCoreAttr(AMBIENT_CORE | SD_CORE_EFFECT_ENABLE, 0);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_PMON, 0);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_NON, 0);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_VMIXL, AMBIENT_VOICES);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_VMIXR, AMBIENT_VOICES);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_VMIXEL, 0);
  sceSdSetSwitch(AMBIENT_CORE | SD_SWITCH_VMIXER, 0);
  sceSdSetParam(AMBIENT_CORE | SD_PARAM_MVOLL, 0x3fff);
  sceSdSetParam(AMBIENT_CORE | SD_PARAM_MVOLR, 0x3fff);
  if (uploadChannel(file, 0) || uploadChannel(file, 1)) goto fail;
  fclose(file);
  startVoices();
  return;
fail:
  fclose(file);
  ambientStop();
}

void ambientSetEnabled(int shouldPlay) {
  if (!audioInitialized) {
    if (shouldPlay) ambientStart(1);
    return;
  }
  if ((shouldPlay != 0) == enabled) return;
  if (shouldPlay) startVoices();
  else silenceAudio();
}

void ambientStop(void) {
  if (audioInitialized) {
    silenceAudio();
    sceSdSetParam(AMBIENT_CORE | SD_PARAM_MVOLL, 0);
    sceSdSetParam(AMBIENT_CORE | SD_PARAM_MVOLR, 0);
    audioInitialized = 0;
  }
}
