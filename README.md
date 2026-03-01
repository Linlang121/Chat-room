

https://github.com/user-attachments/assets/326d5357-e9a9-4798-9644-e4fdbf7fd9ad

💬 Chat Room — Client-Server Application

<p align="center">
  <strong>A real-time multi-user chat application</strong> built with <strong>C++</strong>, <strong>WinSock</strong>, and <strong>ImGui</strong> (DirectX 12).
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C++-17-blue?style=flat-square&logo=cplusplus" alt="C++17"/>
  <img src="https://img.shields.io/badge/Windows-WinSock-0078D6?style=flat-square&logo=windows" alt="WinSock"/>
  <img src="https://img.shields.io/badge/UI-ImGui-00B294?style=flat-square" alt="ImGui"/>
  <img src="https://img.shields.io/badge/Graphics-DirectX%2012-0078D6?style=flat-square&logo=directx" alt="DirectX 12"/>
</p>

---

## 📖 Overview

This project implements a **client–server chat room** with two programs: **myServer** (TCP server) and **myClient** (GUI client). The server supports multiple concurrent clients, broadcasts public messages to everyone, and routes private (direct) messages to the correct user. The client provides a login screen, a main chat window with an online user list, and separate windows for direct messages, with distinct sound notifications for public vs private messages.

Developed as **Part 2** of the Games Engineering coursework at the **University of Warwick**.

---

## ✨ Features

| Feature | Description |
|--------|-------------|
| **Multi-client** | Server accepts multiple clients; each connection is handled in its own thread. |
| **Public chat** | Broadcast messages visible to all connected users in the main room. |
| **Direct messages (DM)** | Private one-to-one chats in separate windows. |
| **User list** | Live list of online users; updated when someone joins or leaves. |
| **Sound feedback** | Different sounds for public messages vs private messages (e.g. `music1.mp3`, `music2.mp3`). |
| **Non-blocking UI** | Connection and receiving run in background threads; GUI stays responsive. |

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         myClient (ImGui + DX12)                   │
│  ┌─────────────┐  ┌──────────────────┐  ┌─────────────────────┐  │
│  │ Login       │  │ Main Chat        │  │ DM Windows          │  │
│  │ (username)  │  │ Users │ Messages │  │ (one per contact)   │  │
│  └──────┬──────┘  └────────┬─────────┘  └──────────┬──────────┘  │
│         │                  │                       │              │
│         └──────────────────┼───────────────────────┘              │
│                            │ message queue                         │
│  ┌─────────────────────────▼─────────────────────────────────┐   │
│  │ Client (WinSock): send thread + recv thread → queue         │   │
│  └─────────────────────────────┬──────────────────────────────┘   │
└────────────────────────────────┼─────────────────────────────────┘
                                 │ TCP (port 65432)
┌────────────────────────────────▼─────────────────────────────────┐
│                         myServer (WinSock)                         │
│  accept loop (thread) → for each client: handleClient (thread)     │
│  parseMessage: public → broadcast; private → sendToClient         │
│  clients_ (mutex-protected list)                                   │
└───────────────────────────────────────────────────────────────────┘
```

- **Server**: Listens on **port 65432**. An accept thread runs `accept()` in a loop; for each new connection it reads the username, adds the client to a shared list (with mutex), sends the user list to the new client and broadcasts it to others, then spawns a **handleClient** thread that reads from that socket in a loop.
- **Client**: On “Connect”, a background thread performs `WSAStartup`, creates the socket, connects, sends the username, and starts a **receive thread**. The receive thread pushes messages into a queue; the main (GUI) thread polls with `hasNewMessages()` / `getNewMessages()` each frame and updates the UI.

---

## 📡 Message Protocol

Messages are **plain strings** with a **type prefix** (everything before the first `/` is the type, the rest is the payload).

| Type | Format | Description |
|------|--------|-------------|
| *(none)* | `username` | First message on connect: client sends username. |
| **clients/** | `clients/Alice,Bob,` | Server sends/broadcasts comma-separated online user list. |
| **public/** | `public/username: content` | Public chat; server broadcasts to all other clients. |
| **private/** | `private/targetName: content` | DM; server sends only to the socket for `targetName`. |
| **!bye** | `!bye` | Client disconnects; server removes client and broadcasts updated list. |

---

## 🖥️ GUI Layout (ImGui)

- **Login**: Full-screen window; user enters username, clicks “Confirm” (starts `initializeAsync` in a background thread), then “Connect” to enter the chat room. Title becomes “Chat Room” on success.
- **Main chat**: Two-column layout (ImGui Columns). **Left**: “Current Users” and a “chat” button per user to open a DM. **Right**: Scrollable public messages and a multiline input with “Send”. Own messages are right-aligned.
- **DM windows**: One window per conversation (e.g. “Private Chat with Alice”), with messages, input, “Send” and “Close”. If the other user leaves, the title shows “(offline)” and Send is disabled until they reconnect.

---

## 📁 Project Structure

```
Chatroom/
├── myServer/                 # Server application
│   ├── Server.h, Server.cpp  # Server logic (WinSock, threads, client list)
│   ├── main.cpp              # Entry point, starts server
│   └── ...
├── myclient/                 # Client application
│   ├── client.h, client.cpp  # Client networking (WinSock, async connect, recv thread)
│   ├── myClient.cpp          # ImGui + D3D12 UI (login, main chat, DM windows)
│   ├── Sound.h               # Sound playback (e.g. FMOD or similar)
│   ├── IMGUI/                # ImGui library (Win32 + DX12 backends)
│   └── ...
├── Report.md                 # Games Engineering report (Part 1 & Part 2)
└── README.md                 # This file
```

---

## 🛠️ Build & Run

### Prerequisites

- **Windows** (WinSock, Win32 API)
- **Visual Studio** (e.g. 2019/2022) with C++ desktop development workload
- **DirectX 12** (usually included with Windows SDK)

### Build

1. Open the solution or project files in **Visual Studio** (`myServer` and `myclient` are separate projects).
2. Build **myServer** and **myclient** (e.g. x64 Debug or Release).

### Run

1. **Start the server**  
   Run `myServer.exe` (or the built server executable). It will listen on `127.0.0.1:65432`.

2. **Start one or more clients**  
   Run `myclient.exe`. Enter a username, click “Confirm”, then “Connect”. Place `music1.mp3` and `music2.mp3` in the same folder as the client executable for sound notifications.

3. **Optional**: Change `Client::HOST` and `Client::PORT` in `myclient/client.h` to point to another machine or port.

---

## 🔧 Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C++ (std::thread, mutex, atomic) |
| Networking | WinSock2 (TCP) |
| Client UI | ImGui with Win32 + DirectX 12 |
| Sound | Custom sound API (see `Sound.h`; use music1.mp3 / music2.mp3) |

---

## 👤 Author

**Linlang Zou**  
University of Warwick — Games Engineering  
*February 2026*

---



<p align="center">
  <sub>Built with C++, WinSock, and ImGui</sub>
</p>
