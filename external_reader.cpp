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
    bool isActive;
};
#pragma pack(pop)

const char SIG[] = "DEMO_ENTITY_LIST_V1";
const size_t SIG_LEN = sizeof(SIG) - 1;
constexpr std::ptrdiff_t m_iszPlayerName = 0x6E8; // офсет имени

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
                    auto it = std::search(buffer.begin(), buffer.begin() + bytesRead, sig.begin(), sig.end());
                    if (it != buffer.begin() + bytesRead) {
                        return (std::uintptr_t)mbi.BaseAddress + offset + (it - buffer.begin());
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
    if (hp <= 0) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    else if (hp < 30) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    else if (hp < 70) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
    else SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
}

int main() {
    SetConsoleTitleA("External Reader for Demo Game");
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    std::cout << "Looking for window \"Demo Game\"...\n";
    HWND hwnd = nullptr;
    while (g_running && !(hwnd = FindWindowA(nullptr, "Demo Game"))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    if (!g_running) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return 1;

    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return 1;

    std::cout << "Scanning process memory for signature...\n";
    std::uintptr_t sigAddr = 0;
    while (g_running && !(sigAddr = ScanForSignature(hProcess, SIG))) {
        std::cout << "Signature not found — retrying...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }
    if (!g_running) return 0;

    int count = 0;
    ReadProcessMemory(hProcess, (LPCVOID)(sigAddr + SIG_LEN), &count, sizeof(count), nullptr);
    if (count <= 0 || count > 10000) return 1;

    std::uintptr_t entitiesBase = sigAddr + SIG_LEN + sizeof(int);

    while (g_running) {
        std::vector<DemoEntity> ents(count);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(hProcess, (LPCVOID)entitiesBase, ents.data(), count * sizeof(DemoEntity), &bytesRead) || bytesRead != count * sizeof(DemoEntity)) break;

        DemoEntity local = ents[0];
        int localTeam = local.team;

        std::vector<std::pair<DemoEntity, std::string>> enemies;
        for (int i = 0; i < count; ++i) {
            DemoEntity& e = ents[i];
            if (!e.isActive || e.id == local.id || e.team == 0 || e.team == localTeam || e.health <= 0) continue;

            char nameBuffer[128] = {};
            std::uintptr_t entityAddr = entitiesBase + i * sizeof(DemoEntity);
            ReadProcessMemory(hProcess, (LPCVOID)(entityAddr + m_iszPlayerName), nameBuffer, sizeof(nameBuffer), nullptr);
            enemies.emplace_back(e, std::string(nameBuffer));
        }

        std::sort(enemies.begin(), enemies.end(), [&local](const auto& a, const auto& b) {
            return Distance(a.first, local) < Distance(b.first, local);
        });

        system("cls");
        auto now = std::chrono::system_clock::now();
        std::time_t tnow = std::chrono::system_clock::to_time_t(now);
        std::cout << "=== Enemy Coordinates (Demo) ===   Time: " << std::put_time(std::localtime(&tnow), "%F %T") << "\n";
        std::cout << "Local ID: " << local.id << "  Team: " << local.team << "  HP: " << local.health
                  << "  Pos: (" << local.x << ", " << local.y << ", " << local.z << ")\n";
        std::cout << "---------------------------------------------\n";
        std::cout << std::left << std::setw(6) << "ID" << std::setw(20) << "Name" << std::setw(6) << "HP" << std::setw(10) << "Dist"
                  << "Position\n";
        std::cout << "---------------------------------------------\n";

        for (auto& [e, name] : enemies) {
            double dist = Distance(local, e);
            SetColorForHP(hConsole, e.health);
            std::cout << std::left << std::setw(6) << e.id << std::setw(20) << name << std::setw(6) << e.health
                      << std::setw(10) << std::fixed << std::setprecision(1) << dist
                      << "(" << e.x << ", " << e.y << ", " << e.z << ")\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }

        std::cout << "---------------------------------------------\n";
        std::cout << "Enemies: " << enemies.size() << "   (Press Ctrl+C to quit)\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    CloseHandle(hProcess);
    return 0;
}
