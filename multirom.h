#ifndef MULTIROM_H
#define MULTIROM_H

#include <pthread.h>

enum
{
    ROM_DEFAULT           = 0,
    ROM_ANDROID_INTERNAL  = 1,
    ROM_UBUNTU_INTERNAL   = 2,
    ROM_ANDROID_USB_IMG   = 3,
    ROM_UBUNTU_USB_IMG    = 4,
    ROM_ANDROID_USB_DIR   = 5,
    ROM_UBUNTU_USB_DIR    = 6,

    ROM_UNSUPPORTED_INT   = 7,
    ROM_UNSUPPORTED_USB   = 8,
    ROM_UNKNOWN           = 9
};

#define M(x) (1 << x)
#define MASK_INTERNAL (M(ROM_DEFAULT) | M(ROM_ANDROID_INTERNAL) | M(ROM_UBUNTU_INTERNAL) | M(ROM_UNSUPPORTED_INT))
#define MASK_USB_ROMS (M(ROM_ANDROID_USB_IMG) | M(ROM_UBUNTU_USB_IMG) | M(ROM_ANDROID_USB_DIR) | M(ROM_UBUNTU_USB_DIR) | M(ROM_UNSUPPORTED_USB))
#define MASK_UBUNTU (M(ROM_UBUNTU_INTERNAL) | M(ROM_UBUNTU_USB_IMG)| M(ROM_UBUNTU_USB_DIR))
#define MASK_ANDROID (M(ROM_ANDROID_USB_DIR) | M(ROM_ANDROID_USB_IMG) | M(ROM_ANDROID_INTERNAL))
#define MASK_UNSUPPORTED (M(ROM_UNSUPPORTED_USB) | M(ROM_UNSUPPORTED_INT))

enum 
{
    EXIT_REBOOT              = 0x01,
    EXIT_UMOUNT              = 0x02,
    EXIT_REBOOT_RECOVERY     = 0x04,
    EXIT_REBOOT_BOOTLOADER   = 0x08,
    EXIT_SHUTDOWN            = 0x10,
    EXIT_KEXEC               = 0x20,

    EXIT_REBOOT_MASK         = (EXIT_REBOOT | EXIT_REBOOT_RECOVERY | EXIT_REBOOT_BOOTLOADER | EXIT_SHUTDOWN),
};

struct usb_partition
{
    char *name;
    char *mount_path;
    char *uuid;
    char *fs;
    int keep_mounted;
};

struct multirom_rom
{
    int id;
    char *name;
    char *base_path;
    int type;
    int has_bootimg;
    struct usb_partition *partition;
};

struct multirom_status
{
    int is_second_boot;
    int auto_boot_seconds;
    struct multirom_rom *auto_boot_rom;
    struct multirom_rom *current_rom;
    struct multirom_rom **roms;
    struct usb_partition **partitions;
    char *curr_rom_part;
};

typedef struct boot_img_hdr boot_img_hdr;

int multirom(void);
int multirom_find_base_dir(void);
void multirom_emergency_reboot(void);
int multirom_default_status(struct multirom_status *s);
void multirom_find_usb_roms(struct multirom_status *s);
int multirom_generate_rom_id(void);
struct multirom_rom *multirom_get_rom(struct multirom_status *s, const char *name, const char *part_uuid);
struct multirom_rom *multirom_get_rom_by_id(struct multirom_status *s, int id);
int multirom_load_status(struct multirom_status *s);
int multirom_import_internal(void);
void multirom_dump_status(struct multirom_status *s);
int multirom_save_status(struct multirom_status *s);
int multirom_prepare_for_boot(struct multirom_status *s, struct multirom_rom *to_boot);
int multirom_dump_boot(const char *dest);
void multirom_free_status(struct multirom_status *s);
void multirom_free_rom(void *rom);
int multirom_init_fb(void);
int multirom_prep_android_mounts(struct multirom_rom *rom);
int multirom_create_media_link(void);
int multirom_get_api_level(const char *path);
int multirom_get_rom_type(struct multirom_rom *rom);
void multirom_take_screenshot(void);
int multirom_get_trampoline_ver(void);
int multirom_has_kexec(void);
int multirom_load_kexec(struct multirom_status *s, struct multirom_rom *rom);
int multirom_get_cmdline(char *str, size_t size);
int multirom_find_file(char *res, const char *name_part, const char *path);
int multirom_fill_kexec_ubuntu(struct multirom_status *s, struct multirom_rom *rom, char **cmd);
int multirom_fill_kexec_android(struct multirom_rom *rom, char **cmd);
int multirom_extract_bytes(const char *dst, FILE *src, size_t size);
int multirom_update_partitions(struct multirom_status *s);
void multirom_destroy_partition(void *part);
void multirom_set_usb_refresh_thread(struct multirom_status *s, int run);
void multirom_set_usb_refresh_handler(void (*handler)(void));
int multirom_mount_usb(struct usb_partition *part);
int multirom_mount_loop(const char *src, const char *dst, int flags);
int multirom_copy_log(void);
int multirom_scan_partition_for_roms(struct multirom_status *s, struct usb_partition *p);
struct usb_partition *multirom_get_partition(struct multirom_status *s, char *uuid);
struct usb_partition *multirom_get_data_partition(struct multirom_status *s);
int multirom_path_exists(char *base, char *filename);

/*
** +-----------------+ 
** | boot header     | 1 page
** +-----------------+
** | kernel          | n pages  
** +-----------------+
** | ramdisk         | m pages  
** +-----------------+
** | second stage    | o pages
** +-----------------+
**
** n = (kernel_size + page_size - 1) / page_size
** m = (ramdisk_size + page_size - 1) / page_size
** o = (second_size + page_size - 1) / page_size
**
** 0. all entities are page_size aligned in flash
** 1. kernel and ramdisk are required (size != 0)
** 2. second is optional (second_size == 0 -> no second)
** 3. load each element (kernel, ramdisk, second) at
**    the specified physical address (kernel_addr, etc)
** 4. prepare tags at tag_addr.  kernel_args[] is
**    appended to the kernel commandline in the tags.
** 5. r0 = 0, r1 = MACHINE_TYPE, r2 = tags_addr
** 6. if second_size != 0: jump to second_addr
**    else: jump to kernel_addr
*/

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512

struct boot_img_hdr
{
    unsigned char magic[BOOT_MAGIC_SIZE];

    unsigned kernel_size;  /* size in bytes */
    unsigned kernel_addr;  /* physical load addr */

    unsigned ramdisk_size; /* size in bytes */
    unsigned ramdisk_addr; /* physical load addr */

    unsigned second_size;  /* size in bytes */
    unsigned second_addr;  /* physical load addr */

    unsigned tags_addr;    /* physical addr for kernel tags */
    unsigned page_size;    /* flash page size we assume */
    unsigned unused[2];    /* future expansion: should be 0 */

    unsigned char name[BOOT_NAME_SIZE]; /* asciiz product name */
    
    unsigned char cmdline[BOOT_ARGS_SIZE];

    unsigned id[8]; /* timestamp / checksum / sha1 / etc */
};

#endif
