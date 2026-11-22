#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <array>
#include <string>
#include <functional>
#include <cstdint>

struct PatternDisplayData {
  int instrument = 0;
  uint64_t steps = 0;
  int playingStep = -1;
  bool running = false;
};

namespace {

  struct Glyph {
    char character;
    uint8_t columns[5];
  };

  constexpr Glyph FONT5X7[] = {
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},

    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},

    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
    {'<', {0x08, 0x14, 0x22, 0x41, 0x00}}
  };

  const Glyph* findGlyph(char c) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }

    for (const auto& glyph : FONT5X7) {
      if (glyph.character == c) {
	return &glyph;
      }
    }

    return nullptr;
  }
} // end namespace

struct InstrumentDisplayData {
  int instrument = 0;

  float level = 0.0f;
  float attack = 0.0f;
  float decay = 0.0f;
  float tune = 0.0f;
  float cutoff = 0.0f;
  float resonance = 0.0f;
};

class UI {
public:
  UI();
  ~UI();

  bool init();
  void run(const std::function<void()>& update = {});    
  void shutdown();
  void setInstrumentDisplayData(const InstrumentDisplayData& data);    
  void setPatternDisplayData(const PatternDisplayData& data);
  void setPatternStepCallback(std::function<void(int, int)> callback);
  void setTransportCallback(std::function<void(bool)> callback);
  void setInstrumentSelectCallback(std::function<void(int)> callback);  
    
private:
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;    

  bool running = false;

  static constexpr int WINDOW_WIDTH = 900;
  static constexpr int WINDOW_HEIGHT = 760;

  static constexpr int OLED_SIZE = 128;
  static constexpr int OLED_SCALE = 3;

  TTF_Font* panelFont = nullptr;

  // OLED framebuffer: one byte per pixel, 0 = off, 1 = on.
  std::array<uint8_t, OLED_SIZE * OLED_SIZE> oledPixels{};

  InstrumentDisplayData displayData;
  bool displayDataValid = false;

  PatternDisplayData patternData;
  bool patternDataValid = false;

  int selectedBank = 0;

  std::function<void(int, int)> patternStepCallback;
  std::function<void(bool)> transportCallback;

  bool instrumentSelectMode = false;
  std::function<void(int)> instrumentSelectCallback;  

  void handleMouseClick(int x, int y);    

  void updateOLEDContent();

  void clearOLED();
  void oledSetPixel(int x, int y, bool on = true);
  void oledDrawChar(char c, int x, int y);
  void oledDrawString(const std::string& text, int x, int y);
  void renderOLEDBuffer(int startx, int starty);    

  bool loadFonts();
  void handleEvents();
  void render();
  void renderEncoders();
  void renderShiftButton();    
  void renderOLED();
  void renderFunctionButtons();
  void renderStepButtons();    
 
  void drawText(
		const std::string& text,
		int x,
		int y,
		SDL_Color color
		);
  void drawFilledCircle(int x, int y, int radius, SDL_Color color);    
};
