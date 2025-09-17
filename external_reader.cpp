// external_reader.cpp
#include <algorithm>
#include <sstream>
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <cmath>

#pragma pack(push, 1)
struct DemoEntity {
    int id;
    int team;
    int health;
    float x, y, z;
    bool isActive; // true = реально на карте
};
#pragma pack(pop)

const char SIG[] = "DEMO_ENTITY_LIST_V1";
const size_t SIG_LEN = sizeof(SIG) - 1;
constexpr std::ptrdiff_t m_iszPlayerName = 0x6E8; // offset для имени игрока (char[128])

volatile bool g_running = true;

BOOL WINAPI CtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

std::uintptr_t ScanForSignature(HANDLE hProcess, const std::string& sig) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::uintptr_t addr = (std::uintptr_t)si.lpMinimumApplicationAddress;
    std::uintptr_t maxAddr = (std::uintptr_t)si.lpMaximumApplicationAddress;

    while (addr < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T q = VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi));
        if (q == 0) break;

        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD)) {
            SIZE_T regionSize = mbi.RegionSize;
            const SIZE_T chunkMax = 1024 * 1024;
            SIZE_T offset = 0;
            while (offset < regionSize) {
                SIZE_T toRead = std::min<SIZE_T>(chunkMax, regionSize - offset);
                std::vector<char> buffer(toRead);
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProcess, (LPCVOID)((std::uintptr_t)mbi.BaseAddress + offset), buffer.data(), toRead, &bytesRead) && bytesRead > 0) {
                    auto it = std::search(buffer.begin(), buffer.begin() + bytesRead,
                                          sig.begin(), sig.end());
                    if (it != buffer.begin() + bytesRead) {
                        std::uintptr_t foundAddr = (std::uintptr_t)mbi.BaseAddress + offset + (it - buffer.begin());
                        return foundAddr;
                    }
                }
                offset += toRead;
                if (!g_running) return 0;
            }
        }
        addr += mbi.RegionSize;
    }
    return 0;
}

double Distance(const DemoEntity& a, const DemoEntity& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx*dx + dy*dy + dz*dz);
}

void SetColorForHP(HANDLE hConsole, int hp) {
    if (hp <= 0) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    } else if (hp < 30) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    } else if (hp < 70) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
    } else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
}

int main() {
    SetConsoleTitleA("External Reader for Demo Game");
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    std::cout << "Looking for window \"Demo Game\"..." << std::endl;
    HWND hwnd = nullptr;
    while (g_running && !(hwnd = FindWindowA(nullptr, "Demo Game"))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    if (!g_running) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        std::cerr << "Failed to get PID from window." << std::endl;
        return 1;
    }
    std::cout << "Found window. PID = " << pid << std::endl;

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        std::cerr << "Failed to open process. Try running as administrator." << std::endl;
        return 1;
    }

    std::cout << "Scanning process memory for signature..." << std::endl;
    std::uintptr_t sigAddr = 0;
    while (g_running && !(sigAddr = ScanForSignature(hProcess, SIG))) {
        std::cout << "Signature not found yet — retrying in 700ms..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }
    if (!g_running) {
        CloseHandle(hProcess);
        return 0;
    }
    std::cout << "Signature found at address: 0x" << std::hex << sigAddr << std::dec << std::endl;

    int count = 0;
    if (!ReadProcessMemory(hProcess, (LPCVOID)(sigAddr + SIG_LEN), &count, sizeof(count), nullptr) || count <= 0 || count > 10000) {
        std::cerr << "Failed to read count or invalid count: " << count << std::endl;
        CloseHandle(hProcess);
        return 1;
    }
    std::cout << "Entity count = " << count << std::endl;

    const std::uintptr_t entitiesBase = sigAddr + SIG_LEN + sizeof(int);
    const SIZE_T entitiesSize = (SIZE_T)count * sizeof(DemoEntity);

    while (g_running) {
        std::vector<DemoEntity> ents(count);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)entitiesBase, ents.data(), entitiesSize, &bytesRead) || bytesRead != entitiesSize) {
            std::cerr << "ReadProcessMemory failed or incomplete read. Exiting." << std::endl;
            break;
        }

        DemoEntity local = ents[0];
        int localTeam = local.team;

        std::vector<DemoEntity> enemies;
        std::vector<std::string> enemyNames;
        for (int i = 0; i < count; ++i) {
            const DemoEntity& e = ents[i];
            if (e.id == local.id) continue;
            if (e.team == 0 || e.team == localTeam) continue;
            if (e.health <= 0) continue;
            if (!e.isActive) continue;

            // читаем имя игрока
            char nameBuffer[128] = {0};
            ReadProcessMemory(hProcess, (LPCVOID)((std::uintptr_t)&e + m_iszPlayerName), nameBuffer, sizeof(nameBuffer), nullptr);
            enemies.push_back(e);
            enemyNames.push_back(std::string(nameBuffer));
        }

        std::sort(enemies.begin(), enemies.end(),
                  [&local](const DemoEntity& a, const DemoEntity& b) {
                      return Distance(a, local) < Distance(b, local);
                  });

        std::sort(enemyNames.begin(), enemyNames.end()); // optional, keep alignment with enemies if needed

        system("cls");
        auto now = std::chrono::system_clock::now();
        std::time_t tnow = std::chrono::system_clock::to_time_t(now);
        std::cout << "=== Enemy Coordinates (Demo) ===   Time: " << std::put_time(std::localtime(&tnow), "%F %T") << "\n";
        std::cout << "Local ID: " << local.id << "  Team: " << local.team
                  << "  HP: " << local.health
                  << "  Pos: (" << local.x << ", " << local.y << ", " << local.z << ")\n";
        std::cout << "---------------------------------------------\n";
        std::cout << std::left << std::setw(6) << "ID"
                  << std::setw(15) << "Name"
                  << std::setw(6) << "HP"
                  << std::setw(10) << "Dist"
                  << std::setw(20) << "Position"
                  << "\n";
        std::cout << "---------------------------------------------\n";

        for (size_t i = 0; i < enemies.size(); ++i) {
            const auto& e = enemies[i];
            double dist = Distance(local, e);
            SetColorForHP(hConsole, e.health);

            std::ostringstream pososs;
            pososs << "(" << std::fixed << std::setprecision(1) << e.x << ", " << e.y << ", " << e.z << ")";
            std::cout << std::left << std::setw(6) << e.id
                      << std::setw(15) << enemyNames[i]
                      << std::setw(6) << e.health
                      << std::setw(10) << std::fixed << std::setprecision(1) << dist
                      << std::setw(20) << pososs.str()
                      << "\n";

            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }

        std::cout << "---------------------------------------------\n";
        std::cout << "Enemies: " << enemies.size() << "   (Press Ctrl+C to quit)\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    CloseHandle(hProcess);
    std::cout << "Exiting external reader." << std::endl;
    return 0;
}
