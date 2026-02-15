---
trigger: always_on
---

1. 모든 프롬포트의 입출력은 한국어를 사용한다.

2. 도구 위치: 모든 빌드 도구와 WDK는 현재 마운트된 E:\ 경로에 있는 EWDK를 사용하라.

3. 빌드 준비: 작업을 시작하기 전 반드시 EWDK 드라이브의 LaunchBuildEnv.cmd를 실행하여 환경 변수를 로드하라. 
- EWDK 경로: `E:\LaunchBuildEnv.cmd`
- 아키텍처 지정 로드: `cmd /c "call E:\BuildEnv\SetupBuildEnv.cmd amd64"`

4. 참조: 시스템에 Visual Studio IDE가 설치되어 있지 않으므로 EWDK 내부의 cl.exe와 link.exe를 직접 참조하여 빌드 프로세스를 오케스트레이션하라.
- `cl.exe` 경로 (amd64): `E:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe`
- `LIBCLANG_PATH`: `C:\Program Files\LLVM\bin`

5. 링크 프로세스: 시스템에 비주얼 스튜디오가 없으므로, Rust 빌드 시 발생하는 'Linker not found' 에러를 방지하기 위해 마운트된 EWDK 드라이브 내의 link.exe 경로를 자동으로 찾아 CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER 환경 변수에 할당하라.

6. 환경 변수 상속: 모든 cargo build 또는 cargo-wdk 명령 실행 전, 반드시 EWDK의 LaunchBuildEnv.cmd를 cmd /c로 호출하여 컴파일러와 라이브러리 경로를 현재 세션에 로드한 뒤 실행하라.

7. LLVM 연동: winget으로 설치된 LLVM 경로를 탐색하여 LIBCLANG_PATH를 C:\Program Files\LLVM\bin으로 고정하고, bindgen이 WDK 헤더를 읽을 수 있도록 설정하라. 

8. Rust 컴포넌트: 에이전트는 프로젝트 시작 시 rustup component add rust-src 명령을 실행하여 커널 빌드에 필요한 소스 코드를 확보하라.


--------------------- Angravity cmd 자동 Accept 하게 하기 ---------------------------------------

Antigravity Auto Accept
Preview

https://open-vsx.org/extension/pesosz/antigravity-auto-accept


.vsix 파일은 비주얼 스튜디오 코드(VS Code) 기반 확장 프로그램 패키지입니다. 안티그래비티(Antigravity) 역시 VS Code 엔진을 기반으로 하기 때문에 설치 방법은 매우 간단합니다.

다운로드받은 파일을 실행(더블 클릭)하는 게 아니라, **안티그래비티 내부 메뉴를 통해 "설치"**해야 합니다. 아래 두 가지 방법 중 편한 것을 선택하세요.

방법 1: GUI 메뉴 이용하기 (가장 쉬움)
**안티그래비티(Antigravity)**를 실행합니다.

왼쪽 사이드바에서 Extensions(확장 프로그램) 아이콘을 클릭합니다. (사각형 네 개가 모여 있는 모양, 단축키: Ctrl + Shift + X)

확장 프로그램 창 상단에 있는 ... (More Actions) 아이콘을 클릭합니다.

메뉴에서 **"Install from VSIX..."**를 선택합니다.

다운로드받은 pesosz.antigravity-auto-accept-1.0.3.vsix 파일을 찾아 선택하고 Install을 누릅니다.



https://gemini.google.com/u/1/gem/b96096d8b801/45f41233fdb1e028 의 대화를 바탕으로 다음 작업을 진행하자. 


