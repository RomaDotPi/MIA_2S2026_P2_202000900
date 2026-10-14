#pragma once
#include "structures.hpp"
#include <string>
#include <vector>
#include <map>

struct Command {
    std::string name;
    std::map<std::string,std::string> params;
    std::vector<std::string> flags;
    std::string raw;
};

struct MountedPartition {
    std::string path;
    std::string name;
    std::string id;
    int start;
    int size;
};

struct Session {
    bool active=false;
    std::string user;
    std::string group;
    int uid=-1;
    int gid=-1;
    std::string mountId;
};

class ExtreamFS {
public:
    explicit ExtreamFS(std::string carnet="202000900");
    std::string executeScript(const std::string& script);
    std::string executeLine(const std::string& line);
    std::string stateJson();
    std::string explorerJson(const std::string& path);
    std::string journalJson(const std::string& id);
private:
    std::string carnet;
    std::vector<MountedPartition> mounts;
    std::vector<std::string> knownDisks;
    Session session;

    Command parse(const std::string& line);
    std::string cmdMkdisk(const Command& c);
    std::string cmdRmdisk(const Command& c);
    std::string cmdFdisk(const Command& c);
    std::string cmdMount(const Command& c);
    std::string cmdUnmount(const Command& c);
    std::string cmdMounted(const Command& c);
    std::string cmdMkfs(const Command& c);
    std::string cmdLogin(const Command& c);
    std::string cmdLogout(const Command& c);
    std::string cmdMkgrp(const Command& c);
    std::string cmdRmgrp(const Command& c);
    std::string cmdMkusr(const Command& c);
    std::string cmdRmusr(const Command& c);
    std::string cmdChgrp(const Command& c);
    std::string cmdCat(const Command& c);
    std::string cmdMkdir(const Command& c);
    std::string cmdMkfile(const Command& c);
    std::string cmdRemove(const Command& c);
    std::string cmdRename(const Command& c);
    std::string cmdCopy(const Command& c);
    std::string cmdMove(const Command& c);
    std::string cmdFind(const Command& c);
    std::string cmdChown(const Command& c);
    std::string cmdChmod(const Command& c);
    std::string cmdJournaling(const Command& c);
    std::string cmdLoss(const Command& c);
    std::string cmdRep(const Command& c);

    MountedPartition* findMount(const std::string& id);
    bool readMBR(const std::string& path, MBR& mbr);
    bool writeMBR(const std::string& path, const MBR& mbr);
    Partition* findPrimary(MBR& mbr, const std::string& name);
    std::string usersRead(MountedPartition& mp);
    bool usersWrite(MountedPartition& mp, const std::string& content);
    bool loadSB(MountedPartition& mp, SuperBlock& sb);
    int allocInode(std::fstream& f, SuperBlock& sb);
    int allocBlock(std::fstream& f, SuperBlock& sb);
    void saveSB(std::fstream& f, MountedPartition& mp, const SuperBlock& sb);
    bool readInode(std::fstream& f, const SuperBlock& sb, int idx, Inode& ino);
    bool writeInode(std::fstream& f, const SuperBlock& sb, int idx, const Inode& ino);
    bool readPointerBlock(std::fstream& f, const SuperBlock& sb, int idx, PointerBlock& block);
    bool writePointerBlock(std::fstream& f, const SuperBlock& sb, int idx, const PointerBlock& block);
    std::vector<int> fileDataBlocks(std::fstream& f, const SuperBlock& sb, const Inode& ino);
    std::vector<int> filePointerBlocks(std::fstream& f, const SuperBlock& sb, const Inode& ino);
    bool writeFileContent(std::fstream& f, SuperBlock& sb, Inode& ino,
                          const std::string& content, std::string& err);
    void releaseFileBlocks(std::fstream& f, SuperBlock& sb, Inode& ino);
    int resolvePath(MountedPartition& mp, const std::string& path, int* parentOut=nullptr, std::string* leafOut=nullptr);
    bool createDirectory(MountedPartition& mp, const std::string& path, bool recursive, std::string& err);
    bool createFile(MountedPartition& mp, const std::string& path, const std::string& content, bool recursive, std::string& err);
    bool canRead(const Inode& inode) const;
    bool canWrite(const Inode& inode) const;
    bool appendJournal(MountedPartition& mp, const std::string& operation,
                       const std::string& path, const std::string& content);
    std::string readFile(MountedPartition& mp, const std::string& path, bool* ok=nullptr);
    bool appendChild(std::fstream& f, SuperBlock& sb, int parentInode, const std::string& name, int childInode);
    std::string graphvizRender(const std::string& dot, const std::string& outPath);
};
