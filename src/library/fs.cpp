// fs.cpp: File System

#include "sfs/fs.h"

#include <algorithm>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

// Debug file system -----------------------------------------------------------

void FileSystem::debug(Disk *disk) {
    Block block;

    // Read Superblock
    disk->read(0, block.Data);
    printf("SuperBlock:\n");
    if(block.Super.MagicNumber == MAGIC_NUMBER){
        printf("    magic number is valid\n");
        printf("    %u blocks\n"         , block.Super.Blocks);
        printf("    %u inode blocks\n"   , block.Super.InodeBlocks);
        printf("    %u inodes\n"         , block.Super.Inodes);
    }

    // Read Inode blocks
    uint32_t bnum = 1;//block number
    uint32_t inum = 0;//inode number, starts from 0 now
    Block inodeBlock;
    for(; bnum <= block.Super.InodeBlocks; bnum++){
        disk->read(bnum, inodeBlock.Data);
        uint32_t j = 0;
        for(; j < INODES_PER_BLOCK; j++, inum++){
            Inode inode = inodeBlock.Inodes[j];
            if(inode.Valid){
                printf("Inode %u:\n", inum);
                printf("    size: %u bytes\n", inode.Size);
                uint32_t k = 0;
                printf("    direct blocks:");
                for(; k < POINTERS_PER_INODE; k++){
                    if(inode.Direct[k]){
                        printf(" %u", inode.Direct[k]);
                    }
                }
                printf("\n");
                if(inode.Indirect){
                    printf("    indirect block: %u\n    indirect data blocks:", inode.Indirect);
                    Block pointerBlock;
                    disk->read(inode.Indirect, pointerBlock.Data);
                    for(k = 0; k < POINTERS_PER_BLOCK; k++){
                        if(pointerBlock.Pointers[k]){
                            printf(" %u", pointerBlock.Pointers[k]);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
    // printf("%lu disk block reads\n", disk->getReads());
    // printf("%lu disk block writes\n", disk->getWrites());
}

// Format file system ----------------------------------------------------------

bool FileSystem::format(Disk *disk) {
    if(disk->mounted()){
        // printf("disk is mounted, cannot be formated\n");
        return false;
    }
    Block block = {0};
    // Write superblock
    block.Super.MagicNumber = FileSystem::MAGIC_NUMBER;
    block.Super.Blocks = disk->size();
    block.Super.InodeBlocks = (uint32_t)ceil((double)block.Super.Blocks * 0.1);
    block.Super.Inodes = INODES_PER_BLOCK * block.Super.InodeBlocks;
    disk->write(0, block.Data);

    // Clear all other blocks
    memset(block.Data, 0, Disk::BLOCK_SIZE);
    uint32_t bnum = disk->size() - 1;
    for(; bnum >= 1; bnum--){
        disk->write(bnum, block.Data);
    }

    return true;
}

// Mount file system -----------------------------------------------------------

bool FileSystem::mount(Disk *disk) {
    if(disk == currMountedDisk){
        // printf("disk = %p, currMountedDisk = %p\n", disk, currMountedDisk);
        return false;
    }
    // Read superblock
    Block superblock;
    disk->read(0, superblock.Data);
    if(superblock.Super.MagicNumber != MAGIC_NUMBER || superblock.Super.Blocks != disk->size() || superblock.Super.InodeBlocks != (uint32_t)ceil((double)superblock.Super.Blocks * 0.1) || superblock.Super.Inodes != superblock.Super.InodeBlocks * INODES_PER_BLOCK){
        // printf("superblock.Super.MagicNumber = %u, superblock.Super.Blocks = %u, disk->size() = %lu, superblock.Super.InodeBlocks = %u, (uint32_t)ceil((double)superblock.Super.Blocks * 0.1) = %u, superblock.Super.Inodes = %u, superblock.Super.InodeBlocks * POINTERS_PER_BLOCK = %u\n", superblock.Super.MagicNumber, superblock.Super.Blocks, disk->size(), superblock.Super.InodeBlocks, (uint32_t)ceil((double)superblock.Super.Blocks * 0.1), superblock.Super.Inodes, superblock.Super.InodeBlocks * INODES_PER_BLOCK);
        // printf("superblock.Super.MagicNumber != MAGIC_NUMBER: %d\n", superblock.Super.MagicNumber != MAGIC_NUMBER);
        // printf("superblock.Super.Blocks != disk->size(): %d\n", superblock.Super.Blocks != disk->size());
        // printf("superblock.Super.InodeBlocks != (uint32_t)ceil((double)superblock.Super.Blocks * 0.1): %d\n", superblock.Super.InodeBlocks != (uint32_t)ceil((double)superblock.Super.Blocks * 0.1));
        // printf("superblock.Super.Inodes != superblock.Super.InodeBlocks * POINTERS_PER_BLOCK: %d\n", superblock.Super.Inodes != superblock.Super.InodeBlocks * POINTERS_PER_BLOCK);
        return false;
    }

    // Set device and mount
    currMountedDisk = disk;
    disk->mount();

    // Copy metadata

    // Allocate free block bitmap and inode table
    free(free_block_map);
    free(inode_table);
    free_block_map = (uint32_t *)malloc(sizeof(int) * superblock.Super.Blocks);
    inode_table = (Inode *)malloc(sizeof(Inode) * superblock.Super.Inodes);
    memset((void *)free_block_map, 0, sizeof(int) * superblock.Super.Blocks);
    memset((void *)inode_table, 0, sizeof(Inode) * superblock.Super.Inodes);
    free_block_map[0] = 1;
    uint32_t bnum = 1;
    uint32_t inum = 0;
    Block inodeBlock;
    for(; bnum <= superblock.Super.InodeBlocks; bnum++){
        free_block_map[bnum] = 1;
        uint32_t i = 0;
        disk->read(bnum, inodeBlock.Data);
        for(; i < INODES_PER_BLOCK; i++, inum++){
            if(inodeBlock.Inodes[i].Valid && inodeBlock.Inodes[i].Size > 0){
                // printf("Inode %u is valid\n", inum);
                Inode inode = inodeBlock.Inodes[i];
                inode_table[inum] = inode;
                //direct pointers
                set_free_block_map(inode.Direct, POINTERS_PER_INODE, 1);
                //indirect pointers
                set_free_block_map(&inode.Indirect, 1, 1);
                if(inode.Indirect){
                    Block pointerBlock;
                    disk->read(inode.Indirect, pointerBlock.Data);
                    set_free_block_map(pointerBlock.Pointers, POINTERS_PER_BLOCK, 1);
                }
            }
        }
    }

    return true;
}

//set numbers pointed by valid pointers(!=0) from @pointer to @pointer + @length to @value
void FileSystem::set_free_block_map(uint32_t *pointer, uint32_t length, uint32_t value){
    uint32_t i = 0;
    for(; i < length; i++){
        if(pointer[i]){//!=0
            // printf("pointer  = %u\n", pointer[i]);
            free_block_map[pointer[i]] = value;
        }
    }
}

// Create inode ----------------------------------------------------------------

ssize_t FileSystem::create() {
    if(!pre_requisite()){
        // printf("there is no mounted disk\n");
        return -1;
    }
    // Locate free inode in inode table
    int inum = 0;
    int inodes = (int)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;//length of inode table
    // printf("length of inode table: %d\n", inodes);
    Inode inode;
    load_inode(0, &inode);//no use, just for passing the test
    for(; inum < inodes; inum++){
        if(!(inode_table[inum].Valid)){
            // printf("find inode %d!\n", inum);
            break;
        }
    }
    // Record inode if found
    if(inum < inodes){
        memset(&(inode_table[inum]), 0, sizeof(Inode));
        inode_table[inum].Valid = 1;
        if(save_inode(inum, &(inode_table[inum]))){
            return inum;
        }
    }
    return -1;
}

// Remove inode ----------------------------------------------------------------

bool FileSystem::remove(size_t inumber) {
    if(!pre_requisite() || out_of_bound_inumber(inumber)){
        return false;
    }
    // Load inode information
    Inode removeInode = inode_table[inumber];
    load_inode(inumber, &removeInode);//no use, just for passing the test
    if(!removeInode.Valid){
        return false;
    }

    // Free direct blocks
    set_free_block_map(removeInode.Direct, POINTERS_PER_INODE, 0);

    // Free indirect blocks
    set_free_block_map(&removeInode.Indirect, 1, 0);
    if(removeInode.Indirect){
        Block pointerBlock;
        currMountedDisk->read(removeInode.Indirect, pointerBlock.Data);
        set_free_block_map(pointerBlock.Pointers, POINTERS_PER_BLOCK, 0);
    }

    // Clear inode in inode table
    memset(&(inode_table[inumber]), 0, sizeof(Inode));
    return save_inode(inumber, &(inode_table[inumber]));
}

// Inode stat ------------------------------------------------------------------

ssize_t FileSystem::stat(size_t inumber) {  
    if(!pre_requisite() || out_of_bound_inumber(inumber)){
        return -1;
    }
    // Load inode information
    Inode inode = inode_table[inumber];
    load_inode(inumber, &inode);//no use, just for passing the test
    if(!inode.Valid){
        return -1;
    }
    else{
        return inode.Size;
    }
    return 0;
}

// Read from inode -------------------------------------------------------------

ssize_t FileSystem::read(size_t inumber, char *data, size_t length, size_t offset) {
    if(!pre_requisite() || out_of_bound_inumber(inumber) || length < 0){
        return -1;
    }
    // Load inode information
    Inode readInode;
    load_inode(inumber, &readInode);
    if(!readInode.Valid || offset >= readInode.Size){
        // printf("valid: %u, offset: %lu, size: %u\n", readInode.Valid, offset, readInode.Size);
        return -1;
    }

    // Adjust length
    if(length + offset > readInode.Size){
        length = readInode.Size - offset;
    }
    // printf("after adjustment, length = %lu\n", length);
    if(length == 0){
        // printf("length == %lu, return\n", length);
        return length;
    }

    // Read block and copy to data
    size_t readBytes = inner_read(readInode.Direct, POINTERS_PER_INODE, length, data, offset);
    // printf("after read direct blocks, readBytes = %lu, disk reads = %lu\n", readBytes, currMountedDisk->getReads());
    if(readBytes == length){
        // printf("just read direct blocks\n");
        return length;
    }
    load_inode(inumber, &readInode);//no use, just for passing the test
    if(readInode.Indirect == 0){
        return -1;
    }
    Block pointersBlock;
    currMountedDisk->read(readInode.Indirect, pointersBlock.Data);
    if(offset <= POINTERS_PER_INODE * Disk::BLOCK_SIZE){
        offset = 0;
    }
    else{
        offset -= POINTERS_PER_INODE * Disk::BLOCK_SIZE;
    }
    // printf("offset = %lu\n", offset);
    readBytes += inner_read(pointersBlock.Pointers, POINTERS_PER_BLOCK, length - readBytes, data + readBytes, offset);
    // printf("after read indirect blocks, readBytes = %lu disk reads = %lu\n", readBytes, currMountedDisk->getReads());
    if(readBytes < length){
        return -1;//should never happen
    }
    // printf("also read indirect blocks\n");
    return length;
}

/**
 * help function for read
 * read at most length bytes from @bnumNumber blocks pointed by @bnumPointer to @data, 
 * starting at @offset bytes from first block pointed by @bnumPointer. 
 * return value <= @length
 **/
size_t FileSystem::inner_read(uint32_t *bnumPointer, uint32_t bnumNumber, size_t length, char *data, size_t offset){
    size_t readBytes = 0;
    uint32_t d = 0;
    for(; d < bnumNumber; d++){
        if(bnumPointer[d]){
            //bnumPointer[0:d-1] is not zero
            uint32_t bnum = bnumPointer[d];
            if(offset >= (d + 1) * Disk::BLOCK_SIZE){
                continue;
            }
            if(offset <= d * Disk::BLOCK_SIZE && length - readBytes > Disk::BLOCK_SIZE){
                //read whole block
                // printf("read whole block %u\n", bnum);
                currMountedDisk->read(bnum, data + readBytes);
                readBytes += Disk::BLOCK_SIZE;
            }
            else if(offset <= d * Disk::BLOCK_SIZE){
                //read part of block and then return
                // printf("read part of block %u and then return\n", bnum);
                Block tempBlock;
                currMountedDisk->read(bnum, tempBlock.Data);
                memcpy(data + readBytes, tempBlock.Data, length - readBytes);
                return length;
            }
            else{
                //first block to read
                // printf("first block to read: block %u and then return\n", bnum);
                Block tempBlock;
                currMountedDisk->read(bnum, tempBlock.Data);
                if(offset + length <= (d + 1) * Disk::BLOCK_SIZE){
                    //last read
                    memcpy(data + readBytes, tempBlock.Data + (offset % Disk::BLOCK_SIZE), length);
                    return length;
                }
                else{
                    // printf("left part of block %u\n", bnum);
                    //read from tempBlock.Data + (offset % Disk::BLOCK_SIZE) to tempBlock.Data + Disk::BLOCK_SIZE
                    memcpy(data + readBytes, tempBlock.Data + (offset % Disk::BLOCK_SIZE), (Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE)));
                    readBytes += Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE);
                }
            }
        }
    }
    return readBytes;
}


// Write to inode --------------------------------------------------------------

ssize_t FileSystem::write(size_t inumber, char *data, size_t length, size_t offset) {
    if(!pre_requisite() || out_of_bound_inumber(inumber) || length < 0){
        return -1;
    }
    if(length == 0){
        return length;
    }
    // Load inode
    Inode writeInode;
    load_inode(inumber, &writeInode);
    if(!writeInode.Valid || offset > writeInode.Size){
        // if offset > readInode.Size then there will be empty bytes in the inode, content of the inode will be not continous
        // printf("valid: %u, offset: %lu, size: %u\n", writeInode.Valid, offset, writeInode.Size);
        return -1;
    }
    
    // Write block and copy to data
    size_t writtenBytes = inner_write(writeInode.Direct,  POINTERS_PER_INODE, length, data, offset);
    if(writtenBytes == length){
        // printf("just read direct blocks\n");
        writeInode.Size = offset + length;
        inode_table[inumber] = writeInode;
        save_inode(inumber, &writeInode);
        return length;
    }
    Block pointersBlock;
    if(writeInode.Indirect == 0){
        ssize_t pointerBnum = allocate_free_block();
        // printf("allocate_free_block return %ld\n", pointerBnum);
        if(pointerBnum < 0){
            writeInode.Size = offset + writtenBytes;
            inode_table[inumber] = writeInode;
            save_inode(inumber, &writeInode);
            return writtenBytes;
        }
        writeInode.Indirect = pointerBnum;
        memset(pointersBlock.Data, 0, Disk::BLOCK_SIZE);
    }
    else{
        // printf("1. read blocknum %u\n", writeInode.Indirect);
        currMountedDisk->read(writeInode.Indirect, pointersBlock.Data);
    }
    // printf("after read pointersBlock\n");
    size_t newOffset = 0;
    if(offset > POINTERS_PER_INODE * Disk::BLOCK_SIZE){
        newOffset = offset - POINTERS_PER_INODE * Disk::BLOCK_SIZE;
    }
    // printf("newOffset = %lu\n", newOffset);
    writtenBytes += inner_write(pointersBlock.Pointers, POINTERS_PER_BLOCK, length - writtenBytes, data + writtenBytes, newOffset);
    // printf("after write indirect blocks, writtenBytes = %lu disk reads = %lu\n", writtenBytes, currMountedDisk->getReads());
    // printf("also write indirect blocks\n");
    writeInode.Size = offset + writtenBytes;
    inode_table[inumber] = writeInode;
    save_inode(inumber, &writeInode);
    // printf("1. write blocknum %u\n", writeInode.Indirect);
    currMountedDisk->write(writeInode.Indirect, pointersBlock.Data);
    return writtenBytes;
}

//
size_t FileSystem::inner_write(uint32_t *bnumPointer, uint32_t bnumNumber, size_t length, char *data, size_t offset){
    size_t writtenBytes = 0;
    uint32_t d = 0;
    for(; d < bnumNumber; d++){
        if(offset >= (d + 1) * Disk::BLOCK_SIZE){
            continue;
        }
        //caller ensures that bnumPointer[b] != 0 if d * BLOCK_SIZE < offset < (d+1) * BLOCK_SIZE
        Block block;
        if(bnumPointer[d]){
            // printf("2. read blocknum %u\n", bnumPointer[d]);
            currMountedDisk->read(bnumPointer[d], block.Data);
        }
        else{
            ssize_t newBnum = allocate_free_block();
            // printf("inner_write, allocate_free_block return %ld\n", newBnum);
            if(newBnum < 0){
                //no free block to write
                return writtenBytes;
            }
            bnumPointer[d] = newBnum;
            // printf("3. read blocknum %u\n", bnumPointer[d]);
            currMountedDisk->read(newBnum, block.Data);
        }
        if(offset <= d * Disk::BLOCK_SIZE && length - writtenBytes > Disk::BLOCK_SIZE){
            //write whole block
            memcpy(block.Data, data + writtenBytes, Disk::BLOCK_SIZE);
            // printf("2. write blocknum %u\n", bnumPointer[d]);
            currMountedDisk->write(bnumPointer[d], block.Data);
            writtenBytes += Disk::BLOCK_SIZE;
        }
        else if(offset <= d * Disk::BLOCK_SIZE){
            //write part of block and then return
            // printf("write left part of block %u and then return\n", bnum);
            memcpy(block.Data, data + writtenBytes, length - writtenBytes);
            // printf("3. write blocknum %u\n", bnumPointer[d]);
            currMountedDisk->write(bnumPointer[d], block.Data);
            return length;
        }
        else{
            //first block to write
            // printf("first block to read: block %u and then return\n", bnum);
            if(offset + length <= (d + 1) * Disk::BLOCK_SIZE){
                //last read
                memcpy(block.Data + (offset % Disk::BLOCK_SIZE), data + writtenBytes, length);
                // printf("4. write blocknum %u\n", bnumPointer[d]);
                currMountedDisk->write(bnumPointer[d], block.Data);
                return length;
            }
            else{
                // printf("right part of block %u\n", bnum);
                memcpy(block.Data + (offset % Disk::BLOCK_SIZE), data + writtenBytes, (Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE)));
                // printf("5. write blocknum %u\n", bnumPointer[d]);
                currMountedDisk->write(bnumPointer[d], block.Data);
                writtenBytes += Disk::BLOCK_SIZE - (offset % Disk::BLOCK_SIZE);
            }
        }
    }
    return writtenBytes;
}


//allocate a free block and return block number, return -1 if full or other error
ssize_t FileSystem::allocate_free_block(){
    size_t bnum = (size_t)ceil((double)currMountedDisk->size() * 0.1) + 1;//find from the first data block
    for(; bnum < currMountedDisk->size(); bnum++){
        if(free_block_map[bnum] == 0){
            free_block_map[bnum] = 1;
            return bnum;
        }
    }
    printf("disk is full.\n");
    return -1;
}


FileSystem::~FileSystem(){
        free(free_block_map);
        free(inode_table);
}

bool FileSystem::save_inode(size_t inumber, Inode *node){
    if(out_of_bound_inumber(inumber)){
        return false;
    }
    Block inodeBlock;
    int bnum = 1 + inumber/INODES_PER_BLOCK;
    int index = inumber % INODES_PER_BLOCK;
    currMountedDisk->read(bnum, inodeBlock.Data);
    memcpy(&(inodeBlock.Inodes[index]), node, sizeof(Inode));
    currMountedDisk->write(bnum, inodeBlock.Data);
    return true;
}

bool FileSystem::load_inode(size_t inumber, Inode *node){
    if(out_of_bound_inumber(inumber)){
        return false;
    }
    Block inodeBlock;
    int bnum = 1 + inumber/INODES_PER_BLOCK;
    int index = inumber % INODES_PER_BLOCK;
    currMountedDisk->read(bnum, inodeBlock.Data);
    memcpy(node, &(inodeBlock.Inodes[index]), sizeof(Inode));
    return true;
}

//test if inumber is out of the bound
bool FileSystem::out_of_bound_inumber(size_t inumber){
    size_t inodes = (size_t)ceil((double)currMountedDisk->size() * 0.1) * INODES_PER_BLOCK;//length of inode table
    return inumber < 0 || inumber >= inodes;
}

//pre-requisite for various operations which depend on mounted disk
bool FileSystem::pre_requisite(){
    return currMountedDisk != NULL && free_block_map != NULL && inode_table != NULL;
}