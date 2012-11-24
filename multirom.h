#ifndef MULTIROM_H
#define MULTIROM_H

enum
{
    ROM_DEFAULT           = 0,
    ROM_ANDROID_INTERNAL  = 1,
    ROM_UBUNTU_INTERNAL   = 2,
    ROM_ANDROID_USB       = 3,
    ROM_UBUNTU_USB        = 4,

    ROM_UNKNOWN           = 5
};

#define M(x) (1 << x)
#define MASK_USB_ROMS (M(ROM_ANDROID_USB) | M(ROM_UBUNTU_USB))

enum 
{
    EXIT_REBOOT              = 0x01,
    EXIT_UMOUNT              = 0x02,
    EXIT_REBOOT_RECOVERY     = 0x04,
    EXIT_REBOOT_BOOTLOADER   = 0x08,
    EXIT_SHUTDOWN            = 0x10,

    EXIT_REBOOT_MASK         = (EXIT_REBOOT | EXIT_REBOOT_RECOVERY | EXIT_REBOOT_BOOTLOADER | EXIT_SHUTDOWN),
};

struct multirom_rom {
    int id;
    char *name;
    int type;
    int is_in_root;
    int has_bootimg;
    unsigned boot_image_id[8];
};

struct multirom_status {
    int is_second_boot;
    int auto_boot_seconds;
    struct multirom_rom *auto_boot_rom;
    struct multirom_rom *current_rom;
    struct multirom_rom **roms;
};

typedef struct boot_img_hdr boot_img_hdr;

int multirom(void);
int multirom_find_base_dir(void);
void multirom_emergency_reboot(void);
int multirom_default_status(struct multirom_status *s);
void multirom_find_usb_roms(struct multirom_status *s);
int multirom_get_rom_bootid(struct multirom_rom *rom, const char *roms_root_path);
int multirom_generate_rom_id(void);
struct multirom_rom *multirom_get_rom(struct multirom_status *s, const char *name);
struct multirom_rom *multirom_get_rom_by_id(struct multirom_status *s, int id);
struct multirom_rom *multirom_get_rom_in_root(struct multirom_status *s);
int multirom_load_status(struct multirom_status *s);
int multirom_import_internal(void);
void multirom_dump_status(struct multirom_status *s);
int multirom_save_status(struct multirom_status *s);
struct multirom_rom *multirom_select_rom(struct multirom_status *s);
int multirom_prepare_for_boot(struct multirom_status *s, struct multirom_rom *to_boot);
int multirom_load_bootimg_header(const char *path, struct boot_img_hdr *header);
int multirom_check_bootimg(struct multirom_status *s, struct multirom_rom *rom);
int multirom_dump_boot(const char *dest);
int multirom_move_out_of_root(struct multirom_rom *rom);
int multirom_move_to_root(struct multirom_rom *rom);
void multirom_free_status(struct multirom_status *s);
void multirom_free_rom(void *rom);
int multirom_init_fb(void);
int multirom_prep_android_mounts(struct multirom_rom *rom);
int multirom_create_media_link(void);
int multirom_get_api_level(const char *path);
int multirom_get_rom_type(struct multirom_rom *rom);
void multirom_take_screenshot(void);
int multirom_get_trampoline_ver(void);

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
