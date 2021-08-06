# SimpleFS

SimpleFS is a Unix-like filesystem implemented in C.

The SimpleFS system has three major components: the shell, the filesystem itself, and the emulated disk. 

At the top level a user gives typed commands to a shell, instructing it to format or mount a disk, and to copy data in and out of the filesystem. The shell converts these typed commands into high-level operations on the filesystem, such as `fs_format`, `fs_mount`, `fs_read`, and `fs_write`. The filesystem is responsible for accepting these operations on files and converting them into simple block read and write operations on the emulated disk, called `disk_read`, and `disk_write`. The emulated disk, in turn, stores all of its data in an image file in the filesystem.

## Installation
To complie the code, run the following command at this directory level. It is execute the provided makefile.

```bash
make
```
3 example disk images are provided. The name of each disk image tells how many blocks are in each image. Each image contains some familiar files and documents.

## Usage
We have provided a simple shell that will be used to exercise the filesystem and the simulated disk. To use the shell, simply run `simplefs` with the name of a disk image, and the number of blocks in that image. For example, to use the image.5 example given below, run:

```bash
./simplefs image.5 5
```

Or, to start with a fresh new disk image, just give a new filename and number of blocks:

```bash
./simplefs mydisk 25
```

Once the shell starts, you can use the help command to list the available commands:

```bash
simplefs> help
Commands are:
    format
    mount
    debug
    create
    delete  <inode>
    cat     <inode>
    copyin  <file> <inode>
    copyout <inode> <file>
    help
    quit
    exit
```
Most of the commands correspond closely to the filesystem interface. For example, `format`, `mount`, `debug`, `create` and `delete` call the corresponding functions in the filesystem. A filesystem must be formatted once before it can be used. Likewise, it must be mounted before being read or written.

The complex commands are `cat`, `copyin`, and `copyout cat` reads an entire file out of the filesystem and displays it on the console, just like the Unix command of the same name. `copyin` and `copyout` copy a file from the local Unix filesystem into your emulated filesystem. For example, to copy the dictionary file into inode 10 in your filesystem, do the following:

```bash
 simplefs> copyin /usr/share/dict/words 10
```

Note that these three commands work by making a large number of calls to fs_read and fs_write for each file to be copied.

## Contributor
Yize Qi             yqi2@nd.edu
Hongrui Zhang		hzhang24@nd.edu
Kai Zhang           kzhang4@nd.edu
