#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

UI::UI() = default;

UI::~UI() {
  shutdown();
}

bool UI::init() {

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    return false;
  }

  if (TTF_Init() != 0) {
    return false;
  }

  panelFont = TTF_OpenFont("fonts/data-latin.ttf",
			   16);

  if (!panelFont) {
    SDL_Log("Could not load font: %s", TTF_GetError());
    return false;
  }  

  window = SDL_CreateWindow(
			    "Drum Machine",
			    SDL_WINDOWPOS_CENTERED,
			    SDL_WINDOWPOS_CENTERED,
			    WINDOW_WIDTH,
			    WINDOW_HEIGHT,
			    SDL_WINDOW_SHOWN
			    );

  if (!window) {
    return false;
  }

  renderer = SDL_CreateRenderer(
				window,
				-1,
				SDL_RENDERER_ACCELERATED |
				SDL_RENDERER_PRESENTVSYNC
				);

  if (!renderer) {
    return false;
  }

  running = true;
  return true;
}

void UI::shutdown() {
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }

  SDL_Quit();
}

void UI::run(const std::function<void()>& update) {
  while (running) {
    if (update) {
      update();
    }

    handleEvents();
    render();
  }
}

void UI::handleEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {

        if (event.type == SDL_QUIT) {
            running = false;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN &&
            event.button.button == SDL_BUTTON_LEFT) {

            const int centerY = 100;
            const int spacing = 190;
            const int startX = 165;

            for (int i = 0; i < 4; ++i) {
                int centerX = startX + i * spacing;

                int dx = event.button.x - centerX;
                int dy = event.button.y - centerY;

                if (dx * dx + dy * dy <= 36 * 36) {
                    draggingEncoder = i;
                    lastMouseY = event.button.y;
                    SDL_CaptureMouse(SDL_TRUE);
                    break;
                }
            }

            if (draggingEncoder < 0) {
                handleMouseClick(
                    event.button.x,
                    event.button.y
                );
            }
        }

        if (event.type == SDL_MOUSEMOTION &&
            draggingEncoder >= 0) {
            handleEncoderMotion(event.motion.y);
        }

        if (event.type == SDL_MOUSEBUTTONUP &&
            event.button.button == SDL_BUTTON_LEFT) {
            draggingEncoder = -1;
            SDL_CaptureMouse(SDL_FALSE);
        }
    }
}

void UI::drawText(
		  const std::string& text,
		  int x,
		  int y,
		  SDL_Color color
		  ) {
  if (!panelFont) {
    return;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(
						panelFont,
						text.c_str(),
						color
						);

  if (!surface) {
    return;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(
						      renderer,
						      surface
						      );

  if (texture) {
    SDL_Rect destination{
      x,
      y,
      surface->w,
      surface->h
    };

    SDL_RenderCopy(renderer, texture, nullptr, &destination);
    SDL_DestroyTexture(texture);
  }

  SDL_FreeSurface(surface);
}  

void UI::renderEncoders() {

  const int centerY = 100;
  const int spacing = 190;
  const int startX = 165;

  const char* labels[] = {
    "LEVEL",
    "TUNE",
    "CUTOFF",
    "DECAY"
  };

  for (int i = 0; i < 4; ++i) {
    int x = startX + i * spacing;

    // Encoder outer housing.
    SDL_SetRenderDrawColor(renderer, 65, 65, 65, 255);
    SDL_Rect housing{x - 43, centerY - 43, 86, 86};
    SDL_RenderFillRect(renderer, &housing);

    // Circular knob.
    drawFilledCircle(x, centerY, 36, {25, 25, 25, 255});	

    // Indicator mark.
    SDL_SetRenderDrawColor(renderer, 230, 230, 220, 255);
    SDL_Rect indicator{x - 2, centerY - 30, 4, 12};
    SDL_RenderFillRect(renderer, &indicator);

    // Parameter label.
    drawText(labels[i], x - 30, centerY + 53,
	     {220, 220, 220, 255});
  }
}

void UI::renderShiftButton() {

  const int x = 800;
  const int y = 80;

  // Button housing.
  SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
  SDL_Rect housing{x - 8, y - 8, 105, 65};
  SDL_RenderFillRect(renderer, &housing);

  // SHIFT button.
  SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
  SDL_Rect button{x, y, 65, 40};
  SDL_RenderFillRect(renderer, &button);

  // Latched-state LED (temporary always-off state).
  SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);

  int ledX = x + 32;
  int ledY = y - 15;

  for (int dy = -5; dy <= 5; ++dy) {
    for (int dx = -5; dx <= 5; ++dx) {
      if (dx * dx + dy * dy <= 25) {
	SDL_RenderDrawPoint(
			    renderer,
			    ledX + dx,
			    ledY + dy
			    );
      }
    }
  }

  drawText("SHIFT", x + 8, y + 13,
	   {255, 255, 255, 255});
}

void UI::renderOLED() {

  const int x = (WINDOW_WIDTH - OLED_SIZE * OLED_SCALE) / 2;
  const int y = 200;

  // OLED bezel.
  SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
  SDL_Rect bezel{
    x - 12,
    y - 12,
    OLED_SIZE * OLED_SCALE + 24,
    OLED_SIZE * OLED_SCALE + 24
  };
  SDL_RenderFillRect(renderer, &bezel);

  // OLED screen.
  SDL_SetRenderDrawColor(renderer, 3, 18, 10, 255);
  SDL_Rect screen{
    x,
    y,
    OLED_SIZE * OLED_SCALE,
    OLED_SIZE * OLED_SCALE
  };
  SDL_RenderFillRect(renderer, &screen);

  /*    clearOLED();

	oledDrawString("DRUM MACHINE", 8, 8);
	oledDrawString("INSTRUMENT 01", 8, 25);
	oledDrawString("LEVEL: 100%", 8, 43);
	oledDrawString("TUNE:  64", 8, 61);
	oledDrawString("CUTOFF: 90", 8, 79);
	oledDrawString("DECAY:  72", 8, 97); */
  updateOLEDContent();

  renderOLEDBuffer(x, y);    

  // Temporary OLED content.
  /*    SDL_SetRenderDrawColor(renderer, 80, 220, 120, 255);

	SDL_Rect border{
        x + 8,
        y + 8,
        OLED_SIZE * OLED_SCALE - 16,
        OLED_SIZE * OLED_SCALE - 16
	};
	SDL_RenderDrawRect(renderer, &border); */
}

void UI::renderFunctionButtons() {

    const int y = 610;
    const int width = 95;
    const int spacing = 105;
    const int startX = 25;

    const char* labels[] = {
        "INST",
        "BANK 1",
        "BANK 2",
        "BANK 3",
        "BANK 4",
        "LAST STEP",
        recordingData.recording ? "STOP REC" : "REC"
    };

    for (int i = 0; i < 7; ++i) {

        int x = startX + i * spacing;

        bool selected =
            (i >= 1 && i <= 4 &&
             selectedBank == i - 1);

        if (i == 6 && recordingData.recording) {
            SDL_SetRenderDrawColor(renderer, 150, 35, 35, 255);
        }
        else if (selected) {
            SDL_SetRenderDrawColor(renderer, 35, 100, 55, 255);
        }
        else {
            SDL_SetRenderDrawColor(renderer, 65, 65, 65, 255);
        }

        SDL_Rect button{x, y, width, 42};
        SDL_RenderFillRect(renderer, &button);

        int textWidth = 0;
        int textHeight = 0;

        if (panelFont) {
            TTF_SizeUTF8(
                panelFont,
                labels[i],
                &textWidth,
                &textHeight
            );

            drawText(
                labels[i],
                x + (width - textWidth) / 2,
                y + (42 - textHeight) / 2,
                {230, 230, 220, 255}
            );
        }
    }

    // Play / Stop button.
    const char* transportLabel =
        patternData.running ? "STOP" : "PLAY";

    SDL_SetRenderDrawColor(renderer, 65, 65, 65, 255);

    SDL_Rect transportButton{760, y, 95, 42};
    SDL_RenderFillRect(renderer, &transportButton);

    int textWidth = 0;
    int textHeight = 0;

    if (panelFont) {
        TTF_SizeUTF8(
            panelFont,
            transportLabel,
            &textWidth,
            &textHeight
        );

        drawText(
            transportLabel,
            760 + (95 - textWidth) / 2,
            y + (42 - textHeight) / 2,
            {230, 230, 220, 255}
        );
    }
}

void UI::renderStepButtons() {

  const int count = 16;
  const int buttonSize = 38;
  const int spacing = 51;
  const int startX = 45;
  const int y = 690;

  for (int i = 0; i < count; ++i) {

    int x = startX + i * spacing;

    SDL_Color ledColor{45, 45, 45, 255};

    int absoluteStep = selectedBank * 16 + i;

    if (instrumentSelectMode) {
      if (patternDataValid &&
	  patternData.instrument == i) {
        ledColor = {90, 255, 120, 255};
      }
    }
    else if (lastStepSelectMode) {
      if (patternDataValid &&
	  patternData.lastStep == absoluteStep + 1) {
        ledColor = {90, 255, 120, 255};
      }
    }
    else {
      bool active = patternDataValid &&
        (patternData.steps & (uint64_t{1} << absoluteStep)) != 0;

      bool playing = patternDataValid &&
        patternData.running &&
        patternData.playingStep == absoluteStep;

      if (active) {
        ledColor = {20, 150, 65, 255};
      }

      if (playing) {
        ledColor = {90, 255, 120, 255};
      }
    }    

    SDL_SetRenderDrawColor(renderer,
			   ledColor.r,
			   ledColor.g,
			   ledColor.b,
			   ledColor.a);

    SDL_Rect led{x + 10, y - 13, 18, 7};
    SDL_RenderFillRect(renderer, &led);

    // Button body.
    SDL_SetRenderDrawColor(renderer, 75, 75, 75, 255);

    SDL_Rect button{x, y, buttonSize, buttonSize};
    SDL_RenderFillRect(renderer, &button);

    // Step number.
    char label[4];
    std::snprintf(
		  label, sizeof(label), "%d", i + 1
		  );

    int textWidth = 0;
    int textHeight = 0;

    if (panelFont) {
      TTF_SizeUTF8(
		   panelFont, label,
		   &textWidth, &textHeight
		   );

      drawText(
	       label,
	       x + (buttonSize - textWidth) / 2,
	       y + (buttonSize - textHeight) / 2,
	       {230, 230, 220, 255}
	       );
    }
  }
}

void UI::render() {
  SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
  SDL_RenderClear(renderer);

  renderEncoders();
  renderShiftButton();
  renderOLED();
  renderFunctionButtons();
  renderStepButtons();

  SDL_RenderPresent(renderer);
}

void UI::drawFilledCircle(
			  int x,
			  int y,
			  int radius,
			  SDL_Color color
			  ) {
  SDL_SetRenderDrawColor(
			 renderer,
			 color.r,
			 color.g,
			 color.b,
			 color.a
			 );

  for (int dy = -radius; dy <= radius; ++dy) {
    int dx = static_cast<int>(
			      std::sqrt(radius * radius - dy * dy)
			      );

    SDL_RenderDrawLine(
		       renderer,
		       x - dx,
		       y + dy,
		       x + dx,
		       y + dy
		       );
  }
}

void UI::clearOLED() {
  oledPixels.fill(0);
}

void UI::oledSetPixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_SIZE ||
      y < 0 || y >= OLED_SIZE) {
    return;
  }

  oledPixels[y * OLED_SIZE + x] = on ? 1 : 0;
}

void UI::oledDrawChar(char c, int x, int y) {
  const Glyph* glyph = findGlyph(c);

  if (!glyph) {
    return;
  }

  for (int col = 0; col < 5; ++col) {
    for (int row = 0; row < 7; ++row) {
      bool pixel = glyph->columns[col] & (1 << row);

      if (pixel) {
	oledSetPixel(x + col, y + row);
      }
    }
  }
}

void UI::oledDrawString(
			const std::string& text,
			int x,
			int y
			) {
  int cursorX = x;

  for (char c : text) {
    if (c == '\n') {
      cursorX = x;
      y += 9;
      continue;
    }

    if (c != ' ') {
      oledDrawChar(c, cursorX, y);
    }

    cursorX += 6;
  }
}

void UI::renderOLEDBuffer(int startx, int starty) {

  SDL_SetRenderDrawColor(
			 renderer,
			 80, 220, 120, 255
			 );

  const int scale = OLED_SCALE;

  for (int y = 0; y < OLED_SIZE; ++y) {
    for (int x = 0; x < OLED_SIZE; ++x) {
      if (oledPixels[y * OLED_SIZE + x]) {
	SDL_Rect pixel{
	  startx + x * scale,
	  starty + y * scale,
	  scale,
	  scale
	};

	SDL_RenderFillRect(renderer, &pixel);
      }
    }
  }
}

void UI::setInstrumentDisplayData(
				  const InstrumentDisplayData& data
				  ) {
  displayData = data;
  displayDataValid = true;
}

void UI::updateOLEDContent() {

  clearOLED();

  if (!displayDataValid) {
    oledDrawString("DRUM MACHINE", 8, 8);
    oledDrawString("WAITING...", 8, 30);
    return;
  }

  char buffer[32];

  std::snprintf(
      buffer,
      sizeof(buffer),
      "INST %02d",
      displayData.instrument + 1
  );
  oledDrawString(buffer, 8, 8);

  std::snprintf(
		buffer,
		sizeof(buffer),
		"B%d L%02d",
		selectedBank + 1,
		patternData.lastStep
		);

  oledDrawString(buffer, 80, 8);  

  oledDrawString("LEVEL", 8, 27);
  std::snprintf(buffer, sizeof(buffer), "%.2f",
		displayData.level);
  oledDrawString(buffer, 50, 27);

  oledDrawString("TUNE", 8, 39);
  std::snprintf(buffer, sizeof(buffer), "%+.1f",
		displayData.tune);
  oledDrawString(buffer, 50, 39);

  oledDrawString("CUTOFF", 8, 51);
  std::snprintf(buffer, sizeof(buffer), "%.0f",
		displayData.cutoff);
  oledDrawString(buffer, 50, 51);

  oledDrawString("DECAY", 8, 63);
  std::snprintf(buffer, sizeof(buffer), "%.2f",
		displayData.decay);
  oledDrawString(buffer, 50, 63);

  oledDrawString("ATTACK", 8, 75);
  std::snprintf(buffer, sizeof(buffer), "%.3f",
		displayData.attack);
  oledDrawString(buffer, 50, 75);

  oledDrawString("RES", 8, 87);
  std::snprintf(buffer, sizeof(buffer), "%.2f",
		displayData.resonance);
  oledDrawString(buffer, 50, 87);

  if (recordingData.recording) {
    oledDrawString("REC RECORDING", 8, 105);

    std::snprintf(
		  buffer,
		  sizeof(buffer),
		  "%02d:%04.1f",
		  static_cast<int>(recordingData.elapsedSeconds / 60.0f),
		  std::fmod(recordingData.elapsedSeconds, 60.0f)
		  );

    oledDrawString(buffer, 8, 117);
  }
  else if (recordingData.saved) {
    oledDrawString("SAMPLE SAVED", 8, 105);
  }
  else {
    oledDrawString("REC READY", 8, 105);
  }  
}

void UI::setPatternDisplayData(
			       const PatternDisplayData& data
			       ) {
  patternData = data;
  patternDataValid = true;
}

void UI::setPatternStepCallback(
				std::function<void(int, int)> callback
				) {
  patternStepCallback = std::move(callback);
}

void UI::setTransportCallback(
			      std::function<void(bool)> callback
			      ) {
  transportCallback = std::move(callback);
}

void UI::handleMouseClick(int x, int y) {

  // INST button
  if (y >= 610 && y <= 652 &&
      x >= 100 && x < 195) {
    instrumentSelectMode = !instrumentSelectMode;
    lastStepSelectMode = false;
    return;
  }

  // Function buttons.
  if (y >= 610 && y <= 652) {

    const int width = 95;
    const int spacing = 105;
    const int startX = 25;

    // INST button.
    if (x >= startX && x < startX + width) {
      instrumentSelectMode = !instrumentSelectMode;
      lastStepSelectMode = false;
      return;
    }

    // Bank buttons.
    for (int i = 1; i <= 4; ++i) {
      int buttonX = startX + i * spacing;

      if (x >= buttonX && x < buttonX + width) {
	selectedBank = i - 1;
	return;
      }
    }

    // LAST STEP button.
    if (x >= 550 && x < 645) {
      lastStepSelectMode = !lastStepSelectMode;
      instrumentSelectMode = false;
      return;
    }

    // RECORD button.
    if (x >= 655 && x < 750) {
      if (recordCallback) {
	recordCallback();
      }
      return;
    }

    // Play / Stop button.
    if (x >= 760 && x < 855) {
      if (transportCallback) {
	bool start = !patternData.running;
	transportCallback(start);
      }
      return;
    }
  }  

  // Pattern step buttons.
  if (y >= 690 && y <= 728) {
    const int buttonSize = 38;
    const int spacing = 51;
    const int startX = 45;

    for (int i = 0; i < 16; ++i) {
      int buttonX = startX + i * spacing;

      if (x >= buttonX && x < buttonX + buttonSize) {

	if (instrumentSelectMode) {
	  if (instrumentSelectCallback) {
	    instrumentSelectCallback(i);
	  }

	  instrumentSelectMode = false;
	}
	else if (lastStepSelectMode) {
	  int absoluteStep = selectedBank * 16 + i;

	  if (lastStepCallback) {
	    lastStepCallback(absoluteStep + 1);
	  }

	  lastStepSelectMode = false;
	}
	else {
	  int absoluteStep = selectedBank * 16 + i;

	  if (patternStepCallback && patternDataValid) {
	    patternStepCallback(
				patternData.instrument,
				absoluteStep
				);
	  }
	}

	return;
      }
    }
  }  
}

void UI::setInstrumentSelectCallback(std::function<void(int)> callback) {
  instrumentSelectCallback = callback;
}

void UI::setLastStepCallback(std::function<void(int)> callback) {
    lastStepCallback = callback;
}

void UI::setEncoderCallback(
    std::function<void(int, int, float)> callback)
{
    encoderCallback = callback;
}

void UI::handleEncoderMotion(int mouseY)
{
    if (draggingEncoder < 0 ||
        !encoderCallback ||
        !displayDataValid) {
        return;
    }

    int deltaY = lastMouseY - mouseY;
    lastMouseY = mouseY;

    if (deltaY == 0)
        return;

    constexpr float sensitivity = 0.005f;

    encoderCallback(
        displayData.instrument,
        draggingEncoder,
        deltaY * sensitivity
    );
}

void UI::setRecordingDisplayData(
    const RecordingDisplayData& data)
{
    recordingData = data;
}

void UI::setRecordCallback(std::function<void()> callback) {
    recordCallback = std::move(callback);
}
