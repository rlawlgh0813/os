# LAB02 — Stride Scheduling in xv6

xv6 운영체제의 기본 라운드 로빈 스케줄러를 **Stride Scheduling 기반으로 재설계·구현**한 프로젝트입니다. 커널 수준에서 CPU 자원 분배 정책을 직접 구현하며, 공정성·비례성·안정성을 만족하는 스케줄러의 동작 원리를 학습했습니다.
본 프로젝트에서는 단순히 알고리즘을 구현하는 데 그치지 않고, 프로세스 생애주기, 타이머 인터럽트, 커널 동기화, 오버플로우 대응까지 포함한 실제 운영체제 스케줄러의 현실적인 설계 요소를 반영했습니다.

---

## Project Overview

- **Base OS**: MIT xv6 (RISC-V)
- **Category**: Operating System / CPU Scheduling
- **Key Topics**
  - Stride Scheduling
  - Kernel-level Scheduler Design
  - Timer Interrupt & Preemption
  - Fair CPU Share & Proportional Allocation
- **Artifacts**
  - Modified xv6 kernel (scheduler, trap, proc)
  - Custom system call (`settickets`)
  - Scheduler validation test programs

---

## Why Stride Scheduling?

xv6의 기본 스케줄러는 **라운드 로빈 방식**으로,
모든 RUNNABLE 프로세스에 동일한 실행 기회를 제공합니다.

이 방식은 단순하지만,
- 프로세스 중요도 반영 불가
- CPU 점유율 제어 불가
- 단기적 변동성이 큼
이라는 한계가 있습니다.

Stride Scheduling은 **티켓(ticket)** 을 통해 프로세스별 CPU 점유 비율을 명시적으로 제어할 수 있으며, 예측 가능하고 공정한 실행을 보장합니다.

---

## What I Implemented

### 1. Process Structure Extension

Stride 스케줄링을 위해 `struct proc`을 확장했습니다.

- `tickets` : CPU 점유 비율 기준
- `stride` : `STRIDE_MAX / tickets`
- `pass` : 누적 실행 기준값
- `ticks` : 실제 실행된 tick 수
- `end_ticks` : 프로세스 수명 제한

프로세스 생성 시(`allocproc`) 모든 필드를 일관된 초기값으로 설정하여,  
이후 시스템 콜과 스케줄러 로직이 안정적으로 동작하도록 설계했습니다.


### 2. `settickets()` System Call

사용자 프로그램이 **자신의 CPU 점유율과 실행 수명**을 직접 설정할 수 있도록  
`settickets(tickets, end_ticks)` 시스템 콜을 구현했습니다.

**Responsibilities**
- 사용자 인자 검증 (`argint`)
- 티켓 수 범위 검증 (오버플로우 방지)
- `stride`, `tickets`, `end_ticks` 커널 필드 갱신

이 시스템 콜을 통해,  
사용자 공간의 요청이 커널 스케줄링 정책에 직접 반영되도록 연결했습니다.


### 3. Timer Interrupt–Driven Pass Update

Stride 스케줄링의 핵심은 스케줄러가 아니라 타이머 인터럽트 경로에서 pass를 누적하는 점입니다.
- 매 타이머 tick마다
  - `ticks++`
  - `pass += stride`
- `end_ticks` 도달 시 자동 `exit()`
- 이후 `yield()`로 선점 발생
이를 통해 완전한 선점형(preemptive) 스케줄링을 구현했습니다.


### 4. Stride-Based `scheduler()`

기존 라운드 로빈 대신,
- RUNNABLE 프로세스 중 **가장 작은 pass**
- 동률 시 **PID가 작은 프로세스**
를 선택하도록 `scheduler()`를 수정했습니다.

**Rebase Strategy**
- `PASS_MAX` 초과 시 overflow 방지
- 최소 pass 기준으로 재정렬
- `DISTANCE_MAX`로 격차 제한

이 설계를 통해:
- 장시간 실행에도 안정성 유지
- 공정성과 비례성 동시 확보


### 5. Kernel Debug Logging

스케줄러 동작을 **정확히 검증**하기 위해,
커널에 디버그 로그를 삽입했습니다.

- `fork()` : 프로세스 생성 시점
- `trap()` : 매 tick 선택 정보
- `exit()` : 종료 시점

이를 통해:
- 생성 → 실행 → 종료 흐름
- pass 증가 패턴
- ticket 비례 CPU 분배
를 로그 기반으로 검증했습니다.

---

## Validation & Results

다양한 테스트 시나리오를 통해 다음을 확인했습니다.
- 동일 ticket → 라운드 로빈과 동일한 분배
- 상이한 ticket → CPU 점유율이 ticket 비율에 근접
- 낮은 ticket 프로세스 → 큰 stride → 선택 빈도 감소
- PASS_MAX 초과 → rebase 정상 동작
- 동적 프로세스 생성/종료 환경에서도 공정성 유지

Stride Scheduling의 이론적 특성이 실제 커널 실행 결과로 재현됨을 확인했습니다.