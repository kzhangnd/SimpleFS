/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

// queue.h is included to build a queue data structure

#include "page_table.h"
#include "disk.h"
#include "program.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

// for easier access for swapping algorithm
char *physmem;  
struct disk *disk;

int npages;
int nframes;

// record performance
int num_page_fault = 0;
int num_disk_read = 0;
int num_disk_write = 0;

// data stucture for swapping algorithm
struct frame_table {
    int size;
    int *table; // store the accociated page number remember to free
} ft;

Queue *q = NULL; // queue for fifo

// data structure for clock algorithm
struct clock_table {
    int hand;
    int size;
    int *table; // store the state bit of associated page number of each frame
    int *state; // store the state of the corresponding table entry
} ct;

// get the index of the page to be evited from the clock table
int getEvictPage() {
    while (1) {
        // find 0 state
        if (!ct.state[ct.hand]) {
            int result = ct.hand;
            if ((++ct.hand) == ct.size) ct.hand = 0;
            return result;
        } 

        // 1 state, decrease to 0
        ct.state[ct.hand] = 0;
        if ((++ct.hand) == ct.size) ct.hand = 0;
    }
}

// check if a char[] is a number, trailing 0 is allowed
int isNumber(char *id) {
    while (*id != '\0') {
        if (!isdigit(*id)) return 0;
        id++;
    }
    return 1;
}

// print out page fault and disk read & write info
void print_info() {
    printf("Number of page faults: %d\n", num_page_fault);
    printf("Number of disk reads: %d\n", num_disk_read);
    printf("Number of disk writes: %d\n", num_disk_write);
    return;
}

void page_fault_handler_rand(struct page_table *pt, int page) {
    num_page_fault++;
    int frame;
    int bits;

    // get state of the page
    page_table_get_entry(pt, page, &frame, &bits);
    if (!(bits)) {  // page not in memory yet
        if (ft.size < nframes) { // frame not all used yet
            frame = ft.size;
            ft.table[ft.size++] = page;
            page_table_set_entry(pt, page, frame, PROT_READ);

            disk_read(disk, page, &physmem[frame * PAGE_SIZE]);
            num_disk_read++;
        } else { // need to evit a certain frame
            // randomly find one to evict
            int index = rand() % nframes;
            int page_evict = ft.table[index];
            ft.table[index] = page;
            int bits_evict;
            page_table_get_entry(pt, page_evict, &frame, &bits_evict);

            if (bits_evict & PROT_WRITE) { // need to write back to disk, since modified
                disk_write(disk, page_evict, &physmem[frame * PAGE_SIZE]);
                num_disk_write++;
            } 

            disk_read(disk, page, &physmem[frame * PAGE_SIZE]); // read new page
            num_disk_read++;

            page_table_set_entry(pt, page, frame, PROT_READ);
            page_table_set_entry(pt, page_evict, frame, 0);
        }

    } else if (bits & PROT_READ) { // need write permission
        page_table_set_entry(pt, page, frame, PROT_READ|PROT_WRITE);
    } else {
        fprintf(stderr, "Undefined behavior\n");
        exit(1);
    }
    return;
}

void page_fault_handler_fifo(struct page_table *pt, int page) {
    num_page_fault++;
    int frame;
    int bits;

    // get state of the page
    page_table_get_entry(pt, page, &frame, &bits);
    if (!(bits)) {  // page not in memory yet
        if ((frame = sizeQueue(q)) < nframes) { // frame not all used yet
            pushQueue(q, page);
            page_table_set_entry(pt, page, frame, PROT_READ);

            disk_read(disk, page, &physmem[frame * PAGE_SIZE]);
            num_disk_read++;
        } else { // need to evit a certain frame
            int page_evict = frontQueue(q)->val;
            int bits_evict;
            page_table_get_entry(pt, page_evict, &frame, &bits_evict);

            if (bits_evict & PROT_WRITE) {  // need to write back to disk, since modified
                disk_write(disk, page_evict, &physmem[frame * PAGE_SIZE]);
                num_disk_write++;
            } 

            disk_read(disk, page, &physmem[frame * PAGE_SIZE]); // read new page
            num_disk_read++;

            page_table_set_entry(pt, page, frame, PROT_READ);
            page_table_set_entry(pt, page_evict, frame, 0);

            // remove the previous page from queue, and add the new one
            popQueue(q);
            pushQueue(q, page);
        }

    } else if (bits & PROT_READ) { // need write permission
        page_table_set_entry(pt, page, frame, PROT_READ|PROT_WRITE);
    } else {
        fprintf(stderr, "Undefined behavior\n");
        exit(1);
    }
    return;
}

void page_fault_handler_custom(struct page_table *pt, int page) {
    num_page_fault++;
    int frame;
    int bits;

    // get state of the page
    page_table_get_entry(pt, page, &frame, &bits);
    if (!(bits)) {  // page not in memory yet
        if (ct.size < nframes) { // frame not all used yet
            frame = ct.size;
            ct.table[ct.size++] = page;
    
            page_table_set_entry(pt, page, frame, PROT_READ);
            disk_read(disk, page, &physmem[frame * PAGE_SIZE]);
            num_disk_read++;
        } else { // need to evit a certain frame
            int index = getEvictPage();
            int page_evict = ct.table[index];
            int bits_evict;
            page_table_get_entry(pt, page_evict, &frame, &bits_evict);

            // update clock table
            ct.table[index] = page;

            if (bits_evict & PROT_WRITE) {  // need to write back to disk, since modified
                disk_write(disk, page_evict, &physmem[frame * PAGE_SIZE]);
                num_disk_write++;
            } 

            disk_read(disk, page, &physmem[frame * PAGE_SIZE]); // read new page
            num_disk_read++;

            page_table_set_entry(pt, page, frame, PROT_READ);
            page_table_set_entry(pt, page_evict, frame, 0);
        }

    } else if (bits & PROT_READ) { // need write permission
        page_table_set_entry(pt, page, frame, PROT_READ|PROT_WRITE);
        ct.state[frame] = 1;
    } else {
        fprintf(stderr, "Undefined behavior\n");
        exit(1);
    }
    return;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("use: virtmem <npages> <nframes> <rand|fifo|custom> <alpha|beta|gamma|delta>\n");
        return 1;
    }

    if (!isNumber(argv[1])) {
        printf("number of pages has to be a number\n");
        return 1;
    }

    if (!isNumber(argv[2])) {
        printf("number of frames has to be a number\n");
        return 1;
    }

    npages = atoi(argv[1]);
    nframes = atoi(argv[2]);

    if (npages < 3 || nframes < 3) {
        printf("number of pages and frames has to be at least 3\n");
        return 1;
    }

    const char *algorithm = argv[3];
    const char *program = argv[4];

    disk = disk_open("myvirtualdisk", npages);
    if (!disk) {
        fprintf(stderr, "couldn't create virtual disk: %s\n", strerror(errno));
        return 1;
    }

    void *page_fault_handler;
    if (!strcmp(algorithm, "rand")) {
        page_fault_handler = page_fault_handler_rand;

        // initialize frame table
        ft.size = 0;
        ft.table = (int*) malloc(sizeof(int)*nframes);
        if (!ft.table) {
            fprintf(stderr, "couldn't create frame table: %s\n", strerror(errno));
            return 1;
        }
    }
    else if (!strcmp(algorithm, "fifo")) {
        page_fault_handler = page_fault_handler_fifo;

        // initialize queue
        q = initQueue();
    }
    else if (!strcmp(algorithm, "custom")) {
        page_fault_handler = page_fault_handler_custom;

        // initialize clock table
        ct.size = 0;
        ct.hand = 0;
        ct.table = (int*) malloc(sizeof(int)*nframes);
        if (!ct.table) {
            fprintf(stderr, "couldn't create clock table: %s\n", strerror(errno));
            return 1;
        }
        ct.state = (int*) calloc(nframes, sizeof(int));
        if (!ct.state) {
            fprintf(stderr, "couldn't create clock state table: %s\n", strerror(errno));
            return 1;
        }
    }
    else {
        fprintf(stderr, "unknown algorithm: %s\n", algorithm);
        return 1;
    }


    struct page_table *pt = page_table_create(npages, nframes, page_fault_handler);
    if(!pt) {
        fprintf(stderr, "couldn't create page table: %s\n", strerror(errno));
        return 1;
    }

        

    char *virtmem = page_table_get_virtmem(pt);

    physmem = page_table_get_physmem(pt);

    if(!strcmp(program, "alpha")) {
        alpha_program(virtmem, npages*PAGE_SIZE);

    } else if(!strcmp(program, "beta")) {
        beta_program(virtmem, npages*PAGE_SIZE);

    } else if(!strcmp(program, "gamma")) {
        gamma_program(virtmem, npages*PAGE_SIZE);

    } else if(!strcmp(program, "delta")) {
        delta_program(virtmem, npages*PAGE_SIZE);

    } else {
        fprintf(stderr,"unknown program: %s\n",argv[3]);
        return 1;
    }

    // page fault and disk read & write info    
    print_info();

    free(ft.table);
    page_table_delete(pt);
    disk_close(disk);

    return 0;
}
