#pragma once
#include <ctime>

#pragma pack(push,1)
struct Partition {
    char part_status;
    char part_type;
    char part_fit;
    int part_start;
    int part_s;
    char part_name[16];
    int part_correlative;
    char part_id[4];
};

struct MBR {
    int mbr_tamano;
    std::time_t mbr_fecha_creacion;
    int mbr_dsk_signature;
    char dsk_fit;
    Partition mbr_partitions[4];
};

struct EBR {
    char part_mount;
    char part_fit;
    int part_start;
    int part_s;
    int part_next;
    char part_name[16];
};

struct SuperBlock {
    int s_filesystem_type;
    int s_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    int s_free_inodes_count;
    std::time_t s_mtime;
    std::time_t s_umtime;
    int s_mnt_count;
    int s_magic;
    int s_inode_s;
    int s_block_s;
    int s_first_ino;
    int s_first_blo;
    int s_bm_inode_start;
    int s_bm_block_start;
    int s_inode_start;
    int s_block_start;
};

constexpr int JOURNAL_CAPACITY = 50;

struct JournalInformation {
    char i_operation[10];
    char i_path[32];
    char i_content[64];
    float i_date;
};

struct Journaling {
    int j_count;
    JournalInformation j_content[JOURNAL_CAPACITY];
};

struct Inode {
    int i_uid;
    int i_gid;
    int i_s;
    std::time_t i_atime;
    std::time_t i_ctime;
    std::time_t i_mtime;
    int i_block[15];
    char i_type;
    char i_perm[3];
};

struct Content { char b_name[12]; int b_inodo; };
struct FolderBlock { Content b_content[4]; };
struct FileBlock { char b_content[64]; };
struct PointerBlock { int b_pointers[16]; };
#pragma pack(pop)

static_assert(sizeof(FolderBlock)==64, "FolderBlock must be 64 bytes");
static_assert(sizeof(FileBlock)==64, "FileBlock must be 64 bytes");
static_assert(sizeof(PointerBlock)==64, "PointerBlock must be 64 bytes");
static_assert(sizeof(JournalInformation)==110, "JournalInformation binary layout changed");
