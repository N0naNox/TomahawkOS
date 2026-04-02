#pragma once

#include <stdint.h>

#define MAX_USERNAME 32
#define BCRYPT_HASH_LEN 64 // bcrypt hash הוא בדרך כלל 60 תווים

typedef struct {
    uint32_t uid;                   // המזהה שהקרנל מכיר (למשל 1000)
    char username[MAX_USERNAME];     // שם המשתמש (לנוחות ול-Login)
    char password_hash[BCRYPT_HASH_LEN]; // ה-Hash שכולל בתוכו את ה-Salt וה-Cost
} user_credential_t;