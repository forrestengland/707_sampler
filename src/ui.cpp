
#include "ui.h"

#include <algorithm>
#include <cmath>

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

void UI::run() {
    while (running) {
        handleEvents();
        render();
    }
}

void UI::handleEvents() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
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

    clearOLED();

    oledDrawString("DRUM MACHINE", 8, 8);
    oledDrawString("INSTRUMENT 01", 8, 25);
    oledDrawString("LEVEL: 100%", 8, 43);
    oledDrawString("TUNE:  64", 8, 61);
    oledDrawString("CUTOFF: 90", 8, 79);
    oledDrawString("DECAY:  72", 8, 97);

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
    const int spacing = 115;
    const int startX = 100;

    const char* labels[] = {
        "INST",
        "BANK 1",
        "BANK 2",
        "BANK 3",
        "BANK 4",
        "LAST STEP"
    };

    for (int i = 0; i < 6; ++i) {
        int x = startX + i * spacing;

        SDL_SetRenderDrawColor(renderer, 65, 65, 65, 255);
        SDL_Rect button{x, y, width, 42};
        SDL_RenderFillRect(renderer, &button);

	int textWidth = 0;
	int textHeight = 0;

	if (panelFont) {
	  TTF_SizeUTF8(panelFont, labels[i], &textWidth, &textHeight);

	  drawText(
		   labels[i],
		   x + (width - textWidth) / 2,
		   y + (42 - textHeight) / 2,
		   {230, 230, 220, 255}
		   );
	}	
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

        // LED.
        SDL_SetRenderDrawColor(
            renderer,
            i == 0 ? 240 : 45,
            i == 0 ? 190 : 45,
            45,
            255
        );

        SDL_Rect led{x + 13, y - 13, 12, 7};
        SDL_RenderFillRect(renderer, &led);

        // Button.
        SDL_SetRenderDrawColor(renderer, 75, 75, 75, 255);
        SDL_Rect button{x, y, buttonSize, buttonSize};
        SDL_RenderFillRect(renderer, &button);
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
