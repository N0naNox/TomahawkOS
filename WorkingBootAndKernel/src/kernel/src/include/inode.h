#pragma once


#include <cstdint>



struct page_cache_entry {
    uint64_t offset;          // המיקום בקובץ (חייב להיות כפולה של PAGE_SIZE)
    void* physical_addr;      // הכתובת הפיזית ב-RAM (שהקצית מה-frame_alloc)
    int is_dirty;             // האם המידע שונה?
    struct page_cache_entry* next;
};


struct inode {
    uint32_t i_no;          // מספר מזהה (Inode Number)
    uint32_t i_size;        // גודל
    uint16_t i_mode;        // סוג (קובץ/תיקייה) והרשאות
    uint32_t i_refcount;    // כמה "משתמשים" בו כרגע בזיכרון
    void* i_private;
    struct page_cache_entry* cache_list;  // רשימת מטמון עמודים
    // Additional fields can be added as necessary
};