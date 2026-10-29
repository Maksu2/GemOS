# GemFS v3

On-disk format of the GemOS data disk. `tools/mkgemfs` creates and inspects
it, `kernel/fs/gemfs.c` mounts it. All numbers are little endian.

## Layout

The file system starts at LBA 0 of a whole ATA disk and is made of 4096-byte
blocks (8 sectors): block `n` starts at LBA `8n`.

| Blocks | Content |
|---|---|
| 0 | superblock (the first 512 bytes; the rest of the block is zero) |
| `bitmap_start` … | allocation bitmap, `bitmap_blocks` blocks: bit `n` (byte `n / 8`, bit `n % 8`) set = block `n` in use |
| `inode_start` … | inode table, `inode_blocks` blocks of 32 inodes |
| `data_start` … `total_blocks - 1` | file and directory data |

## Superblock

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | magic `GEMOS-FS` |
| 8 | 4 | version = 3 |
| 12 | 4 | block size = 4096 |
| 16 | 4 | `total_blocks`: size of the file system; it must fit on the disk |
| 20 | 4 | `bitmap_start` = 1 |
| 24 | 4 | `bitmap_blocks` = ⌈`total_blocks` / 32768⌉ |
| 28 | 4 | `inode_start` = `bitmap_start` + `bitmap_blocks` |
| 32 | 4 | `inode_blocks` = `inode_count` / 32 |
| 36 | 4 | `inode_count`: a multiple of 32, 64 … 8192; inode 0 is never used |
| 40 | 4 | `data_start` = `inode_start` + `inode_blocks` |
| 44 | 4 | root inode = 1 |
| 48 | 16 | label, NUL padded |
| 64 | 444 | reserved, zero |
| 508 | 4 | CRC-32 (IEEE 802.3, as `zlib.crc32`) of bytes 0–507 |

The kernel mounts a disk only if the magic, version, block size, CRC and all
layout fields are right, `total_blocks` fits on the disk (at most 2^20
blocks, 4 GiB), the bitmap marks blocks 0 … `data_start - 1` as used and
inode 1 is a directory. A disk that fails any check is never written: the
system then runs without a file system. Only `tools/mkgemfs format` writes a
superblock; the kernel never formats a disk.

## Inode (128 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | type: 0 free, 1 file, 2 directory |
| 2 | 2 | flags: bit 0 = system file (a program seeded by the kernel; read-only for processes) |
| 4 | 4 | size in bytes; a directory has 4096 × its number of blocks |
| 8 | 4 | parent directory inode (the root's parent is the root) |
| 12 | 4 | version: CRC-32 of the image for seeded programs, 0 otherwise |
| 16 | 48 | `direct[12]`: block numbers of the first 12 blocks, 0 = none |
| 64 | 4 | `indirect`: a block with 1024 block numbers for blocks 12 … 1035, 0 = none |
| 68 | 60 | reserved, zero |

A file of `size` bytes has ⌈`size` / 4096⌉ blocks, in order and all listed;
the unused end of its last block is zero. The largest file has 1036 blocks,
4 243 456 bytes. Directories use the direct blocks only: at most 12 blocks of
64 entries.

## Directory entry (64 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | inode, 0 = free slot |
| 4 | 1 | type, the same as the inode's |
| 5 | 3 | reserved, zero |
| 8 | 56 | name, NUL padded: 1–55 bytes, no `/`, not `.` or `..` |

There are no `.` and `..` entries; an inode knows its parent. Every used
inode except the root has exactly one directory entry.

## Paths

Names separated by `/`, relative to the root; a leading `/` is optional.
At most 127 bytes. Names are case sensitive.

## Update order

Writes are ordered so that a crash leaves at worst blocks or inodes that are
marked used but not referenced (`tools/mkgemfs check` lists them), never a
reference to a free block or inode:

- Writing a file puts the new contents into newly allocated blocks (bitmap
  first, then the data), then writes the inode (the commit), then frees the
  old blocks. A crash keeps either the old or the new contents.
- A new file or directory gets its inode written before the directory entry
  that points to it.
- Deleting removes the directory entry first, then frees the blocks and the
  inode. A directory is deleted with everything in it.
