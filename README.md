```diff
+[38;2;255;20;147m       |                _)         |        _)      |             
+[38;2;200;40;180m  _ \    \    _ \    \   |   _|     _ \   _| |   _` |   _` |   -_)
![38;2;150;50;210m .__/ _| _| \___/ _| _| _| \__|   _.__/ _|  _| \__,_| \__, | \___|
#[38;2;100;60;240m_|                                                    ____/       

@@[38;2;0;120;255m          >>> [ Secure Audio Streaming & Network Capture Module ] <<<
-                                                                  [38;2;255;0;0mm[38;2;235;0;0ma[38;2;215;0;0md[38;2;195;0;0me [38;2;175;0;0mb[38;2;155;0;0my [38;2;135;0;0mx[38;2;115;0;0mx[38;2;95;0;0ml[38;2;75;0;0mr[38;2;55;0;0mn
```

**Secure Audio Streaming & Network Capture Module**

Phonic Bridge is a fast, zero-configuration C++ networking application designed to seamlessly capture and stream quality audio over UDP. Built purely using native Windows APIs (WASAPI, Winsock2) without heavy dependencies, it achieves minimum latency and extreme reliability for real-time audio sharing.

Made by **XXLRN**.

## ✨ Features
* **Lossless Audio Bridge:** Captures Windows output directly from the sound engine using WASAPI Loopback at 32-bit Float 48kHz, bypassing virtual audio cables.
* **Dual Networking:** Works flawlessly locally or over the internet via Zero-Config UPnP Port Forwarding or VPN software (Such as Radmin!).
* **Smart Sender/Receiver Model:**
  * **Sender:** Captures specific applications or the entire system output.
  * **Receiver:** Dynamically auto-reconnects, buffers data safely, and plays matching bit-perfect audio.
  * **Host Relay:** Centralized UDP server that can relay audio to multiple dynamically connected listeners in real-time.
* **Well Made Command Line Interface:** Beautiful ANSI TrueColor coded interface.
* **Global Mutes:** Built-in global hotkey muting (for Senders).
* **Profiles:** Save and load `.xxlrn` profiles containing Host IP, Ports, and secured PIN configurations... for instant connections.
* **Active Status HUD:** Real-time terminal overlay HUD (In Host Relay) that tracks user connectivity states utilizing periodic heartbeats (KeepAlives).

## 📌 Detailed Usage Guide
Phonic Bridge operates in a **3-Way Relay Architecture**. To successfully stream audio to your friends, you need to understand the three core roles. 

### 🖥️ 1. The Host (The Bridge)
The Host is the central server that acts as a secure bridge between the Sender and the Receiver(s). Neither the Sender nor the Receiver directly connect to each other—they both connect to the **Host**.
* **Who should be the Host?** Usually, the person with the best internet connection or whoever is hosting the VPN network. (The Host can also run the Receiver or Sender on the same PC simultaneously by simply opening another `app.exe`).
* **How to start:** Open `app.exe`, press `3` to become the Host. 
* **Choosing a Port:** The app will ask for a port. A port is just a 4-digit communication door (e.g., `4444` or `7777`). Pick any number you like. **Remember this number**, as you will need to give it to your friends!
* **Status:** Wait on the screen. The Host console will display a real-time HUD of exactly who is currently in the room.

### 🎤 2. The Sender
The Sender is the person whose audio is being captured and broadcasted.
* **How to start:** Open `app.exe`, press `1` to become the Sender.
* **Host IP Address:** You must enter the Host's IP address. 
   * *If playing over the internet via VPN (like Radmin)*: Copy the IP address shown next to the Host's name in the VPN app (usually starts with `26.x.x.x`). 
   * *If playing on the same local Wi-Fi*: Use the Host's local IPv4 address (found via `ipconfig` in cmd).
   * *If the Host is YOU*: Simply type `127.0.0.1`.
* **Host Port:** Enter the exact 4-digit port number the Host chose earlier (e.g., `4444`).
* **Room PIN:** Enter a secure Room PIN (e.g., `1234`). Only Receivers who also type this exact PIN will be able to hear you.
* **Target:** You can choose to capture your entire system's audio by entering `0`, or target a specific application (like Spotify or FL Studio) by entering its Process ID (PID).
* **Mute:** You can set a global mute hotkey. You can instantly cut your audio transmission anytime, even while tabbed into a full-screen game!

### 🎧 3. The Receiver
The Receiver is the person who is simply listening to the Sender's audio.
* **How to start:** Open `app.exe`, press `2` to become the Receiver.
* **Connection:** Enter the exact same **Host IP** and **Host Port** that the Sender used.
* **Room PIN:** Enter the exact same **Room PIN** the Sender created.
* **Listen:** Sit back and enjoy bit-perfect, lag-free audio! You can use the `LEFT` and `RIGHT` arrow keys on your keyboard to instantly adjust your local volume.

---

## 🌐 Playing Over the Internet (VPN Setup)
If you are playing with friends over the internet, a direct UDP connection might be blocked by your router's firewall. To bypass this effortlessly without logging into your router and opening ports, we recommend using a Virtual LAN (VPN) tool.

**Disclaimer:** I am not sponsored by or affiliated with Radmin VPN in any way. It's simply a free and reliable tool that I used to test this project.*

### Step-by-Step Radmin VPN Setup:
1. **Download & Install:** Every friend (Host, Sender, and Receivers) must download and install **Radmin VPN** (or Hamachi).
2. **Create Network:** The Host opens Radmin VPN, clicks `Network -> Create Network`, creates a Network Name and Password.
3. **Join Network:** The other friends click `Network -> Join Network`, and enter the Host's Network Name and Password.
4. **Get the IP:** In the Radmin VPN window, you will see an IP address next to the Host's PC name (it usually starts with `26.x.x.x`). 
5. **Connect:** The Sender and the Receivers simply enter **that exact Radmin IP address** when Phonic Bridge asks for the "Host IP".
6. Boom! You are now streaming studio-quality audio directly to your friends with zero lag.
## 🤝 Contributing
Feel free to open an issue or submit a Pull Request! And please dont be shy to tell any features that you wanna see in this app in the future!

## 📜 Story
This project started because I produce music, and I often share my demo tracks with friends over Discord streams. We usually had to listen to music through Discord bots or screen sharing, but that either caused heavy PC lag (making it impossible to play games simultaneously) or completely ruined the audio quality.

I promised a friend that I would find a real solution to this problem we've suffered through for years and here it is. I present to everyone this simple, lightweight, yet incredibly effective audio tool. Happy listening!
