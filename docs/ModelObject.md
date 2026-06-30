# MiniEngine 주요 객체 클래스 구조

## 1. 애플리케이션 계층 (`Core/GameCore.h`)

### 클래스 계층

```
IGameApp (순수 인터페이스)
└── ModelViewer : public IGameApp
```

### IGameApp 인터페이스

```cpp
class IGameApp {
    virtual void Startup()  = 0;           // 디바이스 준비 후 1회 초기화
    virtual void Cleanup()  = 0;
    virtual bool IsDone();                 // 기본: ESC 키 감지
    virtual void Update(float deltaT) = 0; // 입력 + 시뮬레이션
    virtual void RenderScene()        = 0; // GPU 커맨드 기록
    virtual void RenderUI(GraphicsContext&) {}
};
```

### 앱 등록 매크로

```cpp
CREATE_APPLICATION(MyApp)
// → GameCore::RunApplication(MyApp(), ...) 호출하는 wWinMain 생성
```

### 프레임 루프 순서 (`GameCore.cpp:57`)

```
1. app.Update(deltaT)       // 입력 + 시뮬레이션
2. app.RenderScene()        // GPU 커맨드 기록
3. PostEffects::Render()    // 후처리
4. app.RenderUI(context)    // HUD (옵션)
5. Display::Present()       // 스왑 체인
```

---

## 2. GPU 리소스 계층 (`Core/`)

### 클래스 계층

```
GpuResource                       (GpuResource.h:16)
  ├── GpuBuffer                   (GpuBuffer.h:23)
  │   ├── ByteAddressBuffer         Raw 32-bit access
  │   ├── StructuredBuffer          Counter 내장 UAV
  │   └── TypedBuffer               DXGI_FORMAT 지정
  │
  ├── PixelBuffer                  (PixelBuffer.h:20)
  │   ├── ColorBuffer               RTV + SRV + UAV×Mips
  │   ├── DepthBuffer               DSV 4종 + Depth/Stencil SRV
  │   └── ShadowBuffer
  │
  ├── UploadBuffer                 CPU→GPU, UPLOAD 힙
  └── ReadbackBuffer               GPU→CPU, READBACK 힙
```

### GpuResource 핵심 멤버 (`GpuResource.h:16`)

| 멤버 | 타입 | 역할 |
|---|---|---|
| `m_pResource` | `ComPtr<ID3D12Resource>` | D3D12 리소스 핸들 |
| `m_UsageState` | `D3D12_RESOURCE_STATES` | 현재 상태 (배리어 자동 추적) |
| `m_TransitioningState` | `D3D12_RESOURCE_STATES` | 분할 배리어 전환 중 상태 |
| `m_GpuVirtualAddress` | `D3D12_GPU_VIRTUAL_ADDRESS` | GPU 가상 주소 |
| `m_VersionID` | `uint32_t` | 디스크립터 재바인딩 감지 |

### Create() 공통 흐름

모든 리소스 타입의 `Create()` 메서드는 동일한 흐름을 따른다.

```
Destroy()
  → Describe*()           // D3D12_RESOURCE_DESC 구성
  → CreateTextureResource() / CreateCommittedResource()
  → CreateDerivedViews()  // 타입별 View 생성 (가상 함수)
```

### 힙 타입 및 초기 상태

| 클래스 | Heap Type | 초기 ResourceState | View |
|---|---|---|---|
| GpuBuffer | DEFAULT | COMMON | UAV, SRV |
| ColorBuffer | DEFAULT | COMMON | RTV, SRV, UAV×Mips |
| DepthBuffer | DEFAULT | COMMON | DSV×4, Depth SRV, Stencil SRV |
| UploadBuffer | UPLOAD | GENERIC_READ | 없음 |
| ReadbackBuffer | READBACK | COPY_DEST | 없음 |

### 사용 예

```cpp
ColorBuffer g_SceneColorBuffer;
g_SceneColorBuffer.Create(L"Scene Color", width, height, 1, DXGI_FORMAT_R11G11B10_FLOAT);

DepthBuffer g_SceneDepthBuffer(1.0f, 0);
g_SceneDepthBuffer.Create(L"Scene Depth", width, height, DXGI_FORMAT_D32_FLOAT);
```

---

## 3. CommandContext 시스템 (`Core/CommandContext.h:84`)

### 클래스 계층

```
CommandContext                (스레드-안전 풀에서 할당)
  ├── GraphicsContext         그래픽 파이프라인 커맨드
  └── ComputeContext          컴퓨트 전용 (Async 가능)
```

### 핵심 멤버

| 멤버 | 역할 |
|---|---|
| `m_CpuLinearAllocator` | 동적 상수 버퍼 (UPLOAD 힙, 프레임마다 할당) |
| `m_GpuLinearAllocator` | GPU 전용 임시 메모리 (DEFAULT 힙) |
| `m_DynamicViewDescriptorHeap` | CBV/SRV/UAV 셰이더 바인딩 |
| `m_ResourceBarrierBuffer[16]` | 배리어 배칭 (최대 16개 모아서 한 번 제출) |
| `m_CurPipelineState` | PSO 캐싱 (동일 PSO 재설정 방지) |

### 라이프사이클

```cpp
// 1. 풀에서 컨텍스트 획득 (내부: ContextManager 뮤텍스 락)
GraphicsContext& ctx = GraphicsContext::Begin(L"Scene Render");

// 2. 커맨드 기록
ctx.TransitionResource(colorBuf, D3D12_RESOURCE_STATE_RENDER_TARGET);
ctx.SetRenderTarget(colorBuf.GetRTV());
ctx.DrawIndexedInstanced(...);

// 3. GPU 큐에 제출 → 풀에 반납
ctx.Finish();
```

### 스레드 안전성

`ContextManager`는 4개의 풀(DIRECT / COMPUTE / COPY)을 하나의 뮤텍스로 보호한다.
`LinearAllocator` 페이지와 디스크립터 힙은 GPU 펜스 값 기준으로 재활용된다.

---

## 4. Model 시스템 (`Model/`)

### 클래스 관계

```
Model  (정적 데이터 컨테이너, 읽기 전용)
  ├── m_DataBuffer          ByteAddressBuffer — 버텍스/인덱스 데이터
  ├── m_MaterialConstants   ByteAddressBuffer — PBR 파라미터
  ├── m_SceneGraph[]        GraphNode 계층 구조
  ├── m_MeshData            Mesh 구조체 배열
  ├── m_Animations[]        AnimationSet (duration, firstCurve, numCurves)
  ├── m_CurveData[]         AnimationCurve (보간 방식, 키프레임 오프셋)
  ├── m_KeyFrameData        원시 키프레임 바이트
  └── m_JointIBMs[]         역 바인드 행렬 (스켈레탈)

ModelInstance  (런타임 인스턴스, per-object)
  ├── shared_ptr<const Model>   Model 공유 참조
  ├── m_MeshConstantsCPU        UploadBuffer — 월드 행렬 CPU 쪽
  ├── m_MeshConstantsGPU        ByteAddressBuffer — 월드 행렬 GPU 쪽
  ├── m_AnimGraph[]             애니메이션 평가용 SceneGraph 복사본
  ├── m_AnimState[]             재생 상태 per-animation
  └── m_Skeleton[]              계산된 조인트 행렬

MeshSorter  (렌더 패스별 정렬 및 드로우 발행)
  ├── m_SortObjects[]       메시 제출 목록 (Mesh*, CBV 주소 등)
  ├── m_SortKeys[]          64-bit 정렬 키 (pass|pso|depth|objectIdx)
  └── m_PassCounts[]        패스별 메시 수 (ZPass, Opaque, Transparent)
```

### ModelInstance 생성

```cpp
// ModelLoader가 파일을 읽어 shared_ptr<Model> 반환
std::shared_ptr<Model> model = ModelLoader::LoadModel(L"path/to/model.mini");

// Instance 생성 시 GPU 버퍼 자동 할당
ModelInstance instance(model);
```

### 프레임당 데이터 흐름

```
ModelInstance::Update(ctx, deltaT)
  → UpdateAnimations(deltaT)          // 커브 보간 → AnimGraph 노드 갱신
  → SceneGraph 순회 (깊이 우선)       // 월드 행렬 계산
  → Skeleton 조인트 행렬 계산         // worldMatrix × inverseBindMatrix
  → MeshConstants CPU→GPU 복사        // UploadBuffer → ByteAddressBuffer

ModelInstance::Render(sorter)
  → Model::Render(sorter, ...)
    → 메시별 프러스텀 컬링
    → sorter.AddMesh(mesh, depth, cbv, ...)

MeshSorter::Sort()                    // std::sort on 64-bit keys
MeshSorter::RenderMeshes(pass, ctx)   // PSO 설정 → DrawIndexedInstanced
```

---

## 5. Renderer 네임스페이스 (`Model/Renderer.h:36`)

### PSO 관리

`Renderer::GetPSO(psoFlags)` — 플래그 조합으로 동적 PSO 생성 및 캐싱.

```cpp
// PSOFlags 조합 예
kHasPosition | kHasNormal | kHasTangent | kHasUV0 | kHasSkin
```

각 PSO에 대해 depth-write / depth-equal 두 변형이 자동으로 쌍으로 생성된다.

### 루트 시그니처 바인딩 (`RootBindings` enum)

| 슬롯 | 이름 | 내용 |
|---|---|---|
| 0 | `kMeshConstants` | 메시별 월드 행렬 CBV |
| 1 | `kMaterialConstants` | PBR 파라미터 CBV |
| 2 | `kMaterialSRVs` | 머티리얼 텍스처 테이블 (최대 10개) |
| 3 | `kMaterialSamplers` | 샘플러 테이블 |
| 4 | `kCommonSRVs` | IBL, SSAO, 섀도우맵, 라이트 그리드 |
| 5 | `kCommonCBV` | 전역 View/Projection/Lighting |
| 6 | `kSkinMatrices` | 조인트 행렬 SRV (스켈레탈) |

---

## 6. 핵심 설계 패턴 요약

| 패턴 | 적용 위치 | 설명 |
|---|---|---|
| **오브젝트 풀** | ContextManager | CommandContext 재사용, 뮤텍스 보호 |
| **펜스 기반 재활용** | LinearAllocator, DynamicDescriptorHeap | GPU 완료 후 메모리 재사용 |
| **friend 상태 추적** | GpuResource ↔ CommandContext | 자동 배리어 삽입 |
| **버전 ID** | GpuResource::m_VersionID | 디스크립터 재바인딩 최소화 |
| **파생 뷰 분리** | `CreateDerivedViews()` 가상 함수 | 리소스 타입별 View 생성 분리 |
| **Model 공유** | `shared_ptr<const Model>` | 여러 Instance가 데이터 공유 |
| **64-bit Sort Key** | MeshSorter | PSO·깊이·패스를 단일 정수로 인코딩 후 정렬 |
| **배리어 배칭** | CommandContext | 최대 16개 배리어를 모아 한 번에 제출 |
| **두 단계 상수 버퍼** | ModelInstance | CPU(UploadBuffer) → GPU(ByteAddressBuffer) 분리 |
