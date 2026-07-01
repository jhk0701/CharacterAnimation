# DxMiniEngine 프로젝트 구조 및 작동 흐름

> Microsoft DirectX-Graphics-Samples 기반 DirectX 12 MiniEngine 분석 문서

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [전체 디렉토리 구조](#2-전체-디렉토리-구조)
3. [레이어 아키텍처](#3-레이어-아키텍처)
4. [초기화 흐름](#4-초기화-흐름)
5. [프레임 루프](#5-프레임-루프)
6. [렌더링 파이프라인 상세](#6-렌더링-파이프라인-상세)
7. [핵심 서브시스템](#7-핵심-서브시스템)
8. [모델 시스템](#8-모델-시스템)
9. [빌드 시스템](#9-빌드-시스템)
10. [Samples 목록](#10-samples-목록)
11. [새 앱 생성 방법](#11-새-앱-생성-방법)

---

## 1. 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 원본 | [Microsoft/DirectX-Graphics-Samples](https://github.com/microsoft/DirectX-Graphics-Samples) (MIT) |
| 목적 | DirectX 12 MiniEngine 학습 및 연구용 포크 |
| 플랫폼 | Windows 10 (2004 이상), x64 전용 |
| 개발환경 | Visual Studio 2019+, Windows 10 SDK 19041 |
| 브랜치 전략 | `main` (안정), `feat-Framework` (개발) |

MiniEngine은 DX12 앱을 빠르게 시작할 수 있도록 Team Minigraph(Microsoft)가 설계한 **엔진 스타터 킷**이다. `Startup()`, `Update()`, `RenderScene()` 세 함수만 구현하면 렌더링 앱을 만들 수 있도록 설계돼 있다.

---

## 2. 전체 디렉토리 구조

```
project-DxMiniEngine/
│
├── MiniEngine/                  ← 엔진 핵심
│   ├── Core/                    ← D3D12 래퍼, GPU 리소스, 후처리 (정적 라이브러리)
│   ├── Model/                   ← 모델 로딩(glTF 2.0), 렌더러, 라이팅
│   ├── ModelViewer/             ← 메인 실행 앱 (IGameApp 구현체)
│   ├── ModelConverter/          ← 외부 포맷 → H3D 변환 도구 (ASSIMP 3.0 필요)
│   ├── PropertySheets/          ← MSBuild 공통 설정 (.props)
│   │   ├── Build.props          ← 출력 경로, 셰이더 컴파일, 최적화 플래그
│   │   └── Desktop.props        ← 데스크톱 SDK 경로, SM6.2
│   ├── Tools/
│   │   └── Scripts/             ← 새 프로젝트 생성 Python 스크립트
│   │       └── ProjectTemplates/← 앱/라이브러리 VS 프로젝트 템플릿
│   └── NuGet.Config             ← 패키지 저장소: ../Packages/
│
├── Samples/
│   ├── Desktop/                 ← DX12 기능별 독립 데모 (~30종)
│   └── UWP/                     ← 동일 데모의 UWP 빌드
│
├── Libraries/
│   ├── D3DX12/                  ← d3dx12.h (Microsoft DX12 헬퍼 헤더)
│   ├── D3D12RaytracingFallback/ ← DXR 미지원 GPU용 소프트웨어 BVH 구현
│   ├── D3DX12AffinityLayer/     ← 멀티 GPU(Linked Adapter) 추상화
│   └── D3DX12Residency/         ← GPU 메모리 거주(Residency) 관리
│
├── TechniqueDemos/              ← 기법 데모 (D3D12MemoryManagement 등)
├── Tools/                       ← DXGI 어댑터 테스트 도구
├── Assets/                      ← 저장소 공용 이미지 에셋
├── Packages/                    ← NuGet 패키지 (git 추적 제외)
│
├── CLAUDE.md                    ← Claude Code 가이드
├── README.md                    ← 원본 Microsoft 설명
└── docs/
    └── ProjectStructure.md      ← 이 파일
```

---

## 3. 레이어 아키텍처

```
┌──────────────────────────────────────────────────────────┐
│              Application Layer                            │
│  ModelViewer — IGameApp 구현, CREATE_APPLICATION 매크로  │
└──────────────────┬───────────────────────────────────────┘
                   │ 사용
┌──────────────────┴───────────────────────────────────────┐
│              Model Layer              │  Post FX Layer    │
│  Renderer   MeshSorter  ModelLoader   │  PostEffects      │
│  glTF 2.0   LightManager Animation   │  SSAO  MotionBlur │
│  (MiniEngine/Model/)                  │  FXAA  DepthOfField│
└──────────────────┬───────────────────┴───────────────────┘
                   │ 사용
┌──────────────────┴───────────────────────────────────────┐
│              Core Layer  (MiniEngine/Core/)               │
│                                                          │
│  GraphicsCore      CommandContext    PipelineState        │
│  CommandListManager  RootSignature   DescriptorHeap       │
│  BufferManager     GpuResource 계층  LinearAllocator      │
│  Display (SwapChain)  EngineTuning   TextRenderer         │
└──────────────────┬───────────────────────────────────────┘
                   │ D3D12 API 호출
┌──────────────────┴───────────────────────────────────────┐
│                   GPU Hardware                            │
└──────────────────────────────────────────────────────────┘
```

**의존 관계**
- `ModelViewer.exe` → `Model.lib` → `Core.lib`
- `Core.lib`에 150개 이상의 HLSL 셰이더가 C++ 헤더로 임베딩됨

---

## 4. 초기화 흐름

```
wWinMain (ModelViewer.cpp)
  │
  └─ GameCore::RunApplication(ModelViewer app, ...)
       │
       ├─ [1] Windows 초기화
       │    XMVerifyCPUSupport()          ← SIMD(SSE/AVX) 확인
       │    RegisterClassEx()             ← 윈도우 클래스 등록
       │    CreateWindow(1920×1080)       ← 윈도우 생성
       │
       ├─ [2] Graphics::Initialize()
       │    ├─ D3D12GetDebugInterface()   ← Debug 레이어 (Debug 빌드)
       │    ├─ CreateDXGIFactory2()       ← DXGI 팩토리
       │    ├─ EnumAdapters1()            ← GPU 열거 (최대 VRAM 선택)
       │    ├─ D3D12CreateDevice()        ← D3D12 디바이스 생성
       │    │    폴백: WARP 소프트웨어 어댑터
       │    ├─ CheckFeatureSupport()      ← UAV Load 지원 여부 확인
       │    ├─ g_CommandManager.Create()  ← 커맨드 큐 3개 생성
       │    │    Graphics / Compute / Copy
       │    ├─ InitializeCommonState()    ← 공용 RootSignature, Sampler
       │    ├─ Display::Initialize()      ← 스왑체인 생성 (3중 버퍼)
       │    ├─ GpuTimeManager::Initialize()
       │    ├─ TemporalEffects::Initialize()
       │    ├─ PostEffects::Initialize()  ← ToneMap, Bloom, FXAA PSO
       │    ├─ SSAO::Initialize()
       │    ├─ TextRenderer::Initialize()
       │    └─ ParticleEffectManager::Initialize()
       │
       ├─ [3] SystemTime / GameInput / EngineTuning 초기화
       │
       └─ [4] app.Startup()   (ModelViewer::Startup)
            ├─ MotionBlur::Enable = true
            ├─ TemporalEffects::EnableTAA = true
            ├─ PostEffects::EnableHDR = true
            ├─ Renderer::Initialize()     ← RootSignature, PSO 생성
            ├─ LoadIBLTextures()          ← 환경맵 큐브맵 로드
            ├─ Renderer::LoadModel(path)  ← glTF 또는 H3D 모델 로드
            └─ 카메라 컨트롤러 설정
```

---

## 5. 프레임 루프

`GameCore::UpdateApplication()` 이 매 프레임 호출된다.

```
UpdateApplication(app)
  │
  ├─ ① 메시지 처리
  │    PeekMessage() → WM_SIZE 등 윈도우 이벤트 처리
  │
  ├─ ② 입력 & 시간
  │    EngineProfiling::Update()
  │    float dt = Graphics::GetFrameTime()   ← 이전 프레임 델타
  │    GameInput::Update(dt)
  │    EngineTuning::Update(dt)
  │
  ├─ ③ 게임 로직
  │    app.Update(dt)
  │    ├─ CameraController::Update(dt)      ← 입력 → 카메라 변환
  │    ├─ GraphicsContext::Begin()
  │    │    ModelInstance::Update()          ← 애니메이션 평가 + GPU 버퍼 갱신
  │    └─ GraphicsContext::Finish()
  │
  ├─ ④ 씬 렌더링
  │    app.RenderScene()                     ← 섹션 6 참조
  │
  ├─ ⑤ 포스트 프로세싱 (ComputeContext)
  │    PostEffects::Render()
  │    ├─ GenerateBloom()                    ← 블룸 피라미드 생성 (5단계)
  │    ├─ ToneMapHDRCS / ToneMapCS           ← HDR → LDR 톤매핑
  │    ├─ UpdateExposure()                   ← 히스토그램 기반 자동 노출
  │    └─ FXAA::Render()                     ← 안티앨리어싱
  │
  ├─ ⑥ UI 오버레이 (GraphicsContext)
  │    GraphicsContext → g_OverlayBuffer
  │    ├─ app.RenderUI()                     ← 사용자 UI
  │    └─ EngineTuning::Display()            ← 디버그 메뉴/텍스트
  │
  └─ ⑦ 화면 출력
       Display::Present()
       ├─ PreparePresentSDR() 또는 PreparePresentHDR()
       │    └─ g_SceneColorBuffer + g_OverlayBuffer → g_DisplayPlane[i]
       │         (스케일링 필요 시 BicubicUpsampling 적용)
       ├─ SwapChain::Present(VSync ? interval : 0)
       └─ g_CurrentBuffer = (g_CurrentBuffer + 1) % 3
```

---

## 6. 렌더링 파이프라인 상세

`ModelViewer::RenderScene()` 내부의 단계별 흐름.

### Pass 1. Z-Pass (깊이 프리패스)

```cpp
MeshSorter sorter(MeshSorter::kDefault);
sorter.SetCamera(m_Camera);
sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
m_ModelInst.Render(sorter);   // 모든 메시를 sorter에 등록
sorter.Sort();                // 정렬 키: [passID][psoIdx][distance][objIdx]
sorter.RenderMeshes(kZPass, gfxContext, globals);
```

- 사용 셰이더: `DepthOnlyVS`, `CutoutDepthVS/PS`(알파 테스트), `DepthOnlySkinVS`(스킨)
- 이후 메인 패스에서 `Depth-Test-Equal`로 과다 셰이딩 방지

### Pass 2. SSAO

```
SSAO::Render(gfxContext, m_Camera)
  → AoPrepareDepthBuffers (계층적 깊이 다운샘플 4단계)
  → AoRender1CS / AoRender2CS (인터리브드 샘플링)
  → AoBlurUpsampleCS (업샘플링 + 스무딩)
  → g_SSAOFullScreen (R8_UNORM) 완성
```

### Pass 3. 섀도우맵 (태양광)

```cpp
MeshSorter shadowSorter(MeshSorter::kShadows);
shadowSorter.SetCamera(m_SunShadowCamera);
m_ModelInst.Render(shadowSorter);
shadowSorter.RenderMeshes(kZPass, gfxContext, globals);
// → g_ShadowBuffer에 기록
```

### Pass 4. 컬러 렌더 (메인)

```
gfxContext.ClearColor(g_SceneColorBuffer)
gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly())

sorter.RenderMeshes(kOpaque, ...)
  for each mesh (정렬된 순서):
    SetPipelineState(colorPSO)          ← DepthTestEqual
    SetConstantBuffer(kMeshConstants)   ← 월드 매트릭스
    SetConstantBuffer(kMaterialConstants) ← 재질 파라미터
    SetDescriptorTable(kMaterialSRVs)   ← 5개 텍스처 (Diffuse/MR/Occlusion/Emissive/Normal)
    SetDescriptorTable(kCommonSRVs)     ← IBL 큐브맵, SSAO, 섀도우맵
    DrawIndexed(...)
      → DefaultPS: PBR + IBL + 클러스터드 포워드+ 라이팅

Renderer::DrawSkybox(...)
  → SkyboxVS/PS, IBL Specular 큐브맵

sorter.RenderMeshes(kTransparent, ...)
  → 역순 정렬 (먼쪽부터), 알파 블렌딩 활성화
```

### Pass 5. 후처리 (씬 렌더 컨텍스트 내)

```
MotionBlur::GenerateCameraVelocityBuffer()  ← 깊이 → 카메라 속도 맵
TemporalEffects::ResolveImage()             ← TAA (Temporal Anti-Aliasing)
ParticleEffectManager::Render()             ← 파티클
DepthOfField::Render() 또는 MotionBlur::RenderObjectBlur()
```

---

## 7. 핵심 서브시스템

### 7-1. GPU 리소스 계층

```
GpuResource  (ID3D12Resource 래퍼 + 리소스 상태 추적)
├── GpuBuffer           — 정점/인덱스/구조체/바이트어드레스 버퍼
├── PixelBuffer          — 텍스처 기반 리소스의 기반 클래스
│   ├── ColorBuffer     — 렌더 타겟 (RTV) / UAV 텍스처
│   ├── DepthBuffer     — 깊이 스텐실 (DSV)
│   └── ShadowBuffer    — 섀도우맵 (DSV + SRV)
├── UploadBuffer        — CPU→GPU 스테이징 (Write-Combined 메모리)
└── ReadbackBuffer      — GPU→CPU 읽기 (Read-Back 힙)
```

`GpuResource`는 현재 리소스 상태(`m_UsageState`)를 추적하므로, `CommandContext::TransitionResource()`가 자동으로 올바른 배리어를 삽입한다.

### 7-2. CommandContext 시스템

```
CommandContext (기반)
├── GraphicsContext   — Draw, SetRenderTarget, ClearColor 등
└── ComputeContext    — Dispatch, ClearUAV 등

ContextManager        — 스레드 안전 풀 (std::lock_guard)
└── sm_ContextPool[4] — 타입별 컨텍스트 풀
```

**사용 패턴**
```cpp
GraphicsContext& ctx = GraphicsContext::Begin(L"Scene Render");
// ... 커맨드 기록 ...
ctx.Finish();   // GPU 제출 → 풀 반환
```

**내부 동작**
- 배리어를 최대 16개 버퍼링 후 자동 플러시 (`FlushResourceBarriers`)
- `Finish()` 시 LinearAllocator 정리 + DynamicDescriptorHeap 정리
- 펜스 값 상위 56비트에 큐 타입 인코딩 → GPU 완료 확인 오버헤드 최소화

### 7-3. 셰이더 임베딩 메커니즘

```
MiniEngine/Core/Shaders/*.hlsl
        │
        │ [MSBuild FxCompile 작업]
        │  - VariableName: g_p%(Filename)
        │  - ShaderModel: 6.2 (Desktop.props)
        │  - Debug 빌드: -Qembed_debug
        ▼
../Build/x64/<Config>/Output/Core/CompiledShaders/
    AdaptExposureCS.h        ← unsigned char g_pAdaptExposureCS[] = { ... };
    BicubicUpsampleCS.h
    ... (150개 이상)

        │
        │ [C++ #include]
        ▼
PostEffects.cpp, SSAO.cpp, MotionBlur.cpp ...
    #include "CompiledShaders/AdaptExposureCS.h"
    psoDesc.CS = { g_pAdaptExposureCS, sizeof(g_pAdaptExposureCS) };
```

→ 런타임에 파일 로드 없이 모든 셰이더가 실행파일에 정적 임베딩됨.

### 7-4. EngineTuning — 런타임 조정 변수

| 타입 | 용도 | 예시 |
|------|------|------|
| `NumVar` | 선형 값 | `NumVar sunInc("Viewer/Sun/Inclination", 0.75f, 0, 1, 0.01f)` |
| `BoolVar` | ON/OFF 토글 | `BoolVar enableBloom("PostFX/Bloom/Enable", true)` |
| `ExpVar` | 지수 스케일 값 | `ExpVar exposure("PostFX/Exposure", 0, -8, 8, 0.25f)` |
| `DynamicEnumVar` | 동적 선택지 | `DynamicEnumVar iblSet("Viewer/IBL/Set", callback)` |

**디버그 메뉴 접근**: 게임 실행 중 `Backspace` 키 또는 컨트롤러 Back 버튼

### 7-5. 주요 프레임 버퍼

| 버퍼 | 포맷 | 용도 |
|------|------|------|
| `g_SceneColorBuffer` | R11G11B10_FLOAT | 메인 HDR 색상 렌더 타겟 |
| `g_SceneDepthBuffer` | D32_FLOAT_S8_UINT | 깊이 + 스텐실 |
| `g_SceneNormalBuffer` | R16G16B16A16_FLOAT | 월드 법선 맵 |
| `g_VelocityBuffer` | R10G10B10A2 | 모션 벡터 (TAA, MotionBlur용) |
| `g_OverlayBuffer` | R8G8B8A8_UNORM | UI/HUD LDR 오버레이 |
| `g_ShadowBuffer` | — | 태양광 섀도우맵 |
| `g_SSAOFullScreen` | R8_UNORM | SSAO 결과 (풀 해상도) |
| `g_aBloomUAV1~5[2]` | R11G11B10_FLOAT | 블룸 피라미드 (5단계, 각 2버퍼) |
| `g_TemporalColor[2]` | R11G11B10_FLOAT | TAA 누적 버퍼 (핑퐁) |
| `g_DisplayPlane[3]` | R10G10B10A2_UNORM | 스왑체인 백버퍼 (3중) |

### 7-6. CommandListManager — 3 큐 구조

```
CommandListManager
├── m_GraphicsQueue  (D3D12_COMMAND_LIST_TYPE_DIRECT)
│    └─ CommandAllocatorPool + Fence
├── m_ComputeQueue   (D3D12_COMMAND_LIST_TYPE_COMPUTE)
│    └─ CommandAllocatorPool + Fence
└── m_CopyQueue      (D3D12_COMMAND_LIST_TYPE_COPY)
     └─ CommandAllocatorPool + Fence

펜스 값 인코딩:
  Graphics: 0x0000_0000_0000_0001 ~
  Compute:  0x0100_0000_0000_0001 ~
  Copy:     0x0200_0000_0000_0001 ~
```

---

## 8. 모델 시스템

### 모델 로딩 파이프라인

```
Renderer::LoadModel(filePath, forceRebuild)
  │
  ├─ .mini 캐시 파일 존재 & 최신인지 확인
  │    (CURRENT_MINI_FILE_VERSION = 13)
  │
  ├─ [캐시 미스] glTF::Asset::Load(filePath)
  │    ├─ JSON 파싱 (nlohmann/json)
  │    ├─ ProcessNodes()      → GraphNode 계층 구성
  │    ├─ ProcessMeshes()     → Mesh + Draw 구조 컴파일
  │    ├─ ProcessSkins()      → 스켈레톤 & InverseBindMatrix
  │    └─ ProcessAnimations() → AnimationCurve 배열 생성
  │         (보간: Linear / Step / CatmullRom / CubicSpline)
  │
  ├─ .mini 파일로 직렬화 저장 (차후 빠른 로드)
  │
  └─ ModelInstance 생성
       ├─ MeshConstantsGPU 버퍼 할당
       ├─ 스켈레톤 조인트 배열 (GPU 업로드 버퍼)
       └─ 애니메이션 상태 초기화
```

### 씬 그래프 구조

```cpp
struct GraphNode {        // 96 bytes
    Matrix4  xform;       // 로컬 변환 행렬
    Quaternion rotation;  // 분리 저장 (애니메이션용)
    XMFLOAT3 scale;
    uint32_t matrixIdx  : 28;
    uint32_t hasSibling : 1;
    uint32_t hasChildren : 1;
    uint32_t staleMatrix : 1;
    uint32_t skeletonRoot : 1;
};
```

### MeshSorter & PSO 선택

```
메시 로드 시점에 psoFlags 결정:
  정점 속성: kHasPosition | kHasNormal | kHasTangent | kHasUV0 | kHasUV1
  스키닝:    kHasSkin
  재질:      kAlphaBlend | kAlphaTest | kTwoSided

→ Renderer::GetPSO(psoFlags)
     해당 조합의 PSO가 없으면 새로 생성 후 캐시
```

**Root Signature 바인딩**

| 슬롯 | 이름 | 내용 |
|------|------|------|
| 0 | kMeshConstants | 월드 매트릭스 (CBV) |
| 1 | kMaterialConstants | 베이스컬러, 메탈릭, 러프니스 (CBV) |
| 2 | kMaterialSRVs | 5개 텍스처 (Diffuse/MetalRoughness/Occlusion/Emissive/Normal) |
| 3 | kMaterialSamplers | 5개 샘플러 |
| 4 | kCommonSRVs | IBL 큐브맵, SSAO, 섀도우맵 |
| 5 | kCommonCBV | GlobalConstants (VP 행렬, 카메라, 태양광) |
| 6 | kSkinMatrices | 조인트 행렬 배열 (SRV) |

### 라이팅: 클러스터드 포워드+

```
Lighting::FillLightGrid()
  → FillLightGridCS_(8|16|24|32).hlsl  ← 타일 크기별 변형
  → 카메라 뷰프러스텀 기준 각 타일과 교차하는 라이트 목록 저장
  → g_LightGrid (ByteAddressBuffer)
  → g_LightBuffer (StructuredBuffer<LightData>)
```

IBL 환경맵: `diffuseIBL.dds` + `specularIBL.dds` (큐브맵 10×2개 기본 제공)

### Math 라이브러리

`MiniEngine/Core/Math/` 아래 SIMD(XMVECTOR) 기반 수학 라이브러리:

```
Vector3 / Vector4      — SIMD 래퍼
Matrix3 / Matrix4      — 행렬 연산
Quaternion             — 구면 선형 보간
AffineTransform        — 회전 + 이동
OrthogonalTransform    — 정규직교 변환
Frustum                — 시야 절두체 컬링
BoundingSphere         — 바운딩 스피어 교차
AxisAlignedBox         — AABB
```

---

## 9. 빌드 시스템

### 구성 (Configuration)

| 구성 | 전처리기 매크로 | 특징 |
|------|----------------|------|
| **Debug** | `_DEBUG` | D3D12 검증 레이어, 최적화 OFF, 셰이더 디버그 정보 포함 |
| **Profile** | `NDEBUG;PROFILE` | 최적화 ON, PIX 이벤트 지원, 함수 레벨 링킹 |
| **Release** | `NDEBUG;RELEASE` | 전체 프로그램 최적화(WPO), COMDAT 폴딩 |

### 출력 구조

```
../Build/
└── x64/
    ├── Debug/
    │   ├── Output/
    │   │   ├── Core/
    │   │   │   ├── CompiledShaders/     ← 셰이더 헤더 ~150개
    │   │   │   └── Core.lib
    │   │   ├── Model/
    │   │   │   └── Model.lib
    │   │   └── ModelViewer/
    │   │       └── ModelViewer.exe
    │   └── Intermediate/                ← *.obj 파일
    ├── Profile/
    └── Release/
```

### 프로젝트 의존성

```
ModelViewer.vcxproj
  ├─→ Core.vcxproj      (정적 라이브러리)
  └─→ Model.vcxproj     (정적 라이브러리)
       └─→ Core.vcxproj
```

### NuGet 패키지

`../Packages/` 디렉토리에 설치됨 (`NuGet.Config` 기준)

| 패키지 | 용도 |
|--------|------|
| `zlib-msvc-x64.1.2.11.8900` | ZLib 압축 (모델 로딩) |
| `directxtex_desktop_win10.2021.1.10.2` | 텍스처 처리 (mip 생성, BC 압축) |
| `directxmesh_desktop_win10.2021.1.10.1` | 메시 처리 (탄젠트 생성 등) |
| `WinPixEventRuntime.1.0.210209001` | GPU 디버깅 (PIX 이벤트) |

### Core 셰이더 카테고리

| 카테고리 | 파일 수 | 주요 파일 |
|----------|---------|-----------|
| SSAO | 9 | AoPrepareDepthBuffers, AoRender, AoBlurUpsample |
| Depth of Field | 21 | DoFPass1/2, DoFTilePass, DoFCombine, DoFPreFilter |
| FXAA | 15 | FXAAPass1/2H/V, FXAAResolveWorkQueue |
| 업샘플링 | 16 | BicubicUpsampling(Fast16/24/32), Lanczos |
| 블룸/노출 | 7 | BloomExtractAndDownsample, UpsampleAndBlur, AdaptExposure |
| 파티클 | 24 | ParticleSpawn/Update/Cull/TileRender/Sort |
| 정렬 | 8 | Bitonic32/64 (Inner/Outer/PreSort) |
| 모션 블러 | 6 | CameraMotionBlurPrePass, CameraVelocity, MotionBlurFinalPass |
| TAA | 3 | TemporalBlend, ResolveTA, SharpenTAA |
| HDR/톤매핑 | 5 | ToneMapCS/HDR, CompositeHDR |
| Present | 7 | CompositeSDR, ScaleAndComposite, BlendUIHDR, PresentHDR/SDR |
| 텍스트/UI | 6 | TextVS/PS, PerfGraphVS/PS |

---

## 10. Samples 목록

`Samples/Desktop/` 아래 각각 독립된 `.sln` 파일을 가진 데모들.

### 기초

| 샘플 | 주제 |
|------|------|
| D3D12HelloWorld | 기본 창, 디바이스 생성, 삼각형 렌더 |
| D3D12Bundles | Bundle을 이용한 드로우콜 최적화 |
| D3D12Fullscreen | 전체화면 전환 처리 |

### 고급 렌더링

| 샘플 | 주제 |
|------|------|
| D3D12MeshShaders | Amplification + Mesh Shader 파이프라인, Meshlet 컬링/인스턴싱/동적 LOD |
| D3D12VariableRateShading | VRS (가변 레이트 셰이딩) |
| D3D12Raytracing | DXR — HelloWorld, SimpleLighting, ProceduralGeometry, RTAO |
| D3D12HDR | 10비트/HDR10 디스플레이 출력 |

### 최적화

| 샘플 | 주제 |
|------|------|
| D3D12ExecuteIndirect | GPU 생성 드로우콜 (Compute + ExecuteIndirect) |
| D3D12DynamicIndexing | 동적 리소스 인덱싱 |
| D3D12Multithreading | 멀티스레드 CommandList 기록 |
| D3D12PipelineStateCache | PSO 직렬화/역직렬화 캐시 |
| D3D12Residency | GPU 메모리 거주(Residency) 관리 |
| D3D12ReservedResources | 타일 리소스 (Sparse Texture) |
| D3D12nBodyGravity | N-Body 시뮬레이션 (Compute Shader) |

### 멀티 GPU

| 샘플 | 주제 |
|------|------|
| D3D12LinkedGpus | 링크드 어댑터 AFR (Affinity Layer 사용) |
| D3D12HeterogeneousMultiadapter | 이질 멀티 어댑터 (iGPU + dGPU) |
| D3D12xGPU | 크로스-GPU 작업 |

### 하위 호환성

| 샘플 | 주제 |
|------|------|
| D3D12On7 | Windows 7에서 DX12 API 사용 |
| D3D1211On12 | 기존 D3D11 코드를 D3D12 위에서 실행 |
| D3D12SM6WaveIntrinsics | Shader Model 6 Wave Intrinsics |

---

## 11. 새 앱 생성 방법

`MiniEngine/` 디렉토리 기준 Python 3 필요.

```batch
:: 새 앱 프로젝트 생성
cd MiniEngine
python.exe Tools\Scripts\CreateNewProject.py APP <프로젝트명>

:: 단축 배치 파일
CreateNewSolution.bat <프로젝트명>
```

생성되는 파일:
- `<프로젝트명>/<프로젝트명>.sln`
- `<프로젝트명>/<프로젝트명>.vcxproj`
- `<프로젝트명>/Main.cpp` — `IGameApp` 구현 템플릿 + `CREATE_APPLICATION` 매크로

**IGameApp 최소 구현**

```cpp
class MyApp : public GameCore::IGameApp
{
public:
    void Startup()  override { /* 리소스 초기화 */ }
    void Cleanup()  override { /* 리소스 정리 */ }
    void Update(float dt) override { /* 입력/로직 */ }
    void RenderScene() override
    {
        GraphicsContext& ctx = GraphicsContext::Begin(L"Render");
        // ... 렌더링 커맨드 ...
        ctx.Finish();
    }
    // void RenderUI(GraphicsContext&) override { } // 선택사항
};
CREATE_APPLICATION(MyApp)
```

---

## 12. Character 프로젝트 — 자체 FBX 직접 렌더 경로

`MiniEngine/Character/`는 Model 프로젝트의 로딩/렌더 파이프라인(`Renderer::LoadModel`,
`ModelData`, `.mini` 캐시, `MeshSorter`)을 사용하지 않고, **FBX SDK로 직접 파싱한 뒤 Core의
그래픽스 래퍼만으로 직접 렌더링**하는 독립 경로를 갖는다. (2026-07-02 기준, `Assets/Capoeira.fbx`
스켈레탈 메시를 로드해 **CPU 스키닝으로 스켈레톤 애니메이션을 재생**. 흰색 diffuse.)

### 애니메이션 / CPU 스키닝

- `Capoeira.fbx`(Mixamo "with skin")는 메시+스켈레톤+스킨+애니메이션을 모두 포함해 단일 파일로 로드.
- `FbxModel::Load`: 스킨 클러스터에서 본/가중치/바인드행렬 추출. **본에 애니메이션 커브가 연결된
  스택**을 선택(빈 `Take 001` 스택 회피, 실제 `mixamo.com` 사용). FbxScene을 보관해 런타임에
  `node->EvaluateGlobalTransform(t)`로 본 글로벌을 평가.
- **곱 순서 모호성 회피**: 검증된 `TransformPoint`(v*M 행벡터) 중첩으로 컨트롤포인트별 **본-로컬
  바인드 위치/노멀**(`cp·M·L^-1`)을 로드 시 미리 계산. 런타임 `FbxModel::Update`는
  `Σ w·TransformPoint(G_b(t), localPos)`(LBS)만 수행 후 Z 반전.
- `FbxRenderer::Render`: 매 프레임 `GraphicsContext::SetDynamicVB`로 스킨드 정점 업로드 +
  `DrawInstanced`(비인덱스). 정점 포맷(pos+normal)/셰이더/RootSignature는 정적 렌더와 동일.
- `Main.cpp::Update`가 `m_FbxModel.Update(deltaT)`로 애니메이션을 진행(구간 루프).

### 구성 파일

| 파일 | 역할 |
|------|------|
| `FbxModel.{h,cpp}` | FBX SDK 파싱 → 스킨/스켈레톤/애니 추출, 본-로컬 바인드 미리 계산. `Update(dt)`로 매 프레임 CPU LBS 스키닝(pimpl, 씬 보관) |
| `FbxRenderer.{h,cpp}` | `RootSignature`(b0 CBV) + `GraphicsPSO` 생성, `Render()`에서 상수 세팅 후 `SetDynamicVB` + `DrawInstanced` |
| `Shaders/SimpleVS.hlsl`, `SimplePS.hlsl` | 최소 정점/픽셀 셰이더(ViewProj 변환 + N·L 조명). FxCompile로 `CompiledShaders/*.h` 자동 생성 |
| `Main.cpp` | `IGameApp` 구현. `FbxModel`/`FbxRenderer`로 로드·애니 업데이트·렌더, 회색 배경 |

### 렌더 흐름

```
Character::Startup()
  ├─ FbxRenderer::Initialize()   ← RootSignature + PSO 생성
  ├─ FbxModel::Load("Assets/Capoeira.fbx")
  │    FbxImporter → Import → SystemUnit::m(단위만) / Triangulate
  │    → 스킨 클러스터에서 본/가중치/바인드행렬 추출, 애니 커브 있는 스택 선택
  │    → 컨트롤포인트별 본-로컬 바인드 위치/노멀 미리 계산(씬 보관)
  └─ OrbitCamera 타깃 = 모델 바운딩 스피어

Character::Update(dt)
  └─ FbxModel::Update(dt)   ← 시간 진행(루프) + CPU LBS로 이번 프레임 정점 계산

Character::RenderScene()   (프레임워크 루프가 이후 PostEffects::Render → Present 수행)
  ├─ ClearDepth(g_SceneDepthBuffer)
  ├─ ClearColor(g_SceneColorBuffer, 회색 RGB 100,100,100)
  ├─ SetRenderTarget(SceneColor RTV, SceneDepth DSV)
  └─ FbxRenderer::Render(ctx, camera, model)
       SetRootSignature/PSO → SetDynamicConstantBufferView(b0) → SetDynamicVB(스킨드 정점) → DrawInstanced
```

### Root Signature / 상수

| 슬롯 | 내용 |
|------|------|
| b0 (CBV) | `float4x4 ViewProj; float3 SunDirection; float pad0; float3 BaseColor; float pad1;` |

- `BaseColor`는 diffuse. 현재 흰색(1,1,1) 고정으로 선명한 렌더. PS 조명은 `BaseColor*(0.55+0.45*NdL)`.
- 정점 포맷: `POSITION`(R32G32B32_FLOAT) + `NORMAL`(R32G32B32_FLOAT), stride 24.
- 셰이더는 오프라인 FxCompile(`Build.props` 전역 규칙, 변수명 `g_p<파일명>`).
- 렌더 상태: `RasterizerTwoSided`(Z 반전에 따른 와인딩 반전 대응), `BlendDisable`,
  `DepthStateReadWrite`.

### 주의점

- **축 처리**: `FbxAxisSystem::DirectX.ConvertScene`는 파일에 따라 Y축까지 뒤집는 문제가 있어
  사용하지 않는다. 대신 단위만 미터로 변환(`FbxSystemUnit::m`)하고, 베이크 시 **Z만 반전**해
  RH Y-up → LH Y-up(DirectX)으로 직접 변환한다. Z 반전으로 와인딩이 뒤집히므로 후면 컬링을
  켜면 메시가 사라진다 → `RasterizerTwoSided`로 회피.
- TAA 미사용(`TemporalEffects::EnableTAA = false`) → `ResolveImage` 불필요, 지터 없는 뷰포트.
- Model 프로젝트 참조는 현재 링크만 유지(미사용). 자세한 작업 내역은 `docs/log.md` 참조.
