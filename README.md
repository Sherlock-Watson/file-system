CSE.30341.FA17: Project 06
==========================

This is the documentation for [Project 06] of [CSE.30341.FA17].

## Test result

![](D:\复旦大学%20软件工程\大三（下）\OS\file-system\assets\2022-06-05-22-58-21-image.png)

Design
------

> 1. To implement `Filesystem::debug`, you will need to load the file system
>    data structures and report the **superblock** and **inodes**.
>    
>    - How will you read the superblock?
>    - How will you traverse all the inodes?
>    - How will you determine all the information related to an inode?
>    - How will you determine all the blocks related to an inode?

Response.

- 首先声明一个Block变量，先将其视作一个普通的block，读出其中内容之后，再将其作为Super类型来处理
- 先通过`Super.InodeBlocks`来遍历所有的inode block，在循环内部通过`POINTERS_PER_INODE`来遍历每个inode
- 首先通过`inode.Valid`来判断该inode是否有效，若有效则获取并打印inode的size和direct pointer信息
- 若pointer不等于0则说明inode包含了这一个block

> 2. To implement `FileSystem::format`, you will need to write the superblock
>    and clear the remaining blocks in the file system.
>    
>    - What pre-condition must be true before this operation can succeed?
>    - What information must be written into the superblock?
>    - How would you clear all the remaining blocks?

Response.

- !disk->mounted()
- `MagicNumber`, `InodeBlocks`,`Inodes`,`Blocks`,其中InodeBlock = ceil(Blocks * 0.1), Inodes = InodesBlocks * 128
- 首先将block的所有内容初始化为0，然后将这个block写入disk

> 3. To implement `FileSystem::mount`, you will need to prepare a filesystem
>    for use by reading the superblock and allocating the free block bitmap.
>    
>    - What pre-condition must be true before this operation can succeed?
>    - What sanity checks must you perform?
>    - How will you record that you mounted a disk?
>    - How will you determine which blocks are free?

Response.

- 当前被挂载的不是这个磁盘
- magic number 是合法的， `InodeBlocks`,`Inodes`,`Blocks`的值满足InodeBlock = ceil(Blocks * 0.1), Inodes = InodesBlocks * 128
- 将currMountedDisk由null变成挂载的磁盘
- 用`free block bitmap`即长度为磁盘的block数量的整数数组，0代表free，1代表used/reserved

> 4. To implement `FileSystem::create`, you will need to locate a free inode
>    and save a new inode into the inode table.
>    
>    - How will you locate a free inode?
>    - What information would you see in a new inode?
>    - How will you record this new inode?

Response.

- 首先用一个全局的数组`inode_table`来将所有的inode都记录在内存中，遍历`inode_table`找到第一个valid=0的inode，返回。
- valid = 1，size = 0，direct/indirect pointers = 0
- 将新的inode写入到`inode_table`中，并且调用save_inode()存到磁盘里。

> 5. To implement `FileSystem::remove`, you will need to locate the inode and
>    then free its associated blocks.
>    
>    - How will you determine if the specified inode is valid?
>    - How will you free the direct blocks?
>    - How will you free the indirect blocks?
>    - How will you update the inode table?

Response.

- inode.Valid != 0
- 遍历`Direct`数组，如果pointer != 0就将`free_block_map`对应的元素置为0，不需要写回磁盘
- 先通过`Indirect`找到indirect block，然后像对待Direct数组一样去free其指向的data block
- inode.Valid = 0, Size = 0, Direct/Indirect = 0，然后更新`inode_table`，再写回磁盘

> 6. To implement `FileSystem::stat`, you will need to locate the inode and
>    return its size.
>    
>    - How will you determine if the specified inode is valid?
>    - How will you determine the inode's size?

Response.

- 查看inode.Valid的值，若为1则有效，否则无效
- inode.Size即为大小

> 7. To implement `FileSystem::read`, you will need to locate the inode and
>    copy data from appropriate blocks to the user-specified data buffer.
>    
>    - How will you determine if the specified inode is valid?
>    - How will you determine which block to read from?
>    - How will you handle the offset?
>    - How will you copy from a block to the data buffer?

Response.

- 通过观察inode.Valid的值
- 通过inode的Direct和Indirect
- 首先确保offset比size小，然后在遍历每一个data block的时候，将offset与当前block第一个字节、最后一个字节的index比较，如果比最后一个字节还大，则直接跳过该block，如果在第一个和最后一个之间，则从offset % BLOCKSIZE开始read，如果比第一个字节还小，就从block的第一个字节开始read
- 如果是copy整个block，则直接调用disk的read方法读到data buffer中，如果是copy部分block，则先用disk的read方法存到一个暂时的block中，再用memcpy将要copy的部分copy到data buffer中。

> 8. To implement `FileSystem::write`, you will need to locate the inode and
>    copy data the user-specified data buffer to data blocks in the file
>    system.
>    
>    - How will you determine if the specified inode is valid?
>    - How will you determine which block to write to?
>    - How will you handle the offset?
>    - How will you know if you need a new block?
>    - How will you manage allocating a new block if you need another one?
>    - How will you copy from a block to the data buffer?
>    - How will you update the inode?

Response.

- inode.Valid
- 通过inode的Direct和Indirect，如果不为零的话直接写，否则先通过free_block_map找到一个空闲的block，然后再写
- 首先确保offset不大于size，否则就会出现不连续的数据，然后在遍历每一个data block的时候，将offset与当前block第一个字节、最后一个字节的index比较，如果比最后一个字节还大，则直接跳过该block，如果在第一个和最后一个之间，则从offset % BLOCKSIZE开始write，如果比第一个字节还小，就从block的第一个字节开始write
- inode的pointer为0
- 创建一个函数`alloc_free_block()`用于返回free的block number然后在`free_block_map`中做标记，这样每次需要新的block的时候调用该函数即可
- 如果是copy整个block，则直接调用disk的write方法写道磁盘中，如果是copy部分block，则先用disk的read方法存到一个暂时的block中，再用memcpy将要copy的部分copy到block中，再将整个block写回磁盘。
- inode.Size = offset + writtenBytes，更新`inode_table`，调用save_inode将inode写回磁盘

Errata
------

> Describe any known errors, bugs, or deviations from the requirements.

并没有通过全部的测试，主要是磁盘读写次数出现了出入，这是和标答实现不同而不可避免的结果。

Extra Credit
------------

> Describe what extra credit (if any) that you implemented.

[Project 06]:       https://www3.nd.edu/~pbui/teaching/cse.30341.fa17/project06.html
[CSE.30341.FA17]:   https://www3.nd.edu/~pbui/teaching/cse.30341.fa17/
[Google Drive]:     https://drive.google.com
