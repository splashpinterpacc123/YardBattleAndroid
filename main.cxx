#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// === КОНСТАНТЫ ИСПОЛИНСКОГО МАСШТАБА ===
const int ROWS = 5; 
const int COLS = 13;
const int TILE_W = 160; 
const int TILE_H = 175; 
const int OFFSET_X = 60;
const int OFFSET_Y = 160; // Место под верхнюю панель

// === СТРУКТУРЫ ДАННЫХ ===

struct Plant {
    int row, col, shootTimer, hp;
    float scale = 1.0f, animTime = 0.0f;
};

struct Zombie {
    float x;
    int row, hp, eatTimer = 0;
    bool eating = false;
    float hitAnim = 1.0f, animTime;
};

struct Projectile { float x, y; int row; };
struct Sun { float x, y, targetY; };

// === ГЛОБАЛЬНЫЕ РЕСУРСЫ ===
SDL_Texture *texGrass = nullptr, *texPea = nullptr, *texBullet = nullptr, *texZombie = nullptr, *texSun = nullptr;
Mix_Chunk *sfxFire = nullptr, *sfxCoin = nullptr, *sfxLose = nullptr, *sfxEat = nullptr, *sfxBonk = nullptr, *sfxDie = nullptr;
TTF_Font* font = nullptr;

// Функция загрузки (с проверкой ошибок)
SDL_Texture* loadTexture(SDL_Renderer* ren, const std::string& path) {
    SDL_Texture* t = IMG_LoadTexture(ren, path.c_str());
    if (!t) SDL_Log("Ошибка загрузки: %s", path.c_str());
    return t;
}

// Функция сброса игры
void resetGame(std::vector<Plant>& p, std::vector<Zombie>& z, std::vector<Projectile>& b, std::vector<Sun>& s, int& suns, int& kills, Uint32& time) {
    p.clear(); z.clear(); b.clear(); s.clear();
    suns = 450; kills = 0; time = SDL_GetTicks();
}

int main(int argc, char* argv[]) {
    // 1. Инициализация всего
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return -1;
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return -1;
    if (TTF_Init() < 0) return -1;
    // Настройка аудио: 44100 Гц, 2 канала (стерео)
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) return -1;

    // Создаем окно (2200 пикселей в ширину, чтобы влезло всё поле!)
    SDL_Window* win = SDL_CreateWindow("PvZ: Yard Battle 5x13", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 2200, 1100, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // 2. Загрузка ресурсов точно по твоему скриншоту
    texGrass = loadTexture(ren, "greenery.png");
    texPea = loadTexture(ren, "peas.png");
    texBullet = loadTexture(ren, "projectile.png");
    texZombie = loadTexture(ren, "zombie.png");
    texSun = loadTexture(ren, "coin.png");
    font = TTF_OpenFont("Kenney Future.ttf", 48); // Шрифт крупнее

    sfxFire = Mix_LoadWAV("Firepea.wav");
    sfxCoin = Mix_LoadWAV("coin.wav");
    sfxLose = Mix_LoadWAV("bungee_scream.wav");
    sfxEat = Mix_LoadWAV("hit.wav");
    sfxBonk = Mix_LoadWAV("bonk.wav");
    sfxDie = Mix_LoadWAV("diamond.wav");

    // Игровые переменные
    std::vector<Plant> plants;
    std::vector<Zombie> zombies;
    std::vector<Projectile> bullets;
    std::vector<Sun> suns;
    int sunCount = 450, killCount = 0, zombieTimer = 0;
    Uint32 startTime = SDL_GetTicks();
    bool isDragging = false, gameOver = false, running = true;
    int dragX, dragY;

    // Зоны интерфейса
    SDL_Rect cardRect = {40, 30, 150, 110}; // Карта горохострела в меню
    SDL_Rect btnRestart = {900, 500, 250, 80}; // Кнопка рестарта
    SDL_Rect btnExit = {1200, 500, 250, 80};   // Кнопка выхода

    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            
            if (!gameOver) {
                // Обработка кликов в игре
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    // Сбор солнц
                    for (int i = (int)suns.size() - 1; i >= 0; i--) {
                        if (abs(e.button.x - (int)suns[i].x) < 100 && abs(e.button.y - (int)suns[i].y) < 100) {
                            sunCount += 60; Mix_PlayChannel(-1, sfxCoin, 0);
                            suns.erase(suns.begin() + i); break;
                        }
                    }
                    // Посадка
                    if (e.button.x > cardRect.x && e.button.x < cardRect.x+cardRect.w && e.button.y > cardRect.y && e.button.y < cardRect.y+cardRect.h && sunCount >= 100) isDragging = true;
                }
                if (e.type == SDL_MOUSEBUTTONUP && isDragging) {
                    int col = (e.button.x - OFFSET_X) / TILE_W;
                    int row = (e.button.y - OFFSET_Y) / TILE_H;
                    if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
                        bool occ = false; for(auto& p : plants) if(p.row == row && p.col == col) occ = true;
                        if (!occ) { plants.push_back({row, col, 0, 100, 1.0f, 0.0f}); sunCount -= 100; }
                    }
                    isDragging = false;
                }
            } else {
                // Клик по кнопкам в меню проигрыша
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    if (e.button.x > btnRestart.x && e.button.x < btnRestart.x + btnRestart.w && e.button.y > btnRestart.y && e.button.y < btnRestart.y + btnRestart.h) {
                        resetGame(plants, zombies, bullets, suns, sunCount, killCount, startTime);
                        gameOver = false;
                    }
                    if (e.button.x > btnExit.x && e.button.x < btnExit.x + btnExit.w && e.button.y > btnExit.y && e.button.y < btnExit.y + btnExit.h) running = false;
                }
            }
            if (e.type == SDL_MOUSEMOTION) { dragX = e.motion.x; dragY = e.motion.y; }
        }

        if (!gameOver) {
            // === ЛОГИКА ИГРЫ ===
            Uint32 curTime = SDL_GetTicks() - startTime;
            
            // Зомби не спавнятся первые 10 секунд
            if (curTime > 10000) {
                if (++zombieTimer > 150) {
                    zombies.push_back({2150.0f, rand() % ROWS, 15, false, 0, 1.0f, (float)(rand()%100)});
                    zombieTimer = 0;
                }
            }
            // Каждые 20 киллов - волна
            static int lastWave = 0;
            if (killCount > 0 && killCount % 20 == 0 && killCount != lastWave) {
                lastWave = killCount;
                for(int i=0; i<8; i++) zombies.push_back({2200.0f+(rand()%500), rand()%ROWS, 15, false, 0, 1.0f, (float)(rand()%100)});
            }

            // Поведение зомби
            for (auto& z : zombies) {
                z.animTime += 0.07f;
                int targetPlant = -1;
                // Проверка на столкновение с растением
                for (int i=0; i<(int)plants.size(); i++) {
                    float pX = plants[i].col*TILE_W+OFFSET_X;
                    if (z.row == plants[i].row && z.x > pX && z.x < pX + 110) { targetPlant = i; break; }
                }
                if (targetPlant != -1) {
                    z.eating = true;
                    if (++z.eatTimer > 40) { // Кусает каждые 40 кадров
                        plants[targetPlant].hp -= 10;
                        Mix_PlayChannel(-1, sfxEat, 0);
                        z.eatTimer = 0;
                    }
                    if (plants[targetPlant].hp <= 0) { plants.erase(plants.begin()+targetPlant); z.eating = false; }
                } else {
                    z.eating = false; z.x -= 1.3f;
                }
                // Проигрыш
                if (z.x < -60) { gameOver = true; Mix_PlayChannel(-1, sfxLose, 0); }
                if (z.hitAnim > 1.0f) z.hitAnim -= 0.04f;
            }

            // Растения
            for (auto& p : plants) {
                p.animTime += 0.03f;
                if (p.scale > 1.0f) p.scale -= 0.04f;
                bool enemy = false;
                for (auto& z : zombies) if (z.row == p.row && z.x > p.col*TILE_W) enemy = true;
                if (enemy && ++p.shootTimer > 105) {
                    bullets.push_back({(float)p.col*TILE_W+OFFSET_X+110, (float)p.row*TILE_H+OFFSET_Y+70, p.row});
                    p.shootTimer = 0; p.scale = 1.45f; Mix_PlayChannel(-1, sfxFire, 0);
                }
            }

            // Пули
            for (int i = (int)bullets.size()-1; i >= 0; i--) {
                bullets[i].x += 14.0f;
                for (int j = (int)zombies.size()-1; j >= 0; j--) {
                    if (bullets[i].row == zombies[j].row && std::abs(bullets[i].x - zombies[j].x) < 85) {
                        zombies[j].hp--; zombies[j].hitAnim = 1.35f; Mix_PlayChannel(-1, sfxBonk, 0);
                        bullets.erase(bullets.begin()+i);
                        if (zombies[j].hp <= 0) { killCount++; Mix_PlayChannel(-1, sfxDie, 0); zombies.erase(zombies.begin()+j); }
                        goto nextB;
                    }
                }
                if (bullets[i].x > 2200) bullets.erase(bullets.begin()+i);
                nextB:;
            }

            // Солнца
            if (rand() % 140 == 0) suns.push_back({(float)(400+rand()%1600), -120.0f, (float)(350+rand()%550)});
            for (auto& s : suns) if (s.y < s.targetY) s.y += 2.8f;
        }

        // === ОТРИСОВКА ===
        SDL_RenderClear(ren);
        // Газон
        for (int r=0; r<ROWS; r++) for (int c=0; c<COLS; c++) {
            SDL_Rect tr = {c * TILE_W + OFFSET_X, r * TILE_H + OFFSET_Y, TILE_W, TILE_H};
            SDL_RenderCopy(ren, texGrass, NULL, &tr);
        }
        // Растения (Колоссальные 210x210, с анимацией дыхания)
        for (auto& p : plants) {
            int sz = (int)(210 * p.scale + std::sin(p.animTime)*10);
            SDL_Rect r = {p.col*TILE_W+OFFSET_X-25, p.row*TILE_H+OFFSET_Y-25, sz, sz};
            SDL_RenderCopy(ren, texPea, NULL, &r);
            SDL_Rect hp = {p.col*TILE_W+OFFSET_X+30, p.row*TILE_H+OFFSET_Y+10, (int)(100*(p.hp/100.0f)), 10};
            SDL_SetRenderDrawColor(ren, 0, 255, 0, 255); SDL_RenderFillRect(ren, &hp);
        }
        // Зомби (Колоссальные 230x250, с покачиванием)
        for (auto& z : zombies) {
            int sw = (int)(230 * z.hitAnim), sh = (int)(250 * z.hitAnim);
            SDL_Rect r = {(int)z.x-60, z.row*TILE_H+OFFSET_Y-70, sw, sh};
            double angle = std::sin(z.animTime)*7.0; SDL_Point center = {sw/2, sh};
            SDL_RenderCopyEx(ren, texZombie, NULL, &r, angle, &center, SDL_FLIP_NONE);
        }
        for (auto& b : bullets) { SDL_Rect r = {(int)b.x, (int)b.y, 65, 65}; SDL_RenderCopy(ren, texBullet, NULL, &r); }
        for (auto& s : suns) { SDL_Rect r = {(int)s.x, (int)s.y, 130, 130}; SDL_RenderCopy(ren, texSun, NULL, &r); }

        // === ПАНЕЛЬ UI ===
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255); SDL_Rect bar = {0, 0, 2200, 150}; SDL_RenderFillRect(ren, &bar);
        SDL_RenderCopy(ren, texPea, NULL, &cardRect); // Карта в меню
        if (font) {
            std::string st = "SUNS: " + std::to_string(sunCount) + "  KILLS: " + std::to_string(killCount);
            SDL_Surface* sT = TTF_RenderText_Blended(font, st.c_str(), {255, 255, 0, 255});
            SDL_Texture* tT = SDL_CreateTextureFromSurface(ren, sT);
            SDL_Rect rT = {230, 50, sT->w, sT->h}; SDL_RenderCopy(ren, tT, NULL, &rT);
            SDL_FreeSurface(sT); SDL_DestroyTexture(tT);
        }
        if (isDragging) {
            SDL_Rect dr = {dragX-105, dragY-105, 210, 210};
            SDL_SetTextureAlphaMod(texPea, 170); SDL_RenderCopy(ren, texPea, NULL, &dr); SDL_SetTextureAlphaMod(texPea, 255);
        }

        // === МЕНЮ ПРОИГРЫША ===
        if (gameOver) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 220); SDL_Rect fs = {0, 0, 2200, 1100}; SDL_RenderFillRect(ren, &fs);
            
            if (font) {
                SDL_Surface* sL = TTF_RenderText_Blended(font, "THE ZOMBIES ATE YOUR BRAINS!", {255, 0, 0, 255});
                SDL_Texture* tL = SDL_CreateTextureFromSurface(ren, sL);
                SDL_Rect rL = {1100-sL->w/2, 350, sL->w, sL->h}; SDL_RenderCopy(ren, tL, NULL, &rL);
                SDL_FreeSurface(sL); SDL_DestroyTexture(tL);

                SDL_SetRenderDrawColor(ren, 0, 150, 0, 255); SDL_RenderFillRect(ren, &btnRestart);
                SDL_SetRenderDrawColor(ren, 150, 0, 0, 255); SDL_RenderFillRect(ren, &btnExit);

                SDL_Surface* sR = TTF_RenderText_Blended(font, "RESTART", {255,255,255,255});
                SDL_Texture* tR = SDL_CreateTextureFromSurface(ren, sR);
                SDL_Rect rR = {btnRestart.x+(btnRestart.w-sR->w/2)/2, btnRestart.y+15, sR->w/2, sR->h/2};
                SDL_RenderCopy(ren, tR, NULL, &rR);
                SDL_FreeSurface(sR); SDL_DestroyTexture(tR);

                SDL_Surface* sE = TTF_RenderText_Blended(font, "EXIT", {255,255,255,255});
                SDL_Texture* tE = SDL_CreateTextureFromSurface(ren, sE);
                SDL_Rect rE = {btnExit.x+(btnExit.w-sE->w/2)/2, btnExit.y+15, sE->w/2, sE->h/2};
                SDL_RenderCopy(ren, tE, NULL, &rE);
                SDL_FreeSurface(sE); SDL_DestroyTexture(tE);
            }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    // Очистка
    Mix_Quit(); IMG_Quit(); TTF_Quit(); SDL_Quit();
    return 0;
}
