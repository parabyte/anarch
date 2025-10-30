#include <Arduino.h>
#include <fabgl.h>

#define SFG_SCREEN_RESOLUTION_X 320
#define SFG_SCREEN_RESOLUTION_Y 240
#define SFG_RESOLUTION_SCALEDOWN 1
#define SFG_FPS 20
#define SFG_RAYCASTING_MAX_STEPS 20
#define SFG_RAYCASTING_MAX_HITS 6
#define SFG_RAYCASTING_SUBSAMPLE 2
#define SFG_DIMINISH_SPRITES 1
#define SFG_CAN_EXIT 0

#include "game.h"

static constexpr int kScreenWidth = SFG_SCREEN_RESOLUTION_X;
static constexpr int kScreenHeight = SFG_SCREEN_RESOLUTION_Y;
static constexpr int kScanlinesPerCallback = 2;
static constexpr uint32_t kMenuDemoDelayMs = 15000; // 15 seconds

fabgl::VGADirectController DisplayController;

static uint8_t frameBuffer[kScreenWidth * kScreenHeight];
static uint8_t paletteLUT[256];

struct DemoAction
{
  uint16_t duration;
  uint16_t keyMask;
};

static const DemoAction kDemoScript[] = {
  {120, (uint16_t(1) << SFG_KEY_UP) | (uint16_t(1) << SFG_KEY_A)},
  {40,  (uint16_t(1) << SFG_KEY_RIGHT)},
  {100, (uint16_t(1) << SFG_KEY_UP)},
  {60,  (uint16_t(1) << SFG_KEY_LEFT)},
  {80,  (uint16_t(1) << SFG_KEY_UP) | (uint16_t(1) << SFG_KEY_RIGHT)},
  {50,  (uint16_t(1) << SFG_KEY_A)}
};

static constexpr size_t kDemoScriptLength = sizeof(kDemoScript) / sizeof(kDemoScript[0]);

static bool demoActive = false;
static uint16_t demoKeyMask = 0;
static size_t demoScriptIndex = 0;
static uint16_t demoStepCounter = 0;

static uint32_t menuIdleStartMs = 0;
static uint8_t previousState = SFG_GAME_STATE_INIT;

static inline void applyDemoStep()
{
  if (kDemoScriptLength == 0)
  {
    demoKeyMask = 0;
    return;
  }

  demoKeyMask = kDemoScript[demoScriptIndex].keyMask;
}

static void resetDemo()
{
  demoActive = false;
  demoKeyMask = 0;
  demoScriptIndex = 0;
  demoStepCounter = 0;
}

static void startDemo()
{
  if (demoActive || kDemoScriptLength == 0)
    return;

  demoActive = true;
  demoScriptIndex = 0;
  demoStepCounter = 0;
  applyDemoStep();

  SFG_game.selectedLevel = 0;
  SFG_setAndInitLevel(0);
}

static void advanceDemo()
{
  if (!demoActive || kDemoScriptLength == 0)
    return;

  demoStepCounter++;

  if (demoStepCounter >= kDemoScript[demoScriptIndex].duration)
  {
    demoStepCounter = 0;
    demoScriptIndex = (demoScriptIndex + 1) % kDemoScriptLength;
    applyDemoStep();
  }
}

void IRAM_ATTR drawScanline(void *, uint8_t *dest, int scanLine)
{
  for (int line = 0; line < kScanlinesPerCallback; ++line)
  {
    const int y = scanLine + line;

    if (y >= kScreenHeight)
      break;

    const uint8_t *lineSrc = frameBuffer + y * kScreenWidth;

    for (int x = 0; x < kScreenWidth; ++x)
      dest[x] = paletteLUT[lineSrc[x]];

    dest += kScreenWidth;
  }
}

void SFG_setPixel(uint16_t x, uint16_t y, uint8_t colorIndex)
{
  frameBuffer[y * kScreenWidth + x] = colorIndex;
}

uint32_t SFG_getTimeMs()
{
  return millis();
}

void SFG_sleepMs(uint16_t timeMs)
{
  delay(timeMs);
}

int8_t SFG_keyPressed(uint8_t key)
{
  if (demoActive && (demoKeyMask & (uint16_t(1) << key)))
    return 1;

  return 0;
}

void SFG_getMouseOffset(int16_t *x, int16_t *y)
{
  *x = 0;
  *y = 0;
}

void SFG_setMusic(uint8_t)
{
}

void SFG_save(uint8_t data[SFG_SAVE_SIZE])
{
  (void) data;
}

void SFG_processEvent(uint8_t event, uint8_t data)
{
  (void) event;
  (void) data;
}

uint8_t SFG_load(uint8_t data[SFG_SAVE_SIZE])
{
  (void) data;
  return 0;
}

void SFG_playSound(uint8_t, uint8_t)
{
}

static void initPaletteLUT()
{
  for (int i = 0; i < 256; ++i)
  {
    uint16_t rgb565 = paletteRGB565[i];

    uint8_t r = (rgb565 >> 11) & 0x1F;
    uint8_t g = (rgb565 >> 5) & 0x3F;
    uint8_t b = rgb565 & 0x1F;

    uint8_t r2 = r >> 3;
    uint8_t g2 = g >> 4;
    uint8_t b2 = b >> 3;

    paletteLUT[i] = 0xC0 | (b2 << 4) | (g2 << 2) | r2;
  }
}

static void clearBuffers(uint8_t value)
{
  const size_t total = static_cast<size_t>(kScreenWidth) * kScreenHeight;

  for (size_t i = 0; i < total; ++i)
    frameBuffer[i] = value;
}

void setup()
{
  DisplayController.begin();
  DisplayController.setScanlinesPerCallBack(kScanlinesPerCallback);
  DisplayController.setDrawScanlineCallback(drawScanline);
  DisplayController.setResolution(QVGA_320x240_60Hz);

  initPaletteLUT();
  clearBuffers(0);

  menuIdleStartMs = millis();
  previousState = SFG_GAME_STATE_INIT;
  resetDemo();

  SFG_init();
}

void loop()
{
  SFG_mainLoopBody();

  uint8_t currentState = SFG_game.state;

  if (currentState != previousState)
  {
    if (currentState == SFG_GAME_STATE_MENU)
    {
      menuIdleStartMs = millis();
      resetDemo();
    }

    previousState = currentState;
  }

  if (!demoActive && currentState == SFG_GAME_STATE_MENU)
  {
    bool menuInput = false;

    for (uint8_t i = 0; i < SFG_KEY_COUNT; ++i)
    {
      if (SFG_game.keyStates[i] > 0)
      {
        menuInput = true;
        break;
      }
    }

    if (menuInput)
      menuIdleStartMs = millis();

    if (millis() - menuIdleStartMs >= kMenuDemoDelayMs)
    {
      startDemo();
      currentState = SFG_game.state;
      previousState = currentState;
    }
  }

  if (demoActive)
  {
    if (currentState == SFG_GAME_STATE_MENU)
    {
      resetDemo();
      menuIdleStartMs = millis();
    }
    else
    {
      advanceDemo();
    }
  }
}
