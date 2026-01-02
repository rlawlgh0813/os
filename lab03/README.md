# LAB03 — Memory Management Introspection in xv6

xv6 운영체제의 메모리 관리 구조를 확장하여 물리 프레임 추적, 가상→물리 주소 변환 관측, IPT(Inverted Page Table), Software TLB, Copy-on-Write(COW)를 하나의 일관된 설계로 구현한 프로젝트입니다.
이 프로젝트의 핵심은 단순한 기능 추가가 아니라, '커널이 실제로 어떤 메모리를 누구에게 할당했고, 그 매핑이 언제 생성·변경·해제되는지'를 관측 가능하게 만드는 것입니다.

---

## Project Overview

- **Base OS**: MIT xv6 (RISC-V)
- **Category**: Operating System / Memory Management
- **Core Topics**
  - Physical Frame Tracking
  - Page Table Walk & Address Translation
  - Inverted Page Table (IPT)
  - Software TLB (STLB)
  - Copy-on-Write (COW) Consistency
- **Artifacts**
  - Modified xv6 kernel (memory subsystem)
  - New system calls: `dump_physmem_info`, `vtop`, `phys2virt`
  - User-space validation tools (`memdump`, `vtop`, `ctest`)

---

## What I Built (High-Level)

이 Lab은 세 개의 축으로 구성됩니다.

### A. Physical Frame Tracking
→ **“지금 이 순간, 어떤 프로세스가 어떤 물리 프레임을 쓰고 있는가?”**

### B. Validation Programs
→ **동시 실행·해제 상황에서도 프레임 관리가 일관적인지 검증**

### C. Address Translation Introspection
→ **가상 주소 → 물리 주소 변환 과정을 사용자 수준에서 관측**

이 세 축이 **서로 독립적으로 동작하지 않고**,  
페이지 할당·해제·COW 상황에서 **항상 함께 동기화되도록 설계**한 것이 핵심입니다.

---

## A. Physical Frame Tracking

### Design Goal
기존 xv6는 `kalloc()` / `kfree()`를 통해 물리 페이지를 관리하지만,
- 누가 쓰는지
- 언제부터 쓰는지
- 언제 해제되는지  
를 추적할 수 없습니다.

이를 해결하기 위해 **전역 물리 프레임 메타데이터 테이블**을 도입했습니다.

### Key Data Structure
**struct physframe_info**
- 모든 PFN(Page Frame Number)에 대해 1:1 대응
- pf_lock 스핀락으로 보호
- 페이지 할당/해제 시 즉시 갱신

### System Call
**dump_physmem_info(buf, max)**
- 커널의 프레임 테이블을 사용자 공간으로 안전하게 복사
- copyout() + 락 기반 스냅샷 보장

---

## B. Validation Programs

프레임 관리가 “이론적으로 맞다”가 아니라 실제로 멀티 프로세스 환경에서도 맞는지를 검증하기 위해 전용 사용자 테스트 프로그램을 작성했습니다.

- **`memstress`**
  - 다수의 페이지를 동적으로 할당하여 메모리 부하 생성
- **`memdump`**
  - 특정 시점의 물리 프레임 점유 현황 출력
- **`memtest`**
  - 위 두 프로그램을 조합한 자동 검증 도구

### Validation Results
- 프레임 중복 할당 없음
- 프로세스 종료 후 모든 프레임 즉시 회수
- 동시 실행 환경에서도 `pf_info` 테이블의 일관성 유지

---

## C. Address Translation Introspection

이 단계의 목표는 '가상주소 변환 과정을 커널 밖에서도 관측 가능하게 만드는 것'입니다.
이를 위해 다음 세 가지 구성 요소를 결합했습니다.

### 1. `vtop()` — Software Page Walker

- 사용자 가상주소를 입력받아
  - 물리주소
  - P/U/W 권한 비트
  를 반환하는 시스템 콜

**Lookup Path**
1. Software TLB (STLB) 조회
2. miss 발생 시 `walkpgdir()`로 페이지 테이블 직접 탐색
3. 필요 시 IPT와 교차 검증하여 결과 확정


### 2. IPT (Inverted Page Table)

기존 xv6는 VA → PA 단방향 구조입니다.  
IPT를 도입해 **PFN 기준 역추적**이 가능하도록 확장했습니다.
- `(pfn, pgdir, va, perm)` 단위로 매핑 관리
- PFN별 참조 수(refcnt) 유지
- 프로세스 종료 시 일괄 제거

이를 통해
- 공유 페이지(COW) 추적
- 유령 매핑 제거
- use-after-free 방지
가 가능해졌습니다.


### 3. Software TLB (STLB)

- `(pgdir, vpg) → (ppg, flags)` 캐시
- 해시 + 체인 구조
- hit/miss 통계 제공

**Design Principles**
- 캐시는 성능을 위한 보조 수단
- 진실의 근원(source of truth)은 Page Table + IPT
- unmap / exit / COW 시 **즉시 invalidate**

---

## Copy-on-Write (COW) Consistency

fork 시:
- 부모 PTE의 W 비트 제거
- IPT/STLB 정보 갱신
- HW TLB flush

첫 write fault 발생 시:
- 새 물리 페이지 할당
- IPT 참조 분리
- STLB 무효화 및 재삽입

이로써
- 부모/자식 간 페이지 공유
- 쓰기 시 분기
- 종료 후 정리
의 전 과정에서 주소 변환 결과가 즉시 수렴하도록 보장했습니다.