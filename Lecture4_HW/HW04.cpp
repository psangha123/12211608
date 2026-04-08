#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct VideoConfig
{
    int Width = 800;
    int Height = 600;
    bool IsFullscreen = false;
    bool NeedsResize = false;
    int VSync = 1;
} g_Config;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

ID3D11Buffer* g_pConstantBuffer = nullptr;

struct Vertex
{
    float x, y, z;
    float r, g, b, a;
};

struct CB_Transform
{
    float x;
    float y;
    float padding1;
    float padding2;
};

void RebuildVideoResources(HWND hWnd)
{
    if (!g_pSwapChain) return;

    if (g_pRenderTargetView) {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }

    g_pSwapChain->ResizeBuffers(0, g_Config.Width, g_Config.Height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer == nullptr) return;

    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();

    if (!g_Config.IsFullscreen) {
        RECT rc = { 0, 0, g_Config.Width, g_Config.Height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    }

    g_Config.NeedsResize = false;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

class Component
{
public:
    class GameObject* pOwner = nullptr;
    bool isStarted = false;

    virtual void Start() = 0;
    virtual void Input() {}
    virtual void Update(float dt) = 0;
    virtual void Render() {}
    virtual ~Component() {}
};

class GameObject {
public:
    std::string name;
    float x; // Position X
    float y; // Position Y
    std::vector<Component*> components;

    GameObject(std::string n, float startX = 0.0f, float startY = 0.0f)
    {
        name = n;
        x = startX;
        y = startY;
    }

    ~GameObject() {
        for (int i = 0; i < (int)components.size(); i++) {
            delete components[i];
        }
    }

    void AddComponent(Component* pComp) {
        pComp->pOwner = this;
        pComp->isStarted = false;
        components.push_back(pComp);
    }
};

class PlayerControl : public Component {
public:
    int playerType;
    float speed;
    bool moveUp, moveDown, moveLeft, moveRight;

    PlayerControl(int type) : playerType(type) {}

    void Start() override {
        speed = 1.5f; 
        moveUp = moveDown = moveLeft = moveRight = false;
    }

    void Input() override {
        if (playerType == 0) { // Player 1 (방향키)
            moveUp = (GetAsyncKeyState(VK_UP) & 0x8000);
            moveDown = (GetAsyncKeyState(VK_DOWN) & 0x8000);
            moveLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000);
            moveRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        }
        else if (playerType == 1) { // Player 2 (WASD)
            moveUp = (GetAsyncKeyState('W') & 0x8000);
            moveDown = (GetAsyncKeyState('S') & 0x8000);
            moveLeft = (GetAsyncKeyState('A') & 0x8000);
            moveRight = (GetAsyncKeyState('D') & 0x8000);
        }
    }

    void Update(float dt) override {
        if (moveUp)    pOwner->y += speed * dt;
        if (moveDown)  pOwner->y -= speed * dt;
        if (moveLeft)  pOwner->x -= speed * dt;
        if (moveRight) pOwner->x += speed * dt;
    }
};

class TriangleRenderer : public Component {
private:
    float r, g, b;
    ID3D11Buffer* pVertexBuffer = nullptr;

public:
    TriangleRenderer(float r, float g, float b) : r(r), g(g), b(b) {}

    void Start() override {
        Vertex vertices[] = {
            {  0.0f,  0.1f, 0.5f, r, g, b, 1.0f },
            {  0.1f, -0.1f, 0.5f, r, g, b, 1.0f },
            { -0.1f, -0.1f, 0.5f, r, g, b, 1.0f },
        };
        D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA initData = { vertices, 0, 0 };
        g_pd3dDevice->CreateBuffer(&bd, &initData, &pVertexBuffer);
    }

    void Update(float dt) override {}

    void Render() override {
        if (!pVertexBuffer) return;

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        g_pImmediateContext->Map(g_pConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        CB_Transform* cb = (CB_Transform*)mappedResource.pData;
        cb->x = pOwner->x;
        cb->y = pOwner->y;
        g_pImmediateContext->Unmap(g_pConstantBuffer, 0);

        UINT stride = sizeof(Vertex), offset = 0;
        g_pImmediateContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
        g_pImmediateContext->VSSetConstantBuffers(0, 1, &g_pConstantBuffer);
        g_pImmediateContext->Draw(3, 0);
    }

    ~TriangleRenderer() {
        if (pVertexBuffer) pVertexBuffer->Release();
    }
};

class GameLoop
{
public:
    bool isRunning;
    std::vector<GameObject*> gameWorld;
    std::chrono::high_resolution_clock::time_point prevTime;
    float deltaTime;

    void Initialize() {
        isRunning = true;
        gameWorld.clear();
        prevTime = std::chrono::high_resolution_clock::now();
        deltaTime = 0.0f;
    }

    void Input() {
        for (auto* obj : gameWorld)
            for (auto* comp : obj->components)
                comp->Input();
    }

    void Update() {
        for (auto* obj : gameWorld) {
            for (auto* comp : obj->components) {
                if (!comp->isStarted) {
                    comp->Start();
                    comp->isStarted = true;
                }
                comp->Update(deltaTime);
            }
        }
    }

    void Render() {
        for (auto* obj : gameWorld)
            for (auto* comp : obj->components)
                comp->Render();
    }

    ~GameLoop() {
        for (auto* obj : gameWorld) delete obj;
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"DX11VideoClass";
    RegisterClassExW(&wcex);

    RECT rc = { 0, 0, g_Config.Width, g_Config.Height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowW(L"DX11VideoClass", L"DirectX 11 Component Engine (ESC: Exit, F: Fullscreen)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);
    ShowWindow(hWnd, nCmdShow);

    // DX11 초기화
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_Config.Width;
    sd.BufferDesc.Height = g_Config.Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext);

    RebuildVideoResources(hWnd);

    const char* shaderSource = R"(
        cbuffer TransformBuffer : register(b0) {
            float2 offset;
            float2 padding;
        };

        struct VS_IN { float3 pos : POSITION; float4 col : COLOR; };
        struct PS_IN { float4 pos : SV_POSITION; float4 col : COLOR; };
        
        PS_IN VS(VS_IN input) { 
            PS_IN output; 
            // 전달받은 offset(GameObject의 x, y)을 더해 이동을 구현
            output.pos = float4(input.pos.x + offset.x, input.pos.y + offset.y, input.pos.z, 1.0f); 
            output.col = input.col; 
            return output; 
        }
        
        float4 PS(PS_IN input) : SV_Target { return input.col; }
    )";

    ID3DBlob* vsBlob, * psBlob;
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, nullptr);
    g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pPixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pInputLayout);
    vsBlob->Release(); psBlob->Release();

    // Constant Buffer 생성 (동적 업데이트)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(CB_Transform);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&cbDesc, nullptr, &g_pConstantBuffer);

    GameLoop gLoop;
    gLoop.Initialize();

    // 1. 플레이어 1 (방향키 조작, 빨간색) 생성
    GameObject* player1 = new GameObject("Player1", 0.5f, 0.0f);
    player1->AddComponent(new TriangleRenderer(1.0f, 0.3f, 0.3f));
    player1->AddComponent(new PlayerControl(0));
    gLoop.gameWorld.push_back(player1);

    // 2. 플레이어 2 (WASD 조작, 파란색) 생성
    GameObject* player2 = new GameObject("Player2", -0.5f, 0.0f);
    player2->AddComponent(new TriangleRenderer(0.3f, 0.3f, 1.0f));
    player2->AddComponent(new PlayerControl(1));
    gLoop.gameWorld.push_back(player2);

    MSG msg = { 0 };
    while (WM_QUIT != msg.message && gLoop.isRunning) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                gLoop.isRunning = false;
                continue;
            }
            if (GetAsyncKeyState('F') & 0x0001) {
                g_Config.IsFullscreen = !g_Config.IsFullscreen;
                g_pSwapChain->SetFullscreenState(g_Config.IsFullscreen, nullptr);
            }
            if (GetAsyncKeyState('1') & 0x0001) { g_Config.Width = 800; g_Config.Height = 600; g_Config.NeedsResize = true; }
            if (GetAsyncKeyState('2') & 0x0001) { g_Config.Width = 1280; g_Config.Height = 720; g_Config.NeedsResize = true; }

            if (g_Config.NeedsResize) RebuildVideoResources(hWnd);

            auto currentTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> elapsed = currentTime - gLoop.prevTime;
            gLoop.deltaTime = elapsed.count();
            gLoop.prevTime = currentTime;

            gLoop.Input();
            gLoop.Update();

            float clearColor[] = { 0.1f, 0.2f, 0.3f, 1.0f };
            g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

            D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)g_Config.Width, (float)g_Config.Height, 0.0f, 1.0f };
            g_pImmediateContext->RSSetViewports(1, &vp);
            g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

            g_pImmediateContext->IASetInputLayout(g_pInputLayout);
            g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_pImmediateContext->VSSetShader(g_pVertexShader, nullptr, 0);
            g_pImmediateContext->PSSetShader(g_pPixelShader, nullptr, 0);

            gLoop.Render();

            g_pSwapChain->Present(g_Config.VSync, 0);
        }
    }

    if (g_pConstantBuffer) g_pConstantBuffer->Release();
    if (g_pInputLayout) g_pInputLayout->Release();
    if (g_pVertexShader) g_pVertexShader->Release();
    if (g_pPixelShader) g_pPixelShader->Release();
    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pImmediateContext) g_pImmediateContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();

    return (int)msg.wParam;
}