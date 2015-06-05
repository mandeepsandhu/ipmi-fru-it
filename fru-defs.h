#include <inttypes.h>

/*
 * Platform Management FRU Information Storage Definition
 *
 * v1.0 
 *
 * Refer: http://www.intel.com/content/dam/www/public/us/en/documents/product-briefs/platform-management-fru-document-rev-1-2-feb-2013.pdf
 *
 */

/* FRU type/length, inspired from Linux kernel's include/linux/ipmi-fru.h */
struct fru_type_length {
    uint8_t     type_length;
    uint8_t     data[];
};

/* 8. Common Header Format */
struct fru_common_header {
    uint8_t     format_version;
    uint8_t     internal_use_offset;
    uint8_t     chassis_info_offset;
    uint8_t     board_info_offset;
    uint8_t     product_info_offset;
    uint8_t     multirecord_info_offset;
    uint8_t     pad;
    uint8_t     checksum;
};

/* 9. Internal Use Area Format */
struct internal_use_area {
    uint8_t     format_version;
    uint8_t     data[];
};

/* 10. Chassis Info Area Format
 * tl - Type/Length
 */
struct __attribute__ ((__packed__)) chassis_info_area {
    uint8_t     format_version;
    uint8_t     area_length;
    uint8_t     chassis_type;
    struct fru_type_length tl[];
};

/* 11. Board Info Area Format */
struct __attribute__ ((__packed__)) board_info_area {
    uint8_t     format_version;
    uint8_t     area_length;
    uint8_t     language_code;
    uint8_t     mfg_date[3];
    struct fru_type_length tl[];
};

/* 12. Product Info Area Format */
struct __attribute__ ((__packed__)) product_info_area {
    uint8_t     format_version;
    uint8_t     area_length;
    uint8_t     language_code;
    struct fru_type_length tl[];
};

/* Type code */
enum fru_type_code {
    TYPE_CODE_BINARY    = 0x00,
    TYPE_CODE_BCDPLUS   = 0x40,
    TYPE_CODE_ASCII6    = 0x80,
    TYPE_CODE_UNILATIN  = 0xc0,
};
