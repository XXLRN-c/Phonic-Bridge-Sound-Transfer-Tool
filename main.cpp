#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <map>
#include <conio.h>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

// Networking Libraries
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h> // For SIO_UDP_CONNRESET
#include <natupnp.h>

// Windows Audio (WASAPI) Libraries
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <psapi.h>
#include <wrl.h>
#include <wrl/implements.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

using namespace Microsoft::WRL;

// COLOR THEMES (ANSI ESCAPE CODES)
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

void EnableColors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

const char* ASCII_LOGO = 
    "\033[38;2;255;20;147m" "       |                _)         |        _)      |             \n"
    "\033[38;2;200;40;180m" "  _ \\    \\    _ \\    \\   |   _|     _ \\   _| |   _` |   _` |   -_)\n"
    "\033[38;2;150;50;210m" " .__/ _| _| \\___/ _| _| _| \\__|   _.__/ _|  _| \\__,_| \\__, | \\___|\n"
    "\033[38;2;100;60;240m" "_|                                                    ____/       \n\n"
    "\033[38;2;0;120;255m"   "          >>> [ Secure Audio Streaming & Network Capture Module ] <<<\n"
    "                                                                  "
    "\033[38;2;255;0;0m""m"
    "\033[38;2;235;0;0m""a"
    "\033[38;2;215;0;0m""d"
    "\033[38;2;195;0;0m""e"
    " "
    "\033[38;2;175;0;0m""b"
    "\033[38;2;155;0;0m""y"
    " "
    "\033[38;2;135;0;0m""x"
    "\033[38;2;115;0;0m""x"
    "\033[38;2;95;0;0m""l"
    "\033[38;2;75;0;0m""r"
    "\033[38;2;55;0;0m""n\n" RESET;

// ======================================================================
// CONFIG & PROFILE MANAGER (.xxlrn)
// ======================================================================
struct AppConfig {
    std::string ip = "0.0.0.0";
    int port = 0000;
    uint32_t pin = 0000;
    int muteKey = 'M';
    DWORD pid = 0;
    std::string username = "";
};
AppConfig g_Config;

void loadConfig(const std::string& profile) {
    std::ifstream f(profile + ".xxlrn");
    if (f.is_open()) {
        std::string line;
        while(std::getline(f, line)) {
            if (line.find("IP=") == 0) g_Config.ip = line.substr(3);
            else if (line.find("PORT=") == 0) g_Config.port = std::stoi(line.substr(5));
            else if (line.find("PIN=") == 0) g_Config.pin = std::stoul(line.substr(4));
            else if (line.find("MUTEKEY=") == 0) g_Config.muteKey = std::stoi(line.substr(8));
            else if (line.find("PID=") == 0) g_Config.pid = std::stoul(line.substr(4));
            else if (line.find("USERNAME=") == 0) g_Config.username = line.substr(9);
        }
    }
}

void saveConfig(const std::string& profile) {
    if (profile.empty()) return;
    std::ofstream f(profile + ".xxlrn");
    f << "IP=" << g_Config.ip << "\n";
    f << "PORT=" << g_Config.port << "\n";
    f << "PIN=" << g_Config.pin << "\n";
    f << "MUTEKEY=" << g_Config.muteKey << "\n";
    f << "PID=" << g_Config.pid << "\n";
    f << "USERNAME=" << g_Config.username << "\n";
}

// GLOBAL VARIABLES
std::atomic<bool> g_ExitReceiver(false);
std::atomic<bool> g_ExitSender(false);
std::atomic<bool> g_ExitHost(false);
std::atomic<bool> g_SenderMuted(false);
std::atomic<bool> g_ReceiverMuted(false);
float g_Volume = 1.0f; // Default Volume 100%
std::string g_NetworkError = "";

// PACKET PROTOCOL
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;      // 0x50484F4E ('PHON')
    uint8_t type;        // 0=RAW_AUDIO, 1=KEEPALIVE_RECV, 2=KEEPALIVE_SEND, 3=16BIT_AUDIO, 4=KICK, 5=BAN, 6=NAME_REJECTED
    uint32_t roomPin;    // e.g. 9999
    char username[24];
};
#pragma pack(pop)

#define CHECK_HR(hr) \
    if (FAILED(hr)) { \
        std::cout << RED << "[!] ERROR: Line " << __LINE__ << " | Code: 0x" << std::hex << hr << RESET << std::endl; \
        system("pause"); \
        return; \
    }

// ======================================================================
// UPNP (AUTOMATIC PORT FORWARDING) MODULE
// ======================================================================
std::string getLocalIP() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) return "127.0.0.1";
    struct hostent* host = gethostbyname(hostname);
    if (host == nullptr) return "127.0.0.1";
    
    for (int i = 0; host->h_addr_list[i] != 0; ++i) {
        struct in_addr addr;
        addr.s_addr = *(u_long*)host->h_addr_list[i];
        std::string ip = inet_ntoa(addr);
        if (ip.find("192.") == 0 || ip.find("10.") == 0 || ip.find("172.") == 0) return ip;
    }
    return "127.0.0.1";
}

bool UPnP_ForwardPort(int port, const std::string& localIP) {
    IUPnPNAT *nat = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(UPnPNAT), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUPnPNAT), (void**)&nat);
    if (FAILED(hr) || !nat) return false;

    IStaticPortMappingCollection *mappings = nullptr;
    hr = nat->get_StaticPortMappingCollection(&mappings);
    if (FAILED(hr) || !mappings) { nat->Release(); return false; }

    BSTR bstrProtocol = SysAllocString(L"UDP");
    
    int len = MultiByteToWideChar(CP_UTF8, 0, localIP.c_str(), -1, NULL, 0);
    wchar_t* wstrInternalClient = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, localIP.c_str(), -1, wstrInternalClient, len);
    BSTR bstrInternalClient = SysAllocString(wstrInternalClient);
    delete[] wstrInternalClient;

    BSTR bstrDescription = SysAllocString(L"PhonicBridge_V1");
    IStaticPortMapping *mapping = nullptr;
    
    mappings->Remove(port, bstrProtocol);
    hr = mappings->Add(port, bstrProtocol, port, bstrInternalClient, VARIANT_TRUE, bstrDescription, &mapping);

    SysFreeString(bstrProtocol);
    SysFreeString(bstrInternalClient);
    SysFreeString(bstrDescription);

    bool success = SUCCEEDED(hr) && mapping != nullptr;
    if (mapping) mapping->Release();
    mappings->Release();
    nat->Release();
    return success;
}

// ======================================================================
// KEYBOARD LISTENER THREADS
// ======================================================================
void printVolumeBar() {
    int percent = (int)std::round(g_Volume * 100.0f);
    int bars = percent / 10;
    std::cout << "\r" << GREEN << "[~] SYS_VOL: [" << CYAN;
    for(int i=0; i<20; i++) {
        if (i < bars) std::cout << "#";
        else std::cout << " ";
    }
    std::cout << GREEN << "] " << YELLOW << percent << "%   " << RESET << std::flush;
}

void volumeControlThread(int muteKey) {
    bool wasPressed = false;
    std::cout << GREEN << "\n[+] Audio Control Interface Activated." << RESET << std::endl;
    std::cout << YELLOW << "[>] Hotkeys: [MUTE Bind: " << muteKey << "] Toggle Mute | [RIGHT] Vol Up | [LEFT] Vol Down | [ESC] Disconnect\n" << RESET;
    std::cout << GREEN << "[+] MUTE OFF - Audio playback active.\n" << RESET;
    printVolumeBar();
    
    while(!g_ExitReceiver) {
        if (GetAsyncKeyState(muteKey) & 0x8000) {
            if (!wasPressed) {
                g_ReceiverMuted = !g_ReceiverMuted;
                std::cout << "\r\033[A\033[K" << (g_ReceiverMuted ? "\033[31m[-] MUTE ON  - Audio playback suspended.\033[0m" : "\033[32m[+] MUTE OFF - Audio playback active.\033[0m") << "\n";
                printVolumeBar();
                wasPressed = true;
            }
        } else {
            wasPressed = false;
        }

        if (_kbhit()) {
            int ch = _getch();
            if (ch == 27) { // ESC
                g_ExitReceiver = true;
                break;
            }
            if (ch == 224) { 
                ch = _getch();
                if (ch == 75) { 
                    g_Volume = std::round((g_Volume - 0.1f) * 10.0f) / 10.0f;
                    if (g_Volume < 0.0f) g_Volume = 0.0f;
                    printVolumeBar();
                } else if (ch == 77) { 
                    g_Volume = std::round((g_Volume + 0.1f) * 10.0f) / 10.0f;
                    if (g_Volume > 2.0f) g_Volume = 2.0f;
                    printVolumeBar();
                }
            }
        }
        Sleep(50);
    }
}

void senderMuteThread(int muteKey) {
    bool wasPressed = false;
    std::cout << YELLOW << "\n[>] Hotkeys: [MUTE Bind: " << muteKey << "] Toggle Mute | [ESC] Disconnect\n" << RESET;
    std::cout << GREEN << "[+] MUTE OFF - Transmitter active." << RESET;
    while(!g_ExitSender) {
        if (GetAsyncKeyState(muteKey) & 0x8000) {
            if (!wasPressed) {
                g_SenderMuted = !g_SenderMuted;
                if (g_SenderMuted) std::cout << "\r\033[K" << RED << "[-] MUTE ON  - Transmitter suspended (KeepAlives only)." << RESET;
                else std::cout << "\r\033[K" << GREEN << "[+] MUTE OFF - Transmitter active." << RESET;
                wasPressed = true;
            }
        } else {
            wasPressed = false;
        }
        
        if (_kbhit()) {
            if (_getch() == 27) { // 27 = VK_ESCAPE
                g_ExitSender = true;
                break;
            }
        }
        Sleep(50);
    }
}

// ======================================================================
// WINDOW DISCOVERY
// ======================================================================
struct TargetProcessInfo {
    DWORD pid;
    std::string name;
    std::string title;
};

std::vector<TargetProcessInfo> g_ActiveWindows;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd)) {
        char title[256];
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strlen(title) > 0) {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            char processName[256] = "<unknown>";
            if (hProcess) {
                GetModuleBaseNameA(hProcess, NULL, processName, sizeof(processName));
                CloseHandle(hProcess);
            }
            if (strcmp(processName, "<unknown>") != 0) {
                g_ActiveWindows.push_back({pid, std::string(processName), std::string(title)});
            }
        }
    }
    return TRUE;
}

TargetProcessInfo selectWindowByClick() {
    std::cout << "\n" << MAGENTA << "[ TARGET MODE ] " << YELLOW << "Please click on the window you want to capture, or press ESC to cancel..." << RESET << "\n";
    
    // Get current console cursor position to restore it later
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hOut, &csbi);
    COORD originalCursorPos = csbi.dwCursorPosition;

    // Wait for the mouse button to be released first (in case it was held down)
    while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) { Sleep(10); }

    HWND hwnd = NULL;
    DWORD pid = 0;
    char processName[256] = "<unknown>";
    
    while (true) {
        // Check for ESC key press to cancel
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            // Restore cursor position and clear line
            SetConsoleCursorPosition(hOut, originalCursorPos);
            std::cout << "\033[K" << YELLOW << "[ TARGET MODE ] Selection cancelled." << RESET << "\n";
            return {0, "", ""}; // Return empty/invalid info
        }

        // Wait for a new click
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            hwnd = GetForegroundWindow();
            GetWindowThreadProcessId(hwnd, &pid);

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (hProcess) {
                GetModuleBaseNameA(hProcess, NULL, processName, sizeof(processName));
                CloseHandle(hProcess);
            }
            
            // Wait for release again so we don't spam
            while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) { Sleep(10); }
            break; // Click detected and processed, exit loop
        }
        Sleep(10);
    }

    // Restore cursor position and clear line
    SetConsoleCursorPosition(hOut, originalCursorPos);
    std::cout << "\033[K" << GREEN << "[ TARGET MODE ] Window selected: " << YELLOW << processName << RESET << "\n";

    return {pid, std::string(processName), ""};
}

class AudioInterfaceCompletionHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler>
{
public:
    HANDLE m_hEvent;
    ComPtr<IAudioClient> m_AudioClient;

    AudioInterfaceCompletionHandler() { m_hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); }
    ~AudioInterfaceCompletionHandler() { if (m_hEvent) CloseHandle(m_hEvent); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *operation) override {
        HRESULT hr;
        ComPtr<IUnknown> punkAudioInterface;
        operation->GetActivateResult(&hr, &punkAudioInterface);
        if (SUCCEEDED(hr) && punkAudioInterface) { punkAudioInterface.As(&m_AudioClient); }
        SetEvent(m_hEvent);
        return S_OK;
    }
};

// ======================================================================
// [3] HOST MODULE (UDP Relay & Live Tracker)
// ======================================================================
struct ClientInfo {
    sockaddr_in addr;
    DWORD lastSeen;
    int type; // 1=Receiver, 2=Sender
    std::string username;
    std::string ipStr;
    int port;
    bool muted = false;
};

void startHost(int listenPort) {
    g_ExitHost = false;
    SOCKET hostSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    sockaddr_in hostAddr;
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(listenPort);
    hostAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(hostSocket, (SOCKADDR*)&hostAddr, sizeof(hostAddr)) == SOCKET_ERROR) {
        std::cout << RED << "[!] Failed to bind Host port. Is it in use?" << RESET << std::endl;
        return;
    }

    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(hostSocket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);

    std::map<uint32_t, std::vector<ClientInfo>> activeRooms;
    struct BannedUser { std::string ip; std::string name; };
    std::vector<BannedUser> bannedList;
    char buffer[65535];

    std::vector<std::string> hostLogs;
    std::string tempError = "";

    auto addLog = [&](const std::string& msg) {
        time_t t = time(0);
        struct tm* now = localtime(&t);
        char buf[80]; strftime(buf, sizeof(buf), "[\033[90m%H:%M:%S\033[0m] ", now);
        hostLogs.insert(hostLogs.begin(), std::string(buf) + msg);
    };

    system("cls");
    addLog("HOST SERVER ACTIVE ON LOCAL PORT " + std::to_string(listenPort));
    addLog("Type 'help' for admin commands.");

    fd_set readfds;
    timeval tv;

    DWORD lastStatusPrint = GetTickCount();
    int lastSenders = -1, lastReceivers = -1;
    
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode;
    GetConsoleMode(hIn, &oldMode);
    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);
    std::string adminCmd = "";
    
    bool selectingUser = false;
    int selectedIndex = -1;
    int selectedAction = 0; // 0=Kick, 1=Ban, 2=Mute, 3=Unmute
    std::vector<ClientInfo*> flatClients;
    bool isBannedList = false;
    bool flashGreen = false;

    auto redrawHostList = [&]() {
        flatClients.clear();
        DWORD now = GetTickCount();
        for (auto& rp : activeRooms) {
            for (auto& c : rp.second) { if (now - c.lastSeen < 10000) flatClients.push_back(&c); }
        }

        std::cout << "\033[H\033[0J"; // Home and clear screen
        
        std::cout << CYAN << "=== [ LIVE CONNECTIONS ] ===\n" << RESET;
        std::cout << "ID   " << std::left << std::setw(26) << "Username" 
                  << std::setw(25) << "IP:Port" 
                  << std::setw(12) << "Type" 
                  << "Status\n" << std::string(80, '-') << "\n";
        
        if (isBannedList) {
            for (size_t i = 0; i < bannedList.size(); ++i) {
                if (selectingUser && selectedIndex == i + 1) {
                    if (selectedAction > -1) std::cout << (flashGreen ? "\033[42;30m" : "\033[44;97m"); // Flash or Blue
                }
                std::cout << std::left << std::setw(5) << (i + 1)
                          << std::setw(26) << bannedList[i].name
                          << std::setw(25) << bannedList[i].ip
                          << std::setw(12) << "BANNED" << "RESTRICTED\033[K" << RESET << "\n";
            }
            if (bannedList.empty()) std::cout << "No banned users.\n";
        } else {
            for (size_t i = 0; i < flatClients.size(); ++i) {
                if (selectingUser && selectedIndex == i + 1) {
                    if (selectedAction > -1) std::cout << (flashGreen ? "\033[42;30m" : "\033[44;97m"); // Flash or Blue
                }
                std::cout << std::left << std::setw(5) << (i + 1)
                          << std::setw(26) << flatClients[i]->username
                          << std::setw(25) << (flatClients[i]->ipStr + ":" + std::to_string(flatClients[i]->port))
                          << std::setw(12) << (flatClients[i]->type==1 ? "RECEIVER" : "SENDER")
                          << (flatClients[i]->muted ? "\033[31mMUTED\033[0m" : "\033[32mACTIVE\033[0m") 
                          << "\033[K" << RESET << "\n";
            }
            if (flatClients.empty()) std::cout << "No active connections right now.\n";
        }
        
        if (selectingUser) {
            std::cout << "\nAction: ";
            if (isBannedList) {
                if (selectedAction == 0) std::cout << "\033[42;30m[UNBAN]\033[0m "; else std::cout << "[UNBAN] ";
            } else {
                if (selectedAction == 0) std::cout << "\033[42;30m[KICK]\033[0m "; else std::cout << "[KICK] ";
                if (selectedAction == 1) std::cout << "\033[42;30m[BAN]\033[0m "; else std::cout << "[BAN] ";
                if (selectedAction == 2) std::cout << "\033[42;30m[MUTE]\033[0m "; else std::cout << "[MUTE] ";
                if (selectedAction == 3) std::cout << "\033[42;30m[UNMUTE]\033[0m "; else std::cout << "[UNMUTE] ";
            }
            std::cout << "\n";
        }
        
        std::cout << CYAN << "============================\n" << RESET;
        
        if (!tempError.empty()) std::cout << RED << tempError << "\033[K" << RESET << "\n";
        else std::cout << "\033[K\n";
        
        std::cout << "\rHost> " << adminCmd << "\033[K";
        
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(hOut, &csbi);
        COORD promptPos = csbi.dwCursorPosition;
        
        std::cout << "\n\n" << CYAN << "--- EVENT LOGS ---\n" << RESET;
        int linesLeft = 15;
        for (const auto& log : hostLogs) {
            std::cout << log << "\033[K\n";
            if (--linesLeft <= 0) break;
        }
        
        SetConsoleCursorPosition(hOut, promptPos);
    };

    redrawHostList();

    while (!g_ExitHost) {
        // NON-BLOCKING INPUT
        DWORD numEvents = 0;
        GetNumberOfConsoleInputEvents(hIn, &numEvents);
        if (numEvents > 0) {
            INPUT_RECORD ir[32];
            DWORD read;
            ReadConsoleInput(hIn, ir, 32, &read);
            for (DWORD i = 0; i < read; ++i) {
                if (ir[i].EventType == KEY_EVENT && ir[i].Event.KeyEvent.bKeyDown) {
                    WORD vKey = ir[i].Event.KeyEvent.wVirtualKeyCode;
                    char ch = ir[i].Event.KeyEvent.uChar.AsciiChar;
                    
                    if (vKey == VK_RETURN) {
                        if (selectingUser) {
                            std::string tgt = "";
                            if (selectedIndex > 0) {
                                if (isBannedList && selectedIndex <= bannedList.size()) tgt = bannedList[selectedIndex - 1].ip;
                                else if (!isBannedList && selectedIndex <= flatClients.size()) tgt = (flatClients[selectedIndex - 1]->username != "Guest" ? flatClients[selectedIndex - 1]->username : flatClients[selectedIndex - 1]->ipStr);
                            }
                            
                            if (!tgt.empty()) {
                                flashGreen = true;
                                redrawHostList();
                                Sleep(300);
                                flashGreen = false;
                                selectingUser = false;
                                
                                if (isBannedList) adminCmd = "unban " + tgt;
                                else {
                                    if (selectedAction == 0) adminCmd = "kick " + tgt;
                                    else if (selectedAction == 1) adminCmd = "ban " + tgt;
                                    else if (selectedAction == 2) adminCmd = "mute " + tgt;
                                    else if (selectedAction == 3) adminCmd = "unmute " + tgt;
                                }
                            } else {
                                selectingUser = false;
                                continue;
                            }
                        }
                        
                        std::string cmdStr = adminCmd; adminCmd = "";
                        
                        std::vector<std::string> args;
                        std::istringstream iss(cmdStr);
                        std::string tk; while (iss >> tk) args.push_back(tk);
                        
                        if (!args.empty()) {
                            std::string cmd = args[0]; std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
                            std::string targetPrm = args.size() > 1 ? args[1] : "";
                            
                            // Swap if target is args[0] and action is args[1]
                            if (args.size() == 2) {
                                std::string a1 = args[1]; std::transform(a1.begin(), a1.end(), a1.begin(), ::tolower);
                                if (a1 == "kick" || a1 == "ban" || a1 == "mute" || a1 == "unmute") {
                                    cmd = a1; targetPrm = args[0];
                                }
                            }
                            
                            if (args.size() > 2 && (cmd == "kick" || cmd == "ban" || cmd == "mute" || cmd == "unmute")) {
                                tempError = "Invalid syntax. Ex: 'kick username'. Too many words.";
                            } else if (cmd == "help" || cmd == "h" || cmd == "?") {
                                system("powershell -WindowStyle Normal -Command \"$host.UI.RawUI.WindowTitle='Help Menu'; Write-Host '=== HELP MENU ===' -ForegroundColor Cyan; Write-Host 'kick <name>   - Disconnect user' -ForegroundColor Yellow; Write-Host 'ban <name>    - Ban user IP' -ForegroundColor Yellow; Write-Host 'unban <ip>    - Remove IP from banlist' -ForegroundColor Yellow; Write-Host 'mute <name>   - Stop routing user audio' -ForegroundColor Yellow; Write-Host 'unmute <name> - Restore audio routing' -ForegroundColor Yellow; Write-Host 'glog <file>   - Export logs to file' -ForegroundColor Yellow; Write-Host 'clear         - Clear console logs' -ForegroundColor Yellow; Write-Host 'exit          - Stop server safely' -ForegroundColor Yellow; Write-Host '-------------------------------------' -ForegroundColor Cyan; Write-Host 'Press ESC to close this window.' -ForegroundColor Gray; while ($true) { if ([System.Console]::KeyAvailable) { $key = [System.Console]::ReadKey($true); if ($key.Key -eq 'Escape') { break; } } Start-Sleep -Milliseconds 50 }\"");
                                addLog("Help window opened.");
                            } else if (cmd == "exit" || cmd == "quit" || cmd == "q") {
                                addLog("Shutting down Host...");
                                SetConsoleTitleA("PhonicBridge");
                                g_ExitHost = true; break;
                            } else if (cmd == "glog") {
                                std::string lgName = (args.size() > 1) ? args[1] : "";
                                if (lgName.empty()) {
                                    time_t t = time(0); struct tm* now = localtime(&t);
                                    char buf[80]; strftime(buf, sizeof(buf), "HostLog_%Y%m%d_%H%M%S.txt", now);
                                    lgName = buf;
                                }
                                if (lgName.find(".txt") == std::string::npos) lgName += ".txt";
                                
                                std::string baseName = lgName.substr(0, lgName.find_last_of('.'));
                                std::string ext = ".txt";
                                int counter = 1;
                                std::string finalName = lgName;
                                while (std::ifstream(finalName).good()) {
                                    finalName = baseName + "(" + std::to_string(counter++) + ")" + ext;
                                }
                                
                                std::ofstream lg(finalName, std::ios::app);
                                if (lg.is_open()) {
                                    for (auto it = hostLogs.rbegin(); it != hostLogs.rend(); ++it) {
                                        std::string line = *it;
                                        size_t pos;
                                        while ((pos = line.find("\033[")) != std::string::npos) {
                                            size_t end = line.find('m', pos);
                                            if (end != std::string::npos) line.erase(pos, end - pos + 1);
                                            else break;
                                        }
                                        lg << line << "\n";
                                    }
                                    lg.close();
                                    addLog("[+] Exported logs to " + finalName + " successfully.");
                                } else {
                                    tempError = "Failed to create " + finalName;
                                }
                            } else if (cmd == "clear" || cmd == "c" || cmd == "cl") {
                                hostLogs.clear();
                                addLog("Logs cleared.");
                            } else if (cmd == "list" || cmd == "l") {
                                addLog("Live connections are already visible above.");
                            } else if (!targetPrm.empty()) {
                                if (cmd == "kick" || cmd == "ban" || cmd == "mute" || cmd == "unmute") {
                                    bool found = false;
                                    for (auto& roomPair : activeRooms) {
                                        for (auto it = roomPair.second.begin(); it != roomPair.second.end(); ) {
                                            if (it->username == targetPrm || it->ipStr == targetPrm) {
                                                found = true;
                                                if (cmd == "kick") {
                                                    PacketHeader kickHdr = {0x50484F4E, 4, roomPair.first};
                                                    sendto(hostSocket, (char*)&kickHdr, sizeof(kickHdr), 0, (SOCKADDR*)&it->addr, sizeof(sockaddr_in));
                                                    addLog("[-] Kicked " + it->username + " (" + it->ipStr + ")");
                                                    it = roomPair.second.erase(it); continue;
                                                } else if (cmd == "ban") {
                                                    PacketHeader banHdr = {0x50484F4E, 5, roomPair.first};
                                                    sendto(hostSocket, (char*)&banHdr, sizeof(banHdr), 0, (SOCKADDR*)&it->addr, sizeof(sockaddr_in));
                                                    addLog("[!] Banned: " + it->ipStr + " (" + it->username + ")");
                                                    bannedList.push_back({it->ipStr, it->username});
                                                    it = roomPair.second.erase(it); continue;
                                                } else if (cmd == "mute") {
                                                    it->muted = true;
                                                    addLog("[-] Muted audio from " + it->username);
                                                } else if (cmd == "unmute") {
                                                    it->muted = false;
                                                    addLog("[+] Unmuted audio from " + it->username);
                                                }
                                            }
                                            ++it;
                                        }
                                    }
                                    if (!found) tempError = "User '" + targetPrm + "' not found.";
                                } else if (cmd == "unban") {
                                    auto it = std::find_if(bannedList.begin(), bannedList.end(), [&](const BannedUser& u) { return u.ip == targetPrm || u.name == targetPrm; });
                                    if (it != bannedList.end()) {
                                        addLog("[+] " + it->name + " (" + it->ip + ") unbanned.");
                                        bannedList.erase(it);
                                    } else {
                                        tempError = "IP/Name not in banlist.";
                                    }
                                }
                            } else {
                                tempError = "Missing target. E.g. '" + cmd + " username'";
                            }
                        }
                        redrawHostList(); 
                        
                    } else if (vKey == VK_ESCAPE) {
                        if (selectingUser) {
                            selectingUser = false;
                            redrawHostList();
                        } else if (!adminCmd.empty()) {
                            adminCmd = "";
                            tempError = "";
                            redrawHostList();
                        }
                    } else if (vKey == VK_TAB) {
                        if (!selectingUser) {
                            selectingUser = true; selectedIndex = 1;
                            std::string cmdTest = adminCmd; std::transform(cmdTest.begin(), cmdTest.end(), cmdTest.begin(), ::tolower);
                            isBannedList = (cmdTest.find("unban") != std::string::npos);
                        } else {
                            selectingUser = false;
                        }
                        redrawHostList();
                    } else if (vKey == VK_UP && selectingUser) {
                        int maxItems = isBannedList ? bannedList.size() : flatClients.size();
                        if (selectedIndex <= 1) selectedIndex = maxItems; else selectedIndex--;
                        redrawHostList();
                    } else if (vKey == VK_DOWN && selectingUser) {
                        int maxItems = isBannedList ? bannedList.size() : flatClients.size();
                        if (selectedIndex >= maxItems) selectedIndex = 1; else selectedIndex++;
                        redrawHostList();
                    } else if (vKey == VK_LEFT && selectingUser) {
                        if (!isBannedList && selectedAction > 0) selectedAction--;
                        redrawHostList();
                    } else if (vKey == VK_RIGHT && selectingUser) {
                        if (!isBannedList && selectedAction < 3) selectedAction++;
                        redrawHostList();
                    } else if (vKey == VK_BACK) {
                        if (selectingUser) { selectingUser = false; }
                        else if (!adminCmd.empty()) { adminCmd.pop_back(); tempError = ""; }
                        redrawHostList();
                    } else if (ch >= 32 && ch <= 126) {
                        if (selectingUser) { selectingUser = false; }
                        adminCmd += ch; tempError = "";
                        redrawHostList();
                    }
                }
            }
        }
        if (g_ExitHost) break;

        FD_ZERO(&readfds);
        FD_SET(hostSocket, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 10000; 
        
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            int bytes = recvfrom(hostSocket, buffer, sizeof(buffer), 0, (SOCKADDR*)&clientAddr, &clientLen);

            if (bytes >= sizeof(PacketHeader)) {
                std::string c_ip = inet_ntoa(clientAddr.sin_addr);
                auto it = std::find_if(bannedList.begin(), bannedList.end(), [&](const BannedUser& u) { return u.ip == c_ip; });
                if (it == bannedList.end()) {
                    PacketHeader* hdr = (PacketHeader*)buffer;
                    if (hdr->magic == 0x50484F4E) {
                        DWORD now = GetTickCount();
                        auto& roomClients = activeRooms[hdr->roomPin];
                        std::string c_user(hdr->username);
                        if (c_user.empty()) c_user = "Unknown";
                        
                        bool found = false;
                        bool isMuted = false;
                        for (auto& c : roomClients) {
                            if (c.addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr && c.addr.sin_port == clientAddr.sin_port) {
                                c.lastSeen = now;
                                c.username = c_user; 
                                isMuted = c.muted;
                                found = true;
                                if (hdr->type == 1) c.type = 1;
                                else if (hdr->type == 2) c.type = 2;
                                break;
                            }
                        }

                        if (!found && c_user != "Guest" && c_user != "Unknown") {
                            bool nameTaken = false;
                            for (auto& c : roomClients) {
                                if (c.username == c_user) { nameTaken = true; break; }
                            }
                            if (nameTaken) {
                                PacketHeader rejHdr = {0}; rejHdr.magic = 0x50484F4E; rejHdr.type = 6; rejHdr.roomPin = hdr->roomPin;
                                sendto(hostSocket, (char*)&rejHdr, sizeof(rejHdr), 0, (SOCKADDR*)&clientAddr, clientLen);
                                continue;
                            }
                        }

                        if (!found) {
                            ClientInfo ni; ni.addr = clientAddr; ni.lastSeen = now; ni.type = (hdr->type == 1) ? 1 : 2;
                            ni.username = c_user; ni.ipStr = c_ip; ni.port = ntohs(clientAddr.sin_port); ni.muted = false;
                            roomClients.push_back(ni);
                            std::string tName = (ni.type==1 ? "RECEIVER" : "SENDER");
                            addLog("[+] " + tName + " (" + c_user + " @ " + c_ip + ") joined! [Room: " + std::to_string(hdr->roomPin) + "]");
                            lastStatusPrint = 0; 
                        }

                        if (!isMuted && (hdr->type == 0 || hdr->type == 3)) { 
                            for (auto& c : roomClients) {
                                if (c.type == 1 && !c.muted) { 
                                    sendto(hostSocket, buffer, bytes, 0, (SOCKADDR*)&(c.addr), sizeof(sockaddr_in));
                                }
                            }
                        }
                    }
                }
            }
        }
        
        DWORD now = GetTickCount();
        if (now - lastStatusPrint > 2000 || lastStatusPrint == 0) {
            int senders = 0, receivers = 0;
            for (auto& roomPair : activeRooms) {
                for (auto it = roomPair.second.begin(); it != roomPair.second.end(); ) {
                   if (now - it->lastSeen > 3000) { 
                       std::string tName = (it->type==1 ? "RECEIVER" : "SENDER");
                       addLog("[-] " + tName + " (" + it->username + " @ " + it->ipStr + ") left! [Room: " + std::to_string(roomPair.first) + "]");
                       it = roomPair.second.erase(it);
                       lastStatusPrint = 0; 
                   } else {
                       if (it->type == 1) receivers++;
                       else if (it->type == 2) senders++;
                       ++it;
                   }
                }
            }
            if (senders != lastSenders || receivers != lastReceivers || lastStatusPrint == 0) {
                std::string hud = "PhonicBridge Host | " + std::to_string(senders) + " Sender(s) | " + std::to_string(receivers) + " Receiver(s) Active";
                SetConsoleTitleA(hud.c_str());
                lastSenders = senders;
                lastReceivers = receivers;
                redrawHostList();
            }
            lastStatusPrint = GetTickCount();
        }
    }
    SetConsoleMode(hIn, oldMode);
    closesocket(hostSocket);
}



// ======================================================================
// [1] SENDER MODULE
// ======================================================================
void startSender(const std::string& hostIP, int hostPort, uint32_t roomPin, DWORD targetPID, int muteKey, const std::string& username) {
    g_ExitSender = false;
    g_SenderMuted = false;
    std::cout << YELLOW << "\n[*] Initializing Network Transmission..." << RESET << std::endl;

    SOCKET sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in localAddr; localAddr.sin_family = AF_INET; localAddr.sin_port = htons(0); localAddr.sin_addr.s_addr = INADDR_ANY;
    bind(sendSocket, (SOCKADDR*)&localAddr, sizeof(localAddr));

    sockaddr_in hostAddr;
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(hostPort);
    hostAddr.sin_addr.s_addr = inet_addr(hostIP.c_str());

    CoInitialize(nullptr);
    IAudioClient *pAudioClient = nullptr;
    WAVEFORMATEX *pwfx = nullptr;

    if (targetPID == 0) {
        IMMDeviceEnumerator* pEnum = nullptr;
        CHECK_HR(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum));
        IMMDevice* pDev = nullptr;
        CHECK_HR(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev));
        CHECK_HR(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient));
        CHECK_HR(pAudioClient->GetMixFormat(&pwfx));
        pDev->Release(); pEnum->Release();
    } else {
        IMMDeviceEnumerator* pEnum = nullptr;
        CHECK_HR(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum));
        IMMDevice* pDev = nullptr;
        CHECK_HR(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDev));
        IAudioClient* pDefClient = nullptr;
        CHECK_HR(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pDefClient));
        CHECK_HR(pDefClient->GetMixFormat(&pwfx));
        pDefClient->Release(); pDev->Release(); pEnum->Release();

        AUDIOCLIENT_ACTIVATION_PARAMS activateParams = {};
        activateParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activateParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        activateParams.ProcessLoopbackParams.TargetProcessId = targetPID;

        PROPVARIANT propVar = {};  propVar.vt = VT_BLOB; propVar.blob.cbSize = sizeof(activateParams); propVar.blob.pBlobData = (BYTE*)&activateParams;
        ComPtr<AudioInterfaceCompletionHandler> completionHandler = Make<AudioInterfaceCompletionHandler>();
        ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;

        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &propVar, completionHandler.Get(), &asyncOp);
        CHECK_HR(hr);
        WaitForSingleObject(completionHandler->m_hEvent, INFINITE);

        pAudioClient = completionHandler->m_AudioClient.Get();
        if (!pAudioClient) {
            std::cout << RED << "[!] Connection failed. Process silent or PID invalid.\n" << RESET;
            return;
        }
        pAudioClient->AddRef();
    }

    // Force universal 48000Hz 32-bit Stereo to prevent frequency pitch shifts
    WAVEFORMATEX wfxUniversal = {};
    wfxUniversal.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfxUniversal.nChannels = 2;
    wfxUniversal.nSamplesPerSec = 48000;
    wfxUniversal.wBitsPerSample = 32;
    wfxUniversal.nBlockAlign = 8;
    wfxUniversal.nAvgBytesPerSec = 384000;
    wfxUniversal.cbSize = 0;

    REFERENCE_TIME hnsReq = 10000000;
    HRESULT hrInit = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | 0x80000000 | 0x08000000, hnsReq, 0, &wfxUniversal, NULL);
    if (SUCCEEDED(hrInit)) {
        pwfx = &wfxUniversal; // Windows auto-conversion success
    } else {
        CHECK_HR(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsReq, 0, pwfx, NULL));
    }

    IAudioCaptureClient *pCaptureClient = nullptr;
    CHECK_HR(pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient));

    CHECK_HR(pAudioClient->Start());
    std::cout << GREEN << "\n[!] TRANSMISSION ACTIVE. Audio streaming to ROOM " << roomPin << RESET << std::endl;
    
    std::thread muteUi(senderMuteThread, muteKey);
    muteUi.detach();

    DWORD lastKeepAlive = 0;
    char recvBuffer[256];

    while (!g_ExitSender) {
        Sleep(10);
        DWORD now = GetTickCount();

        // TRUE HEARTBEAT logic (Independent from capture state)
        if (now - lastKeepAlive >= 1000) {
            PacketHeader hdr = {0};
            hdr.magic = 0x50484F4E; hdr.type = 2; hdr.roomPin = roomPin; // KEEPALIVE_SENDER
            strncpy_s(hdr.username, 24, username.c_str(), 23);
            sendto(sendSocket, (char*)&hdr, sizeof(hdr), 0, (SOCKADDR*)&hostAddr, sizeof(hostAddr));
            lastKeepAlive = now;
        }

        // BACKCHANNEL LISTENER (Check for Kicks/Bans)
        fd_set readfds; FD_ZERO(&readfds); FD_SET(sendSocket, &readfds);
        timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            int bytesReceived = recvfrom(sendSocket, recvBuffer, sizeof(recvBuffer), 0, NULL, NULL);
            if (bytesReceived >= sizeof(PacketHeader)) {
                PacketHeader* hdr = (PacketHeader*)recvBuffer;
                if (hdr->magic == 0x50484F4E && hdr->roomPin == roomPin) {
                    if (hdr->type == 4) { g_NetworkError = "You were kicked by the Host."; break; }
                    else if (hdr->type == 5) { g_NetworkError = "You are BANNED from this Host."; break; }
                    else if (hdr->type == 6) { g_NetworkError = "Username is already taken or invalid."; break; }
                }
            }
        }

        UINT32 packetLength = 0;
        pCaptureClient->GetNextPacketSize(&packetLength);

        while (packetLength != 0) {
            BYTE *pData;
            UINT32 numFramesAvailable;
            DWORD flags;
            CHECK_HR(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL));

            if (!g_SenderMuted) {
                int bytesToSend = numFramesAvailable * pwfx->nBlockAlign;
                std::vector<char> sendBuf(sizeof(PacketHeader) + bytesToSend);
                PacketHeader* hdr = (PacketHeader*)sendBuf.data();
                memset(hdr, 0, sizeof(PacketHeader));
                hdr->magic = 0x50484F4E; hdr->type = 0; hdr->roomPin = roomPin;
                strncpy_s(hdr->username, 24, username.c_str(), 23);
                memcpy(sendBuf.data() + sizeof(PacketHeader), pData, bytesToSend);
                sendto(sendSocket, sendBuf.data(), sendBuf.size(), 0, (SOCKADDR*)&hostAddr, sizeof(hostAddr));
            }

            CHECK_HR(pCaptureClient->ReleaseBuffer(numFramesAvailable));
            pCaptureClient->GetNextPacketSize(&packetLength);
        }
    }

    pAudioClient->Stop();
    pAudioClient->Release();
    CoUninitialize();
    closesocket(sendSocket);
}

// ======================================================================
// [2] RECEIVER MODULE (Supports Reconnect & 16-bit uncompression)
// ======================================================================
void startReceiver(const std::string& hostIP, int hostPort, uint32_t roomPin, const std::string& username, int muteKey) {
    g_ExitReceiver = false;
    g_ReceiverMuted = false;
    std::cout << YELLOW << "\n[*] Joining HOST: " << hostIP << ":" << hostPort << " [ROOM " << roomPin << "]" << RESET << std::endl;
    
    std::thread ui(volumeControlThread, muteKey);
    ui.detach();

    SOCKET recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in localAddr; localAddr.sin_family = AF_INET; localAddr.sin_port = htons(0); localAddr.sin_addr.s_addr = INADDR_ANY;
    bind(recvSocket, (SOCKADDR*)&localAddr, sizeof(localAddr));

    sockaddr_in hostAddr; hostAddr.sin_family = AF_INET; hostAddr.sin_port = htons(hostPort); hostAddr.sin_addr.s_addr = inet_addr(hostIP.c_str());

    CoInitialize(nullptr);
    IMMDeviceEnumerator *pEnumerator = nullptr;
    CHECK_HR(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator));
    IMMDevice *pDevice = nullptr;
    CHECK_HR(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice));
    IAudioClient *pAudioClient = nullptr;
    CHECK_HR(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient));
    WAVEFORMATEX *pwfx = nullptr;
    CHECK_HR(pAudioClient->GetMixFormat(&pwfx));

    // Force universal 48000Hz 32-bit Stereo for gapless audio sync
    WAVEFORMATEX wfxUniversal = {};
    wfxUniversal.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfxUniversal.nChannels = 2;
    wfxUniversal.nSamplesPerSec = 48000;
    wfxUniversal.wBitsPerSample = 32;
    wfxUniversal.nBlockAlign = 8;
    wfxUniversal.nAvgBytesPerSec = 384000;
    wfxUniversal.cbSize = 0;

    REFERENCE_TIME hnsReq = 10000000;
    HRESULT hrInit = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0x80000000 | 0x08000000, hnsReq, 0, &wfxUniversal, NULL);
    if (SUCCEEDED(hrInit)) {
        pwfx = &wfxUniversal;
    } else {
        CHECK_HR(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsReq, 0, pwfx, NULL));
    }
    
    IAudioRenderClient *pRenderClient = nullptr;
    CHECK_HR(pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient));
    CHECK_HR(pAudioClient->Start());

    char buffer[65535];
    UINT32 bufferFrameCount;
    pAudioClient->GetBufferSize(&bufferFrameCount);

    fd_set readfds;
    timeval tv;
    DWORD lastKeepAlive = 0;
    DWORD lastDataReceived = GetTickCount();
    bool isDisconnected = false;

    while (!g_ExitReceiver) {
        DWORD now = GetTickCount();
        if (now - lastKeepAlive >= 1000) {
            PacketHeader hdr = {0};
            hdr.magic = 0x50484F4E; hdr.type = 1; hdr.roomPin = roomPin; // KEEPALIVE_RECV
            strncpy_s(hdr.username, 24, username.c_str(), 23);
            sendto(recvSocket, (char*)&hdr, sizeof(hdr), 0, (SOCKADDR*)&hostAddr, sizeof(hostAddr));
            lastKeepAlive = now;
        }

        FD_ZERO(&readfds); FD_SET(recvSocket, &readfds);
        tv.tv_sec = 0; tv.tv_usec = 50000;

        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            int bytesReceived = recvfrom(recvSocket, buffer, sizeof(buffer), 0, NULL, NULL);
            if (bytesReceived >= sizeof(PacketHeader)) {
                PacketHeader* hdr = (PacketHeader*)buffer;
                if (hdr->magic == 0x50484F4E && hdr->roomPin == roomPin) {
                    if (hdr->type == 4) { g_NetworkError = "You were kicked by the Host."; break; }
                    else if (hdr->type == 5) { g_NetworkError = "You are BANNED from this Host."; break; }
                    else if (hdr->type == 6) { g_NetworkError = "Username is already taken or invalid."; break; }

                    lastDataReceived = GetTickCount(); // Valid packet resets timeout

                    if (isDisconnected) {
                        std::cout << "\n\033[K" << GREEN << "[+] Reconnected!" << RESET << " (Music continues)\033[A";
                        isDisconnected = false;
                    }

                    if (hdr->type == 0) {
                        int audioBytes = bytesReceived - sizeof(PacketHeader);
                        char* audioData = buffer + sizeof(PacketHeader);
                        
                        UINT32 numFrames = audioBytes / pwfx->nBlockAlign;

                        UINT32 padding; pAudioClient->GetCurrentPadding(&padding);
                        if (bufferFrameCount - padding >= numFrames) {
                            BYTE *pData;
                            CHECK_HR(pRenderClient->GetBuffer(numFrames, &pData));
                            
                            if (pwfx->wBitsPerSample == 32) {
                                float* dst = (float*)pData;
                                float* src = (float*)audioData;
                                int count = audioBytes / 4;
                                for(int i=0; i<count; i++) {
                                    float val = g_ReceiverMuted ? 0.0f : (src[i] * g_Volume);
                                    if (val > 1.0f) val = 1.0f; else if (val < -1.0f) val = -1.0f;
                                    dst[i] = val;
                                }
                            } else {
                                if (g_ReceiverMuted) memset(pData, 0, audioBytes);
                                else memcpy(pData, audioData, audioBytes); // Basic fallback
                            }
                            CHECK_HR(pRenderClient->ReleaseBuffer(numFrames, 0));
                        }
                    }
                }
            }
        } else {
            // TIMEOUT LOGIC
            if (GetTickCount() - lastDataReceived > 3000 && !isDisconnected) {
                isDisconnected = true;
                std::cout << "\n\033[K" << RED << "[-] Connection Lost... Waiting for Host..." << RESET << "\033[A";
            }
        }
    }

    pAudioClient->Stop(); pAudioClient->Release(); CoUninitialize(); closesocket(recvSocket);
}

// ======================================================================
// MAIN ENTRY
// ======================================================================
auto safeStoi = [](const std::string& s, int def) -> int {
    try { return std::stoi(s); } catch(...) { return def; }
};
auto safeStoul = [](const std::string& s, uint32_t def) -> uint32_t {
    try { return std::stoul(s); } catch(...) { return def; }
};

void animateTitleThread() {
    std::string text = "Phonic Bridge By XXLRN";
    std::string current = "";
    for (char c : text) {
        current += c;
        SetConsoleTitleA(current.c_str());
        Sleep(80);
    }
}

int main() {
    EnableColors();
    SetConsoleOutputCP(CP_UTF8);
    
    std::thread titleAnim(animateTitleThread);
    titleAnim.detach();

    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);

    while (true) {
        system("cls");
        if (!g_NetworkError.empty()) {
            std::cout << RED << " [!] SYSTEM MESSAGE: " << g_NetworkError << RESET << "\n\n";
            g_NetworkError = "";
        }
        
        auto getPrompt = [&]() {
            std::string promptName = "phonic";
            if (!g_Config.username.empty() && g_Config.username != "Guest" && g_Config.username != "Unknown") {
                promptName = g_Config.username.substr(0, 6);
            }
            return "\033[35mroot:" + promptName + ":~# \033[0m";
        };

        std::cout << ASCII_LOGO << std::flush;
        std::cout << GREEN << "  1. [ SENDER ] Transmit Audio to a Host Room" << RESET << std::endl;
        std::cout << GREEN << "  2. [RECEIVER] Join a Host Room & Listen" << RESET << std::endl;
        std::cout << CYAN  << "  3. [  HOST  ] Create a Central Relay Server" << RESET << std::endl;
        std::cout << YELLOW << "  4. [  EXIT  ] Terminate Application" << RESET << std::endl;
        
        std::cout << "\n" << getPrompt() << "Select mode: ";
        
        std::string inputStr;
        
        auto safeGetLineGlobalEsc = [&](std::string& outStr) -> bool {
            outStr = "";
            while (true) {
                if (_kbhit()) {
                    int c = _getch();
                    if (c == 27) { // ESCAPE
                        return false; 
                    } else if (c == '\r' || c == '\n') {
                        std::cout << "\n";
                        break;
                    } else if (c == '\b') {
                        if (!outStr.empty()) {
                            outStr.pop_back();
                            std::cout << "\b \b";
                        }
                    } else if (c >= 32 && c <= 126) {
                        outStr += (char)c;
                        std::cout << (char)c;
                    }
                }
                Sleep(10);
            }
            return true;
        };

        if (!safeGetLineGlobalEsc(inputStr)) { std::cout << "\n" << YELLOW << "[-] Returning to Main Menu...\n" << RESET; continue; }
        
        if (inputStr.empty()) continue;
        int choice = safeStoi(inputStr, 0);

        if (choice >= 1 && choice <= 3) {
            std::cout << getPrompt() << "Enter Profile Name (Only english characters) or leave empty: ";
            std::string profileName;
            if (!safeGetLineGlobalEsc(profileName)) { std::cout << "\n" << YELLOW << "[-] Returning to Main Menu...\n" << RESET; continue; }
            if (!profileName.empty()) {
                loadConfig(profileName);
                std::cout << GREEN << "[+] Profile Loaded! IP: " << g_Config.ip << " Port: " << g_Config.port << " PIN: " << g_Config.pin << RESET << "\n";
            }
            
            if (choice == 1 || choice == 2) {
                std::string inBuf = "";
                while (true) {
                    std::cout << "\r\033[K" << getPrompt() << "Enter Username [" << inBuf.length() << "/24]: " << inBuf << std::flush;
                    int c = _getch();
                    if (c == '\r' || c == '\n') break;
                    if (c == '\b') {
                        if (!inBuf.empty()) inBuf.pop_back();
                    } else if (inBuf.length() < 24 && (isalnum(c) || c == '_')) {
                        inBuf += (char)c;
                    }
                }
                std::cout << "\n";
                if (!inBuf.empty()) g_Config.username = inBuf;
                else if (g_Config.username.empty()) g_Config.username = "Guest";
            }
            
            if (choice == 1) { // SENDER
                std::string in;
                std::cout << getPrompt() << "Enter HOST IP [" << g_Config.ip << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.ip = in;
                
                std::cout << getPrompt() << "Enter HOST Port [" << g_Config.port << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.port = safeStoi(in, g_Config.port);
                
                std::cout << getPrompt() << "Enter ROOM PIN (Only numbers) [" << g_Config.pin << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.pin = safeStoul(in, g_Config.pin);
                
                std::cout << getPrompt() << "Press [ENTER] to keep current MUTE Key (" << g_Config.muteKey << "),\nor PRESS ANY NEW KEY (Mouse 4, Numpad, etc.) to bind now... (Press ESC to cancel)\n";
                while (true) { bool anyPressed = false; for (int i = 1; i < 255; i++) { if (GetAsyncKeyState(i) & 0x8000) anyPressed = true; } if (!anyPressed) break; Sleep(10); } // Wait for release
                
                int newMuteKey = 0;
                bool escPressed = false;
                while (newMuteKey == 0) {
                    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { escPressed = true; break; }
                    if (GetAsyncKeyState(VK_RETURN) & 0x8000) { newMuteKey = g_Config.muteKey; break; }
                    for (int i = 1; i < 255; i++) {
                        if (i == VK_RETURN || i == VK_ESCAPE || i == VK_LBUTTON || i == VK_RBUTTON) continue;
                        if (GetAsyncKeyState(i) & 0x8000) { newMuteKey = i; break; }
                    }
                    Sleep(10);
                }
                g_Config.muteKey = newMuteKey;
                std::cout << GREEN << "[+] Mute Key Confirmed (Virtual Key Code: " << newMuteKey << ")\n" << RESET;
                
                Sleep(200); // Wait for physical key release
                while (_kbhit()) _getch(); // Flush literal keyboard buffer so it doesn't bleed into the next cin
                bool pidConfirmed = false;
                
                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                int printedLines = 0; // Maintain printed lines across redraws

                while (!pidConfirmed) {
                    g_ActiveWindows.clear();
                    EnumWindows(EnumWindowsProc, 0);
                    
                    DWORD oldMode;
                    GetConsoleMode(hIn, &oldMode);
                    SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT);
                    
                    std::string inputBuf = "";
                    bool inputComplete = false;
                    int selectedIndex = -1; // -1 means no selection, typing mode
                    int confirmedIndex = -1;
                    
                    auto redrawList = [&]() {
                        if (printedLines > 0) std::cout << "\r\033[" << printedLines << "A";
                        std::cout << "\033[0J"; // Clear all text below cursor
                        int curLines = 0;
                        
                        int totalItems = g_ActiveWindows.size() + 1;
                        int maxItems = 15;
                        int startIdx = 0;
                        
                        if (selectedIndex != -1) {
                            if (selectedIndex >= maxItems / 2) startIdx = selectedIndex - maxItems / 2;
                            if (startIdx + maxItems > totalItems) startIdx = totalItems - maxItems;
                            if (startIdx < 0) startIdx = 0;
                        }
                        
                        int endIdx = startIdx + maxItems;
                        if (endIdx > totalItems) endIdx = totalItems;
                        
                        if (startIdx > 0) {
                            std::cout << CYAN << "   ... (" << startIdx << " more above) ...\033[K" << RESET << "\n";
                            curLines++;
                        }
                        
                        for (int i = startIdx; i < endIdx; ++i) {
                            if (i == 0) {
                                std::string bg0 = (confirmedIndex == 0) ? "\033[42;30m" : (selectedIndex == 0 ? "\033[44;97m" : "");
                                if (!bg0.empty()) std::cout << bg0;
                                std::cout << CYAN << "   [PID: 0]\t" << RESET << bg0 << "Global_System \t- Capture Entire Computer Audio\033[K" << RESET << "\n";
                            } else {
                                int wIdx = i - 1;
                                std::string bgi = (confirmedIndex == i) ? "\033[42;30m" : (selectedIndex == i ? "\033[44;97m" : "");
                                if (!bgi.empty()) std::cout << bgi;
                                
                                std::string tName = g_ActiveWindows[wIdx].name;
                                std::string tTitle = g_ActiveWindows[wIdx].title;
                                if (tName.length() > 25) tName = tName.substr(0, 22) + "...";
                                if (tTitle.length() > 40) tTitle = tTitle.substr(0, 37) + "...";
                                
                                std::cout << CYAN << "   [PID: " << g_ActiveWindows[wIdx].pid << "]\t" << RESET 
                                          << bgi << tName << " \t- " << tTitle << "\033[K" << RESET << "\n";
                            }
                            curLines++;
                        }
                        
                        if (endIdx < totalItems) {
                            std::cout << CYAN << "   ... (" << totalItems - endIdx << " more below) ...\033[K" << RESET << "\n";
                            curLines++;
                        }
                        
                        std::cout << "\nInput Target PID [" << g_Config.pid << "], 'T' to Target Window, 'R' to Reload list: \033[K" << inputBuf;
                        curLines += 1;
                        printedLines = curLines;
                    };
                    
                    redrawList();
                    
                    while (!inputComplete) {
                        INPUT_RECORD ir[32];
                        DWORD read;
                        ReadConsoleInput(hIn, ir, 32, &read);
                        for (DWORD i = 0; i < read; ++i) {
                            if (ir[i].EventType == KEY_EVENT && ir[i].Event.KeyEvent.bKeyDown) {
                                WORD vKey = ir[i].Event.KeyEvent.wVirtualKeyCode;
                                char ch = ir[i].Event.KeyEvent.uChar.AsciiChar;
                                
                                if (vKey == VK_RETURN) {
                                    if (selectedIndex != -1) {
                                        if (selectedIndex == 0) inputBuf = "0";
                                        else inputBuf = std::to_string(g_ActiveWindows[selectedIndex - 1].pid);
                                        std::cout << "\r\033[K" << "Input Target PID [" << g_Config.pid << "], 'T' to Target Window, 'R' to Reload list: " << inputBuf;
                                        confirmedIndex = selectedIndex;
                                        selectedIndex = -1;
                                        redrawList();
                                        Sleep(300); // Visual flair for green highlight
                                        confirmedIndex = -1;
                                        redrawList();
                                    } else {
                                        inputComplete = true; break;
                                    }
                                } else if (vKey == VK_TAB) {
                                    if (selectedIndex == -1) selectedIndex = 0;
                                    else selectedIndex = (selectedIndex + 1) % (g_ActiveWindows.size() + 1);
                                    redrawList();
                                } else if (vKey == VK_UP) {
                                    if (selectedIndex <= 0) selectedIndex = g_ActiveWindows.size();
                                    else selectedIndex--;
                                    redrawList();
                                } else if (vKey == VK_DOWN) {
                                    if (selectedIndex == -1) selectedIndex = 0;
                                    else selectedIndex = (selectedIndex + 1) % (g_ActiveWindows.size() + 1);
                                    redrawList();
                                } else if (vKey == VK_ESCAPE) {
                                    if (selectedIndex != -1) {
                                        selectedIndex = -1;
                                        redrawList();
                                    } else if (!inputBuf.empty()) {
                                        inputBuf = "";
                                        redrawList();
                                    } else {
                                        inputBuf = "ESC_EXIT";
                                        inputComplete = true; break;
                                    }
                                } else if (vKey == VK_BACK) {
                                    if (selectedIndex != -1) { selectedIndex = -1; redrawList(); }
                                    else if (!inputBuf.empty()) {
                                        inputBuf.pop_back();
                                        std::cout << "\b \b";
                                    }
                                } else if (ch >= 32 && ch <= 126) {
                                    if (selectedIndex != -1) { selectedIndex = -1; redrawList(); }
                                    inputBuf += ch;
                                    std::cout << ch;
                                }
                            }
                        }
                    }
                    SetConsoleMode(hIn, oldMode);
                    std::cout << std::endl; 
                    
                    std::string in = inputBuf;
                    if (in == "R" || in == "r") {
                        if (printedLines > 0) std::cout << "\r\033[" << (printedLines + 1) << "A\033[0J";
                        printedLines = 0;
                        continue; 
                    } else if (in == "T" || in == "t") {
                        std::cout << "\n" << MAGENTA << "[ TARGET MODE ] " << YELLOW << "Please click on the window you want to capture... (Press ESC to cancel)" << RESET << "\n";
                        
                        // Target Mode with ESC tracking
                        while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) { Sleep(10); } // Wait for release
                        
                        bool targetAborted = false;
                        while (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) { 
                            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { targetAborted = true; break; }
                            Sleep(10); 
                        }
                        
                        if (targetAborted) {
                            std::cout << YELLOW << "[-] Target Window Mode CANCELLED.\n\n" << RESET;
                            if (printedLines > 0) std::cout << "\r\033[" << (printedLines + 5) << "A\033[0J";
                            printedLines = 0;
                        } else {
                            HWND hwnd = GetForegroundWindow();
                            DWORD pid = 0;
                            GetWindowThreadProcessId(hwnd, &pid);
                            char processName[256] = "<unknown>";
                            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                            if (hProcess) { GetModuleBaseNameA(hProcess, NULL, processName, sizeof(processName)); CloseHandle(hProcess); }
            
                            std::cout << GREEN << "[+] Selected Window: " << processName << " (PID: " << pid << ")\n" << RESET;
                            std::cout << "Is this correct? (Y/N): ";
                            std::string conf;
                            if (safeGetLineGlobalEsc(conf)) {
                                if (conf == "Y" || conf == "y") {
                                    g_Config.pid = pid;
                                    pidConfirmed = true;
                                } else {
                                    std::cout << YELLOW << "[-] Target selection cancelled. Retrying...\n\n" << RESET;
                                    if (printedLines > 0) std::cout << "\r\033[" << (printedLines + 7) << "A\033[0J";
                                    printedLines = 0;
                                }
                            } else {
                                std::cout << YELLOW << "\n[-] Target selection cancelled via ESC. Retrying...\n\n" << RESET;
                                if (printedLines > 0) std::cout << "\r\033[" << (printedLines + 7) << "A\033[0J";
                                printedLines = 0;
                            }
                        }
                    } else if (in == "ESC_EXIT") {
                        pidConfirmed = true; 
                        continue;
                    } else {
                        if (!in.empty()) {
                            try { g_Config.pid = std::stoul(in); } catch (...) { std::cout << RED << "[!] Invalid input. Using previous PID: " << g_Config.pid << RESET << "\n"; }
                        }
                        pidConfirmed = true;
                    }
                }
                if (escPressed) { std::cout << "\n" << YELLOW << "[-] Escaping to Main Menu...\n" << RESET; continue; }
                
                saveConfig(profileName);
                startSender(g_Config.ip, g_Config.port, g_Config.pin, g_Config.pid, g_Config.muteKey, g_Config.username);

            } else if (choice == 2) { // RECEIVER
                std::string in;
                std::cout << getPrompt() << "Enter HOST IP [" << g_Config.ip << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.ip = in;
                
                std::cout << getPrompt() << "Enter HOST Port [" << g_Config.port << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.port = safeStoi(in, g_Config.port);
                
                std::cout << getPrompt() << "Enter ROOM PIN (Only use numbers) [" << g_Config.pin << "]: ";
                if (!safeGetLineGlobalEsc(in)) { std::cout << "\n" << YELLOW << "[-] Cancelled.\n" << RESET; continue; }
                if(!in.empty()) g_Config.pin = safeStoul(in, g_Config.pin);
                
                std::cout << getPrompt() << "Press [ENTER] to keep current MUTE Key (" << g_Config.muteKey << "),\nor PRESS ANY NEW KEY (Mouse 4, Numpad, etc.) to bind now... (Press ESC to cancel)\n";
                while (true) { bool anyPressed = false; for (int i = 1; i < 255; i++) { if (GetAsyncKeyState(i) & 0x8000) anyPressed = true; } if (!anyPressed) break; Sleep(10); } // Wait for release
                
                int newMuteKey = 0;
                bool escPressed = false;
                while (newMuteKey == 0) {
                    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { escPressed = true; break; }
                    if (GetAsyncKeyState(VK_RETURN) & 0x8000) { newMuteKey = g_Config.muteKey; break; }
                    for (int i = 1; i < 255; i++) {
                        if (i == VK_RETURN || i == VK_ESCAPE || i == VK_LBUTTON || i == VK_RBUTTON) continue;
                        if (GetAsyncKeyState(i) & 0x8000) { newMuteKey = i; break; }
                    }
                    Sleep(10);
                }
                g_Config.muteKey = newMuteKey;
                std::cout << GREEN << "[+] Mute Key Confirmed (Virtual Key Code: " << newMuteKey << ")\n" << RESET;
                
                Sleep(200); // Wait for physical key release
                while (_kbhit()) _getch(); // Flush literal keyboard buffer
                
                saveConfig(profileName);
                startReceiver(g_Config.ip, g_Config.port, g_Config.pin, g_Config.username, g_Config.muteKey);

            } else if (choice == 3) { // HOST
                std::cout << getPrompt() << "Enter HOST Listen Port [" << g_Config.port << "]: ";
                std::string in; std::getline(std::cin, in); if(!in.empty()) g_Config.port = safeStoi(in, g_Config.port);
                
                std::cout << GREEN << " 1. [ SECURE ] VPN / Local Area Mode\n" << RESET;
                std::cout << RED << " 2. [INSECURE] UPnP Auto-Port Forward Mode\n" << RESET;
                std::cout << getPrompt() << "Select Mode [1]: ";
                std::getline(std::cin, in);
                int secMode = in.empty() ? 1 : safeStoi(in, 1);

                saveConfig(profileName);

                if (secMode == 2) {
                    std::string ip = getLocalIP();
                    std::cout << YELLOW << "[*] Instructing router to open port " << g_Config.port << " -> " << ip << " ...\n" << RESET;
                    if (UPnP_ForwardPort(g_Config.port, ip)) {
                        std::cout << GREEN << "[+] UPnP SUCCESS! Port is open.\n" << RESET;
                    } else {
                        std::cout << RED << "[-] UPnP FAILED.\n" << RESET;
                    }
                }
                startHost(g_Config.port);
            }
        }
        else if (choice == 4) { break; }
        
        g_Config = AppConfig(); // Clear preset on return to menu
    }

    WSACleanup();
    return 0;
}
