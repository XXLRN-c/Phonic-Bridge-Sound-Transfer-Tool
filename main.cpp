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
}

// GLOBAL VARIABLES
std::atomic<bool> g_ExitReceiver(false);
std::atomic<bool> g_ExitSender(false);
std::atomic<bool> g_ExitHost(false);
std::atomic<bool> g_SenderMuted(false);
float g_Volume = 1.0f; // Default Volume 100%

// PACKET PROTOCOL
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;      // 0x50484F4E ('PHON')
    uint8_t type;        // 0 = RAW_AUDIO, 1 = KEEPALIVE_RECV, 2 = KEEPALIVE_SEND, 3 = 16BIT_AUDIO
    uint32_t roomPin;    // e.g. 9999
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

void volumeControlThread() {
    std::cout << GREEN << "\n[+] Audio Control Interface Activated." << RESET << std::endl;
    std::cout << YELLOW << "[>] Hotkeys: [RIGHT] Vol Up | [LEFT] Vol Down | [ESC] Disconnect\n" << RESET << std::endl;
    printVolumeBar();
    
    while(!g_ExitReceiver) {
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
    std::cout << YELLOW << "\n[>] Hotkeys: [MUTE Bind: " << muteKey << "] Toggle Mute | [ESC] Disconnect\n" << RESET << std::endl;
    while(!g_ExitSender) {
        if (GetAsyncKeyState(muteKey) & 0x8000) {
            if (!wasPressed) {
                g_SenderMuted = !g_SenderMuted;
                if (g_SenderMuted) std::cout << "\n" << RED << " [!] MUTE ON - Transmitter suspended (KeepAlives only).\n" << RESET;
                else std::cout << "\n" << GREEN << " [+] MUTE OFF - Transmitter active.\n" << RESET;
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
                std::cout << CYAN << "   [PID: " << pid << "]\t" << RESET << processName << " \t- " << title << std::endl;
            }
        }
    }
    return TRUE;
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

    // Windows UDP Bug Fix: Prevent WSAECONNRESET from dropping valid packets when a client disappears
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    WSAIoctl(hostSocket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);

    std::map<uint32_t, std::vector<ClientInfo>> activeRooms;
    char buffer[65535];

    std::cout << GREEN << "\n[!] HOST SERVER ACTIVE ON LOCAL PORT " << listenPort << "!" << RESET << std::endl;
    std::cout << YELLOW << "    [L] List Connected Users | [ESC] Stop Server\n" << RESET << std::endl;

    fd_set readfds;
    timeval tv;

    DWORD lastStatusPrint = GetTickCount();
    int lastSenders = -1, lastReceivers = -1;

    while (!g_ExitHost) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 27) {
                std::cout << RED << "\n[-] Shutting down Host..." << RESET << std::endl;
                g_ExitHost = true;
                break;
            } else if (ch == 'L' || ch == 'l') {
                std::cout << CYAN << "\n=== [ LIVE CONNECTIONS ] ===" << RESET << std::endl;
                int count = 0;
                DWORD now = GetTickCount();
                for (auto& roomPair : activeRooms) {
                    std::cout << MAGENTA << "  ROOM [" << roomPair.first << "]:\n" << RESET;
                    for (auto& c : roomPair.second) {
                        if (now - c.lastSeen < 5000) { // Only show active in last 5s
                           std::cout << "    - " << (c.type==1 ? "RECEIVER" : "SENDER") 
                                     << " [" << inet_ntoa(c.addr.sin_addr) << ":" << ntohs(c.addr.sin_port) << "]\n";
                           count++;
                        }
                    }
                }
                if (count == 0) std::cout << "    (No active connections right now)\n";
                std::cout << CYAN << "============================\n\n" << RESET << std::endl;
            }
        }

        FD_ZERO(&readfds);
        FD_SET(hostSocket, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        
        if (select(0, &readfds, NULL, NULL, &tv) > 0) {
            sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            int bytes = recvfrom(hostSocket, buffer, sizeof(buffer), 0, (SOCKADDR*)&clientAddr, &clientLen);

            if (bytes >= sizeof(PacketHeader)) {
                PacketHeader* hdr = (PacketHeader*)buffer;
                if (hdr->magic == 0x50484F4E) {
                    DWORD now = GetTickCount();
                    auto& roomClients = activeRooms[hdr->roomPin];
                    
                    // Maintenance: Find if client exists
                    bool found = false;
                    for (auto& c : roomClients) {
                        if (c.addr.sin_addr.s_addr == clientAddr.sin_addr.s_addr && c.addr.sin_port == clientAddr.sin_port) {
                            c.lastSeen = now;
                            found = true;
                            // Optionally update type if it changed
                            if (hdr->type == 1) c.type = 1;
                            else if (hdr->type == 2) c.type = 2;
                            break;
                        }
                    }

                    if (!found) {
                        ClientInfo ni; ni.addr = clientAddr; ni.lastSeen = now; ni.type = (hdr->type == 1) ? 1 : 2;
                        roomClients.push_back(ni);
                        std::cout << "\r                                                                               \r";
                        std::cout << GREEN << "[+] New " << (ni.type==1 ? "RECEIVER" : "SENDER") 
                                  << " joined ROOM " << hdr->roomPin << " (" << inet_ntoa(clientAddr.sin_addr) << ")\n" << RESET;
                        lastStatusPrint = 0; // Force immediate HUD repaint
                    }

                    // Forward Audio
                    if (hdr->type == 0 || hdr->type == 3) { 
                        for (auto& c : roomClients) {
                            if (c.type == 1) { // Forward to RECEIVERS
                                sendto(hostSocket, buffer, bytes, 0, (SOCKADDR*)&(c.addr), sizeof(sockaddr_in));
                            }
                        }
                    }
                }
            }
        }
        
        // LIVE STATUS OVERLAY
        DWORD now = GetTickCount();
        if (now - lastStatusPrint > 2000 || lastStatusPrint == 0) {
            int senders = 0, receivers = 0;
            for (auto& roomPair : activeRooms) {
                for (auto it = roomPair.second.begin(); it != roomPair.second.end(); ) {
                   if (now - it->lastSeen > 3000) { // 3 seconds timeout for immediate feedback
                       std::cout << "\r                                                                               \r";
                       std::cout << RED << "[-] " << (it->type==1 ? "RECEIVER" : "SENDER") 
                                 << " (" << inet_ntoa(it->addr.sin_addr) << ") ayrildi! [Oda: " << roomPair.first << "]\n" << RESET;
                       it = roomPair.second.erase(it);
                       lastStatusPrint = 0; // Force UI repaint
                   } else {
                       if (it->type == 1) receivers++;
                       else if (it->type == 2) senders++;
                       ++it;
                   }
                }
            }
            if (senders != lastSenders || receivers != lastReceivers || lastStatusPrint == 0) {
                std::cout << "\r                                                                               \r";
                std::cout << MAGENTA << "[ CANLI DURUM ] " << CYAN << senders << " Gonderici | " << receivers << " Dinleyici odada aktif!        " << RESET << std::flush;
                lastSenders = senders;
                lastReceivers = receivers;
            }
            lastStatusPrint = GetTickCount();
        }
    }
    closesocket(hostSocket);
}



// ======================================================================
// [1] SENDER MODULE
// ======================================================================
void startSender(const std::string& hostIP, int hostPort, uint32_t roomPin, DWORD targetPID, int muteKey) {
    g_ExitSender = false;
    g_SenderMuted = false;
    std::cout << YELLOW << "\n[*] Initializing Network Transmission..." << RESET << std::endl;

    SOCKET sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

    DWORD lastSent = GetTickCount();

    while (!g_ExitSender) {
        Sleep(10);
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
                hdr->magic = 0x50484F4E; hdr->type = 0; hdr->roomPin = roomPin;
                memcpy(sendBuf.data() + sizeof(PacketHeader), pData, bytesToSend);
                sendto(sendSocket, sendBuf.data(), sendBuf.size(), 0, (SOCKADDR*)&hostAddr, sizeof(hostAddr));
                lastSent = GetTickCount();
            }

            CHECK_HR(pCaptureClient->ReleaseBuffer(numFramesAvailable));
            pCaptureClient->GetNextPacketSize(&packetLength);
        }

        // TRUE HEARTBEAT logic
        if (GetTickCount() - lastSent >= 1000) {
            PacketHeader hdr = {0x50484F4E, 2, roomPin}; // KEEPALIVE_SENDER
            sendto(sendSocket, (char*)&hdr, sizeof(hdr), 0, (SOCKADDR*)&hostAddr, sizeof(hostAddr));
            lastSent = GetTickCount();
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
void startReceiver(const std::string& hostIP, int hostPort, uint32_t roomPin) {
    g_ExitReceiver = false;
    std::cout << YELLOW << "\n[*] Joining HOST: " << hostIP << ":" << hostPort << " [ROOM " << roomPin << "]" << RESET << std::endl;
    
    std::thread ui(volumeControlThread);
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
            PacketHeader hdr = {0x50484F4E, 1, roomPin}; // KEEPALIVE_RECV
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
                    lastDataReceived = GetTickCount(); // Valid packet resets timeout

                    if (isDisconnected) {
                        std::cout << "\n" << GREEN << "[+] Yeniden Bağlandı!" << RESET << " (Müzik devam ediyor)\n";
                        printVolumeBar();
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
                                    float val = src[i] * g_Volume;
                                    if (val > 1.0f) val = 1.0f; else if (val < -1.0f) val = -1.0f;
                                    dst[i] = val;
                                }
                            } else {
                                memcpy(pData, audioData, audioBytes); // Basic fallback
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
                std::cout << "\n" << RED << "[!] Bağlantı Koptu... Host Bekleniyor..." << RESET << "\n";
            }
        }
    }

    pAudioClient->Stop(); pAudioClient->Release(); CoUninitialize(); closesocket(recvSocket);
}

// ======================================================================
// MAIN ENTRY
// ======================================================================
void animateTitleThread() {
    std::string text = "Phonic Bridge By XXLRN";
    while (true) {
        std::string current = "";
        for (char c : text) {
            current += c;
            SetConsoleTitleA(current.c_str());
            Sleep(80);
        }
        Sleep(3000);
        while (current.length() > 0) {
            current.pop_back();
            SetConsoleTitleA(current.empty() ? " " : current.c_str());
            Sleep(30);
        }
        Sleep(1000);
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
        std::cout << ASCII_LOGO << std::flush;
        std::cout << GREEN << "  1. [ SENDER ] Transmit Audio to a Host Room" << RESET << std::endl;
        std::cout << GREEN << "  2. [RECEIVER] Join a Host Room & Listen" << RESET << std::endl;
        std::cout << CYAN  << "  3. [  HOST  ] Create a Central Relay Server" << RESET << std::endl;
        std::cout << YELLOW << "  4. [  EXIT  ] Terminate Application" << RESET << std::endl;
        std::cout << "\n" << MAGENTA << "root@phonic:~# " << RESET << "Select mode: ";
        
        std::string inputStr;
        std::getline(std::cin, inputStr);
        if (inputStr.empty()) continue;
        int choice = std::stoi(inputStr);

        if (choice >= 1 && choice <= 3) {
            std::cout << MAGENTA << "root@phonic:~# " << RESET << "Enter Profile Name (Only english characters) or leave empty: ";
            std::string profileName;
            std::getline(std::cin, profileName);
            if (!profileName.empty()) {
                loadConfig(profileName);
                std::cout << GREEN << "[+] Profile Loaded! IP: " << g_Config.ip << " Port: " << g_Config.port << " PIN: " << g_Config.pin << RESET << "\n";
            }
            
            if (choice == 1) { // SENDER
                std::cout << "Enter HOST IP [" << g_Config.ip << "]: ";
                std::string in; std::getline(std::cin, in); if(!in.empty()) g_Config.ip = in;
                
                std::cout << "Enter HOST Port [" << g_Config.port << "]: ";
                std::getline(std::cin, in); if(!in.empty()) g_Config.port = std::stoi(in);
                
                std::cout << "Enter ROOM PIN (Lutfen sadece sayi giriniz) [" << g_Config.pin << "]: ";
                std::getline(std::cin, in); if(!in.empty()) g_Config.pin = std::stoul(in);
                
                std::cout << "Press [ENTER] to keep current MUTE Key (" << g_Config.muteKey << "),\nor PRESS ANY NEW KEY (Mouse 4, Numpad, etc.) to bind now...\n";
                while (true) { bool anyPressed = false; for (int i = 1; i < 255; i++) { if (GetAsyncKeyState(i) & 0x8000) anyPressed = true; } if (!anyPressed) break; Sleep(10); } // Wait for release
                
                int newMuteKey = 0;
                while (newMuteKey == 0) {
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
                std::cout << YELLOW << "\n[ SCANNING ACTIVE PROCESSES... ]\n" << RESET;
                std::cout << CYAN << "   [PID: 0]\t" << RESET << "Global_System \t- Capture Entire Computer Audio" << std::endl;
                EnumWindows(EnumWindowsProc, 0);
                std::cout << "Input Target PID [" << g_Config.pid << "]: ";
                std::getline(std::cin, in);
                if (!in.empty()) g_Config.pid = std::stoul(in);
                
                saveConfig(profileName);
                startSender(g_Config.ip, g_Config.port, g_Config.pin, g_Config.pid, g_Config.muteKey);

            } else if (choice == 2) { // RECEIVER
                std::cout << "Enter HOST IP [" << g_Config.ip << "]: ";
                std::string in; std::getline(std::cin, in); if(!in.empty()) g_Config.ip = in;
                
                std::cout << "Enter HOST Port [" << g_Config.port << "]: ";
                std::getline(std::cin, in); if(!in.empty()) g_Config.port = std::stoi(in);
                
                std::cout << "Enter ROOM PIN (Only use numbers) [" << g_Config.pin << "]: ";
                std::getline(std::cin, in); if(!in.empty()) g_Config.pin = std::stoul(in);
                
                saveConfig(profileName);
                startReceiver(g_Config.ip, g_Config.port, g_Config.pin);

            } else if (choice == 3) { // HOST
                std::cout << "Enter HOST Listen Port [" << g_Config.port << "]: ";
                std::string in; std::getline(std::cin, in); if(!in.empty()) g_Config.port = std::stoi(in);
                
                std::cout << GREEN << " 1. [ SECURE ] VPN / Local Area Mode\n" << RESET;
                std::cout << RED << " 2. [INSECURE] UPnP Auto-Port Forward Mode\n" << RESET;
                std::cout << "Select Model [1]: ";
                std::getline(std::cin, in);
                int secMode = in.empty() ? 1 : std::stoi(in);

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
    }

    WSACleanup();
    return 0;
}
