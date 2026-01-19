#pragma once
#include <cstdint>

struct vnode {
    enum vtype v_type;      // VREG (קובץ), VDIR (תיקייה), VCHR (מכשיר כמו מקלדת)
    struct vnode_ops *v_op; // "השיטות" של האובייקט (מצביעים לפונקציות)
    void* v_data;           // בדרך כלל יצביע ל-struct inode
};

struct vnode_ops {
    int (*vop_open)(struct vnode *vp);
    int (*vop_read)(struct vnode *vp, void *buf, size_t nbyte);
    int (*vop_write)(struct vnode *vp, void *buf, size_t nbyte);
    // More functions may be added as needed
};