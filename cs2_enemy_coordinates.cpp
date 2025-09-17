#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <cstddef>
#include <string>

namespace cs2_dumper {
    namespace offsets {
        // Module: client.dll
        namespace client_dll {
            constexpr std::ptrdiff_t dwEntityList = 0x1D01F58; // Указатель на список сущностей
            constexpr std::ptrdiff_t dwLocalPlayerController = 0x1E0AFD8; // Указатель на контроллер локального игрока
            constexpr std::ptrdiff_t dwViewMatrix = 0x1E1EC80; // Матрица вида (для преобразования в экранные координаты)
        }

        // Структура C_BaseEntity
        namespace C_BaseEntity {
            constexpr std::ptrdiff_t m_pGameSceneNode = 0x330; // Указатель на CGameSceneNode
            constexpr std::ptrdiff_t m_iHealth = 0x34C; // Здоровье игрока
        }

        // Структура CGameSceneNode
        namespace CGameSceneNode {
            constexpr std::ptrdiff_t m_vecOrigin = 0x88; // Координаты XYZ (Vector)
        }

        // Дополнительные смещения
        constexpr std::ptrdiff_t m_iTeamNum = 0x3EB; // Номер команды игрока
        constexpr std::ptrdiff_t m_hPlayerPawn = 0x8FC; // Указатель на пешку игрока (CCSPlayerPawn)
    }
}

struct Vector3 {
    float x, y, z;
};

HANDLE hProcess = NULL;

template <typename T>
T ReadMemory(uintptr_t address) {
    T buffer;
    ReadProcessMemory(hProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
    return buffer;
}

// Function to convert WCHAR to std::string
std::string WcharToString(const WCHAR* wcharStr) {
    if (!wcharStr) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wcharStr, -1, nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wcharStr, -1, &str[0], size, nullptr, nullptr);
    return str.substr(0, str.size() - 1); // Remove null terminator
}

uintptr_t GetModuleBaseAddress(DWORD procId, const char* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry)) {
            do {
                if (WcharToString(modEntry.szModule) == modName) {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
        CloseHandle(hSnap);
    }
    return modBaseAddr;
}

uintptr_t GetEntity(uintptr_t entityList, int index) {
    uintptr_t listEntry = ReadMemory<uintptr_t>(entityList + 0x8 * ((index & 0x7FFF) >> 9) + 0x10);
    if (!listEntry) return 0;
    return ReadMemory<uintptr_t>(listEntry + 0x78 * (index & 0x1FF));
}

int main() {
    // Find CS2 window and process ID
    HWND hwnd = FindWindowA(NULL, "Counter-Strike 2");
    if (!hwnd) {
        std::cerr << "CS2 window not found!" << std::endl;
        return 1;
    }

    DWORD procId = 0;
    GetWindowThreadProcessId(hwnd, &procId);
    if (procId == 0) {
        std::cerr << "Failed to get process ID!" << std::endl;
        return 1;
    }

    // Open process
    hProcess = OpenProcess(PROCESS_VM_READ, FALSE, procId);
    if (!hProcess) {
        std::cerr << "Failed to open CS2 process! Run as administrator." << std::endl;
        return 1;
    }

    // Get client.dll base address
    uintptr_t clientBase = GetModuleBaseAddress(procId, "client.dll");
    if (!clientBase) {
        std::cerr << "Failed to find client.dll base address!" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    // Read entity list
    uintptr_t entityList = ReadMemory<uintptr_t>(clientBase + cs2_dumper::offsets::client_dll::dwEntityList);
    if (!entityList) {
        std::cerr << "Entity list not found!" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    // Read local player controller
    uintptr_t localPlayerController = ReadMemory<uintptr_t>(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
    if (!localPlayerController) {
        std::cerr << "Local player controller not found!" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    // Get local team
    BYTE localTeam = ReadMemory<BYTE>(localPlayerController + cs2_dumper::offsets::m_iTeamNum);

    // Loop through entities (players 1 to 64)
    for (int i = 1; i < 65; ++i) {
        uintptr_t controller = GetEntity(entityList, i);
        if (!controller) continue;

        // Read team
        BYTE team = ReadMemory<BYTE>(controller + cs2_dumper::offsets::m_iTeamNum);
        if (team == localTeam || team == 0) continue; // Skip teammates and invalid

        // Read player pawn
        uintptr_t playerPawn = ReadMemory<uintptr_t>(controller + cs2_dumper::offsets::m_hPlayerPawn);
        if (!playerPawn) continue;

        // Read health
        int health = ReadMemory<int>(playerPawn + cs2_dumper::offsets::C_BaseEntity::m_iHealth);
        if (health < 1 || health > 100) continue; // Skip dead or invalid

        // Read game scene node
        uintptr_t gameSceneNode = ReadMemory<uintptr_t>(playerPawn + cs2_dumper::offsets::C_BaseEntity::m_pGameSceneNode);
        if (!gameSceneNode) continue;

        // Read XYZ coordinates
        Vector3 coords = ReadMemory<Vector3>(gameSceneNode + cs2_dumper::offsets::CGameSceneNode::m_vecOrigin);

        // Output to console
        std::cout << "Enemy " << i << " XYZ: (" << coords.x << ", " << coords.y << ", " << coords.z << ")" << std::endl;
    }

    CloseHandle(hProcess);
    return 0;
}
