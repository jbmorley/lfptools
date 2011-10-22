/* lfpsplitter - Splits the .lfp files generated by Lytro's desktop app into
 *  .jpg and .txt files in a hopefully platform independent way
 * 
 * Copyright (c) 2011, Nirav Patel <nrp@eclecti.cc>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "lfpsplitter.h"

#define SHA1_LENGTH 45
#define MAGIC_LENGTH 12
#define BLANK_LENGTH 35

static char *load_file(const char *filename, int *len)
{
    char *buf = NULL;
    FILE *fp;
    
    if (!(fp = fopen(filename, "r"))) {
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    *len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = malloc(*len);
    
    *len = fread(buf, 1, *len, fp);
    fclose(fp);
    
    return buf;
}

static int lfp_file_check(const char *lfp, int len)
{
    char magic[8] = {0x89, 0x4C, 0x46, 0x50, 0x0D, 0x0A, 0x1A, 0x0A};
    if (len > 8 && memcmp(lfp, magic, 8) == 0) return 1;
    return 0;
}

static lfp_section_p parse_section(char **lfp, int *in_len)
{
    char *ptr = *lfp;
    int len = *in_len;
    lfp_section_p section = calloc(1,sizeof(lfp_section_t));
    if (!section) return NULL;
    
    // There may be some null region between sections
    while (*ptr == '\0' && len) {
        ptr++;
        len--;
    }
    
    if (len <= MAGIC_LENGTH+sizeof(uint32_t)+SHA1_LENGTH+BLANK_LENGTH) {
        free(section);
        return NULL;
    }
    
    // Copy the magic type
    memcpy(section->type, ptr, 4);
    // Move past the magic and the first 4 bytes of 0s
    ptr += MAGIC_LENGTH;
    len -= MAGIC_LENGTH;
    
    // the length is stored as a big endian unsigned 32 bit int
    section->len = ntohl(*(uint32_t *)ptr);
    ptr += sizeof(uint32_t);
    len -= sizeof(uint32_t);
    
    // copy and move past the sha1 string and the 35 byte empty space
    memcpy(section->sha1, ptr, SHA1_LENGTH);
    ptr += SHA1_LENGTH+BLANK_LENGTH;
    len -= SHA1_LENGTH+BLANK_LENGTH;
    
    // make sure there exists as much data as the header claims
    if (len < section->len) {
        free(section);
        return NULL;
    }
    
    section->data = malloc(section->len);
    if (!section->data) {
        free(section);
        return NULL;
    }
    
    memcpy(section->data, ptr, section->len);
    ptr += section->len;
    len -= section->len;
    
    *lfp = ptr;
    *in_len = len;
    
    return section;
}

static char *depth_string(const char *data, int *datalen, int len)
{
    // make sure there is enough space for the ascii formatted floats
    int filelen = 20*len/4;
    char *depth = malloc(filelen);
    char *start = depth;
    int i = 0;
    
    if (!depth) return NULL;
    depth[0] = '\0';
    
    for (i = 0; i < len/4; i++) {
        char val[20];
        int vallen = 0;
        snprintf(val, 20, "%f\n",*(float *)(data+i*4));
        vallen = strlen(val);
        strncpy(depth, val, vallen);
        depth += vallen;
    }
    
    *datalen = depth-start;
    
    return start;
}

static int save_data(const char *data, int len, const char *filename)
{
    FILE *fp;
    
    if (!(fp = fopen(filename, "wb")))
        return 0;
    
    if (fwrite(data, 1, len, fp) != len)
        return 0;
    
    fclose(fp);
    
    return 1;
}

int main(int argc, char *argv[])
{
    char *lfp = NULL;
    char *ptr = NULL;
    char *period = NULL;
    char *prefix = NULL;
    int len = 0;
    int num_sections = 0;
    int i = 0;
    lfp_section_p sections[100];

    if (argc < 2) {
        fprintf(stderr, "Usage: lfpsplitter file.lfp\n");
        return 1;
    }
    
    if (!(lfp = load_file(argv[1],&len))) {
        fprintf(stderr, "Failed to open file %s\n", argv[1]);
        return 1;
    }
    
    if (!lfp_file_check(lfp, len)) {
        fprintf(stderr, "File %s does not look like an lfp\n", argv[1]);
        free(lfp);
        return 1;
    }
    
    // save the first part of the filename to name the jpgs later
    if (!(prefix = strdup(argv[1]))) {
        free(lfp);
        return 1;
    }
    period = strrchr(prefix,'.');
    if (period) *period = '\0';
    
    ptr = lfp;
    
    // Move past the first header
    ptr += MAGIC_LENGTH+sizeof(uint32_t);
    len -= MAGIC_LENGTH+sizeof(uint32_t);
    
    while (len > 0) {
        lfp_section_p section = parse_section(&ptr, &len);
        if (!section) break;
        
        sections[num_sections] = section;
        num_sections++;
    }
    
    if (num_sections > 2) {
        char name[256];
        char *depth;
        int depthlen = 0;
        
        // Save the plaintext metadata
        snprintf(name, 256, "%s_metadata.txt", prefix);
        if (save_data(sections[0]->data, sections[0]->len, name)) {
            printf("Saved %s\n", name);
        } else {
            fprintf(stderr, "Failed to save %s\n", name);
        }
        
        // Parse the depth lookup table and save as plaintext
        depth = depth_string(sections[1]->data, &depthlen, sections[1]->len);
        if (depth) {
            snprintf(name, 256, "%s_depth.txt", prefix);
            if (save_data(depth, depthlen, name)) {
                printf("Saved %s\n", name);
            } else {
                fprintf(stderr, "Failed to save %s\n", name);
            }
        }
        
        // Save the jpegs
        for (i = 2; i < num_sections; i++) {
            snprintf(name, 256, "%s_%d.jpg", prefix, i-2);
            if (save_data(sections[i]->data, sections[i]->len, name)) {
                printf("Saved %s\n", name);
            } else {
                fprintf(stderr, "Failed to save %s\n", name);
            }
        }
    } else {
        fprintf(stderr, "Something went wrong, no images found in %s\n", argv[1]);
    }
    
    for (i = 0; i < num_sections; i++) {
        free(sections[i]);
    }
    
    free(prefix);
    free(lfp);
    return 0;
}
