# 애니메이션 재생 설계 문서 (AnimPlan)

> Character 프로젝트에서 스켈레탈 메시 애니메이션을 **로드 → 본 평가 → 스키닝 → 재생**하는
> 전체 파이프라인을 처음부터 설계한 문서. 각 단계에서 실제 사용하는 FBX SDK / DirectX12 API 를
> 세부적으로 명시한다.
>
> 현재 상태 요약: `MiniEngine/Character/FbxModel.cpp` 에 **CPU 스키닝 기반 애니메이션 재생이 이미
> 구현**되어 있으며, 본 문서는 그 구현을 "참조(Reference)"로 매핑하면서 설계 관점으로 서술한다.
> 현재 브랜치는 `Bug-LinkError` 이므로, **빌드/실행 검증을 최우선 1단계**로 둔다.

---

## 1. 개요 / 목표

- **목표**: `./Assets/*.fbx` 스켈레탈 메시(예: `x bot.fbx`, `Capoeira.fbx`)의 애니메이션을
  로드하여 DirectX12 화면에 실시간 재생한다.
- **범위(1차)**: 단일 애니메이션 클립을 **루프 재생**. 회색 배경(RGB `100,100,100`) 위에
  스킨 메시가 애니메이션으로 움직이는 것을 화면에서 확인.
- **설계 원칙**
  - 파싱/스켈레톤/스킨/애니메이션 추출과 본 평가는 **FBX SDK** 로 수행.
  - 스키닝은 1차적으로 **CPU 선형 블렌드 스키닝(LBS)** + 매 프레임 **동적 정점버퍼 업로드**.
  - **GPU 스키닝**(본 팔레트 상수/구조화버퍼 + 정점 셰이더 LBS)은 확장 항목으로 분리.
- **좌표계 규약**: FBX 는 RH(Right-Handed) Y-up, 행-벡터(row-vector) 규약. 엔진은 LH.
  단위만 미터로 변환하고 축은 변환하지 않으며, **스킨 출력 시 z 부호만 반전**하여 RH→LH 처리.

---

## 2. 파이프라인 전체 설계 (0~5단계)

각 단계를 `입력 → 처리 → 출력 → 사용 API` 형식으로 서술한다.

### (0) 사전 검증
- **입력**: 현재 브랜치 소스.
- **처리**: `Character` 프로젝트 빌드 → 실행하여 링크/런타임 상태 확인.
- **출력**: 빌드 성공 여부, 애니메이션 재생 여부(→ 5절 절차, 8절 기록).

### (1) FBX 로드 / 초기화
- **입력**: FBX 파일 경로(`std::wstring`).
- **처리**: FBX SDK 매니저/IO 세팅 생성 → 임포터로 씬 로드 → 단위/삼각화 정규화.
- **출력**: `FbxScene*` (런타임 본 평가를 위해 보관).
- **사용 API**

  | 목적 | API |
  |------|-----|
  | 매니저 싱글턴 | `FbxManager::Create()` |
  | IO 설정 | `FbxIOSettings::Create(manager, IOSROOT)`, `FbxManager::SetIOSettings()` |
  | 임포터 | `FbxImporter::Create(manager, "")` |
  | 초기화 | `FbxImporter::Initialize(pathUtf8, -1, ios)`, `GetStatus().GetErrorString()` |
  | 씬 생성/로드 | `FbxScene::Create(manager, "Scene")`, `FbxImporter::Import(scene)` |
  | 정리 | `FbxImporter::Destroy()` |
  | 단위 변환 | `FbxSystemUnit::m.ConvertScene(scene)` (미터 기준) |
  | 삼각화 | `FbxGeometryConverter::Triangulate(scene, true)` |
  | 경로 변환 | `Utility::WideStringToUTF8(filePath)` (wide → UTF-8) |

### (2) 스켈레톤 / 스킨 추출
- **입력**: `FbxScene*`.
- **처리**: 노드 트리를 순회하며 **스킨 디포머가 있는 메시**만 수집.
  클러스터별로 연결 본과 바인드 행렬을 얻고, 컨트롤포인트별 가중치를 수집·정규화.
  각 영향의 위치/노멀을 **"바인드 로컬" 공간**(`v · M · L⁻¹`)으로 미리 계산해 두어
  런타임에 `G_b(t)` 만 곱하면 되도록 한다.
- **출력**: 메시별 `MeshSkin`(본 배열, 컨트롤포인트별 영향, 폴리곤-정점→CP 매핑).
- **사용 API**

  | 목적 | API |
  |------|-----|
  | 루트/자식 순회 | `FbxScene::GetRootNode()`, `FbxNode::GetChildCount()`, `FbxNode::GetChild(i)` |
  | 메시 취득 | `FbxNode::GetMesh()` |
  | 스킨 판별 | `FbxMesh::GetDeformerCount(FbxDeformer::eSkin)`, `GetDeformer(0, FbxDeformer::eSkin)` |
  | 노멀 보정 | `FbxMesh::GetElementNormalCount()`, `GenerateNormals()` |
  | 컨트롤포인트 | `GetControlPointsCount()`, `GetControlPoints()` |
  | 폴리곤/정점 | `GetPolygonCount()`, `GetPolygonSize(pi)`, `GetPolygonVertex(pi, vi)`, `GetPolygonVertexNormal(pi, vi, out)` |
  | 클러스터/본 | `FbxSkin::GetClusterCount()`, `GetCluster(c)`, `FbxCluster::GetLink()` |
  | 바인드 행렬 | `FbxCluster::GetTransformMatrix(M)`(메시 글로벌), `GetTransformLinkMatrix(L)`(본 글로벌) → 역바인드 `L.Inverse()` |
  | 가중치 | `FbxCluster::GetControlPointIndices()`, `GetControlPointWeights()`, `GetControlPointIndicesCount()` |

- **가중치 규약**: 컨트롤포인트당 영향을 합으로 정규화(`w /= Σw`). GPU 스키닝 확장 시에는
  **가중치 내림차순 정렬 후 상위 4개**만 사용하고 재정규화한다.
  (참조 구현: `MiniEngine/Model/FBX.cpp` 의 top-4 정렬/정규화 로직)

### (3) 애니메이션 스택 선택 & 시간축
- **입력**: `FbxScene*`, 스킨 본 목록.
- **처리**: 씬 내 모든 `FbxAnimStack` 을 조회하여, **본에 실제 애니메이션 커브가 연결된 스택**을
  선택한다. (Mixamo FBX 는 빈 스택 `Take 001` 과 실제 스택 `mixamo.com` 이 공존할 수 있음.)
  선택된 스택을 활성화하고 재생 구간(시작/길이)을 계산.
- **출력**: `animStart`(FbxTime), `animDuration`(초).
- **사용 API**

  | 목적 | API |
  |------|-----|
  | 스택 열거 | `FbxScene::GetSrcObjectCount<FbxAnimStack>()`, `GetSrcObject<FbxAnimStack>(i)` |
  | 레이어 열거 | `FbxAnimStack::GetMemberCount<FbxAnimLayer>()`, `GetMember<FbxAnimLayer>(i)` |
  | 커브 유무 | `FbxNode::LclRotation.GetCurve(layer)`, `LclTranslation.GetCurve(layer)`, `LclScaling.GetCurve(layer)` |
  | 스택 활성화 | `FbxScene::SetCurrentAnimationStack(stack)`, `GetAnimationEvaluator()->Reset()` |
  | 구간 계산 | `FbxAnimStack::GetLocalTimeSpan()`, `FbxTimeSpan::GetStart()/GetStop()`, `FbxTime::GetSecondDouble()` |

### (4) 프레임 평가 & 스키닝 (재생)
- **입력**: 현재 시각 `time`, `MeshSkin`.
- **처리**: 프레임마다 `time` 을 진행/루프시키고, 지정 시각에서 각 본의 글로벌 변환 `G_b(t)` 을
  평가한 뒤, 컨트롤포인트마다 가중합(LBS)으로 스킨드 위치/노멀을 계산한다.
  마지막에 폴리곤-정점으로 전개하고 z 부호를 반전(RH→LH)한다.
- **출력**: 전개된 스킨드 정점 배열(`FbxModel::Vertex[]`).
- **사용 API / 수식**

  | 목적 | API / 수식 |
  |------|-----------|
  | 프레임 시간 | `Graphics::GetFrameTime()` → `Character::Update(deltaT)` → `FbxModel::Update(deltaT)` |
  | 시간 진행/루프 | `time = fmod(time + deltaT, animDuration)` |
  | 시각 지정 | `FbxTime::SetSecondDouble(animStart + time)` |
  | 본 글로벌 변환 | `FbxNode::EvaluateGlobalTransform(FbxTime)` (대안: `FbxAnimEvaluator::GetNodeLocalTransform`) |
  | LBS(위치) | `p = Σ_b weight_b · (G_b(t) · localPos_b)` |
  | LBS(노멀) | `n = normalize( Σ_b weight_b · (R(G_b(t)) · localNrm_b) )` (이동 제외) |
  | 좌표계 변환 | `pos.z *= -1`, `normal.z *= -1` (RH Y-up → LH Y-up) |

- **정적 폴백**: 가중치가 없는 컨트롤포인트는 `bone = -1` 로 두고, 메시 바인드 변환을 적용한
  월드 위치/노멀을 그대로 사용한다.

### (5) GPU 업로드 & 렌더
- **입력**: 스킨드 정점 배열, 카메라.
- **처리**: 루트시그니처/PSO 설정(초기화 1회), 매 프레임 상수버퍼 + 동적 정점버퍼 업로드 후 드로우.
- **출력**: 렌더 타깃에 그려진 스킨 메시.
- **사용 API**

  | 목적 | API |
  |------|-----|
  | 루트시그니처 | `RootSignature::Reset(1,0)`, `[0].InitAsConstantBuffer(0)`, `Finalize(..., ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)` |
  | 입력 레이아웃 | `POSITION` `R32G32B32_FLOAT`(offset 0), `NORMAL` `R32G32B32_FLOAT`(offset 12) |
  | PSO | `GraphicsPSO::SetRootSignature/SetInputLayout/SetVertexShader/SetPixelShader/SetRasterizerState(RasterizerTwoSided)/SetDepthStencilState(DepthStateReadWrite)/SetRenderTargetFormat/Finalize` |
  | 셰이더 | `Shaders/SimpleVS.hlsl`, `Shaders/SimplePS.hlsl` → 산출물 `CompiledShaders/SimpleVS.h`(`g_pSimpleVS`), `SimplePS.h` |
  | 상수 업로드 | `GraphicsContext::SetDynamicConstantBufferView(0, sizeof(cb), &cb)` (`ViewProj`, `SunDirection`, `BaseColor`) |
  | 정점 업로드 | `GraphicsContext::SetDynamicVB(0, VertexCount(), sizeof(Vertex), SkinnedVertices())` |
  | 드로우 | `SetPrimitiveTopology(TRIANGLELIST)`, `DrawInstanced(VertexCount(), 1, 0, 0)` (비인덱스) |
  | 카메라 프레이밍 | 스킨드 정점 AABB → `Math::BoundingSphere` → `OrbitCamera(camera, sphere, kYUnitVector)` |

---

## 3. 데이터 구조 설계

```cpp
// GPU 로 올라가는 최종 정점(현행: CPU 스키닝 결과, 이미 월드/LH 공간)
struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 normal; };

// 정점 영향: bone < 0 이면 정적. localPos/localNrm 은 "바인드 로컬" 공간 값(= cp·M·L⁻¹).
struct Influence { int bone; float weight; XMFLOAT3 localPos; XMFLOAT3 localNrm; };

struct MeshSkin {
    std::vector<FbxNode*>                bones;        // 클러스터별 연결 본
    std::vector<std::vector<Influence>>  cpInfluence;  // 컨트롤포인트별 영향
    std::vector<int>                     pvCP;         // 폴리곤-정점 → 컨트롤포인트
    uint32_t                             pvBase;       // 전역 스킨드 배열 시작 오프셋
    std::vector<FbxAMatrix>              boneGlobal;   // 프레임 스크래치: G_b(t)
    std::vector<XMFLOAT3>                skinnedPos, skinnedNrm;
};

struct FbxModel::Impl {
    FbxScene* scene;            // 런타임 본 평가용 보관(소유는 FbxManager)
    FbxTime   animStart;
    double    animDuration, time;
    std::vector<MeshSkin>         meshes;
    std::vector<FbxModel::Vertex> skinned;   // 전역 전개 정점(출력)
};
```

- **GPU 스키닝 확장 시 정점 포맷**: 위 `Vertex` 에 `BLENDINDICES`(`R8G8B8A8_UINT` 또는 `UINT4`) +
  `BLENDWEIGHTS`(`R8G8B8A8_UNORM` 또는 `FLOAT4`) 추가, 본 매트릭스 팔레트를 상수/구조화버퍼로 바인딩.
- **오프라인 키프레임 포맷(향후 확장 참조)**: `MiniEngine/Model/Animation.h` 의
  `AnimationCurve` / `AnimationSet` / `AnimationState` — 30FPS 샘플링 키프레임 저장 및
  재생 상태(`kStopped/kPlaying/kLooping`) 관리 구조.

---

## 4. 작업 태스크 목록 (체크리스트)

CLAUDE.md 의 `Plan → Execute → Valify → Write Docs` 흐름에 맞춘 순서:

- [ ] **T1. 현재 상태 확인** — `Character`(x64) 빌드 + 실행 (→ 5절)
- [ ] **T2. 링크 에러 진단·수정**(필요 시) — `Character.vcxproj` 의 FBX 링크 항목 및
      `libfbxsdk.dll` 포스트빌드 복사(`FbxDllPath`) 확인
- [ ] **T3. 로드/스킨/애니 평가 경로 검증** — 콘솔 로그 확인
      (`[FbxModel] anim stack '...' dur=..s`, `Loaded ... : N verts, M meshes`)
- [ ] **T4. 재생/루프 시각 확인** — 회색 배경 위 캐릭터 애니메이션 육안 확인
- [ ] **T5. (확장) 다중 클립 전환 / 재생·일시정지 UI / GPU 스키닝**
- [ ] **T6. 문서 갱신** — `./docs/log.md` 작업 내역, `ProjectStructure.md` 구조/사용법 반영

---

## 5. 빌드 & 검증 절차 (상태 미확인 → 최우선)

- **빌드**: MSBuild 로 `MiniEngine` 솔루션의 `Character` 프로젝트를 `x64`(Debug/Release)로 빌드.
- **링크 에러 발생 시 점검 포인트**
  - `PropertySheets/FbxSdk.props` 의 `FBXSDK_ROOT`
    (기본값 `C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.9`) 실제 설치 여부.
  - 라이브러리: `libfbxsdk.lib`, `libxml2-md.lib`, `zlib-md.lib` 경로(`$(FbxLibDir)`) 존재.
  - 전처리기 정의 `FBXSDK_SHARED`(공유 라이브러리 사용 시) 일치 여부.
  - 포스트빌드에서 `libfbxsdk.dll`(`$(FbxDllPath)`)이 출력 폴더(`$(OutDir)`)로 복사되는지.
  - `Assets` 폴더가 `$(OutDir)Assets\` 로 복사되는지(실행 시 FBX 경로 해석).
- **실행 검증**
  - 회색 배경(RGB 100,100,100) 위에 캐릭터가 애니메이션(예: Capoeira)으로 움직이는지 육안 확인.
  - 콘솔 로그로 선택된 애니메이션 스택 이름/길이, 정점·메시 수 확인.

---

## 6. 참조 구현 매핑 (현재 코드 → 설계 단계)

| 설계 단계 | 현재 구현 위치 |
|-----------|----------------|
| (1) 로드/초기화 · (2) 스킨 추출 · (3) 스택 선택 | `MiniEngine/Character/FbxModel.cpp :: FbxModel::Load()` |
| (4) 프레임 평가 & 스키닝 | `FbxModel.cpp :: SkinAtTime()`, `FbxModel::Update()` |
| (5) GPU 업로드 & 렌더 | `MiniEngine/Character/FbxRenderer.cpp :: Initialize()/Render()` |
| 루프 통합(Update/RenderScene) | `MiniEngine/Character/Main.cpp` |
| 오프라인 키프레임 / GPU 스키닝 확장 참조 | `MiniEngine/Model/FBX.cpp`, `BuildFBX.cpp`, `Animation.h` |

---

## 7. 확장 로드맵 (선택)

1. **다중 클립**: 여러 `FbxAnimStack` 을 목록화하고 런타임 전환(`SetCurrentAnimationStack`).
2. **재생 제어 UI**: `EngineTuning`(엔진 내장 오버레이) 또는 `Character::RenderUI` 확장으로
   재생/일시정지/타임라인 스크럽 추가. (`AnimationState` 구조 재사용)
3. **GPU 스키닝**: 본 팔레트를 상수/구조화버퍼로 업로드하고 정점 셰이더에서 LBS 수행 →
   CPU 스키닝 제거, 정점 포맷에 `BLENDINDICES/BLENDWEIGHTS` 추가.

---

## 8. 현재 상태 기록 (검증 결과)

> T1 빌드/실행 검증 결과. (검증일: 2026-07-02, 브랜치 `Bug-LinkError`)

- **빌드 결과**: ✅ **성공** — `Character.sln` / `Configuration=Debug` / `Platform=x64` MSBuild 빌드.
  - **오류 0개**, 경고 28개(모두 무해: HLSL `ambiguous bit shift`, `C4100/C4189` 미사용 변수,
    `LNK4099` zlib PDB 누락). **링크 에러 없음** → 브랜치명과 달리 링크 문제는 이미 해결된 상태.
  - 산출물: `MiniEngine/Build/x64/Debug/Output/Character/Character.exe` (약 6.7MB).
- **런타임 의존성**: ✅ 갖춰짐.
  - `libfbxsdk.dll`(약 16MB)이 출력 폴더에 복사됨.
  - `Output/Character/Assets/` 에 `Capoeira.fbx`, `X Bot.fbx`, `cube.fbx` 등 존재
    (`Main.cpp` 이 로드하는 `Assets/Capoeira.fbx` 확보).
- **알려진 경고(빌드 실패 아님)**: 포스트빌드 스텝에서 `'pwsh.exe' is not recognized` 메시지 +
  `0 File(s) copied`(xcopy `/d` 최신본 스킵)이 출력됨. 빌드 성공에는 영향 없으나,
  깨끗한 환경에서 DLL/Assets 자동복사를 보장하려면 포스트빌드 스크립트의 `pwsh` 의존성 점검 권장.
- **실행/재생 여부**: _(GUI 실행 육안 확인 미수행)_ — 빌드/링크/의존성은 모두 정상이므로
  `Character.exe` 직접 실행 시 회색 배경 위 Capoeira 애니메이션 재생이 기대됨.
