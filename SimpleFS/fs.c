
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024


// 0 initialized
static const char emptyblock[DISK_BLOCK_SIZE];


int *bitmap;
int mounted = 0;
int ninodes;

struct fs_superblock {
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode {
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union fs_block {
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

// check which inode and which component a data block belongs to
struct fs_belong {
    int inode;
    int direct_pointer;
    int indirect_pointer;
    int indirect_block;
};

struct fs_belong *belong;


// load an inode based on inumber, assume valid inumber
void inode_load(int inumber, struct fs_inode *inode) {
    int block_number = inumber / INODES_PER_BLOCK + 1;
    int offset = inumber % INODES_PER_BLOCK;

    // read the block with that inode
    union fs_block block;
    disk_read(block_number, block.data);

    // get one inode
    *inode = block.inode[offset];
    return;
}

// save an inode based on inumber, assume valid inumber
void inode_save(int inumber, struct fs_inode *inode) {
    int block_number = inumber / INODES_PER_BLOCK + 1;
    int offset = inumber % INODES_PER_BLOCK;

    // read the block with that inode
    union fs_block block;
    disk_read(block_number, block.data);

    // save one inode
    block.inode[offset] = *inode;

    // write back
    disk_write(block_number, block.data);
    return;
}

// return the block number of a free data block. return 0 at failure
int get_block() {
    union fs_block block;

    // superblock information
    disk_read(0, block.data);
    int nblocks = block.super.nblocks;

    for (int i = block.super.ninodeblocks+1; i < nblocks; ++i) {
        if (bitmap[i]) continue;

        // not in use
        bitmap[i] = 1;
        return i;
    }
    
    // disk full
    return 0;
}

int fs_format() {
    if (mounted) {
        fprintf(stderr, "file system already mounted\n");
        return 0;
    }

    union fs_block block;

    // superblock information
    disk_read(0, block.data);

    int nblocks = disk_size();
    int ninodeblocks = (nblocks - 1) / 10 + 1;

    // clear inode table
    for (int i = 1; i <= ninodeblocks; ++i) disk_write(i, emptyblock);

    // set superblock
    block.super.magic = FS_MAGIC;
    block.super.nblocks = nblocks;
    block.super.ninodeblocks = ninodeblocks;
    block.super.ninodes = INODES_PER_BLOCK * ninodeblocks;

    // save superblock info
    disk_write(0, block.data);
    
    return 1;
}

void fs_debug() {
    union fs_block block;

    // superblock information
    disk_read(0, block.data);
    printf("superblock:\n");
    printf("    %d blocks\n", block.super.nblocks);
    printf("    %d inode blocks\n", block.super.ninodeblocks);
    printf("    %d inodes\n", block.super.ninodes);

    // check each inode block
    int ninodeblocks = block.super.ninodeblocks;
    for (int i = 1; i <= ninodeblocks; ++i) {
        disk_read(i, block.data);

        // check each inode in the block
        for (int j = 0; j < INODES_PER_BLOCK; ++j) {
            // not valid
            if (!block.inode[j].isvalid) continue;

            int inumber = (i-1) * INODES_PER_BLOCK + j;
            struct fs_inode curr = block.inode[j];

            printf("inode %d:\n", inumber);
            printf("    size: %d bytes\n", curr.size);

            // direct block
            if (memcmp(emptyblock, curr.direct, POINTERS_PER_INODE)) {  // check there are any non-zero pointer. use memcmp to increase robustness
                printf("    direct blocks:");
                for (int k = 0; k < POINTERS_PER_INODE; ++k) {
                    if (curr.direct[k]) printf(" %d", curr.direct[k]);
                }
                printf("\n");
            }

            // indirect block
            if (curr.indirect) {
                printf("    indirect block: %d\n", curr.indirect);

                // read in pointer block
                union fs_block pointer_block;
                disk_read(curr.indirect, pointer_block.data);

                if (memcmp(emptyblock, pointer_block.pointers, POINTERS_PER_BLOCK)) {
                    printf("    indirect data blocks:");
                    for (int k = 0; k < POINTERS_PER_BLOCK; ++k) {
                        if (pointer_block.pointers[k]) printf(" %d", pointer_block.pointers[k]);
                    }
                    printf("\n");
                }
            }   
        }

    }   
    return;
}

int fs_mount() {
    if (mounted) {
        fprintf(stderr, "file system already mounted\n");
        return 0;
    }

    union fs_block block;

    // superblock information
    disk_read(0, block.data);
    if (FS_MAGIC != block.super.magic) {
        fprintf(stderr, "disk is not formatted\n");
        return 0; 
    }

    // check each inode block
    int ninodeblocks = block.super.ninodeblocks;
    int nblocks = block.super.nblocks;

    if (nblocks != disk_size()) {
        fprintf(stderr, "disk size error\n");
        return 0; 
    }

    // declare bitmap
    bitmap = (int*) calloc(nblocks, sizeof(int));
    if (!bitmap) {
        fprintf(stderr, "couldn't create bitmap: %s\n", strerror(errno));
        return 0;
    }

    // declare belong map
    belong = (struct fs_belong*) calloc(nblocks, sizeof(struct fs_belong));
    if (!belong) {
        fprintf(stderr, "couldn't create belong map: %s\n", strerror(errno));
        return 0;
    }

    // superblock as 1
    *(bitmap) = 1;

    for (int i = 1; i <= ninodeblocks; ++i) {
        union fs_block inode_block;
        disk_read(i, inode_block.data);

        // this inode block is in use
        *(bitmap + i) = 1;

        // check each inode
        for (int j = 0; j < INODES_PER_BLOCK; ++j) {
            if (!inode_block.inode[j].isvalid) continue;

            // check each direct pointer
            struct fs_inode *curr = &inode_block.inode[j];
            int inode_number = (i-1) * INODES_PER_BLOCK + j;

            for (int k = 0; k < POINTERS_PER_INODE; ++k) {
                if (curr->direct[k]) {

                    // change bit map
                    int *bit = bitmap + curr->direct[k];
                    if (*bit) { // multiple inode to the same data block
                        fprintf(stderr, "illegal fs: data block conflict\n");
                        return 0;
                    }

                    *(bit) = 1;

                    // change belong map
                    memset(&belong[curr->direct[k]], 0, sizeof(struct fs_belong));
                    belong[curr->direct[k]].inode = inode_number;
                    belong[curr->direct[k]].direct_pointer = k+1;
                }
            }

            // check indirect pointer
            if (curr->indirect) {
                // this pointer block is in use
                *(bitmap + curr->indirect) = 1;

                // change belong map
                memset(&belong[curr->indirect], 0, sizeof(struct fs_belong));
                belong[curr->indirect].inode = inode_number;
                belong[curr->indirect].indirect_pointer = 1;

                union fs_block pointer_block;
                disk_read(curr->indirect, pointer_block.data);

                // check each pointer
                for (int k = 0; k < POINTERS_PER_BLOCK; ++k) {
                    if (pointer_block.pointers[k]) {
                        // change bitmap
                        *(bitmap + pointer_block.pointers[k]) = 1;

                        // change belong map
                        memset(&belong[pointer_block.pointers[k]], 0, sizeof(struct fs_belong));
                        belong[pointer_block.pointers[k]].inode = inode_number;
                        belong[pointer_block.pointers[k]].indirect_block = k+1;
                    }
                }
            }
        }
    }

    // change related global state
    mounted = 1;
    ninodes = block.super.ninodes;
    return 1;
}

int fs_create() {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return 0;
    }

    // check inode
    for (int i = 1; i < ninodes; ++i) {
        // load inode
        struct fs_inode curr;
        inode_load(i, &curr);

        // valid, already there
        if (curr.isvalid) continue;

        // initialize
        memset(&curr, 0, sizeof(struct fs_inode));
        curr.isvalid = 1;

        // save back
        inode_save(i, &curr);
        return i;
    }

    fprintf(stderr, "cannot create new inode: inode table is full\n");
    return 0;
}

int fs_delete(int inumber) {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return 0;
    }

    if (inumber < 1 || inumber >= ninodes) {
        fprintf(stderr, "illegal inumber\n");
        return 0;
    }

    struct fs_inode curr;
    inode_load(inumber, &curr);

    if (!curr.isvalid) {
        fprintf(stderr, "cannot delete inode %d: inode is not valid\n", inumber);
        return 0;
    }

    for (int i = 0; i < POINTERS_PER_INODE; ++i) {
        // have data block
        if (curr.direct[i]) {
            // clear data and change bitmap
            bitmap[curr.direct[i]] = 0;
            curr.direct[i] = 0;
        }
    }

    // check indirect pointer
    if (curr.indirect) {
        union fs_block pointer_block;
        disk_read(curr.indirect, pointer_block.data);

        // check all pointers in the pointer block
        for (int j = 0; j < POINTERS_PER_BLOCK; ++j) {

            // if valid pointer
            if (pointer_block.pointers[j]) {
                // clear data and change bitmap
                bitmap[pointer_block.pointers[j]] = 0;
                pointer_block.pointers[j] = 0;
            }
        }

        // release indrect block
        bitmap[curr.indirect] = 0;
        curr.indirect = 0;
    }

    // change valid bit
    curr.isvalid = 0;
    inode_save(inumber, &curr);

    return 1;
}

int fs_getsize(int inumber) {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return -1;
    }

    if (inumber < 1 || inumber >= ninodes) {
        fprintf(stderr, "illegal inumber\n");
        return -1;
    }

    struct fs_inode curr;
    inode_load(inumber, &curr);

    if (!curr.isvalid) {
        fprintf(stderr, "cannot get size of inode %d: inode is not valid\n", inumber);
        return -1;
    }

    return curr.size;
}

int fs_read(int inumber, char *data, int length, int offset) {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return 0;
    }

    if (inumber < 1 || inumber >= ninodes) {
        fprintf(stderr, "illegal inumber\n");
        return 0;
    }

    struct fs_inode curr;
    inode_load(inumber, &curr);

    if (!length) {
        fprintf(stderr, "cannot read 0 byte\n");
        return 0;
    }

    if (!curr.isvalid) {
        fprintf(stderr, "cannot read from inode %d: inode is not valid\n", inumber);
        return 0;
    }

    if (offset > curr.size) {
        fprintf(stderr, "offset is larger than size\n");
        return 0;
    }

    // adjust length if exceed
    if (curr.size < offset + length)
        length = curr.size - offset;


    int read_data = 0;
    int p = offset / DISK_BLOCK_SIZE;
    int offset_p = offset % DISK_BLOCK_SIZE;

    while (length && p < POINTERS_PER_INODE) {
        union fs_block block;
        //printf("read from %d\n", curr.direct[p]);
        disk_read(curr.direct[p], block.data);

        // size of read
        int to_read;
        if (offset_p + length > DISK_BLOCK_SIZE) to_read = DISK_BLOCK_SIZE - offset_p;
        else to_read = length - offset_p;

        // copy data
        memcpy(data+read_data, block.data+offset_p, to_read);

        read_data += to_read;
        offset_p = 0;
        length -= to_read;
        p++;
    }

    // finished within direct pointers
    if (!length) return read_data;

    // read indriect pointer
    union fs_block pointer_block;
    disk_read(curr.indirect, pointer_block.data);
    p -= POINTERS_PER_INODE;

    // read from block pointed by the pointers in the pointer block
    while (length) {
        // get data block
        union fs_block block;
        disk_read(pointer_block.pointers[p], block.data);

        int to_read;
        if (offset_p + length > DISK_BLOCK_SIZE) to_read = DISK_BLOCK_SIZE - offset_p;
        else to_read = length - offset_p;

        // copy data
        memcpy(data+read_data, block.data+offset_p, to_read);

        read_data += to_read;
        offset_p = 0;
        length -= to_read;
        p++;
    }

    return read_data;
}

// update inode size if necessary
void wrap_up_write(int inumber, int offset, int write_data, struct fs_inode *curr) {
    if (curr->size < offset + write_data) {
        curr->size = offset + write_data;
        inode_save(inumber, curr);
    }

}

int fs_write(int inumber, const char *data, int length, int offset) {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return 0;
    }

    if (inumber < 1 || inumber >= ninodes) {
        fprintf(stderr, "illegal inumber\n");
        return 0;
    }

    struct fs_inode curr;
    inode_load(inumber, &curr);

    if (!length) {
        fprintf(stderr, "cannot write 0 byte\n");
        return 0;
    }

    if (!curr.isvalid) {
        fprintf(stderr, "cannot write to inode %d: inode is not valid\n", inumber);
        return 0;
    }

    int write_data = 0;
    int p = offset / DISK_BLOCK_SIZE;
    int offset_p = offset % DISK_BLOCK_SIZE;

    while (length && p < POINTERS_PER_INODE) {
        union fs_block block;
        
        if (!curr.direct[p]) {
            int new_block_num = get_block();

            // disk is full
            if (!new_block_num) {
                wrap_up_write(inumber, offset, write_data, &curr);
                return write_data;
            } 

            // change belong map
            memset(&belong[new_block_num], 0, sizeof(struct fs_belong));
            belong[new_block_num].inode = inumber;
            belong[new_block_num].direct_pointer = p+1;

            // update inode
            curr.direct[p] = new_block_num;
            inode_save(inumber, &curr);
        }
        disk_read(curr.direct[p], block.data);

        // size of write
        int to_write;
        if (offset_p + length > DISK_BLOCK_SIZE) to_write = DISK_BLOCK_SIZE - offset_p;
        else to_write = length - offset_p;

        // copy data
        memcpy(block.data+offset_p, data+write_data, to_write);

        // write back
        disk_write(curr.direct[p], block.data);


        write_data += to_write;
        offset_p = 0;
        length -= to_write;
        p++;
    }

    // finished within direct pointers
    if (!length) {
        wrap_up_write(inumber, offset, write_data, &curr);
        return write_data;
    } 

    // read indriect pointer
    union fs_block pointer_block;
    if (!curr.indirect) {
        int new_block_num = get_block();

        // disk is full
        if (!new_block_num) {
            wrap_up_write(inumber, offset, write_data, &curr);
            return write_data;
        } 

        // change belong map
        memset(&belong[new_block_num], 0, sizeof(struct fs_belong));
        belong[new_block_num].inode = inumber;
        belong[new_block_num].indirect_pointer = 1;

        curr.indirect = new_block_num;
        inode_save(inumber, &curr);

        // initialize indirect block
        union fs_block indirect_block;
        disk_read(new_block_num, indirect_block.data);
        memset(&indirect_block, 0, sizeof(union fs_block));
        disk_write(new_block_num, indirect_block.data);

    }
    disk_read(curr.indirect, pointer_block.data);
    
    // indirect pointer block sttart from 0
    p -= POINTERS_PER_INODE;

    // write block pointed by the pointers in the pointer block
    while (length && p < POINTERS_PER_BLOCK) {
        // get data block
        union fs_block block;

        if (!pointer_block.pointers[p]) {
            int new_block_num = get_block();

            // disk is full
            if (!new_block_num) {
                wrap_up_write(inumber, offset, write_data, &curr);
                return write_data;
            } 

            // change belong map
            memset(&belong[new_block_num], 0, sizeof(struct fs_belong));
            belong[new_block_num].inode = inumber;
            belong[new_block_num].indirect_block = p+1;

            // write back change in indirect block
            pointer_block.pointers[p] = new_block_num;
            disk_write(curr.indirect, pointer_block.data);

        }
        disk_read(pointer_block.pointers[p], block.data);

        int to_write;
        if (offset_p + length > DISK_BLOCK_SIZE) to_write = DISK_BLOCK_SIZE - offset_p;
        else to_write = length - offset_p;

        // copy data
        memcpy(block.data+offset_p, data+write_data, to_write);

        // write back
        disk_write(pointer_block.pointers[p], block.data);

        write_data += to_write;
        offset_p = 0;
        length -= to_write;
        p++;
    }

    wrap_up_write(inumber, offset, write_data, &curr);
    return write_data;
}

// swap the content of 2 data block
void swap(int *blocknum_a, int *blocknum_b, int a_inumber, struct fs_inode *anode, union fs_block *a_indirect, int a_indirect_pointer) {
    // no need to swap
    if (*blocknum_a == *blocknum_b) {
        (*blocknum_b)++;
        return;
    }

    // read in block a
    union fs_block block_a;
    disk_read(*blocknum_a, block_a.data);

    // b is not in use
    if (!bitmap[*blocknum_b]) {
        disk_write(*blocknum_b, block_a.data);
        bitmap[*blocknum_a] = 0;
        bitmap[*blocknum_b] = 1;
        belong[*blocknum_b] = belong[*blocknum_a];
        *blocknum_a = (*blocknum_b)++;

        return;
    }

    // b is in use, load block b
    union fs_block block_b;
    disk_read(*blocknum_b, block_b.data);

    int b_inumber = belong[*blocknum_b].inode;

    // load b inode
    struct fs_inode bnode;
    if (a_inumber == b_inumber) bnode = *anode;
    else inode_load(b_inumber, &bnode);


    if (belong[*blocknum_b].direct_pointer) {
        bnode.direct[belong[*blocknum_b].direct_pointer-1] = *blocknum_a;
    } else if (belong[*blocknum_b].indirect_pointer) {
        bnode.indirect = *blocknum_a;
    } else {
        // change the pointer for b in the indirect block

        // we work on the same node and working with indirect pointer
        if (a_inumber == b_inumber && (a_indirect_pointer || a_indirect)) {

            // working a's indirect pointer, need to change content of the indirect block
            if (a_indirect_pointer) block_a.pointers[belong[*blocknum_b].indirect_block-1] = *blocknum_a;

            // 2 data block from indirect pointer block
            if (a_indirect) (*a_indirect).pointers[belong[*blocknum_b].indirect_block-1] = *blocknum_a;

        }
        else {
            union fs_block block_b_indirect;
            disk_read(bnode.indirect, block_b_indirect.data);

            block_b_indirect.pointers[belong[*blocknum_b].indirect_block-1] = *blocknum_a;

            // write back change
            disk_write(bnode.indirect, block_b_indirect.data);
        }
    }

    // swap content
    disk_write(*blocknum_b, block_a.data);
    disk_write(*blocknum_a, block_b.data);

    // swap belong for a and b
    struct fs_belong tmp;
    tmp = belong[*blocknum_a];
    belong[*blocknum_a] = belong[*blocknum_b];
    belong[*blocknum_b] = tmp;

    // save back b inode
    
    if (a_inumber == b_inumber) *anode = bnode;
    else inode_save(b_inumber, &bnode);

    // change the pointer value of a
    *blocknum_a = (*blocknum_b)++;

    return;
}

// rearrange data block of each inode so they are continuous on disk
void rearrange_datablock() {
    union fs_block block;

    // superblock information
    disk_read(0, block.data);

    // check each inode block
    int ninodeblocks = block.super.ninodeblocks;

    // starting block number of data block
    int idx = ninodeblocks + 1;

    // check inode
    for (int i = 1; i < block.super.ninodes; ++i) {
        // load inode
        struct fs_inode curr;
        inode_load(i, &curr);

        // not valid
        if (!curr.isvalid) continue;

        // check each direct pointer
        for (int j = 0; j < POINTERS_PER_INODE; ++j) {
            if (curr.direct[j]) swap(&curr.direct[j], &idx, i, &curr, NULL, 0);
        }

        if (curr.indirect) {
            // swap indirect block
            swap(&curr.indirect, &idx, i, &curr, NULL, 1);

            // read in pointer block
            union fs_block pointer_block;
            disk_read(curr.indirect, pointer_block.data);

            for (int k = 0; k < POINTERS_PER_BLOCK; ++k) {
                if (pointer_block.pointers[k]) swap(&pointer_block.pointers[k], &idx, i, &curr, &pointer_block, 0);
            }

            // write back indirect block
            disk_write(curr.indirect, pointer_block.data);   
        }

        // save back
        inode_save(i, &curr);
    }
    return;
}

// rearrange inode to start from inode 1
void rearrange_inode() {
    int idx = 1;

    // check inode
    for (int i = 1; i < ninodes; ++i) {
        // load inode
        struct fs_inode curr;
        inode_load(i, &curr);

        // not valid
        if (!curr.isvalid) continue;

        if (idx == i) { // don't need to do anything
            idx++;
            continue;
        }

        struct fs_inode dest;
        inode_load(idx, &dest);

        // swap content
        struct fs_inode buffer;
        memcpy(&buffer, &curr, sizeof(struct fs_inode));
        memcpy(&curr, &dest, sizeof(struct fs_inode));
        memcpy(&dest, &buffer, sizeof(struct fs_inode));

        // save back
        inode_save(i, &curr);
        inode_save(idx++, &dest);
    }
    return;
}

// defrag the disk
void fs_defrag() {
    if (!mounted) {
        fprintf(stderr, "file system not mounted yet\n");
        return;
    }

    // first rearrange datablock
    rearrange_datablock();

    // put inodes to the initial inodes
    rearrange_inode();

    return;
}
