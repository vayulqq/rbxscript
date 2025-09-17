// demo_game.cpp
// Симулятор "игры" — выделяет блок памяти со служебной подписью и списком сущностей.
// Запуск: скомпилировать и запустить; оставляет окно с заголовком "Demo Game".

#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>

#pragma pack(push, 1)
struct DemoEntity {
    int id;
    int team;
    int health;
    float x, y, z;
    bool isActive; // true = реально на карте
};
#pragma pack(pop)

const char SIG[] = "DEMO_ENTITY_LIST_V1"; // подпись, которую будет искать экстернал
const size_t SIG_LEN = sizeof(SIG) - 1;   // без нуль-терминатора
const int ENTITY_COUNT = 64;

LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcA(hWnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Регистрация простого класса окна, чтобы можно было найти по имени "Demo Game"
    WNDCLASSA wc{};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "DemoGameClass";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("DemoGameClass", "Demo Game", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
                              nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_HIDE); // можно скрыть окно

    std::cout << "Demo Game starting. Window title: \"Demo Game\"\n";

    // Выделяем блок памяти: [SIG][count:int][array of DemoEntity]
    size_t blockSize = SIG_LEN + sizeof(int) + ENTITY_COUNT * sizeof(DemoEntity);
    void* block = VirtualAlloc(nullptr, blockSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!block) {
        std::cerr << "VirtualAlloc failed\n";
        return 1;
    }

    // Инициализация сущностей
    std::default_random_engine rng((unsigned)GetTickCount());
    std::uniform_real_distribution<float> posDist(-1000.0f, 1000.0f);
    std::uniform_int_distribution<int> hpDist(40, 100);

    // Записываем подпись
    memcpy(block, SIG, SIG_LEN);
    // count
    int* pCount = (int*)((char*)block + SIG_LEN);
    *pCount = ENTITY_COUNT;
    // entities start
    DemoEntity* entities = (DemoEntity*)((char*)block + SIG_LEN + sizeof(int));

    for (int i = 0; i < ENTITY_COUNT; ++i) {
        entities[i].id = i;
        entities[i].team = (i == 0) ? 1 : ((i % 2) ? 1 : 2); // id 0 — локальный игрок (team 1)
        entities[i].health = hpDist(rng);
        entities[i].x = posDist(rng);
        entities[i].y = posDist(rng);
        entities[i].z = posDist(rng);
    }

    std::cout << "Memory block at: " << block << " (size " << blockSize << " bytes)\n";
    std::cout << "PID: " << GetCurrentProcessId() << "\n";
    std::cout << "Press Ctrl+C in console to stop demo (or close window)." << std::endl;

    // Обновляем сущности в цикле
    bool running = true;
    int tick = 0;
    while (running) {
        // обновляем: простая волнообразная позиция и урон/лечение
        for (int i = 0; i < ENTITY_COUNT; ++i) {
            entities[i].x += 5.0f * sinf((tick + i) * 0.02f);
            entities[i].y += 3.0f * cosf((tick + i) * 0.015f);
            entities[i].z += 1.0f * sinf((tick + i) * 0.01f);

            // случайные изменения HP
            if ((tick + i) % 50 == 0) {
                entities[i].health += (i % 7 == 0) ? -10 : 2;
                if (entities[i].health > 100) entities[i].health = 100;
                if (entities[i].health < 0) entities[i].health = 0;
            }
        }

        // Демонстрация: иногда меняем команды
        if (tick % 1000 == 0) {
            for (int i = 1; i < ENTITY_COUNT; i += 13) {
                entities[i].team = 3 - entities[i].team; // swap 1<->2
            }
        }

        // Задержка
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        tick++;

        // Обработка сообщений окна (чтобы приложение корректно завершалось при закрытии)
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    VirtualFree(block, 0, MEM_RELEASE);
    DestroyWindow(hwnd);
    UnregisterClassA("DemoGameClass", wc.hInstance);
    return 0;
}

