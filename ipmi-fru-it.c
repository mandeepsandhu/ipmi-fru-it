#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "iniparser.h"
#include "fru-defs.h"

#define TOOL_VERSION "0.1"

char usage[] =
"\nUsage: %s [OPTIONS...]\n\n"
"OPTIONS:\n"
"\t-h\t\tThis help text\n"
"\t-v\t\tPrint version and exit\n"
"\t-r\t\tRead FRU data from file specified by -i\n"
"\t-i FILE\t\tFRU data file (use with -r)\n"
"\t-w\t\tWrite FRU data to file specified in -o\n"
"\t-c FILE\t\tFRU Config file\n"
"\t-s SIZE\t\tMaximum file size (in bytes) allowed for the FRU data file\n"
"\t-o FILE\t\tOutput FRU data filename (use with -w)\n\n";

/* Std IPMI FRU Section headers */
const char *IUA = "iua";
const char *CIA = "cia";
const char *BIA = "bia";
const char *PIA = "pia";

/* IUA section must-have keys */
const char* BINFILE = "bin_file";

/* predefined keys */
const char* CHASSIS_TYPE    = "chassis_type";
const char* PART_NUMBER     = "part_number";
const char* SERIAL_NUMBER   = "serial_number";
const char* LANGUAGE_CODE   = "language_code";
const char* MFG_DATETIME    = "mfg_datetime";
const char* MANUFACTURER    = "manufacturer";
const char* PRODUCT_NAME    = "product_name";
const char* VERSION         = "version";
const char* ASSET_TAG       = "asset_tag";
const char* FRU_FILE_ID     = "fru_file_id";

inline uint8_t get_6bit_ascii(char c)
{
    return (c - 0x20) & 0x3f;
}

inline uint8_t get_aligned_size(uint8_t size, uint8_t align)
{
    return (size + align - 1) & ~(align - 1);
}

inline uint8_t get_fru_tl_type(struct fru_type_length *ftl)
{
    return ftl->type_length & 0xc0;
}

inline uint8_t get_fru_tl_length(struct fru_type_length *ftl)
{
    return ftl->type_length & 0x3f;
}

uint8_t get_zero_cksum(uint8_t *data, int num_bytes)
{
    int sum = 0;
    while (num_bytes--) {
        sum += *(data++);
    }
    return -(sum % 256);
}

char *get_key(const char *section, const char* key)
{
    int len;
    char *concat;

    /* 1 byte for : and another for nul */
    len = strlen(section) + strlen(key) + 2; 
    concat = (char *) malloc(len);
    strcpy(concat, section);
    strcat(concat, ":");
    strcat(concat, key);

    return concat;
}

int pack_ascii6(const char *str, char **raw_data)
{
    char *data;
    struct fru_type_length *ftl;
    uint8_t tl = TYPE_CODE_ASCII6;
    int len, size, i, j;

    len = strlen(str);
    size = 0;

    /* 6-bit ASCII packed allocates 6 bits per char */
    int rem = (len * 6) % 8;
    int div = (len * 6) / 8;
    uint8_t numbytes = (rem ? div + 1 : div) & 0x3f;
    
    /* Set length. It can be a max of 64 bytes */
    tl |= numbytes;

    size = numbytes + sizeof(struct fru_type_length);

    data = (char *) calloc(size, 1);
    ftl = (struct fru_type_length *) data;
    ftl->type_length = tl;

    j = 0;
    for (i = 0; i+3 < len; i += 4) {
        *(ftl->data + j) = get_6bit_ascii(str[i]) | (get_6bit_ascii(str[i+1]) << 6);
        *(ftl->data + j + 1) = (get_6bit_ascii(str[i+1]) >> 2) | (get_6bit_ascii(str[i+2]) << 4);
        *(ftl->data + j + 2) = (get_6bit_ascii(str[i+2]) >> 4) | (get_6bit_ascii(str[i+3]) << 2);
        j += 3;
    }

    /* pack remaining (< 4) bytes */
    switch ((len - i) % 4) {
        case 3:
            *(ftl->data + j) = get_6bit_ascii(str[i]) | (get_6bit_ascii(str[i+1]) << 6);
            *(ftl->data + j + 1) = (get_6bit_ascii(str[i+1]) >> 2) | (get_6bit_ascii(str[i+2]) << 4);
            *(ftl->data + j + 2) = get_6bit_ascii(str[i+2]) >> 4;
            break;
        case 2:
            *(ftl->data + j) = get_6bit_ascii(str[i]) | (get_6bit_ascii(str[i+1]) << 6);
            *(ftl->data + j + 1) = get_6bit_ascii(str[i+1]) >> 2;
            break;
        case 1:
            *(ftl->data + j) = get_6bit_ascii(str[i]);
        default:
            break;
    }

    *raw_data = data;
    return size;
}

/* All gen_* functions, except gen_iua(), return size as multiples of 8 */
int gen_iua(dictionary *ini, char **iua_data)
{
    int fd, flags, size;
    struct stat st;

    char *binkey, *filename, *data;
    struct internal_use_area *iua;

    /* initialize some sane values */
    fd = -1;
    flags = size = 0;
    data = NULL;
    
    /* We expect this section to have a single key - "binfile", with a value
     * of the absolute path to the binary file to write to in the IUA
     */
    binkey = get_key(IUA, BINFILE);
    
    filename = iniparser_getstring(ini, binkey, NULL);

    if (!filename) {
        fprintf(stderr, "\n%s not found!\n\n", binkey);
        exit(EXIT_FAILURE);
    }

    /* Get size of file */
    stat(filename, &st);
    size = get_aligned_size((sizeof(struct internal_use_area)+st.st_size), 8);
    data = (char *) malloc(size);

    /* Write format version */
    iua = ((struct internal_use_area *) data);
    iua->format_version = 0x01;
                
    flags = O_RDONLY;
    if((fd = open(filename, flags)) == -1) {
        fprintf(stderr, "\nUnable to open %s for reading!\n\n", filename);
        exit(EXIT_FAILURE);
    }
    
    int result = read(fd, iua->data, st.st_size);
    if (result != st.st_size) {
        fprintf(stdout,  "\nError reading entire file content!\n\n");
        exit(EXIT_FAILURE);
    }
    
    close(fd);
    *iua_data = data;

    return size;
}

int gen_cia(dictionary *ini, char **cia_data)
{
    struct chassis_info_area *cia;
    char *data,
         *str_data,
         *packed_ascii,
         *key,
         **sec_keys,
         *part_num_packed,
         *serial_num_packed;

    int chassis_type,
        size,
        offset,
        num_keys,
        part_num_size,
        serial_num_size,
        packed_size,
        i;

    uint8_t end_marker, empty_marker, cksum;

    cia = NULL;
    size = offset = cksum = empty_marker = 0;
    end_marker = 0xc1;

    chassis_type = iniparser_getint(ini, get_key(CIA, CHASSIS_TYPE), 0);
    if (!chassis_type) {
        /* 0 is an illegal chassis type */
        fprintf(stderr, "\nInvalid chassis type! Aborting\n\n");
        exit(EXIT_FAILURE);
    }
    size += sizeof(struct chassis_info_area);

    str_data = iniparser_getstring(ini, get_key(CIA, PART_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        part_num_size = pack_ascii6(str_data, &part_num_packed);
        size += part_num_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        part_num_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(CIA, SERIAL_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        serial_num_size = pack_ascii6(str_data, &serial_num_packed);
        size += serial_num_size;
    } else {
        serial_num_packed = NULL;
        size += 1;
    }

    num_keys = iniparser_getsecnkeys(ini, CIA);
    sec_keys = iniparser_getseckeys(ini, CIA);

    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(CIA, CHASSIS_TYPE)) ||
            !strcmp(key, get_key(CIA, PART_NUMBER)) ||
            !strcmp(key, get_key(CIA, SERIAL_NUMBER))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            size += pack_ascii6(str_data, &packed_ascii);
        }
    }

    /* 2 bytes added for chksum & and end marker */
    size = get_aligned_size(size + 2, 8);

    data = (char *) calloc(size, 1);
    cia = (struct chassis_info_area *) data;

    /* Fill up CIA */
    cia->format_version = 0x01;
    /* Length is in multiples of 8 bytes */
    cia->area_length = size / 8;
    cia->chassis_type = chassis_type;

    if (part_num_packed) {
        memcpy(cia->tl + offset, part_num_packed, part_num_size);
        offset += part_num_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        memcpy(cia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (serial_num_packed) {
        memcpy(cia->tl + offset, serial_num_packed, serial_num_size);
        offset += serial_num_size;
    } else {
        memcpy(cia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(CIA, CHASSIS_TYPE)) ||
            !strcmp(key, get_key(CIA, PART_NUMBER)) ||
            !strcmp(key, get_key(CIA, SERIAL_NUMBER))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            packed_size = pack_ascii6(str_data, &packed_ascii);
            memcpy(cia->tl + offset, packed_ascii, packed_size);
            offset += packed_size;
        }
    }

    /* write the end marker 'C1' */
    memcpy(cia->tl + offset, &end_marker, 1);
    /* Calculate checksum of entire CIA */
    cksum = get_zero_cksum((uint8_t *) data, size-1);
    memcpy(data+size-1, &cksum, 1);

    *cia_data = data;

    return cia->area_length;
}

int gen_bia(dictionary *ini, char **bia_data)
{
    struct board_info_area *bia;

    char *data,
         *str_data,
         *packed_ascii,
         *mfg_packed,
         *name_packed,
         *serial_num_packed,
         *part_num_packed,
         *key,
         **sec_keys;

    int lang_code,
        mfg_date,
        size,
        offset,
        mfg_size,
        name_size,
        serial_num_size,
        part_num_size,
        num_keys,
        packed_size,
        i;

    uint8_t end_marker, empty_marker, cksum;

    bia = NULL;
    size = offset = cksum = empty_marker = 0;
    end_marker = 0xc1;

    lang_code = iniparser_getint(ini, get_key(BIA, LANGUAGE_CODE), -1);
    if (lang_code == -1) {
        fprintf(stdout, "Board language code not specified. "
                "Defaulting to English\n");
        lang_code = 0;
    }

    mfg_date = iniparser_getint(ini, get_key(BIA, MFG_DATETIME), -1);
    if (mfg_date == -1) {
        fprintf(stdout, "Manufacturing time not specified. "
                "Defaulting to unspecified\n");
        mfg_date = 0;
    }
    size += sizeof(struct board_info_area);

    str_data = iniparser_getstring(ini, get_key(BIA, MANUFACTURER), NULL);
    if (str_data && strlen(str_data)) {
        mfg_size = pack_ascii6(str_data, &mfg_packed);
        size += mfg_size;
    } else {
        mfg_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(BIA, PRODUCT_NAME), NULL);
    if (str_data && strlen(str_data)) {
        name_size = pack_ascii6(str_data, &name_packed);
        size += name_size;
    } else {
        name_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(BIA, SERIAL_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        serial_num_size = pack_ascii6(str_data, &serial_num_packed);
        size += serial_num_size;
    } else {
        serial_num_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(BIA, PART_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        part_num_size = pack_ascii6(str_data, &part_num_packed);
        size += part_num_size;
    } else {
        part_num_packed = NULL;
        size += 1;
    }
    /* We don't handle FRU File ID for now... */
    size += 1;

    num_keys = iniparser_getsecnkeys(ini, BIA);
    sec_keys = iniparser_getseckeys(ini, BIA);

    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(BIA, LANGUAGE_CODE)) ||
            !strcmp(key, get_key(BIA, MFG_DATETIME)) ||
            !strcmp(key, get_key(BIA, MANUFACTURER)) ||
            !strcmp(key, get_key(BIA, PRODUCT_NAME)) ||
            !strcmp(key, get_key(BIA, SERIAL_NUMBER)) ||
            !strcmp(key, get_key(BIA, PART_NUMBER)) ||
            !strcmp(key, get_key(BIA, FRU_FILE_ID))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            size += pack_ascii6(str_data, &packed_ascii);
        }
    }

    size = get_aligned_size(size + 2, 8);

    data = (char *) calloc(size, 1);
    bia = (struct board_info_area *) data;

    /* Fill up BIA */
    bia->format_version = 0x01;
    /* Length is in multiples of 8 bytes */
    bia->area_length = size / 8;
    bia->language_code = lang_code;
    mfg_date = htole32(mfg_date);
    memcpy(bia->mfg_date, &mfg_date, 3);

    if (mfg_packed) {
        memcpy(bia->tl + offset, mfg_packed, mfg_size);
        offset += mfg_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        memcpy(bia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (name_packed) {
        memcpy(bia->tl + offset, name_packed, name_size);
        offset += name_size;
    } else {
        memcpy(bia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (serial_num_packed) {
        memcpy(bia->tl + offset, serial_num_packed, serial_num_size);
        offset += serial_num_size;
    } else {
        memcpy(bia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (part_num_packed) {
        memcpy(bia->tl + offset, part_num_packed, part_num_size);
        offset += part_num_size;
    } else {
        memcpy(bia->tl + offset, &empty_marker, 1);
        offset += 1;
    }
    /* We don't handle FRU File ID for now... */
    memcpy(bia->tl + offset, &empty_marker, 1);
    offset += 1;

    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(BIA, LANGUAGE_CODE)) ||
            !strcmp(key, get_key(BIA, MFG_DATETIME)) ||
            !strcmp(key, get_key(BIA, MANUFACTURER)) ||
            !strcmp(key, get_key(BIA, PRODUCT_NAME)) ||
            !strcmp(key, get_key(BIA, SERIAL_NUMBER)) ||
            !strcmp(key, get_key(BIA, PART_NUMBER)) ||
            !strcmp(key, get_key(BIA, FRU_FILE_ID))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            packed_size = pack_ascii6(str_data, &packed_ascii);
            memcpy(bia->tl + offset, packed_ascii, packed_size);
            offset += packed_size;
        }
    }
    /* write the end marker 'C1' */
    memcpy(bia->tl + offset, &end_marker, 1);
    /* Calculate checksum of entire BIA */
    cksum = get_zero_cksum((uint8_t *) data, size-1);
    memcpy(data+size-1, &cksum, 1);

    *bia_data = data;

    return bia->area_length;
}

int gen_pia(dictionary *ini, char **pia_data)
{
    struct product_info_area *pia;

    char *data,
         *str_data,
         *packed_ascii,
         *mfg_packed,
         *name_packed,
         *part_num_packed,
         *version_packed,
         *serial_num_packed,
         *asset_tag_packed,
         *key,
         **sec_keys;

    int lang_code,
        size,
        offset,
        mfg_size,
        name_size,
        part_num_size,
        version_size,
        serial_num_size,
        asset_tag_size,
        packed_size,
        num_keys,
        i;

    uint8_t end_marker, empty_marker, cksum;

    pia = NULL;
    size = offset = cksum = empty_marker = 0;
    end_marker = 0xc1;

    lang_code = iniparser_getint(ini, get_key(PIA, LANGUAGE_CODE), -1);
    if (lang_code == -1) {
        fprintf(stdout, "Product language code not specified. "
                "Defaulting to English\n");
        lang_code = 0;
    }
    size += sizeof(struct product_info_area);

    str_data = iniparser_getstring(ini, get_key(PIA, MANUFACTURER), NULL);
    if (str_data && strlen(str_data)) {
        mfg_size = pack_ascii6(str_data, &mfg_packed);
        size += mfg_size;
    } else {
        mfg_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(PIA, PRODUCT_NAME), NULL);
    if (str_data && strlen(str_data)) {
        name_size = pack_ascii6(str_data, &name_packed);
        size += name_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        name_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(PIA, PART_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        part_num_size = pack_ascii6(str_data, &part_num_packed);
        size += part_num_size;
    } else {
        part_num_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(PIA, VERSION), NULL);
    if (str_data && strlen(str_data)) {
        version_size = pack_ascii6(str_data, &version_packed);
        size += version_size;
    } else {
        version_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(PIA, SERIAL_NUMBER), NULL);
    if (str_data && strlen(str_data)) {
        serial_num_size = pack_ascii6(str_data, &serial_num_packed);
        size += serial_num_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        serial_num_packed = NULL;
        size += 1;
    }

    str_data = iniparser_getstring(ini, get_key(PIA, ASSET_TAG), NULL);
    if (str_data && strlen(str_data)) {
        asset_tag_size = pack_ascii6(str_data, &asset_tag_packed);
        size += asset_tag_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        asset_tag_packed = NULL;
        size += 1;
    }
    /* We don't handle FRU File ID for now... */
    size += 1;

    num_keys = iniparser_getsecnkeys(ini, PIA);
    sec_keys = iniparser_getseckeys(ini, PIA);

    /* first iteration calculates the amount of space needed */
    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(PIA, LANGUAGE_CODE)) ||
            !strcmp(key, get_key(PIA, MANUFACTURER)) ||
            !strcmp(key, get_key(PIA, PRODUCT_NAME)) ||
            !strcmp(key, get_key(PIA, PART_NUMBER)) ||
            !strcmp(key, get_key(PIA, VERSION)) ||
            !strcmp(key, get_key(PIA, SERIAL_NUMBER)) ||
            !strcmp(key, get_key(PIA, ASSET_TAG)) ||
            !strcmp(key, get_key(PIA, FRU_FILE_ID))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            size += pack_ascii6(str_data, &packed_ascii);
        }
    }

    size = get_aligned_size(size + 2, 8);

    data = (char *) calloc(size, 1);
    pia = (struct product_info_area *) data;

    /* Fill up PIA */
    pia->format_version = 0x01;
    /* Length is in multiples of 8 bytes */
    pia->area_length = size / 8;
    pia->language_code = lang_code;

    if (mfg_packed) {
        memcpy(pia->tl + offset, mfg_packed, mfg_size);
        offset += mfg_size;
    } else {
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (name_packed) {
        memcpy(pia->tl + offset, name_packed, name_size);
        offset += name_size;
    } else {
        /* predfined fields with no data take 1 byte (for type/length) */
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (part_num_packed) {
        memcpy(pia->tl + offset, part_num_packed, part_num_size);
        offset += part_num_size;
    } else {
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (version_packed) {
        memcpy(pia->tl + offset, version_packed, version_size);
        offset += version_size;
    } else {
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (serial_num_packed) {
        memcpy(pia->tl + offset, serial_num_packed, serial_num_size);
        offset += serial_num_size;
    } else {
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }

    if (asset_tag_packed) {
        memcpy(pia->tl + offset, asset_tag_packed, asset_tag_size);
        offset += asset_tag_size;
    } else {
        memcpy(pia->tl + offset, &empty_marker, 1);
        offset += 1;
    }
    /* We don't handle FRU File ID for now... */
    memcpy(pia->tl + offset, &empty_marker, 1);
    offset += 1;

    /* Second iteration copies packed contents into final buffer */
    for (i = 0; i < num_keys; i++) {
        key = sec_keys[i];
        /* Skip keys we've already accounted for */
        if (!strcmp(key, get_key(PIA, LANGUAGE_CODE)) ||
            !strcmp(key, get_key(PIA, MANUFACTURER)) ||
            !strcmp(key, get_key(PIA, PRODUCT_NAME)) ||
            !strcmp(key, get_key(PIA, PART_NUMBER)) ||
            !strcmp(key, get_key(PIA, VERSION)) ||
            !strcmp(key, get_key(PIA, SERIAL_NUMBER)) ||
            !strcmp(key, get_key(PIA, ASSET_TAG)) ||
            !strcmp(key, get_key(PIA, FRU_FILE_ID))) {
            continue;
        }
        str_data = iniparser_getstring(ini, key, NULL);
        if (str_data && strlen(str_data)) {
            packed_size = pack_ascii6(str_data, &packed_ascii);
            memcpy(pia->tl + offset, packed_ascii, packed_size);
            offset += packed_size;
        }
    }
    /* write the end marker 'C1' */
    memcpy(pia->tl + offset, &end_marker, 1);
    /* Calculate checksum of entire PIA */
    cksum = get_zero_cksum((uint8_t *)data, size-1);
    memcpy(data+size-1, &cksum, 1);

    *pia_data = data;

    return pia->area_length;
}

int gen_fru_data(dictionary *ini, char **raw_data)
{
    int total_length,
        offset,
        len_mul8,
        iua_len,
        size,
        cksum;

    char *iua, *cia, *bia, *pia, *data;

    iua = cia = bia = pia = data = NULL;
    total_length = offset = len_mul8 = iua_len = size = cksum = 0;

    /* A common header always exists even if there's no FRU data */
    struct fru_common_header *fch =
        (struct fru_common_header *) calloc(sizeof(struct fru_common_header),
                                            1);
    fch->format_version = 0x01;
    total_length += sizeof(struct fru_common_header);
    offset = total_length / 8;

    /* Parse "Internal Use Area" (IUA) section */
    if (iniparser_find_entry(ini, IUA)) {
        iua_len = gen_iua(ini, &iua);
        fch->internal_use_offset = offset;
        offset += (iua_len/8);
        total_length += iua_len;
    }

    /* Parse "Chassis Info Area" (CIA) section */
    if (iniparser_find_entry(ini, CIA)) {
        len_mul8 = gen_cia(ini, &cia);
        fch->chassis_info_offset = offset;
        offset += len_mul8;
        total_length += len_mul8 * 8;
    }

    /* Parse "Board Info Area" (BIA) section */
    if (iniparser_find_entry(ini, BIA)) {
        len_mul8 = gen_bia(ini, &bia);
        fch->board_info_offset = offset;
        offset += len_mul8;
        total_length += len_mul8 * 8;
    }

    /* Parse "Product Info Area" (PIA) section */
    if (iniparser_find_entry(ini, PIA)) {
        len_mul8 = gen_pia(ini, &pia);
        fch->product_info_offset = offset;
        offset += len_mul8;
        total_length += len_mul8 * 8;
    }

    /* calculate header checksum */
    cksum = get_zero_cksum((uint8_t *) fch, sizeof(*fch)-1);
    fch->checksum = cksum;

    data = (char *) malloc(total_length);

    /* Copy common header first */
    memcpy(data, fch, sizeof(struct fru_common_header));

    /* Copy each section's data if any */
    if (iua) {
        offset = fch->internal_use_offset * 8;
        size = *(iua + 1) * 8;
        memcpy(data + offset, iua, size);
    }

    if (cia) {
        offset = fch->chassis_info_offset * 8;
        size = *(cia + 1) * 8;
        memcpy(data + offset, cia, size);
    }

    if (bia) {
        offset = fch->board_info_offset * 8;
        size = *(bia + 1) * 8;
        memcpy(data + offset, bia, size);
    }

    if (pia) {
        offset = fch->product_info_offset * 8;
        size = *(pia + 1) * 8;
        memcpy(data + offset, pia, size);
    }

    *raw_data = data;

    return total_length;
}

int write_fru_data(const char*filename, void *data, int length)
{
    int fd, flags;
    mode_t mode;

    fd = -1;
    flags = O_RDWR | O_CREAT | O_TRUNC;
    mode = S_IRWXU | S_IRGRP | S_IROTH;

    if ((fd = open(filename, flags, mode)) == -1) {
        perror("File open:");
        return -1;
    }

    write(fd, data, length);
    close(fd);

    return 0;
}

int main(int argc, char **argv)
{
    char *fru_ini_file, *outfile, *data;
    int c, length, max_size, result;
    dictionary *ini;

    /* supported cmdline options */
    char options[] = "hvri:ws:c:o:";

    fru_ini_file = outfile = data = NULL;
    ini = NULL;

    while((c = getopt(argc, argv, options)) != -1) {
        switch(c) {
            case 'r':
                fprintf(stderr, "\nError! Option not implemented\n\n");
                exit(EXIT_FAILURE);
            case 'i':
                fprintf(stderr, "\nError! Option not implemented\n\n");
                exit(EXIT_FAILURE);
            case 's':
                result = sscanf(optarg, "%d", &max_size);
                if (result == 0 || result == EOF) {
                    fprintf(stderr, "\nError! Invalid maximum file size (-s %s)\n\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'c':
                fru_ini_file = optarg;
                break;
            case 'o':
                outfile = optarg;
                break;
            case 'v':
                fprintf(stdout, "\nipmi-fru-it version %s\n\n", TOOL_VERSION);
                return 0;
            default:
                fprintf(stdout, "\nipmi-fru-it version %s\n", TOOL_VERSION);
                fprintf(stdout, usage, argv[0]);
                return -1;
        }
    }

    if (!fru_ini_file || !outfile) {
        fprintf(stderr, usage, argv[0]);
        exit(EXIT_FAILURE);
    }

    ini = iniparser_load(fru_ini_file);
    if (!ini) {
        fprintf(stderr, "\nError parsing INI file %s!\n\n", fru_ini_file);
        exit(EXIT_FAILURE);
    }

    length = gen_fru_data(ini, &data);

    if (length < 0) {
        fprintf(stderr, "\nError generating FRU data!\n\n");
        exit(EXIT_FAILURE);
    }

    if (length > max_size) {
        fprintf(stderr, "\nError! FRU data length (%d bytes) exceeds maximum "
                "file size (%d bytes)\n\n", length, max_size);
        exit(EXIT_FAILURE);
    }
    
    if (write_fru_data(outfile, data, length)) {
        fprintf(stderr, "\nError writing %s\n\n", outfile);
        exit(EXIT_FAILURE);
    }
    
    iniparser_freedict(ini);

    fprintf(stdout, "\nFRU file \"%s\" created\n\n", outfile);

    return 0;
}
