#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <array>
#include <string>

class UI {
public:
    UI();
    ~UI();

    bool init();
    void run();
    void shutdown();

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;    

    bool running = false;

    static constexpr int WINDOW_WIDTH = 900;
    static constexpr int WINDOW_HEIGHT = 760;

    static constexpr int OLED_SIZE = 128;
    static constexpr int OLED_SCALE = 3;

    TTF_Font* panelFont = nullptr;

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
