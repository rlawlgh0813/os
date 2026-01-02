# LAB04 — Snapshot & Rollback with Block-level Copy-on-Write in xv6
xv6 파일시스템에 스냅샷 생성 / 롤백 / 삭제 기능을 추가하고, 블록 단위 Copy-on-Write(COW)와 참조수 기반 자원 회수(refcounted free)를 결합하여 공간 효율적이고 안전한 시점 복구(file system snapshot)를 구현한 프로젝트입니다.

본 과제의 핵심은  
“데이터를 복사하지 않고도 과거 시점을 안전하게 보존하고, 필요한 순간에만 정확하게 복제하며, 마지막 참조가 사라질 때에만 자원을 회수하는 파일시스템”을 구현하는 것입니다.

---

## Project Overview

- **Base OS**: MIT xv6 (RISC-V)
- **Category**: Operating System / File System
- **Core Topics**
  - File System Snapshot
  - Block-level Copy-on-Write (COW)
  - Reference Counting & Safe Deallocation
  - Journaling & Consistency
- **New System Calls**
  - `snapshot_create()`
  - `snapshot_rollback(int id)`
  - `snapshot_delete(int id)`
  - `print_addr(const char *path)`
- **Artifacts**
  - Snapshot-aware xv6 kernel
  - `/snapshot` read-only namespace
  - User-space validation tools (`mk_test_file`, `append`, `print_addr`)

---

## Design Goals

1. **Zero-copy Snapshot Creation**
   - 스냅샷 생성 시 데이터 블록을 복사하지 않음
2. **Block-level Copy-on-Write**
   - 쓰기가 발생한 블록만 지연 복제
3. **Precise Resource Reclamation**
   - 참조수가 0이 되는 순간에만 실제 블록 해제
4. **Strong Invariance**
   - 스냅샷 트리는 항상 읽기 전용
5. **Crash-safe Consistency**
   - xv6 저널링 규칙을 준수하며 트랜잭션 경계 유지

---

## A. Block-level Copy-on-Write (COW)

### Core Idea
- 스냅샷 생성 시:
  - **데이터 블록은 복사하지 않음**
  - 포인터만 공유하고 참조수(refcount) 증가
- 이후 쓰기 발생 시:
  - 참조수 > 1 → **새 블록 할당 + 전체 복사**
  - 현재 파일만 새 블록을 사용
  - 스냅샷은 과거 데이터 그대로 유지

### Reference Count Storage
- 참조수는 **`/snapshot/.refmap`** 파일에 저장
- 메모리 상 테이블 + 디스크 영속화 구조
- 데이터 블록만 추적 (메타데이터/비트맵 제외)

### Free Policy
- `bfree()`에서 즉시 해제 ❌
- **refcount 감소 → 0일 때만 실제 비트맵 해제**
- 중복 해제, use-after-free 구조적으로 차단

---

## B. Snapshot Subsystem

### `/snapshot` Namespace
- 모든 스냅샷은 `/snapshot/<id>` 형태
- 커널에서 **읽기 전용(R/O)** 강제

### 1. `snapshot_create()`
- 현재 루트(`/`)를 기준으로 스냅샷 생성
- 동작 요약:
  1. `/snapshot` 및 `.refmap` 지연 생성
  2. 새로운 `<id>` 디렉터리 생성
  3. 트리 구조만 재귀 복제
  4. 일반 파일의 데이터 블록 refcount++
- **데이터 복사 비용 없음**
- 성공 시 스냅샷 ID 반환

### 2. `snapshot_rollback(id)`
- 현재 파일시스템을 지정 스냅샷 시점으로 복구
- 동작 요약:
  1. 현재 루트에서 `/snapshot`만 남기고 정리
  2. `/snapshot/<id>` 트리를 루트로 재구성
  3. 데이터 블록은 그대로 공유 (ref++)
- 이후 쓰기는 자동으로 COW 분기
- **대용량 복사 없이 즉시 시점 복구**

### 3. `snapshot_delete(id)`

- `/snapshot/<id>` 트리 제거
- 파일/디렉터리 재귀 정리
- 데이터 블록은:
  - refcount 감소
  - 0이 되는 순간에만 실제 해제
- 공유 중인 블록은 안전하게 유지

---

## C. Validation & Introspection Tools

### `print_addr`
- 파일이 참조하는 디스크 블록 주소를 출력
- 출력 항목:
  - 직접 블록 (0~11)
  - 간접 포인터 블록
  - 간접 엔트리
- 용도:
  - 스냅샷 전/후 주소 동일성 확인
  - 쓰기 후 COW 분기 확인
  - 롤백 후 주소 복원 검증

---

## Correctness Guarantees

- 스냅샷은 항상 **불변**
- 변경은 현재 트리에서만 발생
- 공유 블록은 절대 조기 해제되지 않음
- 마지막 참조 시점에서만 안전한 회수
- 모든 변경은 `begin_op / end_op` 트랜잭션 경계 내 수행

---

## Key Takeaways

- 스냅샷은 “복사” 문제가 아니라 **참조 관리 문제**
- COW의 핵심은 **언제 복제할 것인가**, 그리고 **언제 해제할 것인가**
- refcount + COW + journaling이 결합될 때
  - 공간 효율
  - 성능
  - 안정성
  을 동시에 달성할 수 있음