# LAB01 — Extending xv6 System Calls

xv6 교육용 운영체제를 기반으로 시스템 콜을 직접 설계·구현·검증하며 사용자 공간과 커널 공간 간의 인터페이스, 트랩 처리 흐름, 프로세스 메타데이터 접근 방식을 학습한 프로젝트입니다.
이 프로젝트는 단순한 기능 추가가 아니라, '사용자 함수 호출이 커널 내부 로직으로 안전하게 연결되고 다시 사용자 공간으로 복귀하는 전체 경로'를 직접 구현하고 추적하는 데 초점을 두었습니다.

---

## Project Overview

- **Base OS**: MIT xv6 (RISC-V)
- **Category**: Operating System / Kernel Programming
- **Core Topics**
  - System Call Mechanism
  - User ↔ Kernel Mode Transition
  - Trap & Dispatch Flow
  - Process Table Synchronization
- **Artifacts**
  - Modified xv6 kernel source
  - System call specification & implementation report
  - User-space test programs

---

## What I Implemented

### 1. `hello_number()` — Custom Arithmetic System Call

사용자가 전달한 정수를 커널에서 처리한 뒤,
- 커널 콘솔에 메시지를 출력하고
- 입력값의 **2배를 사용자 공간으로 반환**하는 시스템 콜을 구현했습니다.

**Key Points**
- `argint()`를 이용한 사용자 인자 안전 검증
- `cprintf()`를 통한 커널 영역 출력
- 반환값이 트랩 프레임을 통해 사용자 공간으로 전달되는 흐름 확인

---

### 2. `get_procinfo()` — Process Introspection System Call

특정 프로세스의 내부 정보를 **사용자 프로그램이 직접 조회할 수 있도록** 시스템 콜을 확장했습니다.

조회 가능한 정보:
- PID / Parent PID
- Process State
- Address Space Size
- Process Name

**Design Highlights**
- `ptable.lock`을 이용해 프로세스 테이블 접근 시 동기화 보장
- `argptr()` + `copyout()`을 사용하여 **커널 → 사용자 버퍼 안전 복사**
- `pid <= 0`일 경우 자기 자신을 조회하도록 설계

이 시스템 콜은 단순 출력이 아닌,
**커널 내부 자료구조를 사용자 영역으로 노출할 때 필요한 안정성 설계**를 경험하게 해준 핵심 구현입니다.

---

## System Call Integration Flow

새로운 시스템 콜은 xv6의 기존 구조를 유지한 채 다음 경로로 연결했습니다.

1. **User Interface**
   - `user.h`에 함수 프로토타입 및 구조체 정의
2. **Assembly Stub**
   - `usys.S`에 `SYSCALL()` 매크로 등록
3. **System Call Numbering**
   - `syscall.h`에 고유 번호 정의
4. **Dispatch Table**
   - `syscall.c`의 `syscalls[]` 테이블에 매핑
5. **Kernel Handler**
   - `sysproc.c`에서 실제 로직 구현

---

## User Programs for Validation

시스템 콜 검증을 위해 사용자 프로그램을 직접 작성하고,
Makefile을 수정해 xv6 파일 시스템 이미지에 포함시켰습니다.

- **`helloxv6`**
  - `hello_number()` 호출 및 반환값 검증
- **`psinfo`**
  - `get_procinfo()` 호출
  - 정상/비정상 PID에 대한 동작 확인

테스트 결과:
- 정상 입력에 대해 정확한 반환값 확인
- 존재하지 않는 PID에 대해 오류 처리 정상 동작