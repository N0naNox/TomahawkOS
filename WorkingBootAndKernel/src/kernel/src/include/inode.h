#pragma once


#include <cstdint>
struct inode {
    uint32_t i_no;          // מספר מזהה (Inode Number)
    uint32_t i_size;        // גודל
    uint16_t i_mode;        // סוג (קובץ/תיקייה) והרשאות
    uint32_t i_refcount;    // כמה "משתמשים" בו כרגע בזיכרון
    void* i_private;
    // Additional fields can be added as necessary
};