# 작업 로그

## 2026-07-02 — Capoeira.fbx 스켈레톤 애니메이션 재생 (CPU 스키닝)

### 목표
자체 FBX 렌더 경로에 스켈레톤 애니메이션 재생 추가. `Assets/Capoeira.fbx` 사용.

### 구성 파악
grep으로 확인: `Capoeira.fbx`는 메시(Beta_Surface) + 스킨(Deformer) + 스켈레톤(mixamorig) +
애니메이션(AnimationStack)을 모두 포함 → **단일 파일 로드**로 자기완결 구현(리타게팅 불필요).

### 구현 (CPU 스키닝)
- `FbxModel` 재작성(pimpl): 스킨 클러스터에서 본/가중치/바인드행렬 추출, 애니메이션 스택 선택,
  씬을 보관해 매 프레임 `node->EvaluateGlobalTransform(t)`로 본 글로벌 평가 → LBS.
- **곱 순서 모호성 회피**: 검증된 `TransformPoint`(=v*M 행벡터) 중첩으로 본-로컬 바인드
  위치/노멀(`localPos=cp·M·L^-1`, `localNrm`)을 **로드 시 컨트롤포인트당 미리 계산**.
  런타임은 `Σ w · TransformPoint(G_b(t), localPos)`만 수행(정확 + CP 단위라 저비용). 마지막 Z 반전.
- `FbxRenderer`: 매 프레임 `GraphicsContext::SetDynamicVB`로 스킨드 정점 업로드 + `DrawInstanced`
  (비인덱스). 셰이더/RootSignature/정점 포맷 불변.
- `Main.cpp`: `Assets/Capoeira.fbx` 로드, `Update`에서 `m_FbxModel.Update(deltaT)` 호출(루프 재생).

### 이슈 및 해결
- **정지(바인드 포즈)**: 애니메이션이 시간에 따라 안 바뀜. 진단 결과 stackCount=2에서
  `Take 001`(빈 스택, curvedBones=0)을 고르고 있었고 실제 애니는 `mixamo.com`(curvedBones=52).
  → **본에 애니메이션 커브가 연결된 스택**을 선택하도록 변경(빈 스택 회피).
- **메시 폭발(산산조각)**: 스택 수정 후 움직이나 정점이 흩어짐 → 스킨 행렬 곱 순서/규약 오류.
  → 위의 "본-로컬 바인드 미리 계산" 방식으로 교체해 해결(검증된 단일 변환만 사용).

### 검증 (Verify)
- 빌드: Character Debug|x64 — 오류 0개.
- 실행: 회색 배경 위 흰색 캐릭터가 **카포에라 동작을 재생**(프레임마다 포즈 변화, 메시 온전,
  관절 자연스럽게 굽음)함을 다중 스크린샷으로 확인. CPU 스키닝 ~18ms(Debug).

### 다음 단계
- GPU 스키닝 업그레이드(BLENDINDICES/WEIGHT + 본 행렬 StructuredBuffer + VS LBS).
- 프레임 보간, 애니메이션 블렌딩/전환, 다중 클립.

## 2026-07-02 — X Bot.fbx(스켈레탈 메시) 바인드 포즈 렌더 + 흰색 diffuse

### 목표
애니메이션 재생을 위한 기반으로, 스켈레톤을 가진 `Assets/X Bot.fbx`를 자체 FBX 렌더 경로로
**바인드 포즈 렌더링**. 렌더가 선명하도록 diffuse를 흰색으로 설정. (스킨/애니메이션은 다음 단계.)

### 구현
- `Main.cpp`: 로드 대상을 `Assets/X Bot.fbx`로 변경. (바인드 포즈는 스키닝 없이 노드 월드 변환
  베이크만으로 렌더됨 — 오빗 카메라는 바운딩 스피어로 자동 프레이밍.)
- 흰색 diffuse: `SimpleVS/PS.hlsl`의 b0 cbuffer에 `float3 BaseColor` 추가, PS에서 사용.
  `FbxRenderer`가 `BaseColor=(1,1,1)`로 설정. (C++ `SimpleConstants`도 동일 레이아웃으로 확장.)

### 이슈 및 해결
- **상하 반전**: 최초 렌더 시 캐릭터가 뒤집혀(머리 아래) 나옴. 진단 결과 X Bot.fbx 원본 축은
  **Y-up, 오른손 좌표계(RH)**인데, `FbxAxisSystem::DirectX.ConvertScene`(RH→LH 변환)이 파일에
  따라 Y까지 뒤집는 문제가 있었다. → 해당 축 변환을 제거하고, 베이크 단계에서 **Z만 반전**
  (RH Y-up → LH Y-up)하는 방식으로 교체(`FbxModel.cpp`). 큐브(대칭)는 영향 없음.
- **어두움/선명도**: 흰색 diffuse여도 HDR 톤매핑 + 조명 각도로 배경(회색 0.39)과 잘 구분되지
  않음. → 조명을 카메라 정면-상단(`SunDir=(0.3,-0.7,-1.0)`)으로 조정하고, PS 앰비언트를
  0.55로 높여(디퓨즈 0.45) 캐릭터 최소 밝기를 배경보다 확실히 위로 올림.

### 검증 (Verify)
- 빌드: Character Debug|x64 — 오류 0개.
- 실행: 회색 배경 위에 **X Bot 캐릭터가 바인드 포즈(T-pose)로 똑바로, 흰색으로 선명하게** 렌더됨을
  스크린샷으로 확인. 오빗 카메라 회전/줌 정상.

### 다음 단계
스킨 가중치(BLENDINDICES/BLENDWEIGHT) + 본 계층 + 인버스 바인드 행렬 추출 → 애니메이션 샘플링
→ VS 스키닝(정점 포맷/셰이더/RootSignature 확장).

---

## 2026-07-02 — Character 프로젝트 자체 FBX 직접 렌더 경로 구축 (cube.fbx)

### 목표
기존 Model 프로젝트 파이프라인(`Renderer::LoadModel` → `FBX::Asset` → `ModelData` →
`.mini` → `Model` → `MeshSorter`)과 무관하게, **Character 프로젝트 안에서 FBX SDK로
직접 `Assets/cube.fbx`를 파싱하고 DirectX12로 직접 렌더링**하는 최소 경로를 구축.

### 결정 사항
- 셰이더: 오프라인 FxCompile(엔진 관례). `Build.props`의 전역 `<FxCompile>` 규칙 재사용.
- 범위: cube.fbx 정적 메시 렌더만(position+normal + 간단 N·L 조명). 스킨/애니메이션은 이후.

### 구현
- **신규 `FbxModel.{h,cpp}`**: FBX SDK로 파싱 → 노드 월드 변환 베이크 → position/normal
  전개 + 순차 인덱스 → `ByteAddressBuffer` VB/IB 업로드, 바운딩 스피어 계산.
  좌표계 `FbxAxisSystem::DirectX`, 단위 `FbxSystemUnit::m`, `Triangulate` 수행.
  `FbxManager`는 로컬 싱글턴으로 관리(`Shutdown`으로 해제).
- **신규 `FbxRenderer.{h,cpp}`**: `RootSignature`(b0 CBV 1개) + `GraphicsPSO` 생성,
  `Render(ctx, camera, model)`에서 `ViewProj`·`SunDirection` 상수 세팅 후 `DrawIndexed`.
  렌더 타겟 포맷은 `g_SceneColorBuffer`/`g_SceneDepthBuffer`에서 취득.
- **신규 셰이더 `Shaders/SimpleVS.hlsl`, `SimplePS.hlsl`**: b0 상수버퍼(ViewProj, SunDirection),
  `mul(ViewProj, pos)` 열벡터 규약, N·L 조명(앰비언트 0.2 + 디퓨즈). HDR 선형 출력 →
  프레임워크 `PostEffects`가 톤매핑.
- **`Main.cpp`**: Model/Renderer/MeshSorter 사용 제거, `FbxModel`+`FbxRenderer`로 교체.
  `TempStartUp/Update/Render` 통합 — TAA 비활성(resolve 불필요), 회색 배경(RGB 100,100,100)
  클리어 후 큐브 렌더. 오빗 카메라 타깃을 모델 바운딩 스피어로 설정.
- **`Character.vcxproj`**: `FbxModel/FbxRenderer` Cl* 항목 + `SimpleVS/PS` FxCompile 항목 추가.
  (헤더 출력/변수명은 `Build.props` 전역 규칙이 자동 적용.)

### 검증 (Verify)
- 빌드: Character Debug|x64 — **오류 0개**. FxCompile이 `CompiledShaders/SimpleVS.h`,
  `SimplePS.h`(변수 `g_pSimpleVS`, `g_pSimplePS`) 생성 확인.
- 실행: `Build/x64/Debug/Output/Character/Character.exe` — 크래시 없음(FBX 로드 + PSO 생성 정상).
  회색 배경 위에 조명 적용된 cube.fbx 큐브 렌더 확인.

### 이슈 및 해결
- **증상**: 최초 실행 시 회색 배경만 나오고 큐브가 보이지 않음.
- **원인**: `FbxAxisSystem::DirectX.ConvertScene`가 핸드니스를 뒤집어 삼각형 와인딩이 전역
  반전 → 후면 컬링으로 큐브 전체가 컬링됨.
- **해결**: 최소 뷰어이므로 `RasterizerTwoSided`로 컬링 해제(와인딩 반전과 무관하게 표시).
  (추후 정식 처리 시 인덱스 와인딩을 뒤집는 방식으로 개선 가능.)

### 후속 과제
- 스킨/애니메이션 로드 및 재생.
- 재질/텍스처 반영.
- 와인딩 정규화 후 후면 컬링 복원.
- Model 프로젝트 참조/라이브러리 완전 제거(현재는 안정성 위해 링크 유지).
