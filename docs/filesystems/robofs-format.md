# RoboFS v1 on-disk format

Little-endian. All multi-byte fields are encoded explicitly (not by
casting a C struct onto disk). Block size is 4096 bytes unless a future
compatible minor version says otherwise. The kernel rejects an unknown
**major** version.

Magic: `0x53464252` (`'RBFS'` as a little-endian 32-bit word).

Version word at offset 4: `(minor << 16) | major`. v1 is major 1,
minor 0.

## Layout

```text
Block 0                         Superblock (rest of the block reserved)
Block bitmap_start … +bitmap_blocks-1   Allocation bitmap
Block obj_start … +obj_blocks-1         Object table
Block data_start … total_blocks-1       File and directory data
```

Bitmap bit `i` is block `i`. **1 = allocated, 0 = free.** Blocks
`0 .. data_start-1` are reserved and must remain allocated.

Object 0 is unused. The root object id is in the superblock (id 1 after
format).

## Superblock (block 0)

Offsets in bytes. All fields little-endian unsigned integers.

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | magic |
| 4 | 4 | version `(minor<<16)\|major` |
| 8 | 4 | block_size |
| 12 | 4 | total_blocks |
| 16 | 4 | free_blocks |
| 20 | 4 | object_count |
| 24 | 4 | free_objects |
| 28 | 4 | bitmap_start |
| 32 | 4 | bitmap_blocks |
| 36 | 4 | object_table_start |
| 40 | 4 | object_table_blocks |
| 44 | 4 | data_start |
| 48 | 4 | root_object_id |
| 52 | 4 | flags (0 in v1) |
| 56 | 12 | reserved |
| 68 | 4 | checksum |

Checksum: XOR of every little-endian `u32` in bytes `0..67`. Mount
fails if it does not match bytes 68..71.

Flags are reserved for future encryption and similar features. v1
stores 0 and does not interpret other bits.

## Object table

Each object occupies a **128-byte** slot. Slot `id` starts at byte
`id * 128` from the start of the object-table region.

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | type (0 unused, 1 file, 2 directory) |
| 4 | 4 | mode (reserved for permissions) |
| 8 | 8 | size in bytes |
| 16 | 8 | created |
| 24 | 8 | modified |
| 32 | 8 | accessed |
| 40 | 4 | nlink |
| 44 | 4 | flags |
| 48 | 32 | direct[8] block numbers |
| 80 | 4 | indirect block number |
| 84 | 44 | reserved |

Types 3+ are reserved (symlink, device, pipe, socket). v1 does not
create them.

File data uses up to 8 direct blocks, then one indirect block of
little-endian `u32` pointers (`block_size / 4` pointers). Writes past
EOF allocate and zero new blocks. v1 does not keep sparse holes.

Maximum logical file size is `(8 + block_size/4) * block_size`
(~4 MiB at 4096-byte blocks).

## Directory entries

Directories are files of packed **64-byte** entries. Directory `size`
is a multiple of 64.

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | object id (0 = unused slot) |
| 4 | 4 | type (file or directory) |
| 8 | 55 | name bytes, zero-padded |
| 63 | 1 | pad |

Names must be non-empty, not `.` or `..`, must not contain `/`, and
must be at most 55 bytes. The VFS walker implements `.` and `..`.

## Endianness and alignment

Little-endian. Superblock, objects, and dirents are serialized field
by field. Do not persist host pointers. Alignment of in-memory C
structs is irrelevant to the disk format.
