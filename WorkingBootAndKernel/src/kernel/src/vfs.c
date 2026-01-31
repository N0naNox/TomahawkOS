#include "include/vfs.h"
#include "include/vnode.h"
#include "include/inode.h"
#include "include/frame_alloc.h"
#include "include/string.h"
#include <stddef.h>

static struct vnode* root_vnode = NULL;

/* Forward declarations for ramfs operations */
static int ramfs_open(struct vnode *vp);
static int ramfs_read(struct vnode *vp, void *buf, size_t nbyte);
static int ramfs_write(struct vnode *vp, void *buf, size_t nbyte);

static struct vnode_ops ramfs_ops = {
    .vop_open = ramfs_open,
    .vop_read = ramfs_read,
    .vop_write = ramfs_write,
};

void vfs_init(void) {
    /* Create root directory vnode */
    root_vnode = vfs_create_vnode(VDIR);
}

struct vnode* vfs_create_vnode(enum vtype type) {
    /* Allocate vnode */
    struct vnode* vp = (struct vnode*)pfa_alloc_frame();
    if (!vp) return NULL;
    
    /* Allocate inode */
    struct inode* ip = (struct inode*)pfa_alloc_frame();
    if (!ip) {
        pfa_free_frame((uintptr_t)vp);
        return NULL;
    }
    
    /* Initialize vnode */
    vp->v_type = type;
    vp->v_op = &ramfs_ops;
    vp->v_data = ip;
    
    /* Initialize inode */
    memset(ip, 0, sizeof(struct inode));
    ip->i_refcount = 1;
    ip->cache_list = NULL;
    
    return vp;
}

void vfs_free_vnode(struct vnode* vp) {
    if (!vp) return;
    
    struct inode* ip = (struct inode*)vp->v_data;
    if (ip) {
        /* Free page cache */
        struct page_cache_entry* entry = ip->cache_list;
        while (entry) {
            struct page_cache_entry* next = entry->next;
            if (entry->physical_addr) {
                pfa_free_frame((uintptr_t)entry->physical_addr);
            }
            pfa_free_frame((uintptr_t)entry);
            entry = next;
        }
        pfa_free_frame((uintptr_t)ip);
    }
    
    pfa_free_frame((uintptr_t)vp);
}

struct vnode* vfs_get_root(void) {
    return root_vnode;
}

int vfs_open(struct vnode* vp) {
    if (!vp || !vp->v_op || !vp->v_op->vop_open) {
        return -1;
    }
    return vp->v_op->vop_open(vp);
}

int vfs_read(struct vnode* vp, void* buf, size_t nbyte) {
    if (!vp || !vp->v_op || !vp->v_op->vop_read) {
        return -1;
    }
    return vp->v_op->vop_read(vp, buf, nbyte);
}

int vfs_write(struct vnode* vp, const void* buf, size_t nbyte) {
    if (!vp || !vp->v_op || !vp->v_op->vop_write) {
        return -1;
    }
    return vp->v_op->vop_write(vp, (void*)buf, nbyte);
}

int vfs_close(struct vnode* vp) {
    if (!vp) return -1;
    
    struct inode* ip = (struct inode*)vp->v_data;
    if (ip) {
        ip->i_refcount--;
        if (ip->i_refcount == 0) {
            vfs_free_vnode(vp);
        }
    }
    return 0;
}

/* RAMFS implementation */

static int ramfs_open(struct vnode *vp) {
    /* Nothing special needed for ramfs open */
    return 0;
}

static int ramfs_read(struct vnode *vp, void *buf, size_t nbyte) {
    if (!vp || !buf) return -1;
    
    struct inode* ip = (struct inode*)vp->v_data;
    if (!ip) return -1;
    
    /* Simple implementation: read from first cache page */
    struct page_cache_entry* entry = ip->cache_list;
    if (!entry || !entry->physical_addr) {
        return 0; /* No data */
    }
    
    size_t to_read = nbyte;
    if (to_read > ip->i_size) {
        to_read = ip->i_size;
    }
    
    memcpy(buf, entry->physical_addr, to_read);
    return to_read;
}

static int ramfs_write(struct vnode *vp, void *buf, size_t nbyte) {
    if (!vp || !buf) return -1;
    
    struct inode* ip = (struct inode*)vp->v_data;
    if (!ip) return -1;
    
    /* Allocate cache page if needed */
    if (!ip->cache_list) {
        struct page_cache_entry* entry = (struct page_cache_entry*)pfa_alloc_frame();
        if (!entry) return -1;
        
        entry->physical_addr = (void*)pfa_alloc_frame();
        if (!entry->physical_addr) {
            pfa_free_frame((uintptr_t)entry);
            return -1;
        }
        
        entry->offset = 0;
        entry->is_dirty = 0;
        entry->next = NULL;
        ip->cache_list = entry;
    }
    
    struct page_cache_entry* entry = ip->cache_list;
    memcpy(entry->physical_addr, buf, nbyte);
    entry->is_dirty = 1;
    ip->i_size = nbyte;
    
    return nbyte;
}
