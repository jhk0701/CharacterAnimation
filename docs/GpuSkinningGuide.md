# GPU 스켈레탈 스키닝 이식 가이드 (Character 프로젝트)

> 목적: Character 프로젝트의 **CPU 스키닝(매 프레임 정점 재계산 + 동적 VB 재업로드)** 을
> **정석 GPU 스키닝(정적 VB + 버텍스 셰이더 LBS + 본 행렬 팔레트)** 으로 사용자가 직접 이식한다.
> 이 저장소의 **Model 프로젝트가 이미 정석 구현**을 갖고 있으니 막힐 때 그 코드를 정답지로 참고한다.

이 문서는 "무엇을/왜/어디를" 안내하는 가이드다. **코드는 직접 작성**하고, 각 단계 끝의 검증을
통과한 뒤 다음으로 넘어간다. 예시 스니펫은 이해를 돕기 위한 것이며 그대로 복붙하지 말고 구조를 이해하고 옮겨라.

---

## 0. 개념: 스키닝 한 줄 요약

정점 하나는 여러 본의 영향을 가중 평균한 위치로 변형된다(**선형 블렌드 스키닝, LBS**):

```
skinnedPos = Σ_b  weight_b × ( paletteMatrix_b × bindPos )
```

- `bindPos` : 바인드 포즈(T-pose) 시점의 정점 위치. **정적**. 정점 버퍼에 한 번만 굽는다.
- `paletteMatrix_b` : 본 b의 "스킨 행렬". **매 프레임 변함**. 본 개수만큼의 배열(=팔레트)을 GPU로 올린다.
- 매 프레임 바뀌는 것은 오직 팔레트뿐 → 정점 버퍼는 정적, GPU가 변형을 담당.

지금 코드는 이 `Σ`를 CPU(`ModelData::SkinAtTime`, `ModelData.cpp:94-152`)에서 돌리고 결과를
매 프레임 동적 VB로 올린다. 이걸 셰이더로 옮기는 것이 이번 작업이다.

---

## 1. 참조 정답지 — Model 프로젝트 (막히면 여기를 봐라)

| 무엇 | 위치 |
|------|------|
| VS의 LBS 로직 (`ENABLE_SKINNING`) | `MiniEngine/Model/Shaders/DepthOnlyVS.hlsl:31-78`, `DefaultVS.hlsl:35-108` |
| 팔레트 구조체 `Joint{ Matrix4 posXform; Matrix3 nrmXform; }` | `MiniEngine/Model/Model.h:95-99` |
| 팔레트 root SRV(t20) 선언 | `MiniEngine/Model/Renderer.cpp:96` `InitAsBufferSRV(20, VERTEX)` |
| 드로우 시 팔레트 업로드 | `MiniEngine/Model/Renderer.cpp:663` `SetDynamicSRV(kSkinMatrices, …)` |
| 팔레트 계산 `posXform = nodeWorld × IBM` | `MiniEngine/Model/Model.cpp:229-234` |
| 스킨 정점 포맷 BLENDINDICES/BLENDWEIGHT | `MiniEngine/Model/Renderer.cpp:113-126` |
| 클립 샘플링 Lerp/Slerp | `MiniEngine/Model/Animation.cpp:90-154` |

HLSL LBS 핵심(Model):
```hlsl
StructuredBuffer<Joint> Joints : register(t20);
float4 w = vsInput.jointWeights / dot(vsInput.jointWeights, 1);   // 재정규화
float4x4 skinPosMat =
      Joints[i.x].PosMatrix * w.x + Joints[i.y].PosMatrix * w.y
    + Joints[i.z].PosMatrix * w.z + Joints[i.w].PosMatrix * w.w;
position = mul(skinPosMat, position);
```

---

## 2. Character의 현재 확장 지점 (건드릴 곳)

| 무엇 | 위치 | Stage |
|------|------|-------|
| 정점 구조체 `Vertex{ pos; normal; }` (stride 24) | `ModelData.h:18-22` | 1a |
| 입력 레이아웃 (POSITION/NORMAL) | `Renderer.cpp:36-40` | 1a |
| RootSig `Reset(1,0)` + b0 CBV | `Renderer.cpp:31-34` | 1b |
| PSO (VS/PS 바인딩) | `Renderer.cpp:42-53` | 1b |
| 드로우 `SetDynamicVB` + `DrawInstanced` | `Renderer.cpp:74-76` | 1a/1c |
| 셰이더 `Shaders/SimpleVS.hlsl` / `SimplePS.hlsl` | 동 파일 | 1b |
| vcxproj FxCompile 등록 | `Character.vcxproj`의 `<FxCompile Include="Shaders\…">` | 1b |

**이미 추출돼 있어 재사용할 데이터** (`ModelData.cpp`, 로드 시 계산됨):
- 본 배열 `ms.bones[c] = cl->GetLink()` — `:227`
- **인버스 바인드** `clLinv[c] = L.Inverse()` (L = `GetTransformLinkMatrix`) — `:229-233`
- 원시 가중치 `rawInfl[cp] = {클러스터, weight}` — `:236-241`
- 정규화된 영향+본인덱스 `cpInfluence` — `:265-293`
- 프레임 본 글로벌 `boneGlobal[b] = EvaluateGlobalTransform(t)` (이미 멤버) — `:100-108`
- 폴리곤정점→CP 매핑 `pvCP`, 전역오프셋 `pvBase` — `:253, :297`

> ⚠ `clLinv`, `rawInfl`은 **`Load()` 지역 변수**(`:220-221`)라 함수가 끝나면 사라진다.
> GPU로 옮기려면 **필요한 값을 `MeshSkin` 구조체(`ModelData.cpp:59-71`) 멤버로 보존**해야 한다.

---

## 3. 반드시 짚고 갈 규약 3가지 (여기서 버그의 90%가 난다)

### (A) 행벡터(FBX) ↔ 열벡터(HLSL)
- FBX `FbxAMatrix`와 현재 CPU 코드(`TransformPoint`, `ModelData.cpp:33-41`)는 **행벡터** 규약: `q' = q · M`.
- HLSL `mul(M, v)`는 **열벡터** 규약: `q' = M · q`.
- 두 규약은 서로 **전치(transpose)** 관계다. FBX 행렬을 HLSL로 넘길 때 전치해야 한다.

이번 작업의 팔레트 유도(행벡터 기준):
```
bindPos_world = cp · M           (M = GetTransformMatrix, 메시노드 바인드 글로벌)  → 정점에 굽는 값
paletteRow_b  = L_b^-1 · G_b(t)  (L_b^-1 = clLinv, G_b(t) = boneGlobal)           → 매 프레임 팔레트
skinnedRow    = Σ_b w_b · bindPos_world · paletteRow_b
```
HLSL에 넣을 팔레트 행렬 `P_hlsl`은 `paletteRow_b`를 **전치**한 것:
```cpp
// FbxAMatrix R = clLinv[b] * boneGlobal[b];  (행벡터 곱)  ※ FbxAMatrix 곱 순서는 Model 정답지와 대조해 확정
XMFLOAT4X4 P;
for (int r=0;r<4;++r) for (int c=0;c<4;++c) P.m[r][c] = (float)R.Get(c, r);  // 전치
```
그러면 셰이더에서 `mul(P, float4(bindPos,1))`이 `bindPos·R`(행벡터 결과)과 일치한다.

### (B) RH Y-up → LH Y-up (Z 반전)
현재 CPU는 스키닝 끝에서 **Z만 반전**한다(`ModelData.cpp:148-149`: `out.pos.z=-p.z; out.normal.z=-n.z`).
가장 안전한 이식은 **동일하게 VS에서 LBS 직후 Z를 반전**하는 것:
```hlsl
float3 pw = mul(skinPosMat, float4(bindPos,1)).xyz;  pw.z = -pw.z;   // ModelData.cpp:148과 동일
float3 nw = mul((float3x3)skinPosMat, bindNrm);       nw.z = -nw.z;
output.pos = mul(ViewProj, float4(pw, 1));
```
(팔레트나 ViewProj에 접어 넣는 방법도 있으나, 지금은 "동작하는 CPU 로직의 1:1 포팅"이 목표이므로 VS에서 반전한다.)

### (C) 정점당 본 상위 4개
한 컨트롤포인트의 영향 본이 4개를 넘을 수 있다. **weight 내림차순으로 상위 4개만** 남기고
합이 1이 되도록 재정규화한 뒤 정점에 굽는다. 4개 미만이면 나머지 인덱스=0, weight=0.

---

## 4. Stage 1 — GPU 스키닝 전환 (여기부터 구현)

본 글로벌 `G_b(t)`는 **당분간 FBX 평가(`EvaluateGlobalTransform`)를 그대로 유지**한다.
바꾸는 건 "스키닝을 어디서 하느냐"(CPU→GPU)뿐. 자체 스켈레톤/클립은 Stage 2/3에서.

### Stage 1a — 정점 포맷 확장 + 정적 VB (팔레트는 아직 단위행렬)
목표: 정점에 본 인덱스/가중치를 싣고, **시간과 무관한 정적 정점**을 만든다. 팔레트=단위 → 바인드 포즈가 그대로.

할 일:
1. `ModelData.h:18-22` `Vertex`에 필드 추가:
   ```cpp
   struct Vertex {
       DirectX::XMFLOAT3 pos;        // 바인드 포즈 world 위치 (cp·M)
       DirectX::XMFLOAT3 normal;     // 바인드 포즈 world 노멀 (cp노멀·M의 3x3)
       uint16_t boneIdx[4];          // 영향 본 인덱스
       float    boneWeight[4];       // 정규화된 가중치 (합 1)
   };
   ```
2. `Renderer.cpp:36-40` 입력 레이아웃에 추가(오프셋은 위 구조체 배치대로):
   `BLENDINDICES → DXGI_FORMAT_R16G16B16A16_UINT`, `BLENDWEIGHT → DXGI_FORMAT_R32G32B32A32_FLOAT`.
3. 로드 시(`ModelData::Load`) **정적 정점 배열**을 굽는다: 각 폴리곤정점의 CP에서 `cp·M`(바인드 world
   위치), 바인드 노멀, 그리고 상위4 본 인덱스/가중치(규약 C)를 채운다. `cpInfluence`/`rawInfl`를 재사용.
4. 드로우: 우선은 `SetDynamicVB`에 이 정적 정점을 넘겨도 된다(시간 불변이면 성공). VS/PS는 아직
   기존 것이라 본 데이터를 무시하므로 **바인드 포즈가 예전과 똑같이** 나와야 한다.

**검증 1a**: 빌드 오류 0, 실행 시 회색 배경 위 흰색 캐릭터가 **바인드 포즈로 정지**해 렌더.
(아직 애니메이션 없음. 여기서 정점 포맷/레이아웃/오프셋이 맞는지 확인.)

### Stage 1b — 팔레트 SRV + 스킨 VS (팔레트를 "바인드 시각" 값으로)
목표: LBS를 셰이더로. 단, 팔레트를 **t=바인드** 값으로 채워 결과가 여전히 바인드 포즈여야 한다
→ 규약(A)/전치 실수를 여기서 격리해 잡는다.

할 일:
1. RootSig: `Renderer.cpp:31` `Reset(1,0)`→`Reset(2,0)`, 슬롯 추가
   `m_RootSig[1].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_VERTEX);` (t0).
2. 새 셰이더 `Shaders/SkinnedVS.hlsl`: `StructuredBuffer<float4x4> Bones : register(t0);` 선언,
   BLENDINDICES/BLENDWEIGHT 입력 받아 §1의 LBS + §3(B) Z반전 수행. `Character.vcxproj`의
   FxCompile ItemGroup에 `SimpleVS`와 같은 형식으로 한 줄 등록(→ `g_pSkinnedVS` 자동 생성).
   PSO(`Renderer.cpp:44`)의 VS를 `g_pSkinnedVS`로 교체.
3. `ModelData`가 프레임 팔레트를 노출: 접근자 `const XMFLOAT4X4* BonePalette()`, `uint32_t BoneCount()`.
   팔레트 채우기 = §3(A)의 `P = transpose(clLinv[b] · boneGlobal[b])`. 이번엔 `boneGlobal`을 **바인드
   시각(t=0)** 으로 평가해 채운다 → `clLinv·G(0) ≈ 단위`라 바인드 포즈 유지.
4. `Renderer::Render`에서 팔레트 업로드: `ctx.SetDynamicSRV(1, sizeof(XMFLOAT4X4)*count, palettePtr);`

**검증 1b**: 여전히 **바인드 포즈**가 정확히 렌더되면 규약/전치 OK. 메시가 터지거나(전치 실수),
쭈그러들면(곱 순서 실수) → §3(A)와 Model 정답지(`Model.cpp:229-234`, `DepthOnlyVS.hlsl`)를 대조.

### Stage 1c — 시간 t 팔레트 갱신 + CPU 정점 스키닝 제거
목표: 실제 애니메이션 재생. 매 프레임 바뀌는 건 팔레트뿐.

할 일:
1. 팔레트 채울 때 `boneGlobal[b] = src->EvaluateGlobalTransform(t)`를 **현재 시각 t**로(리타깃이면
   `animBones[b]`, 없으면 `bones[b]` — 기존 `SkinAtTime:104-108` 분기 그대로).
2. `ModelData::Update`는 이제 **시간 진행 + 팔레트 갱신만**. `SkinAtTime`의 정점 재계산 루프
   (`:110-150`)는 **삭제**한다(GPU가 대신함).
3. 정적 VB로 전환: 로드 시 `GpuBuffer`(예: ByteAddressBuffer)를 1회 `Create`해 두고
   `ctx.SetVertexBuffer(0, vb.VertexBufferView())`로 바인딩. `SetDynamicVB`(매 프레임 업로드) 제거.

**검증 1c (Stage 1 완료 기준)**:
- 회색 배경 위 흰색 캐릭터가 **Walking 동작을 재생**(프레임마다 포즈 변화, 메시 온전, 관절 자연스럽게 굽음).
- 동적 VB 재업로드가 사라지고 매 프레임 **팔레트(SRV)만** 갱신됨.
- CPU 스키닝(`SkinnedVertices`/`SkinAtTime` 정점 계산) 제거됨.

---

## 5. Stage 2/3 — 자체 스켈레톤·클립 (정석 완성, FBX 런타임 제거)

Stage 1까지면 "GPU 스키닝"은 달성이다. 아래는 런타임에서 FBX SDK를 걷어내는 정석화.

- **Stage 2 — 스켈레톤 구조 보존**: `MeshSkin`이 `FbxNode*` 대신
  `Skeleton{ int parentIdx[]; Matrix4 invBind[]; }`를 갖도록 로드 시 추출. 팔레트 계산을
  "노드 글로벌 world × invBind"(Model 정답지 방식)로 정리.
- **Stage 3 — 자체 클립 샘플링**: 로드 시 본별 키프레임(T/R/S)을 `AnimationClip`으로 추출.
  런타임 `Sample(t)` = position/scale **Lerp** + rotation **Slerp**(쿼터니언) → 로컬 포즈 →
  계층 DFS로 글로벌 포즈(`Model.cpp:185-226` 패턴) → 팔레트. `EvaluateGlobalTransform` 완전 제거.
- **Stage 4(선택) — 블렌딩/상태머신**: 두 클립의 로컬 포즈를 가중 blend(Slerp), Idle↔Walk 전환.

---

## 6. 검증 원칙
- **단계마다 빌드(Character Debug|x64, 오류 0) + 실행 육안 확인**. 절대 두 단계를 한꺼번에 하지 말 것 —
  1a(정점 포맷) → 1b(규약/전치) → 1c(애니메이션)로 나눈 이유는 **버그를 한 번에 하나만** 만나기 위해서다.
- 이상 증상 → 원인 힌트:
  - 메시가 산산조각/뒤집힘 → §3(A) 전치 또는 FbxAMatrix 곱 순서. Model 정답지와 대조.
  - 상하/앞뒤 반전 → §3(B) Z 반전 위치.
  - 일부 정점만 튐 → §3(C) 상위4 선택/재정규화 또는 본 인덱스 매핑.
  - 컬링으로 사라짐 → 현재 `RasterizerTwoSided` 유지 확인(`Renderer.cpp:47`).
