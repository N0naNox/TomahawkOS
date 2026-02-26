/*
 * tests.c - Kernel tests for COW, signals, and other features
 */

#include <stdint.h>
#include "include/paging.h"
#include "include/refcount.h"
#include "include/proc.h"
#include "include/signal.h"
#include "include/frame_alloc.h"
#include "include/spinlock.h"
#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include <uart.h>

/* PTE flags from paging.h */
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_COW       (1ULL << 9)

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg) \
    do { \
        if (condition) { \
            uart_puts("[TEST PASS] "); \
            uart_puts(msg); \
            uart_puts("\n"); \
            tests_passed++; \
        } else { \
            uart_puts("[TEST FAIL] "); \
            uart_puts(msg); \
            uart_puts("\n"); \
            tests_failed++; \
        } \
    } while(0)

/* ========== Reference Counting Tests ========== */

void test_refcount_basic(void) {
    uart_puts("\n=== Testing Reference Counting ===\n");
    
    uint64_t frame = 0x1000; /* Use frame number, not address */
    
    /* Test initial refcount is 0 */
    TEST_ASSERT(refcount_get(frame) == 0, "Initial refcount is 0");
    
    /* Test increment */
    refcount_inc(frame);
    TEST_ASSERT(refcount_get(frame) == 1, "Refcount after inc is 1");
    
    /* Test multiple increments */
    refcount_inc(frame);
    refcount_inc(frame);
    TEST_ASSERT(refcount_get(frame) == 3, "Refcount after 3 incs is 3");
    
    /* Test is_shared */
    TEST_ASSERT(refcount_is_shared(frame), "Frame with refcount > 1 is shared");
    
    /* Test decrement */
    refcount_dec(frame);
    TEST_ASSERT(refcount_get(frame) == 2, "Refcount after dec is 2");
    
    refcount_dec(frame);
    TEST_ASSERT(refcount_get(frame) == 1, "Refcount after 2nd dec is 1");
    TEST_ASSERT(!refcount_is_shared(frame), "Frame with refcount 1 is not shared");
    
    /* Cleanup */
    refcount_dec(frame);
    TEST_ASSERT(refcount_get(frame) == 0, "Refcount after final dec is 0");
}

/* ========== COW Tests ========== */

void test_cow_refcount_integration(void) {
    uart_puts("\n=== Testing COW + Refcount Integration ===\n");
    
    uintptr_t test_phys = pfa_alloc_frame();
    
    if (!test_phys) {
        uart_puts("[TEST SKIP] Could not allocate frame for integration test\n");
        return;
    }
    
    /* Set initial refcount */
    refcount_inc(test_phys);
    TEST_ASSERT(refcount_get(test_phys) == 1, "Initial refcount is 1");
    
    /* Simulate sharing (fork) */
    refcount_inc(test_phys);
    TEST_ASSERT(refcount_get(test_phys) == 2, "Refcount after share is 2");
    TEST_ASSERT(refcount_is_shared(test_phys), "Frame is marked as shared");
    
    /* Simulate copy-on-write: allocate new frame */
    uintptr_t new_phys = pfa_alloc_frame();
    if (new_phys) {
        /* Decrement old frame refcount */
        refcount_dec(test_phys);
        TEST_ASSERT(refcount_get(test_phys) == 1, "Old frame refcount decremented");
        
        /* New frame gets refcount 1 */
        refcount_inc(new_phys);
        TEST_ASSERT(refcount_get(new_phys) == 1, "New frame refcount is 1");
        
        /* Cleanup */
        refcount_dec(new_phys);
        pfa_free_frame(new_phys);
    }
    
    refcount_dec(test_phys);
    pfa_free_frame(test_phys);
}

void test_paging_clone_cow(void) {
    uart_puts("\n=== Testing COW Page Table Cloning ===\n");
    
    /* Create a test PML4 */
    uintptr_t src_pml4 = paging_create_pml4();
    if (!src_pml4) {
        uart_puts("[TEST SKIP] Could not create source PML4\n");
        return;
    }
    
    /* Map a test page */
    uintptr_t test_virt = 0x400000;
    uintptr_t test_phys = pfa_alloc_frame();
    if (!test_phys) {
        uart_puts("[TEST SKIP] Could not allocate test frame\n");
        paging_free_pml4(src_pml4);
        return;
    }
    
    paging_map_page(src_pml4, test_virt, test_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    
    /* Clone with COW */
    uintptr_t dst_pml4 = paging_clone_cow(src_pml4);
    TEST_ASSERT(dst_pml4 != 0, "COW clone created new PML4");
    TEST_ASSERT(dst_pml4 != src_pml4, "Cloned PML4 is different from source");
    
    /* Check that the page is now marked COW in both PML4s */
    uint64_t* src_pte = paging_get_pte(src_pml4, test_virt);
    uint64_t* dst_pte = paging_get_pte(dst_pml4, test_virt);
    
    if (src_pte && dst_pte) {
        TEST_ASSERT((*src_pte & PTE_COW) != 0, "Source PTE has COW flag");
        TEST_ASSERT((*dst_pte & PTE_COW) != 0, "Destination PTE has COW flag");
        TEST_ASSERT((*src_pte & PTE_WRITABLE) == 0, "Source PTE is read-only");
        TEST_ASSERT((*dst_pte & PTE_WRITABLE) == 0, "Destination PTE is read-only");
    }
    
    /* Cleanup */
    if (dst_pml4) paging_free_pml4(dst_pml4);
    paging_free_pml4(src_pml4);
    pfa_free_frame(test_phys);
}

/* ========== Signal Tests ========== */

void test_signal_queue(void) {
    uart_puts("\n=== Testing Signal Queuing ===\n");
    
    signal_struct_t sig;
    signal_init(&sig);
    
    TEST_ASSERT(sig.pending == 0, "Initial pending mask is 0");
    TEST_ASSERT(sig.blocked == 0, "Initial blocked mask is 0");
    
    /* Queue a signal */
    signal_queue(&sig, SIGUSR1);
    TEST_ASSERT((sig.pending & (1ULL << (SIGUSR1 - 1))) != 0, "SIGUSR1 is pending");
    
    /* Queue another */
    signal_queue(&sig, SIGUSR2);
    TEST_ASSERT((sig.pending & (1ULL << (SIGUSR2 - 1))) != 0, "SIGUSR2 is pending");
    
    /* Check next pending */
    int next = signal_next_pending(&sig);
    TEST_ASSERT(next == SIGUSR1, "Next pending signal is SIGUSR1");
    
    /* Dequeue it */
    signal_dequeue(&sig, SIGUSR1);
    TEST_ASSERT((sig.pending & (1ULL << (SIGUSR1 - 1))) == 0, "SIGUSR1 dequeued");
    
    next = signal_next_pending(&sig);
    TEST_ASSERT(next == SIGUSR2, "Next pending signal is now SIGUSR2");
}

void test_signal_blocking(void) {
    uart_puts("\n=== Testing Signal Blocking ===\n");
    
    signal_struct_t sig;
    signal_init(&sig);
    
    /* Queue a signal */
    signal_queue(&sig, SIGINT);
    TEST_ASSERT(signal_pending(&sig), "Signal is pending and not blocked");
    
    /* Block it */
    sig.blocked |= (1ULL << (SIGINT - 1));
    TEST_ASSERT(!signal_pending(&sig), "Signal is blocked");
    TEST_ASSERT((sig.pending & (1ULL << (SIGINT - 1))) != 0, "Signal still in pending mask");
    
    /* Unblock */
    sig.blocked &= ~(1ULL << (SIGINT - 1));
    TEST_ASSERT(signal_pending(&sig), "Signal is pending after unblock");
}

/* ========== Spinlock Tests ========== */

void test_spinlock_basic(void) {
    uart_puts("\n=== Testing Spinlock Primitives ===\n");

    spinlock_t lock = SPINLOCK_INIT;

    /* Lock should be 0 (unlocked) at init */
    TEST_ASSERT(lock == 0, "Spinlock starts unlocked");

    /* Acquire */
    spin_lock(&lock);
    TEST_ASSERT(lock == 1, "Spinlock is locked after spin_lock");

    /* Release */
    spin_unlock(&lock);
    TEST_ASSERT(lock == 0, "Spinlock is unlocked after spin_unlock");

    /* IRQ-save variant */
    uint64_t saved_flags = 0;
    spin_lock_irqsave(&lock, &saved_flags);
    TEST_ASSERT(lock == 1, "IRQ-save: lock acquired");
    spin_unlock_irqrestore(&lock, &saved_flags);
    TEST_ASSERT(lock == 0, "IRQ-restore: lock released");
}

/* ========== VFS Metadata Locking Tests ========== */

void test_vfs_unlink(void) {
    uart_puts("\n=== Testing vfs_unlink ===\n");

    /* Create a temporary directory to hold test files */
    struct vnode *root = vfs_get_root();
    TEST_ASSERT(root != NULL, "Root vnode exists");

    /* Create a sub-directory for isolation */
    struct vnode *testdir = vfs_mkdir(root, "__test_unlink__");
    TEST_ASSERT(testdir != NULL, "Created test directory");

    /* Create a file inside it */
    struct vnode *f = vfs_create_file(testdir, "testfile.txt");
    TEST_ASSERT(f != NULL, "Created testfile.txt");
    TEST_ASSERT(testdir->v_nchildren == 1, "Directory has 1 child after create");

    /* Unlink the file */
    int ret = vfs_unlink(testdir, "testfile.txt");
    TEST_ASSERT(ret == 0, "vfs_unlink returned 0");
    TEST_ASSERT(testdir->v_nchildren == 0, "Directory is empty after unlink");

    /* Lookup should fail now */
    struct vnode *gone = vfs_lookup(testdir, "testfile.txt");
    TEST_ASSERT(gone == NULL, "Unlinked file not found by lookup");

    /* Unlink non-existent — should return -1 */
    ret = vfs_unlink(testdir, "nonexistent");
    TEST_ASSERT(ret == -1, "Unlink of non-existent returns -1");

    /* Clean up the test dir */
    vfs_unlink(root, "__test_unlink__");
}

void test_vfs_chmod(void) {
    uart_puts("\n=== Testing vfs_chmod ===\n");

    struct vnode *root = vfs_get_root();

    struct vnode *f = vfs_create_file(root, "__test_chmod__.txt");
    TEST_ASSERT(f != NULL, "Created chmod test file");

    /* Default mode is 0 */
    struct inode *ip = (struct inode *)f->v_data;
    TEST_ASSERT(ip != NULL, "Inode exists");

    /* Set mode to 0644 */
    int ret = vfs_chmod(f, 0644);
    TEST_ASSERT(ret == 0, "vfs_chmod returned 0");

    /* Read back and verify lower 12 bits */
    uint64_t flags;
    spin_lock_irqsave(&ip->i_lock, &flags);
    uint16_t mode = ip->i_mode & 0x0FFF;
    spin_unlock_irqrestore(&ip->i_lock, &flags);
    TEST_ASSERT(mode == 0644, "Mode bits set to 0644");

    /* Change again to 0755 */
    vfs_chmod(f, 0755);
    spin_lock_irqsave(&ip->i_lock, &flags);
    mode = ip->i_mode & 0x0FFF;
    spin_unlock_irqrestore(&ip->i_lock, &flags);
    TEST_ASSERT(mode == 0755, "Mode bits updated to 0755");

    /* Clean up */
    vfs_unlink(root, "__test_chmod__.txt");
}

void test_vfs_rename(void) {
    uart_puts("\n=== Testing vfs_rename ===\n");

    struct vnode *root = vfs_get_root();

    /* Create source file */
    struct vnode *f = vfs_create_file(root, "__rename_src__");
    TEST_ASSERT(f != NULL, "Created rename source file");

    /* Rename within root */
    int ret = vfs_rename(root, "__rename_src__", root, "__rename_dst__");
    TEST_ASSERT(ret == 0, "vfs_rename returned 0");

    /* Old name should be gone */
    struct vnode *old_lookup = vfs_lookup(root, "__rename_src__");
    TEST_ASSERT(old_lookup == NULL, "Source name no longer found after rename");

    /* New name should be found and point to the same vnode */
    struct vnode *new_lookup = vfs_lookup(root, "__rename_dst__");
    TEST_ASSERT(new_lookup != NULL, "Destination name found after rename");
    TEST_ASSERT(new_lookup == f, "Renamed vnode is the same object");

    /* Cross-directory rename */
    struct vnode *dir_a = vfs_mkdir(root, "__rename_dir_a__");
    struct vnode *dir_b = vfs_mkdir(root, "__rename_dir_b__");
    TEST_ASSERT(dir_a != NULL && dir_b != NULL, "Created dirs A and B");

    struct vnode *ff = vfs_create_file(dir_a, "file_in_a");
    TEST_ASSERT(ff != NULL, "Created file in dir A");

    ret = vfs_rename(dir_a, "file_in_a", dir_b, "file_in_b");
    TEST_ASSERT(ret == 0, "Cross-dir rename returned 0");
    TEST_ASSERT(vfs_lookup(dir_a, "file_in_a") == NULL, "Source gone from dir A");
    TEST_ASSERT(vfs_lookup(dir_b, "file_in_b") == ff, "File now in dir B");

    /* Clean up */
    vfs_unlink(dir_b, "file_in_b");
    vfs_unlink(root, "__rename_dir_b__");
    vfs_unlink(root, "__rename_dir_a__");
    vfs_unlink(root, "__rename_dst__");
}

/* ========== Main Test Runner ========== */

void run_kernel_tests(void) {
    uart_puts("\n");
    uart_puts("=====================================\n");
    uart_puts("   KERNEL UNIT TESTS\n");
    uart_puts("=====================================\n");

    tests_passed = 0;
    tests_failed = 0;

    /* Run all tests */
    test_refcount_basic();
    test_cow_refcount_integration();
    test_paging_clone_cow();
    test_signal_queue();
    test_signal_blocking();

    /* Locking and VFS metadata tests */
    test_spinlock_basic();
    test_vfs_unlink();
    test_vfs_chmod();
    test_vfs_rename();

    /* Summary */
    uart_puts("\n");
    uart_puts("=====================================\n");
    uart_puts("TEST SUMMARY: ");
    uart_putu(tests_passed);
    uart_puts(" passed, ");
    uart_putu(tests_failed);
    uart_puts(" failed\n");
    uart_puts("=====================================\n");
    uart_puts("\n");
}
