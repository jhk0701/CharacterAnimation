# 작업 로그

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
