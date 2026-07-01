# FBX SDK 링크 에러 해결 기록

`Bug-LinkError` 브랜치에서 FBX SDK를 `Character`/`Model`/`ModelViewer` 프로젝트에 연동하던 중 빌드가 깨진 문제를 진단하고 해결한 기록이다. 문제가 한 겹이 아니라 여러 겹으로 겹쳐 있었기 때문에, 나중에 비슷한 FBX/NuGet 빌드 문제가 재발했을 때 참고할 수 있도록 마주친 순서대로 남긴다.

## 1. 증상

FBX SDK 연동 후 `Character.sln` 빌드가 링크 단계에서 실패했다. 직전 커밋(`723b098 Fix: 메인 프로젝트 링크 에러 수정`)이 `FBX.cpp`의 네임스페이스 문제와 절대경로 하나를 고쳤지만, 실제로는 더 근본적인 원인이 남아있어 여전히 빌드되지 않는 상태였다.

## 2. 원인 분석

### 2.1 벤더 카피에 라이브러리 바이너리가 없음

이 저장소는 서드파티 SDK를 `Packages\` 밑에 커밋하는 컨벤션을 쓴다(directxtex, directxmesh, zlib, assimp, WinPixEventRuntime 등). 그런데 `Packages\FBX-SDK.2020.3.9\`에는 **헤더만** 수동으로 복사되어 있고 `lib\` 폴더 자체가 없었다. `Character.vcxproj`/`Model.vcxproj`는 `AdditionalLibraryDirectories`를 `..\..\Packages\FBX-SDK.2020.3.9\lib\x64\$(Configuration)`로 하드코딩하고 있었는데, 이 경로가 존재하지 않으니 링커가 실패(`LNK1104`)할 수밖에 없었다.

실제 컴파일된 라이브러리를 `Packages\`에 함께 커밋하는 방식은 기각했다 — 이 머신에 설치된 FBX SDK 2020.3.9 기준 `libfbxsdk-md.lib`만 Debug 230MB, Release 130MB로 다른 SDK들과 비교해 git에 넣기엔 너무 크다.

### 2.2 `FbxSdk.props`가 죽은 코드였음

이 문제를 풀기 위해 만들어진 것으로 보이는 `MiniEngine\PropertySheets\FbxSdk.props`가 이미 존재했지만 세 가지 이유로 작동하지 않았다:

- `FBXSDK_ROOT` 기본값이 실제 설치 버전(2020.3.9)이 아닌 `2020.3.4`를 가리킴
- `FbxLibDir`가 실제 설치 경로에 없는 `vs2019` 서브폴더(`lib\vs2019\x64\...`)를 끼워 넣음 — 실제로는 `lib\x64\debug`/`lib\x64\release`
- 계산된 변수(`FbxIncludeDir`/`FbxLibDir`/`FbxDllPath`)를 어떤 `.vcxproj`도 실제로 참조하지 않음 (전체 리포 grep 결과 0건). 대신 모든 프로젝트가 `Packages\FBX-SDK.2020.3.9\...`를 직접 하드코딩

### 2.3 `ModelViewer.vcxproj`에 FBX 설정이 아예 없음

`ModelViewer.vcxproj`는 include 경로, lib 경로, FBX 관련 `AdditionalDependencies` 중 아무것도 갖고 있지 않았다. 하지만 `Model\ModelLoader.cpp`(194~200행)가 `FBX::Asset`을 직접 생성하고 `BuildModel`을 호출하므로, `Model.lib`를 링크하는 `ModelViewer.exe`도 최종 링크 시 FBX SDK 심볼이 필요하다. Character의 링크 에러만 고치면 ModelViewer 쪽에서 같은 에러가 새로 터지는 구조였다.

### 2.4 핵심 버그 — 정적 라이브러리와 DLL 임포트 라이브러리 혼동

Character/Model 프로젝트는 `FBXSDK_SHARED`(DLL 방식 사용을 의미)를 정의해놓고, 실제로는 `libfbxsdk-md.lib`를 링크하고 있었다. FBX SDK 설치 폴더의 파일 크기를 비교해보면:

```
libfbxsdk.dll        16 MB
libfbxsdk.lib        4.5 MB   ← DLL 임포트 라이브러리 (정상)
libfbxsdk-md.lib      230 MB  ← 정적 라이브러리 (SDK 전체 오브젝트 코드 포함)
libfbxsdk-mt.lib      256 MB  ← 정적 라이브러리 (/MT 버전)
```

`libfbxsdk-md.lib`는 이름의 `-md`가 "DLL"을 의미하는 게 아니라 "정적 라이브러리를 `/MD` 런타임으로 빌드했다"는 뜻이었다. `FBXSDK_SHARED`가 정의되면 `fbxsdk.h`가 심볼을 `__declspec(dllimport)`로 선언하는데, 정적 라이브러리에는 그 dllimport 썽크(`__imp_...`)가 없으니 링커가 `LNK2019 unresolved external symbol ... __imp_...`를 대량으로 뱉었다. 올바른 선택은 DLL과 짝을 이루는 작은 임포트 라이브러리 `libfbxsdk.lib`였다.

### 2.5 `Model.vcxproj`가 Debug 구성에만 `FBXSDK_SHARED`를 정의

`libfbxsdk.lib`로 바꾼 뒤 Debug는 바로 통과했지만 Release에서 새로운 링크 에러(`LNK2001`, FbxSystemUnit 등 static 멤버 미해석)가 났다. 원인은 `Model.vcxproj`가 `FBXSDK_SHARED` 전처리기 정의를 Debug 구성에만 넣고 Profile/Release에는 빠뜨려서, Release로 컴파일된 `FBX.obj`가 dllimport가 아닌 일반 정적 링크를 기대하고 있었기 때문이다.

### 2.6 `libfbxsdk.dll`을 복사하는 절차가 없음

`FbxDllPath`가 props에 계산돼 있었지만 실제로 이 값을 쓰는 PostBuildEvent나 타겟이 전혀 없었다(`libfbxsdk.dll`, `FbxDllPath` grep 결과 props 자체 정의 외 0건). 링크가 성공해도 실행 시 DLL을 찾지 못해 바로 크래시했을 것이다.

### 2.7 FBX와 무관하지만 빌드를 막고 있던 사전 존재 버그들

FBX SDK 관련 설정을 전부 고친 뒤에도 빌드가 계속 막혔는데, 원인은 이 리포에 있던 별개의 사전 존재 문제들이었다:

- **`Core.vcxproj`의 오타**: NuGet import의 `Condition="Exists(...)"`에는 `..\Packages\...`(한 단계)를 쓰고 실제 `Import Project="..."`에는 `..\..\Packages\...`(두 단계)를 써서, 조건이 항상 거짓으로 평가되어 zlib/WinPixEventRuntime import가 건너뛰어지고 `EnsureNuGetPackageBuildImports` 타겟에서 "패키지 없음" 에러가 났다.
- **복원되지 않은 NuGet 패키지**: `zlib-msvc-x64`, `WinPixEventRuntime`, `Assimp`(x2), `directxmesh_desktop_win10`, `directxtex_desktop_win10` 패키지가 `.nupkg` 원본 그대로 커밋되어 있고 실제로 압축 해제(restore)된 적이 없었다.
- **소스 인코딩 문제**: `FBX.h`/`FBX.cpp`/`BuildFBX.cpp`가 BOM 없는 CP949(한글 예전 인코딩)로 저장되어 있어서, 이 머신의 로케일/컴파일러 설정에서는 MSVC가 한글 주석 바이트를 UTF-8로 잘못 해석해 `error C2018: 문자를 인식할 수 없습니다`로 컴파일 자체가 실패했다.
- **`Character.vcxproj`의 구버전 경로**: 존재하지 않는 `directxmesh_desktop_win10.2019.2.7.1` / `directxtex_desktop_win10.2019.2.7.1` 경로를 하드코딩하고 있었다. 실제로 저장소에 있는 건 `2021.1.10.1`/`2021.1.10.2`뿐이었고, `ModelViewer.vcxproj`는 이미 해당 버전의 `.targets` 파일을 올바르게 import하고 있었다.
- **`Character.vcxproj`의 CRT 불일치**: Profile 구성이 `RuntimeLibrary`를 `MultiThreadedDebugDLL`(`/MDd`)로 잘못 재정의하고 있어서, `Build.props` 기본값(`/MD`)으로 빌드된 `Core.lib`/`Model.lib`와 CRT가 어긋나 `LNK2038` 불일치 에러가 났다. Debug/Release가 정상화되고 나서야 Profile 빌드에서 이 문제가 드러났다.

### 2.8 시행착오: 툴셋 불일치 판단이 상황에 따라 달라짐

계획 수립 단계에서 `Character`/`Model`/`ModelViewer`의 `PlatformToolset`을 확인했을 때는 셋 다 `v142`로 동일해서 툴셋 불일치는 원인이 아니라고 판단했다. 그런데 구현 도중 Visual Studio가 (세션과 별개로) `Character`/`Core`/`Model`을 `v143`으로 자동 재타겟하면서, `ModelViewer`만 `v142`로 남아 실제로 툴셋 불일치가 발생했다(`__std_find_trivial_4` 등 STL 런타임 심볼 미해석). 처음엔 근거 없다고 결론 내렸던 것이 상황이 바뀌면서 실제로 재현된 사례로, 파일 상태는 세션 도중에도 바뀔 수 있으니 재검증이 필요하다는 교훈을 남긴다.

## 3. 해결 방법

| 원인 | 수정 파일 | 내용 |
|---|---|---|
| 2.2 `FbxSdk.props` 오류 | `MiniEngine/PropertySheets/FbxSdk.props` | `FBXSDK_ROOT` 기본값을 `2020.3.9`로, `FbxLibDir`를 `$(FBXSDK_ROOT)\lib\x64\$(FbxLibSubDir)`로 수정(`vs2019` 세그먼트 제거). `FbxIncludeDir`는 커밋된 헤더(`Packages\FBX-SDK.2020.3.9\include`)를 계속 사용하도록 `$(MSBuildThisFileDirectory)` 기준 경로로 지정 |
| 2.1, 2.2 하드코딩 경로 | `MiniEngine/Character/Character.vcxproj`, `MiniEngine/Model/Model.vcxproj` | `IncludePath`/`LibraryPath`/`AdditionalIncludeDirectories`/`AdditionalLibraryDirectories`의 `Packages\FBX-SDK.2020.3.9\...` 하드코딩을 전부 `$(FbxIncludeDir)`/`$(FbxLibDir)`로 교체. `Character.vcxproj`가 Profile 구성에만 하던 `FbxSdk.props` import를 Debug/Release에도 추가 |
| 2.3 ModelViewer 설정 누락 | `MiniEngine/ModelViewer/ModelViewer.vcxproj` | `FbxSdk.props` import, include 경로, `FBXSDK_SHARED` 정의, lib 경로와 `AdditionalDependencies` 추가 |
| 2.4 정적/DLL 라이브러리 혼동 | `Character.vcxproj`, `Model.vcxproj`, `ModelViewer.vcxproj` | `AdditionalDependencies`의 `libfbxsdk-md.lib`를 `libfbxsdk.lib`로 교체 |
| 2.5 `FBXSDK_SHARED` 누락 | `MiniEngine/Model/Model.vcxproj` | Profile/Release `ClCompile` 조건에도 `FBXSDK_SHARED` 전처리기 정의 추가 |
| 2.6 DLL 복사 누락 | `Character.vcxproj`, `ModelViewer.vcxproj` | `xcopy /y /d "$(FbxDllPath)" "$(OutDir)"`를 실행하는 `PostBuildEvent` 추가 |
| 2.7 `Core.vcxproj` 오타 | `MiniEngine/Core/Core.vcxproj` | `Condition="Exists(...)"`의 `..\Packages\...`를 `..\..\Packages\...`로 수정 |
| 2.7 미복원 NuGet 패키지 | `Packages/*` (커밋 대상 아님) | 각 패키지의 `.nupkg`를 로컬에서 압축 해제(`build`/`native` 폴더는 이미 `.gitignore`의 `build/` 규칙에 걸려 커밋되지 않음) |
| 2.7 소스 인코딩 | `MiniEngine/Model/FBX.h`, `FBX.cpp`, `BuildFBX.cpp` | CP949로 읽어 UTF-8(BOM 포함)로 재저장 |
| 2.7 구버전 경로 | `Character.vcxproj` | `directxmesh_desktop_win10.2019.2.7.1`/`directxtex_desktop_win10.2019.2.7.1` 하드코딩 제거, `ModelViewer.vcxproj`와 동일하게 `2021.1.10.x` `.targets` import로 전환 |
| 2.7 CRT 불일치 | `Character.vcxproj` | Profile 구성의 `RuntimeLibrary` 재정의(`MultiThreadedDebugDLL`)를 제거해 `Build.props` 기본값(`/MD`)을 따르도록 함 |

## 4. 검증 결과

- `Character.sln`을 `Debug|x64`, `Release|x64`, `Profile|x64` 각각 빌드 → 전부 에러 없이 성공.
- 각 출력 폴더(`Build\x64\<Config>\Output\Character\`)에 `libfbxsdk.dll`이 정상적으로 복사됨을 확인.
- `ModelViewer.sln`은 FBX SDK 관련 설정(2.3, 2.4 대응 수정)까지는 링크 에러 없이 통과했으나, 2.8에서 설명한 `v142`/`v143` 툴셋 불일치로 최종 링크 직전 단계(STL 런타임 심볼 미해석)에서 멈췄다. FBX 연동 자체는 이 지점까지 정상 동작이 확인된 상태다.

## 5. 남은 과제

- `ModelViewer.vcxproj`의 `PlatformToolset`을 `v143`으로 맞춰 `Character`/`Core`/`Model`과 통일하는 작업은 이번 범위에서 보류했다 (사용자 요청).
