# Operating Systems — xv6 Kernel Projects

> xv6 교육용 운영체제를 기반으로  
> **시스템 콜 → CPU 스케줄링 → 메모리 관리 → 파일시스템 스냅샷** 까지  
> 운영체제의 핵심 구성 요소를 단계적으로 확장·구현한 커널 프로젝트 모음입니다.

---

## Repository Overview

- **Base OS**: MIT xv6 (RISC-V)
- **Language**: C
- **Category**: Operating System / Kernel Programming
- **Focus Areas**
  - User ↔ Kernel Boundary
  - CPU Scheduling & Fairness
  - Memory Management & Address Translation
  - File System Design (Snapshot & COW)

os/
├─ lab01/ # System Calls & User–Kernel Boundary
├─ lab02/ # CPU Scheduling (Stride Scheduler)
├─ lab03/ # Memory Management & Address Translation
├─ lab04/ # File System Snapshot & Copy-on-Write
└─ README.md

---

## Learning Trajectory (Why This Order?)

1. **System Call**
   - 사용자 프로그램이 커널과 만나는 첫 관문
2. **Scheduler**
   - 커널이 CPU 자원을 어떻게 분배하는지
3. **Memory Management**
   - 커널이 메모리를 어떻게 추적·변환·보호하는지
4. **File System**
   - 영속 데이터와 일관성을 어떻게 보장하는지

---

## LAB01 — System Calls & User–Kernel Boundary

**핵심 질문**  
> “사용자 함수 호출은 어떻게 커널 코드 실행으로 이어지는가?”

### What I Did
- 새로운 시스템 콜 직접 설계 및 구현
- 시스템 콜 전체 경로 연결
  - user.h → usys.S → syscall.c → sysproc.c
- 커널 ↔ 사용자 공간 데이터 전달 (`argint`, `copyout`)

### What It Shows
- 시스템 콜은 단순한 함수가 아닌 **제어 흐름**
- 유저/커널 경계에서의 안전성 검증 능력

---

## LAB02 — CPU Scheduling (Stride Scheduler)

**핵심 질문**  
> “CPU 시간을 공정하고 예측 가능하게 나누려면?”

### What I Did
- xv6 라운드 로빈 스케줄러를 **Stride Scheduling**으로 교체
- `settickets()` 시스템 콜로 CPU 점유율 제어
- pass/stride 기반 선점형 스케줄링 구현
- overflow 방지를 위한 rebase 설계

### What It Shows
- 스케줄러는 정책 + 타이머 + 동기화의 결합체
- 공정성과 안정성을 동시에 고려한 설계 경험

---

## LAB03 — Memory Management & Address Translation

**핵심 질문**  
> “커널은 지금 어떤 메모리를 누가 쓰고 있는지 어떻게 아는가?”

### What I Did
- 물리 프레임 추적 테이블 구현
- 가상 → 물리 주소 변환 관측 시스템 콜 (`vtop`)
- IPT(Inverted Page Table) 도입
- Software TLB(STLB) 구현
- Copy-on-Write(COW) 정합성 검증

### What It Shows
- 메모리 관리는 **관측성 + 일관성 문제**
- 캐시 무효화와 참조 관리의 중요성 이해

---

## LAB04 — File System Snapshot & Copy-on-Write

**핵심 질문**  
> “데이터를 복사하지 않고 과거 상태를 보존할 수 있을까?”

### What I Did
- 파일시스템 스냅샷 생성/롤백/삭제 구현
- 블록 단위 Copy-on-Write(COW)
- refcount 기반 안전한 자원 회수
- `/snapshot` 읽기 전용 네임스페이스 설계

### What It Shows
- 스냅샷은 복사 문제가 아닌 **참조 관리 문제**
- 파일시스템에서의 일관성과 트랜잭션 이해