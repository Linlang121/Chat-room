// chat client: login, public chat, DM. needs myServer.

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "client.h"
#include "Sound.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

static const int APP_NUM_FRAMES_IN_FLIGHT = 2;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;

struct PublicLine {
    std::string line;
    bool isSelf;
};

struct WindowInfo {
    std::string title;
    bool isOpen = true;
    char buffer[512] = {};
    std::vector<PublicLine> conversations;
};

static std::string getExeDir() {
    wchar_t wpath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, wpath, MAX_PATH) == 0) return "";
    std::wstring wstr(wpath);
    size_t last = wstr.find_last_of(L"\\/");
    if (last == std::wstring::npos) return "";
    std::string base;
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)last + 1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    base.resize(len);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)last + 1, &base[0], len, nullptr, nullptr);
    return base;
}

static std::vector<std::string> splitClientsList(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); ) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string name = s.substr(i, j - i);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (!name.empty()) out.push_back(std::move(name));
        i = j == s.size() ? j : j + 1;
    }
    return out;
}

/* D3D12 + ImGui setup */
struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64 FenceValue;
};

struct ExampleDescriptorHeapAllocator {
    ID3D12DescriptorHeap* Heap = nullptr;
    UINT HeapHandleIncrement = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu = {};
    ImVector<int> FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap) {
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(desc.Type);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = (int)desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu->ptr = HeapStartCpu.ptr + (SIZE_T)idx * HeapHandleIncrement;
        out_gpu->ptr = HeapStartGpu.ptr + (SIZE_T)idx * HeapHandleIncrement;
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE) {
        int idx = (int)((cpu_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        FreeIndices.push_back(idx);
    }
};

static FrameContext g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
static UINT g_frameIndex = 0;
static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE g_fenceEvent = nullptr;
static UINT64 g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static HANDLE g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

static HWND g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**) {
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"myClientImGui", nullptr};
    ::RegisterClassExW(&wc);
    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"Login Required!", WS_OVERLAPPEDWINDOW, 100, 100, (int)(960 * main_scale), (int)(640 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(main_scale);
    io.FontGlobalScale = main_scale;

    ImGui_ImplWin32_Init(g_hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_pd3dDevice;
    init_info.CommandQueue = g_pd3dCommandQueue;
    init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
        g_pd3dSrvDescHeapAlloc.Alloc(out_cpu, out_gpu);
    };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_h, D3D12_GPU_DESCRIPTOR_HANDLE gpu_h) {
        g_pd3dSrvDescHeapAlloc.Free(cpu_h, gpu_h);
    };
    ImGui_ImplDX12_Init(&init_info);

    Sound soundPublic;
    Sound soundPrivate;
    soundPublic.init();
    soundPrivate.init();

    Client client;
    std::string current_username;
    char username_buf[65] = {};
    bool username_readonly = false;
    bool entered_chat = false;
    std::string login_status;
    std::vector<PublicLine> public_lines;
    char public_input_buf[2048] = {};
    std::vector<std::string> current_users;
    std::map<std::string, WindowInfo> private_windows;
    std::vector<std::string> pending_open_private;
    bool was_initializing = false;
    std::string exeDir = getExeDir();
    std::string path_music1 = exeDir + "music1.mp3";
    std::string path_music2 = exeDir + "music2.mp3";
    std::string path_music = exeDir + "music.mp3";
    // todo: exe dir 没音乐文件时静默即可

    ImVec4 clear_color = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(g_hwnd)) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        soundPublic.update();
        soundPrivate.update();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (entered_chat && !client.isConnected()) {
            entered_chat = false;
            private_windows.clear();
            login_status = "Disconnected. Please try again.";
            ::SetWindowTextW(g_hwnd, L"Login Required!");
        }

        if (!entered_chat) {
            if (was_initializing && !client.isInitializing() && !current_username.empty())
                login_status = "Cannot connect to server";
            was_initializing = client.isInitializing();

            /* login UI */
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

            ImGui::Text("Login Required!");
            ImGui::Separator();
            ImGui::InputText("Username", username_buf, sizeof(username_buf), username_readonly ? ImGuiInputTextFlags_ReadOnly : 0);

            bool is_connect_phase = !current_username.empty();
            if (is_connect_phase) {
                if (client.isInitializing()) {
                    ImGui::TextColored(ImVec4(1, 1, 0.6f, 1), "Connecting...");
                } else if (!login_status.empty()) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", login_status.c_str());
                }
                if (ImGui::Button("Connect")) {
                    if (client.isConnected()) {
                        entered_chat = true;
                        login_status.clear();
                        ::SetWindowTextW(g_hwnd, L"Chat Room");
                        soundPublic.playLikeAudiotest(path_music.c_str());
                    } else {
                        client.reset();
                        login_status = "Connecting...";
                        client.initializeAsync(current_username);
                    }
                }
            } else {
                if (ImGui::Button("Confirm")) {
                    current_username = username_buf;
                    while (!current_username.empty() && (current_username.back() == ' ' || current_username.back() == '\t'))
                        current_username.pop_back();
                    while (!current_username.empty() && (current_username.front() == ' ' || current_username.front() == '\t'))
                        current_username.erase(0, 1);
                    if (current_username.empty()) {
                        login_status = "Username cannot be empty";
                    } else {
                        username_readonly = true;
                        login_status.clear();
                        client.initializeAsync(current_username);
                    }
                }
                if (!login_status.empty())
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", login_status.c_str());
            }

            ImGui::End();
        } else {
            if (client.hasNewMessages()) {
                std::vector<std::string> msgs = client.getNewMessages();
                for (const std::string& raw : msgs) {
                    if (raw.size() >= 8 && raw.compare(0, 8, "clients/") == 0) {
                        std::string list = raw.substr(8);
                        std::vector<std::string> users = splitClientsList(list);
                        current_users.clear();
                        for (const std::string& u : users) {
                            if (u != current_username)
                                current_users.push_back(u);
                        }
                    } else if (raw.size() >= 7 && raw.compare(0, 7, "public/") == 0) {
                        std::string data = raw.substr(7);
                        size_t colon = data.find(':');
                        std::string sender = colon != std::string::npos ? data.substr(0, colon) : data;
                        std::string content = colon != std::string::npos ? data.substr(colon + 1) : "";
                        bool isSelf = (sender == current_username);
                        std::string display = data;
                        public_lines.push_back({display, isSelf});
                        if (!isSelf)
                            soundPublic.playLikeAudiotest(path_music2.c_str());
                    } else if (raw.size() >= 8 && raw.compare(0, 8, "private/") == 0) {
                        std::string data = raw.substr(8);
                        size_t colon = data.find(':');
                        std::string sender = colon != std::string::npos ? data.substr(0, colon) : data;
                        if (!sender.empty()) {
                            if (private_windows.find(sender) == private_windows.end()) {
                                WindowInfo wi;
                                wi.title = "Private Chat with " + sender;
                                wi.isOpen = true;
                                private_windows[sender] = std::move(wi);
                            }
                            private_windows[sender].conversations.push_back({data, false});
                            soundPrivate.playLikeAudiotest(path_music1.c_str());
                        }
                    }
                }
            }

            /* main chat room */
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("Chat Room", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

            ImGui::Columns(2, "ChatColumns", true);
            ImGui::SetColumnWidth(0, 220);

            ImGui::Text("Current Users");
            ImGui::Separator();
            for (const std::string& u : current_users) {
                ImGui::Text("%s", u.c_str());
                ImGui::SameLine(150);
                ImGui::PushID(u.c_str());
                if (ImGui::SmallButton("chat")) {
                    pending_open_private.push_back(u);
                }
                ImGui::PopID();
            }

            ImGui::NextColumn();

            ImGui::Text("Chat");
            ImGui::Separator();
            ImGui::BeginChild("PublicChatScroll", ImVec2(0, -120), true);
            for (const auto& pl : public_lines) {
                if (pl.isSelf)
                    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(pl.line.c_str()).x - 20);
                ImGui::TextWrapped("%s", pl.line.c_str());
            }
            ImGui::EndChild();

            ImGui::InputTextMultiline("##PublicInput", public_input_buf, sizeof(public_input_buf), ImVec2(-1, 80));
            if (ImGui::Button("Send")) {
                std::string content = public_input_buf;
                while (!content.empty() && (content.back() == '\r' || content.back() == '\n')) content.pop_back();
                if (!content.empty()) {
                    std::string msg = "public/" + current_username + ": " + content;
                    if (client.sendMessage(msg)) {
                        public_lines.push_back({current_username + ": " + content, true});
                        public_input_buf[0] = '\0';
                    }
                }
            }
            ImGui::End();

            std::string bring_to_front_target;
            for (const std::string& u : pending_open_private) {
                auto it = private_windows.find(u);
                if (it == private_windows.end()) {
                    WindowInfo wi;
                    wi.title = "Private Chat with " + u;
                    wi.isOpen = true;
                    private_windows[u] = std::move(wi);
                } else {
                    it->second.isOpen = true;
                }
                if (bring_to_front_target.empty()) bring_to_front_target = u;
            }
            pending_open_private.clear();

            /* DM windows */
            std::vector<std::string> to_remove_private;
            for (auto& kv : private_windows) {
                const std::string& target = kv.first;
                WindowInfo& wi = kv.second;
                if (!wi.isOpen) continue;
                bool target_online = (std::find(current_users.begin(), current_users.end(), target) != current_users.end());
                std::string display_title = target_online ? wi.title : ("Private Chat with " + target + " (offline)");
                ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
                if (target == bring_to_front_target)
                    ImGui::SetNextWindowFocus();
                ImGui::PushID(target.c_str());
                if (!ImGui::Begin(display_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
                    ImGui::End();
                    ImGui::PopID();
                    continue;
                }
                ImGui::BeginChild("PrivateScroll", ImVec2(0, -80), true);
                for (const auto& pl : wi.conversations) {
                    if (pl.isSelf)
                        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(pl.line.c_str()).x - 20);
                    ImGui::TextWrapped("%s", pl.line.c_str());
                }
                ImGui::EndChild();
                ImGui::InputTextMultiline("##PrivateInput", wi.buffer, sizeof(wi.buffer), ImVec2(-1, 60));
                if (!target_online) ImGui::BeginDisabled();
                if (ImGui::Button("Send")) {
                    std::string content = wi.buffer;
                    if (!content.empty()) {
                        std::string msg = "private/" + target + ": " + content;
                        if (client.sendMessage(msg)) {
                            wi.conversations.push_back({current_username + ": " + content, true});
                            wi.buffer[0] = '\0';
                        }
                    }
                }
                if (!target_online) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Close")) {
                    wi.isOpen = false;
                    to_remove_private.push_back(target);
                }
                ImGui::End();
                ImGui::PopID();
            }
            for (const std::string& k : to_remove_private)
                private_windows.erase(k);
        }

        ImGui::Render();

        FrameContext* frameCtx = WaitForNextFrameContext();
        UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
        frameCtx->CommandAllocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
        g_pd3dCommandList->ResourceBarrier(1, &barrier);

        const float clear_with_alpha[4] = {clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w};
        g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_with_alpha, 0, nullptr);
        g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
        g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_pd3dCommandList->ResourceBarrier(1, &barrier);
        g_pd3dCommandList->Close();

        g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
        g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
        frameCtx->FenceValue = g_fenceLastSignaledValue;

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        g_frameIndex++;
    }

    soundPublic.release();
    soundPrivate.release();
    client.disconnect();

    WaitForPendingOperations();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;
        SIZE_T rtvStep = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++) {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvStep;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK || g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent)
        return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = APP_NUM_BACK_BUFFERS;
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.Stereo = FALSE;

    IDXGIFactory5* dxgiFactory = nullptr;
    IDXGISwapChain1* swapChain1 = nullptr;
    if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
        return false;
    if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
        return false;
    if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
        return false;
    swapChain1->Release();
    dxgiFactory->Release();
    g_pSwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
    g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->SetFullscreenState(FALSE, nullptr);
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_hSwapChainWaitableObject) {
        CloseHandle(g_hSwapChainWaitableObject);
        g_hSwapChainWaitableObject = nullptr;
    }
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) {
            g_frameContext[i].CommandAllocator->Release();
            g_frameContext[i].CommandAllocator = nullptr;
        }
    if (g_pd3dCommandQueue) {
        g_pd3dCommandQueue->Release();
        g_pd3dCommandQueue = nullptr;
    }
    if (g_pd3dCommandList) {
        g_pd3dCommandList->Release();
        g_pd3dCommandList = nullptr;
    }
    if (g_pd3dRtvDescHeap) {
        g_pd3dRtvDescHeap->Release();
        g_pd3dRtvDescHeap = nullptr;
    }
    if (g_pd3dSrvDescHeap) {
        g_pd3dSrvDescHeap->Release();
        g_pd3dSrvDescHeap = nullptr;
    }
    if (g_fence) {
        g_fence->Release();
        g_fence = nullptr;
    }
    if (g_fenceEvent) {
        CloseHandle(g_fenceEvent);
        g_fenceEvent = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void CreateRenderTarget() {
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++) {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
        g_mainRenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget() {
    WaitForPendingOperations();
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++) {
        if (g_mainRenderTargetResource[i]) {
            g_mainRenderTargetResource[i]->Release();
            g_mainRenderTargetResource[i] = nullptr;
        }
    }
}

void WaitForPendingOperations() {
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
    g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
    ::WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameContext() {
    FrameContext* frameCtx = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];
    if (g_fence->GetCompletedValue() < frameCtx->FenceValue) {
        g_fence->SetEventOnCompletion(frameCtx->FenceValue, g_fenceEvent);
        HANDLE waitObjs[] = {g_hSwapChainWaitableObject, g_fenceEvent};
        ::WaitForMultipleObjects(2, waitObjs, TRUE, INFINITE);
    } else {
        ::WaitForSingleObject(g_hSwapChainWaitableObject, INFINITE);
    }
    return frameCtx;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            g_pSwapChain->GetDesc1(&desc);
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
