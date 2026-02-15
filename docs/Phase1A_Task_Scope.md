# HA Replication Engine
## Phase 1A – Windows KMDF Blocking Event Channel
**Scope Document for Implementation (Antigravity Input)**
Version: 1.0  
Status: EXECUTION READY  
Platform: Windows x64 (EWDK 26100)  
Language: C (KMDF)  

---

# 1. Objective

Phase 1A implements a stable, cancel-safe, blocking kernel → user event delivery mechanism.

This phase does NOT implement:
- Disk/volume interception
- Block replication
- Network protocol
- WAL
- Bitmap
- Sync/Async logic

The only goal is:

> Build a KMDF driver + user-mode test client with a blocking IOCTL-based event channel.

If Phase 1A is not 100% stable, do NOT proceed to Phase 1B.

---

# 2. Deliverables

## 2.1 Kernel Driver (KMDF)

Driver name: HAStorRep  
Device: `\\Device\\HAStorRepCtl`  
Symbolic link: `\\??\\HAStorRepCtl`  
User open path: `\\\\.\\HAStorRepCtl`

### Required IOCTLs

- IOCTL_HA_REGISTER_ENGINE
- IOCTL_HA_GET_EVENTS (blocking)
- IOCTL_HA_ACK_EVENTS

All IOCTLs must use METHOD_BUFFERED.

---

## 2.2 User-Mode Test Client

Program name: HATestClient.exe

Behavior:
1. Open `\\\\.\\HAStorRepCtl`
2. Call REGISTER
3. Start GET_EVENTS loop
4. Print event count and fields
5. Handle Ctrl+C cleanly

---

# 3. Hard Constraints

These rules are mandatory.

1. No filter attachment.
2. No disk interception.
3. No network.
4. No WAL.
5. No advanced replication logic.
6. No global locks blocking indefinitely.
7. Driver must unload safely.
8. Driver must pass basic Driver Verifier checks.
9. GET_EVENTS must block properly (pending request).
10. Canceling client must NOT cause BSOD.

---

# 4. ABI Contract

Use `public.h` exactly as provided.
Field sizes must not change.
Struct packing must remain stable.

---

# 5. Driver Architecture

## 5.1 Components

Driver.c
- DriverEntry
- EvtDriverUnload

Device.c
- Create control device
- Create symbolic link
- Configure queues

Ioctl.c
- Handle IOCTL dispatch
- Manage pending GET_EVENTS
- Complete requests with event batches

RingBuffer.c
- Internal event ring implementation
- NonPagedPoolNx allocation
- Thread-safe push/pop

Trace.h
- DbgPrint macros

---

# 6. Internal Data Structures

## 6.1 Event Ring

- Allocation: NonPagedPoolNx
- Capacity: 1024 events (initial)
- Lock: WdfSpinLock
- Must not silently drop events

If ring full:
- Emit HA_EVT_OVERFLOW
- Or block producer (if safe)

---

## 6.2 Pending Request Queue

Use:
- WdfIoQueueDispatchManual

Behavior:
- If no events → store request pending
- If events available → complete immediately
- On event arrival → if pending exists → complete with batch
- Cancel-safe

---

# 7. IOCTL Behavior

## 7.1 IOCTL_HA_REGISTER_ENGINE

Input:
- pid
- magic check

Output:
- driver_version
- session_id
- limits

Must:
- Validate magic
- Store pid

For testing:
- Push one HA_EVT_TEST event into ring.

---

## 7.2 IOCTL_HA_GET_EVENTS

Input:
- session_id
- max_events
- timeout_ms

Behavior:
- If events available → return immediately.
- If no events → hold request pending.
- If timeout expires → return 0 events.
- Must be cancel-safe.

---

## 7.3 IOCTL_HA_ACK_EVENTS

For Phase 1A:
- Accept input
- Validate session
- Return success
- No complex logic yet

---

# 8. Cancel Safety Requirements

- Use KMDF queue cancel callbacks.
- If client exits:
  - Pending request must be canceled.
  - No invalid pointer access.
  - No use-after-free.
- On driver unload:
  - Cancel all pending GET_EVENTS.
  - Free ring memory.
  - Delete symbolic link.
  - Delete device object.

---

# 9. Testing Requirements

## 9.1 Basic Functional Test

1. Load driver
2. Start HATestClient
3. REGISTER succeeds
4. GET_EVENTS blocks
5. Driver inserts HA_EVT_TEST → client receives
6. Ctrl+C client → no crash
7. Stop driver → no crash

---

## 9.2 Driver Verifier Test

Enable:
- verifier /standard /driver HAStorRep.sys

Must:
- Not crash
- Not leak memory
- Not deadlock

---

# 10. Explicitly Forbidden in Phase 1A

DO NOT:
- Attach to disk stack
- Hook WRITE/FLUSH
- Implement replication
- Use global static unsafe memory without locks
- Implement networking
- Implement WAL

If code contains any of these, reject and restart Phase 1A.

---

# 11. Completion Criteria

Phase 1A is complete only when:

- Blocking GET_EVENTS works reliably.
- Cancel safe.
- Unload safe.
- Verifier clean.
- No BSOD under:
  - rapid open/close
  - repeated load/unload
  - 100 test loops

Only after passing these criteria may Phase 1B begin.

---

# 12. Implementation Output Requirements (Antigravity)

Antigravity must output:

1. Full file tree.
2. All source files complete.
3. Build steps.
4. Test instructions.
5. INF file.
6. Service creation instructions.

Code must compile under:
EWDK_ge_release_svc_prod1_26100_250904-1728.iso

---

END OF PHASE 1A SCOPE
