; spinlock.asm — x86-64 spinlock primitives
;
; void spin_lock(spinlock_t *lock)   ; rdi = pointer to uint32_t
; void spin_unlock(spinlock_t *lock) ; rdi = pointer to uint32_t
;
; spin_lock uses LOCK XCHG to atomically swap 1 into the lock word.
; If the old value was already 1 (locked) it spins with PAUSE until
; the lock word reads 0 (unlocked by another CPU or by an IRQ handler)
; before retrying.  PAUSE gives a pipeline hint that avoids memory-order
; speculation penalties in a spin loop.
;
; spin_unlock simply writes 0 with a plain store; on x86 stores to
; aligned 32-bit locations are already sequentially consistent with
; respect to other cores, so no LOCK prefix is needed.

global spin_lock
global spin_unlock

section .text

; ─────────────────────────────────────────────────────────────────────────────
; spin_lock(spinlock_t *lock)
;   rdi = address of the lock (volatile uint32_t)
; ─────────────────────────────────────────────────────────────────────────────
spin_lock:
    mov     eax, 1
.retry:
    ; Attempt to acquire: atomically exchange EAX (= 1) with [rdi].
    ; After xchg, EAX holds the *previous* value of the lock.
    xchg    eax, [rdi]
    test    eax, eax           ; was the lock free (== 0)?
    jz      .done              ; yes — we now own it
    ; Lock was held.  Spin without issuing bus-lock traffic:
    ; wait until the lock *looks* free before retrying the xchg.
.spin_wait:
    pause                      ; pipeline hint — reduces memory traffic
    mov     eax, [rdi]         ; read (non-atomic, for hysteresis)
    test    eax, eax
    jnz     .spin_wait         ; still locked — keep waiting
    ; Looks free now — reload EAX = 1 and retry the atomic exchange.
    mov     eax, 1
    jmp     .retry
.done:
    ret

; ─────────────────────────────────────────────────────────────────────────────
; spin_unlock(spinlock_t *lock)
;   rdi = address of the lock (volatile uint32_t)
; ─────────────────────────────────────────────────────────────────────────────
spin_unlock:
    ; A plain aligned 32-bit store is sequentially consistent on x86.
    ; Use a compiler/assembler memory barrier (mfence equivalent via the
    ; store itself) — no LOCK prefix required.
    mov     dword [rdi], 0
    ret
