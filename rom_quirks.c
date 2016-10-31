/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>

#include "rom_quirks.h"
#include "lib/log.h"
#include "lib/util.h"

static void workaround_mount_in_sh(const char *path)
{
    char line[512];
    char *tmp_name = NULL;
    FILE *f_in, *f_out;

    f_in = fopen(path, "re");
    if(!f_in)
        return;

    const int size = strlen(path) + 5;
    tmp_name = malloc(size);
    snprintf(tmp_name, size, "%s-new", path);
    f_out = fopen(tmp_name, "we");
    if(!f_out)
    {
        fclose(f_in);
        free(tmp_name);
        return;
    }

    while(fgets(line, sizeof(line), f_in))
    {
        if(strstr(line, "mount ") && strstr(line, "/system"))
            fputc('#', f_out);
        fputs(line, f_out);
    }

    fclose(f_in);
    fclose(f_out);
    rename(tmp_name, path);
    free(tmp_name);
}

static int inject_file_contexts(const char *path)
{
    FILE *f;
    char line[512];

    f = fopen(path, "re");
    if(!f)
    {
        ERROR("Failed to open /file_contexts!\n");
        return -1;
    }

    while(fgets(line, sizeof(line), f))
    {
        if(strstartswith(line, "/data/media/multirom"))
        {
            INFO("/file_contexts has been already injected.\n");
            fclose(f);
            return 0;
        }
    }

    fclose(f);

    INFO("Injecting /file_contexts\n");
    f = fopen(path, "ae");
    if(!f)
    {
        ERROR("Failed to open /file_contexts for appending!\n");
        return -1;
    }

    fputs("\n"
        "# MultiROM folders\n"
        "/data/media/multirom(/.*)?          <<none>>\n"
        "/data/media/0/multirom(/.*)?        <<none>>\n"
        "/realdata/media/multirom(/.*)?      <<none>>\n"
        "/realdata/media/0/multirom(/.*)?    <<none>>\n"
        "/mnt/mrom(/.*)?                     <<none>>\n",
        f);
    fclose(f);

    return 0;
}

// Keep this as a backup function in case the file_contexts binary injection doesn't work
static void disable_restorecon_recursive(void)
{
    DIR *d = opendir("/");
    if(d)
    {
        struct dirent *dt;
        char path[128];
        while((dt = readdir(d)))
        {
            if(dt->d_type != DT_REG)
                continue;

            if(strendswith(dt->d_name, ".rc"))
            {
                snprintf(path, sizeof(path), "/%s", dt->d_name);
                char line[512];
                char *tmp_name = NULL;
                FILE *f_in, *f_out;

                f_in = fopen(path, "re");
                if(!f_in)
                    return;

                const int size = strlen(path) + 5;
                tmp_name = malloc(size);
                snprintf(tmp_name, size, "%s-new", path);
                f_out = fopen(tmp_name, "we");
                if(!f_out)
                {
                    fclose(f_in);
                    free(tmp_name);
                    return;
                }

                while(fgets(line, sizeof(line), f_in))
                {
                    if(strstr(line, "restorecon_recursive ") && (strstr(line, "/data") || strstr(line, "/system") || strstr(line, "/cache") || strstr(line, "/mnt")))
                        fputc('#', f_out);
                    fputs(line, f_out);
                }

                fclose(f_in);
                fclose(f_out);
                rename(tmp_name, path);
                free(tmp_name);

                chmod(path, 0750);
            }
        }
        closedir(d);
    }
}

/* ************************************************************************************************************************************************ */
/*
 * Functions to deal with bin formatted file_contexts (ie compiled regex)
 *
 */

#define NUM_OF_REGEXS_EXCLUSIONS 5

const char *multirom_exclusion_path[NUM_OF_REGEXS_EXCLUSIONS] = {
    "/data/media/multirom",
    "/data/media/0/multirom",
    "/realdata/media/multirom",
    "/realdata/media/0/multirom",
    "/mnt/mrom",
};

// The structure and content of the compiled regex_info and study_data
// was extrapolated from '/data/media(/.*)?' compiled regex, and then
// hardcoded here, which seems to work fine for now.
//
// Alternatively, we can decompile the existing file_contexts.bin and
// find the '/data/media(/.*)?' compiled regex, and use it to build
// the multirom exclusion regexs; this may actually be a better (if possible)
// solution than hardcoding, but for the time being all the file_contexts.bin
// I've seen are all version 4, and have the same structure:
//
//      Offset(h) 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
//
//      00000000  21 00 00 00 75 3A 6F 62 6A 65 63 74 5F 72 3A 6D  !...u:object_r:m
//      00000010  65 64 69 61 5F 72 77 5F 64 61 74 61 5F 66 69 6C  edia_rw_data_fil
//      00000020  65 3A 73 30 00 12 00 00 00 2F 64 61 74 61 2F 6D  e:s0...../data/m
//      00000030  65 64 69 61 28 2F 2E 2A 29 3F 00 00 00 00 00 03  edia(/.*)?......
//      00000040  00 00 00 01 00 00 00 0B 00 00 00 62 00 00 00 45  ...........b...E
//      00000050  52 43 50 62 00 00 00 14 00 00 00 01 00 00 00 FF  RCPb...........ÿ
//      00000060  FF FF FF FF FF FF FF 00 00 00 00 00 00 01 00 00  ÿÿÿÿÿÿÿ.........
//      00000070  00 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00  .@..............
//      00000080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 83  ...............ƒ
//      00000090  00 1E 1B 1D 2F 1D 6D 1D 65 1D 64 1D 69 1D 61 92  ..../.m.e.d.i.a’
//      000000A0  85 00 09 00 01 1D 2F 55 0D 78 00 09 19 78 00 1E  …...../U.x...x..
//      000000B0  00 2C 00 00 00 2C 00 00 00 02 00 00 00 00 00 00  .,...,..........
//      000000C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//      000000D0  00 00 00 00 00 00 00 00 00 00 00 00 00 06 00 00  ................
//      000000E0  00                                               .

#define COMPILED_ERCP_P1    (char[]) { \
                                0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, \
                                0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, \
                                0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x00 \
                            }

#define COMPILED_ERCP_P2    (char[]) { 0x92, 0x85, 0x00, 0x09, 0x00, 0x01, 0x1D, 0x2F, 0x55, 0x0D, 0x78, 0x00, 0x09, 0x19, 0x78, 0x00 }


#define SELINUX_MAGIC_COMPILED_FCONTEXT	0xf97cff8a

/* Version specific changes */
#define SELINUX_COMPILED_FCONTEXT_NOPCRE_VERS	1
#define SELINUX_COMPILED_FCONTEXT_PCRE_VERS	2
#define SELINUX_COMPILED_FCONTEXT_MODE		3
#define SELINUX_COMPILED_FCONTEXT_PREFIX_LEN	4
#define SELINUX_COMPILED_FCONTEXT_REGEX_ARCH	5

#define SELINUX_COMPILED_FCONTEXT_MAX_VERS \
	SELINUX_COMPILED_FCONTEXT_REGEX_ARCH

/* A regular expression stem */
typedef struct {
    uint32_t stem_len;
    char *stem_string;
} stem_s;

typedef struct {
    uint32_t    len_context_string;
    char *      str_context_string;

    uint32_t    len_regex_string;
    char *      str_regex_string;

    uint32_t    mode_bits;
    int32_t     stem_id;
    uint32_t    spec_has_meta_chars;

    uint32_t    prefix_len;

    uint32_t    len_raw_pcre_regex_info;
    char *      buf_raw_pcre_regex_info;

    uint32_t    len_raw_pcre_regex_study_data;
    char *      buf_raw_pcre_regex_study_data;
} regex_s;


uint8_t  calc_len_raw_pcre_regex_subpart(const char *exclusion_path)
{
    char *path_less_stem = strchr(exclusion_path + 1, '/');
    return (sizeof(uint8_t) + (strlen(path_less_stem) * 2) + sizeof(COMPILED_ERCP_P2) + sizeof(uint8_t));
}

uint32_t calc_len_raw_pcre_regex_info(const char *exclusion_path)
{
    uint32_t r = 0;
    char *path_less_stem = strchr(exclusion_path + 1, '/');

    r += sizeof(uint32_t);              // MAGIC
    r += sizeof(uint32_t);              // len_raw_pcre_regex_info
    r += sizeof(COMPILED_ERCP_P1);      //
    r += sizeof(uint8_t);               // len_raw_pcre_regex_subpart
    r += sizeof(char);                  //
    r += strlen(path_less_stem) * 2;    // length of the unicode string
    r += sizeof(COMPILED_ERCP_P2);      //
    r += sizeof(uint8_t);               // len_raw_pcre_regex_subpart
    r += sizeof(uint8_t);               // null

    return r;
}

char *construct_raw_pcre_regex_info(char *dest, const char *exclusion_path)
{
    char *buf = dest;
    size_t off = 0;
    char *path_less_stem = strchr(exclusion_path + 1, '/');

    uint32_t len_raw_pcre_regex_info = calc_len_raw_pcre_regex_info(exclusion_path);
    uint8_t  len_raw_pcre_regex_subpart = calc_len_raw_pcre_regex_subpart(exclusion_path);

    memcpy(buf + off, "ERCP", 4);
    off += 4;

    memcpy(buf + off, &len_raw_pcre_regex_info, sizeof(uint32_t));
    off += sizeof(uint32_t);

    memcpy(buf + off, COMPILED_ERCP_P1, sizeof(COMPILED_ERCP_P1));
    off += sizeof(COMPILED_ERCP_P1);

    memcpy(buf + off, &len_raw_pcre_regex_subpart, sizeof(uint8_t));
    off += sizeof(uint8_t);

    memcpy(buf + off, (char[]){ 0x1B }, 1);
    off += 1;

    // copy as unicode string, without trailing null
    while (path_less_stem[0])
    {
        buf[off++] = 0x1D;
        buf[off++] = path_less_stem[0];
        path_less_stem++;
    }

    memcpy(buf + off, COMPILED_ERCP_P2, sizeof(COMPILED_ERCP_P2));
    off += sizeof(COMPILED_ERCP_P2);

    memcpy(buf + off, &len_raw_pcre_regex_subpart, sizeof(uint8_t));
    off += sizeof(uint8_t);

    memcpy(buf + off, (char[]){ 0x00 }, 1);
    off += 1;

    return dest;
}


typedef struct {
    uint32_t    len_raw_pcre_regex_study_data;
    uint32_t    unknown02;
    uint32_t    null_1;
    uint32_t    null_2;
    uint32_t    null_3;
    uint32_t    null_4;
    uint32_t    null_5;
    uint32_t    null_6;
    uint32_t    null_7;
    uint32_t    null_9;
    uint32_t    len_path_less_stem;
} raw_pcre_regex_study_data_s;

uint32_t calc_len_raw_pcre_regex_study_data(UNUSED const char *exclusion_path)
{
    return sizeof(raw_pcre_regex_study_data_s);
}

uint32_t calc_len_path_less_stem(const char *exclusion_path)
{
    return strlen(strchr(exclusion_path + 1, '/'));
}

char *construct_raw_pcre_regex_study_data(char *dest, const char *exclusion_path)
{
    uint32_t len_raw_pcre_regex_study_data = calc_len_raw_pcre_regex_study_data(exclusion_path);
    uint32_t len_path_less_stem = calc_len_path_less_stem(exclusion_path);

    memset(dest, 0, sizeof(raw_pcre_regex_study_data_s));

    ((raw_pcre_regex_study_data_s *)dest)->len_raw_pcre_regex_study_data = len_raw_pcre_regex_study_data;
    ((raw_pcre_regex_study_data_s *)dest)->unknown02 = 2;
    ((raw_pcre_regex_study_data_s *)dest)->len_path_less_stem = len_path_less_stem;

    return dest;
}


// TODO: add error handling
static int inject_file_contexts_bin(const char *path)
{
    /*
     * File Format
     *
     * u32 - magic number
     * u32 - version
     * u32 - length of pcre version EXCLUDING nul
     * char - pcre version string EXCLUDING nul
     * u32 - number of stems
     * ** Stems
     *  u32  - length of stem EXCLUDING nul
     *  char - stem char array INCLUDING nul
     * u32 - number of regexs
     * ** Regexes
     *  u32  - length of upcoming context INCLUDING nul
     *  char - char array of the raw context
     *  u32  - length of the upcoming regex_str
     *  char - char array of the original regex string including the stem.
     *  u32  - mode bits for >= SELINUX_COMPILED_FCONTEXT_MODE
     *         mode_t for <= SELINUX_COMPILED_FCONTEXT_PCRE_VERS
     *  s32  - stemid associated with the regex
     *  u32  - spec has meta characters
     *  u32  - The specs prefix_len if >= SELINUX_COMPILED_FCONTEXT_PREFIX_LEN
     *  u32  - data length of the pcre regex
     *  char - a bufer holding the raw pcre regex info
     *  u32  - data length of the pcre regex study daya
     *  char - a buffer holding the raw pcre regex study data
     */

    FILE *bin_file_in;
    FILE *bin_file_out;
    size_t len;

    // header
    uint32_t magic;
    uint32_t version;
    uint32_t reg_version_len;
    char *reg_version_string;
    uint32_t regex_arch_len;
    char *regex_arch_string;

    bin_file_in = fopen(path, "rb");
    if (!bin_file_in) {
        ERROR("Failed to open '%s' for reading!\n", path);
        return -1;
    }

    /* check if this looks like an fcontext file */
    len = fread(&magic, sizeof(uint32_t), 1, bin_file_in);
    if (len != 1 || magic != SELINUX_MAGIC_COMPILED_FCONTEXT)
    {
        fclose(bin_file_in);
        return -1;
    }

    /* check if this version is higher than we understand */
    len = fread(&version, sizeof(uint32_t), 1, bin_file_in);
    if (len != 1 || version != 4)  // if (len != 1 || version > SELINUX_COMPILED_FCONTEXT_MAX_VERS)
    {
        // we currently only support version 4, all the ones i have seen are version 4 at the moment
        ERROR("Unsupported /file_contexts.bin version %d\n", version);
        fclose(bin_file_in);
        return -1;
    }


    // we can process this, let's open the output file and start
    char *tmp_name = NULL;
    const int size = strlen(path) + 5;
    tmp_name = malloc(size);
    snprintf(tmp_name, size, "%s-new", path);

    bin_file_out = fopen(tmp_name, "w");
    if (!bin_file_out) {
        ERROR("Failed to open '%s' for writing!\n", tmp_name);
        fclose(bin_file_in);
        free(tmp_name);
        return -1;
    }

    /* write some magic number */
    len = fwrite(&magic, sizeof(uint32_t), 1, bin_file_out);

    /* write the version */
    len = fwrite(&version, sizeof(uint32_t), 1, bin_file_out);


    if (version >= SELINUX_COMPILED_FCONTEXT_PCRE_VERS) {
        /* read version of the regex back-end */
        len = fread(&reg_version_len, sizeof(uint32_t), 1, bin_file_in);

        reg_version_string = malloc(reg_version_len + 1);
        len = fread(reg_version_string, sizeof(char), reg_version_len, bin_file_in);
        reg_version_string[reg_version_len] = '\0';

        /* write version of the regex back-end */
        len = fwrite(&reg_version_len, sizeof(uint32_t), 1, bin_file_out);
        len = fwrite(reg_version_string, sizeof(char), reg_version_len, bin_file_out);

        free(reg_version_string);

        if (version >= SELINUX_COMPILED_FCONTEXT_REGEX_ARCH) {
            /* read regex arch string */
            len = fread(&regex_arch_len, sizeof(uint32_t), 1, bin_file_in);
            regex_arch_string = malloc(regex_arch_len + 1);
            len = fread(regex_arch_string, sizeof(char), regex_arch_len, bin_file_in);
            regex_arch_string[regex_arch_len] = '\0';

            /* write regex arch string */
            len = fwrite(&regex_arch_len, sizeof(uint32_t), 1, bin_file_out);
            len = fwrite(regex_arch_string, sizeof(char), regex_arch_len, bin_file_out);

            free(regex_arch_string);
        }
    }


    // read in the stems, check for missing and add
    uint32_t i;
    uint32_t number_of_stems;
    int32_t data_stem_id = -1;
    int32_t realdata_stem_id = -1;
    int32_t mnt_stem_id = -1;

    /* read the number of stems coming */
    len = fread(&number_of_stems, sizeof(uint32_t), 1, bin_file_in);
    stem_s *stem_array;
    stem_array = malloc((number_of_stems + 3) * sizeof(stem_s));  // add 3 since mrom additions could use them

    for (i = 0; i < number_of_stems; i++)
    {
        /* read the strlen (aka no nul) */
        len = fread(&stem_array[i].stem_len, sizeof(uint32_t), 1, bin_file_in);

        /* include the nul in the file */
        stem_array[i].stem_string = malloc(stem_array[i].stem_len + 1);
        len = fread(stem_array[i].stem_string, sizeof(char), stem_array[i].stem_len + 1, bin_file_in);

        if (strcmp(stem_array[i].stem_string, "/data") == 0)
            data_stem_id = i;
        else if (strcmp(stem_array[i].stem_string, "/realdata") == 0)
            realdata_stem_id = i;
        else if (strcmp(stem_array[i].stem_string, "/mnt") == 0)
            mnt_stem_id = i;
    }

    // if 'realdata' already exists assume multirom exclusions are present
    // this should be faster than finding 'multirom' in the regexs
    if (realdata_stem_id != -1)
    {
        INFO("/file_contexts.bin has been already injected.\n");
        for (i = 0; i < number_of_stems; i++)
            free(stem_array[i].stem_string);
        free(stem_array);
        goto noerr;
    }

    INFO("Injecting /file_contexts.bin\n");

    // add new stems here
    if (data_stem_id == -1)
    {
        stem_array[i].stem_string = strdup("/data");
        stem_array[number_of_stems].stem_len = strlen(stem_array[i].stem_string);
        data_stem_id = number_of_stems;
        number_of_stems += 1;
    }
    if (realdata_stem_id == -1)
    {
        stem_array[i].stem_string = strdup("/realdata");
        stem_array[number_of_stems].stem_len = strlen(stem_array[i].stem_string);
        realdata_stem_id = number_of_stems;
        number_of_stems += 1;
    }
    if (mnt_stem_id == -1)
    {
        stem_array[i].stem_string = strdup("/mnt");
        stem_array[number_of_stems].stem_len = strlen(stem_array[i].stem_string);
        mnt_stem_id = number_of_stems;
        number_of_stems += 1;
    }

    /* write the number of stems coming */
    len = fwrite(&number_of_stems, sizeof(uint32_t), 1, bin_file_out);

    for (i = 0; i < number_of_stems; i++)
    {
        /* write the strlen (aka no nul) */
        len = fwrite(&stem_array[i].stem_len, sizeof(uint32_t), 1, bin_file_out);

        /* include the nul in the file */
        len = fwrite(stem_array[i].stem_string, sizeof(char), stem_array[i].stem_len + 1, bin_file_out);

        free(stem_array[i].stem_string);
    }
    free(stem_array);


    uint32_t number_of_regexs;
    /* read the number of regexes coming */
    len = fread(&number_of_regexs, sizeof(uint32_t), 1, bin_file_in);

    /* write the number of regexes coming */
    number_of_regexs += NUM_OF_REGEXS_EXCLUSIONS; // add mrom exclusions count
    len = fwrite(&number_of_regexs, sizeof(uint32_t), 1, bin_file_out);


    //// now write (actually copy/paste) the normal regexs back since we don't want to
    //// read and parse them in, do nothing and write them back
    #define BUFFER_SIZE 1024*1024
    char *buf = malloc(BUFFER_SIZE);
    while ((len = fread(buf, 1, BUFFER_SIZE, bin_file_in)) > 0)
    {
        len = fwrite(buf, 1, len, bin_file_out);
    }
    free(buf);


    // now adjust the stem_id and write the multirom exclusions
    for (i = 0; i < NUM_OF_REGEXS_EXCLUSIONS; i++)
    {
        regex_s regex;

        // build the compiled regex
        regex.str_context_string = "<<none>>",
        regex.len_context_string = strlen(regex.str_context_string) + 1;

        regex.str_regex_string = malloc(strlen(multirom_exclusion_path[i]) + 7);
        strcpy(regex.str_regex_string, multirom_exclusion_path[i]);
        strcat(regex.str_regex_string, "(/.*)?");

        regex.len_regex_string = strlen(regex.str_regex_string) + 1;
        regex.prefix_len = strlen(multirom_exclusion_path[i]);

        regex.mode_bits = 0x00000000;
        regex.spec_has_meta_chars = 0x00000001;

        if (strncmp(regex.str_regex_string, "/data", sizeof("/data") - 1) == 0)
            regex.stem_id = data_stem_id;
        else if (strncmp(regex.str_regex_string, "/realdata", sizeof("/realdata") - 1) == 0)
            regex.stem_id = realdata_stem_id;
        else if (strncmp(regex.str_regex_string, "/mnt", sizeof("/mnt") - 1) == 0)
            regex.stem_id = mnt_stem_id;
        // else error


        // now write the compiled regex
        len = fwrite(&regex.len_context_string, sizeof(uint32_t), 1, bin_file_out);
        len = fwrite(regex.str_context_string, sizeof(char), regex.len_context_string, bin_file_out);

        len = fwrite(&regex.len_regex_string, sizeof(uint32_t), 1, bin_file_out);
        len = fwrite(regex.str_regex_string, sizeof(char), regex.len_regex_string, bin_file_out);

        len = fwrite(&regex.mode_bits, sizeof(uint32_t), 1, bin_file_out);
        len = fwrite(&regex.stem_id, sizeof(int32_t), 1, bin_file_out);
        len = fwrite(&regex.spec_has_meta_chars, sizeof(uint32_t), 1, bin_file_out);

        len = fwrite(&regex.prefix_len, sizeof(uint32_t), 1, bin_file_out);
        free(regex.str_regex_string);


        // construct here (ie we're constructing it from extrapolating from '/data/media(/.*)?', not generating a compiled version)
        regex.len_raw_pcre_regex_info = (uint32_t)calc_len_raw_pcre_regex_info(multirom_exclusion_path[i]);
        regex.buf_raw_pcre_regex_info = malloc(regex.len_raw_pcre_regex_info);
        if(regex.buf_raw_pcre_regex_info)
        {
            construct_raw_pcre_regex_info(regex.buf_raw_pcre_regex_info, multirom_exclusion_path[i]);
            len = fwrite(&regex.len_raw_pcre_regex_info, sizeof(uint32_t), 1, bin_file_out);
            len = fwrite(regex.buf_raw_pcre_regex_info, sizeof(char), regex.len_raw_pcre_regex_info, bin_file_out);
            free(regex.buf_raw_pcre_regex_info);
        }
        //else error

        regex.len_raw_pcre_regex_study_data = (uint32_t)calc_len_raw_pcre_regex_study_data(multirom_exclusion_path[i]);
        regex.buf_raw_pcre_regex_study_data = malloc(regex.len_raw_pcre_regex_study_data);
        if(regex.buf_raw_pcre_regex_study_data)
        {
            construct_raw_pcre_regex_study_data(regex.buf_raw_pcre_regex_study_data, multirom_exclusion_path[i]);
            len = fwrite(&regex.len_raw_pcre_regex_study_data, sizeof(uint32_t), 1, bin_file_out);
            len = fwrite(regex.buf_raw_pcre_regex_study_data, sizeof(char), regex.len_raw_pcre_regex_study_data, bin_file_out);
            free(regex.buf_raw_pcre_regex_study_data);
        }
        //else error
    }

out:
    fclose(bin_file_out);
    fclose(bin_file_in);
    rename(tmp_name, path);
    chmod(path, 0644);
    //copy_file(path, "/cache/file_contexts.bin-new"); // in case we need to debug
    free(tmp_name);
    return 0;
noerr:
    fclose(bin_file_out);
    fclose(bin_file_in);
    remove(tmp_name);
    free(tmp_name);
    return 0;
err:
    fclose(bin_file_out);
    fclose(bin_file_in);
    remove(tmp_name);
    free(tmp_name);
    return -2;
}
/* ************************************************************************************************************************************************ */


void rom_quirks_on_initrd_finalized(void)
{
    // walk over all _regular_ files in /
    DIR *d = opendir("/");
    if(d)
    {
        struct dirent *dt;
        char buff[128];
        while((dt = readdir(d)))
        {
            if(dt->d_type != DT_REG)
                continue;

            // The Android L and later releases have SELinux
            // set to "enforcing" and "restorecon_recursive /data" line in init.rc.
            // Restorecon on /data goes into /data/media/0/multirom/roms/ and changes
            // context of all secondary ROMs files to that of /data, including the files
            // in secondary ROMs /system dirs. We need to prevent that.
            // Right now, we do that by adding entries into /file_contexts that say
            // MultiROM folders don't have any context
            //
            // Android N is using the binary format of file_contexts, try to inject it
            // with MultiROM exclusions, if that fails go back to the old method and remove
            // 'restorecon_recursive' from init.rc scripts
            if((strcmp(dt->d_name, "file_contexts") == 0) || (strcmp(dt->d_name, "file_contexts.bin") == 0))
            {
                FILE *f;
                uint32_t magic = 0;
                int res = 1;

                snprintf(buff, sizeof(buff), "/%s", dt->d_name);

                f = fopen(buff, "rb");
                if (f)
                {
                    if (fread(&magic, sizeof magic, 1, f) != 1)
                        ERROR("Could not read magic in '%s'\n", buff);
                    fclose(f);

                    if (magic == SELINUX_MAGIC_COMPILED_FCONTEXT)
                        res = inject_file_contexts_bin(buff);
                    else
                        res = inject_file_contexts(buff);
                }

                if(res != 0)
                    disable_restorecon_recursive();
            }

            // franco.Kernel includes script init.fk.sh which remounts /system as read only
            // comment out lines with mount and /system in all .sh scripts in /
            if(strendswith(dt->d_name, ".sh"))
            {
                snprintf(buff, sizeof(buff), "/%s", dt->d_name);
                workaround_mount_in_sh(buff);
            }
        }
        closedir(d);
    }
}
