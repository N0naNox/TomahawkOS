#pragma once
#include <cstdint>
#include "vnode.h"

struct file
{
    struct vnode* f_vnode;   // מצביע ל-vnode המתאים
    uint32_t f_offset;       // מיקום קריאה/כתיבה נוכחי בתוך הקובץ
    uint16_t f_flags;        // דגלים (למשל O_RDONLY,
    int refcount;          // מונה הפניות למבנה הקובץ
}