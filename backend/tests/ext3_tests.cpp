#include "extreamfs.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition,const std::string& message){
    if(!condition)throw std::runtime_error(message);
}

static void requireContains(const std::string& value,const std::string& expected){
    require(value.find(expected)!=std::string::npos,"No se encontró '"+expected+"' en: "+value);
}

static std::pair<Partition,SuperBlock> readLayout(const fs::path& disk){
    std::ifstream file(disk,std::ios::binary);require((bool)file,"No se pudo abrir el disco de prueba");
    MBR mbr{};file.read((char*)&mbr,sizeof(mbr));require((bool)file,"No se pudo leer el MBR");
    Partition partition{};bool found=false;
    for(const auto& candidate:mbr.mbr_partitions)if(candidate.part_s>0){partition=candidate;found=true;break;}
    require(found,"No se encontró la partición de prueba");
    SuperBlock sb{};file.seekg(partition.part_start);file.read((char*)&sb,sizeof(sb));require((bool)file,"No se pudo leer el superbloque");
    return {partition,sb};
}

static Partition findPartition(const fs::path& disk,const std::string& name){
    std::ifstream file(disk,std::ios::binary);require((bool)file,"No se pudo abrir el disco");MBR mbr{};file.read((char*)&mbr,sizeof(mbr));
    for(const auto& partition:mbr.mbr_partitions){std::string current(partition.part_name,partition.part_name+sizeof(partition.part_name));current=current.c_str();if(partition.part_s>0&&current==name)return partition;}
    throw std::runtime_error("No se encontró la partición "+name);
}

static bool regionIsZero(const fs::path& disk,long long start,long long size){
    std::ifstream file(disk,std::ios::binary);if(!file)return false;file.seekg(start);
    char buffer[4096];
    while(size>0){const auto amount=(std::streamsize)std::min<long long>(size,sizeof(buffer));file.read(buffer,amount);if(!file)return false;for(std::streamsize i=0;i<amount;i++)if(buffer[i]!=0)return false;size-=amount;}
    return true;
}

static void createMountedPartition(ExtreamFS& app,const fs::path& disk){
    requireContains(app.executeLine("mkdisk -size=4 -unit=M -path=\""+disk.string()+"\""),"OK:");
    requireContains(app.executeLine("fdisk -size=3 -unit=M -type=P -path=\""+disk.string()+"\" -name=Part1"),"OK:");
    requireContains(app.executeLine("mount -path=\""+disk.string()+"\" -name=Part1"),"id=001A");
}

static void testExt3(const fs::path& disk){
    ExtreamFS app;createMountedPartition(app,disk);
    requireContains(app.executeLine("mkfs -id=001A -type=full -fs=3fs"),"EXT3");
    auto [partition,sb]=readLayout(disk);
    require(sb.s_filesystem_type==3,"El superbloque no identifica EXT3");
    require(sb.s_bm_inode_start==partition.part_start+(int)sizeof(SuperBlock)+(int)sizeof(Journaling),"El journal no está entre el superbloque y los bitmaps");
    const auto loginOutput=app.executeScript("login -user=root -pass=123 -id=001A");requireContains(loginOutput,"OK:");require(loginOutput.find("-pass=123")==std::string::npos,"La salida expuso la contraseña de login");
    requireContains(app.executeLine("mkdir -path=/docs"),"OK:");
    requireContains(app.executeLine("mkfile -path=/docs/a.txt -size=10"),"OK:");
    require(app.executeLine("cat -file1=/docs/a.txt")=="0123456789","CAT no recuperó el contenido esperado");
    requireContains(app.executeLine("logout"),"OK:");
    const auto journal=app.executeLine("journaling -id=001A");
    requireContains(journal,"Journal EXT3 (5/50)");
    requireContains(journal,"login");requireContains(journal,"mkdir");requireContains(journal,"mkfile");requireContains(journal,"cat");requireContains(journal,"logout");
    require(journal.find("-pass=123")==std::string::npos,"El journal expuso la contraseña de login");
    requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");
    auto [partitionBeforeLoss,sbBeforeLoss]=readLayout(disk);(void)partitionBeforeLoss;
    const auto loss=app.executeLine("loss -id=001A");
    requireContains(loss,"OK: pérdida simulada");requireContains(loss,"Bitmap de inodos antes:");requireContains(loss,"Bitmap de inodos después:");requireContains(loss,"Bitmap de bloques antes:");requireContains(loss,"Bitmap de bloques después:");
    require(regionIsZero(disk,sbBeforeLoss.s_bm_inode_start,sbBeforeLoss.s_inodes_count),"LOSS no limpió el bitmap de inodos");
    require(regionIsZero(disk,sbBeforeLoss.s_bm_block_start,sbBeforeLoss.s_blocks_count),"LOSS no limpió el bitmap de bloques");
    require(regionIsZero(disk,sbBeforeLoss.s_inode_start,(long long)sbBeforeLoss.s_inodes_count*sizeof(Inode)),"LOSS no limpió el área de inodos");
    require(regionIsZero(disk,sbBeforeLoss.s_block_start,(long long)sbBeforeLoss.s_blocks_count*64),"LOSS no limpió el área de bloques");
    auto [partitionAfterLoss,sbAfterLoss]=readLayout(disk);(void)partitionAfterLoss;
    require(std::memcmp(&sbBeforeLoss,&sbAfterLoss,sizeof(SuperBlock))==0,"LOSS modificó el superbloque");
    const auto journalAfterLoss=app.executeLine("journaling -id=001A");
    requireContains(journalAfterLoss,"Journal EXT3 (7/50)");requireContains(journalAfterLoss,"loss");requireContains(journalAfterLoss,"mkdir");
    requireContains(app.executeLine("logout"),"OK: sesión cerrada");
}

static void testExt2Compatibility(const fs::path& disk){
    ExtreamFS app;createMountedPartition(app,disk);
    requireContains(app.executeLine("mkfs -id=001A"),"EXT2");
    auto [partition,sb]=readLayout(disk);
    require(sb.s_filesystem_type==2,"El formato por defecto dejó de ser EXT2");
    require(sb.s_bm_inode_start==partition.part_start+(int)sizeof(SuperBlock),"Cambió el layout binario de EXT2");
    requireContains(app.executeLine("journaling -id=001A"),"solo está disponible en EXT3");
    requireContains(app.executeLine("loss -id=001A"),"solo está disponible en EXT3");
    requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");
    requireContains(app.executeLine("mkdir -path=/compatible"),"OK:");
}

static void testPartitionManagement(const fs::path& disk){
    ExtreamFS app;
    requireContains(app.executeLine("mkdisk -size=10 -unit=M -path=\""+disk.string()+"\""),"OK:");
    requireContains(app.executeLine("fdisk -size=2 -unit=M -path=\""+disk.string()+"\" -name=Part1"),"OK:");
    requireContains(app.executeLine("fdisk -size=2 -unit=M -path=\""+disk.string()+"\" -name=Part2"),"OK:");
    requireContains(app.executeLine("fdisk -add=1 -unit=M -path=\""+disk.string()+"\" -name=Part1"),"no hay espacio contiguo");
    requireContains(app.executeLine("fdisk -add=-512 -unit=K -path=\""+disk.string()+"\" -name=Part1"),"OK:");
    require(findPartition(disk,"Part1").part_s==1536*1024,"FDISK no redujo Part1");
    requireContains(app.executeLine("fdisk -add=256 -unit=K -path=\""+disk.string()+"\" -name=Part1"),"OK:");
    require(findPartition(disk,"Part1").part_s==1792*1024,"FDISK no amplió Part1");
    requireContains(app.executeLine("mount -path=\""+disk.string()+"\" -name=Part1"),"id=001A");
    requireContains(app.executeLine("fdisk -add=-1 -unit=K -path=\""+disk.string()+"\" -name=Part1"),"desmonte");
    requireContains(app.executeLine("unmount -id=001A"),"OK:");
    const auto unmounted=findPartition(disk,"Part1");require(unmounted.part_status=='0'&&unmounted.part_correlative==0&&unmounted.part_id[0]==0,"UNMOUNT no restauró los campos del MBR");
    requireContains(app.executeLine("unmount -id=001A"),"id no montado");
    requireContains(app.executeLine("fdisk -delete=fast -path=\""+disk.string()+"\" -name=Part2"),"CONFIRMACION_REQUERIDA");
    const auto part2=findPartition(disk,"Part2");
    {std::fstream file(disk,std::ios::binary|std::ios::in|std::ios::out);std::array<char,64> marker{};marker.fill('X');file.seekp(part2.part_start);file.write(marker.data(),marker.size());}
    requireContains(app.executeLine("fdisk -delete=full -confirm -path=\""+disk.string()+"\" -name=Part2"),"OK:");
    require(regionIsZero(disk,part2.part_start,part2.part_s),"FDISK full no limpió la partición");
    try{(void)findPartition(disk,"Part2");require(false,"FDISK no liberó la entrada del MBR");}catch(const std::runtime_error& error){requireContains(error.what(),"No se encontró");}
    requireContains(app.executeLine("fdisk -size=4 -unit=M -type=E -path=\""+disk.string()+"\" -name=Extendida"),"OK:");
    requireContains(app.executeLine("fdisk -size=512 -unit=K -type=L -path=\""+disk.string()+"\" -name=Log1"),"OK:");
    requireContains(app.executeLine("fdisk -size=512 -unit=K -type=L -path=\""+disk.string()+"\" -name=Log2"),"OK:");
    requireContains(app.executeLine("fdisk -add=128 -unit=K -path=\""+disk.string()+"\" -name=Log1"),"no hay espacio contiguo");
    requireContains(app.executeLine("fdisk -delete=fast -confirm -path=\""+disk.string()+"\" -name=Log1"),"lógica eliminada");
    requireContains(app.executeLine("fdisk -add=-128 -unit=K -path=\""+disk.string()+"\" -name=Log2"),"lógica redimensionada");
    requireContains(app.executeLine("mount -path=\""+disk.string()+"\" -name=Log2"),"id=001A");requireContains(app.executeLine("mounted"),"Log2");
    const auto logicalState=app.stateJson();requireContains(logicalState,"\"name\":\"Log2\"");requireContains(logicalState,"\"type\":\"L\"");requireContains(logicalState,"\"mounted\":true");
    requireContains(app.executeLine("unmount -id=001A"),"OK:");
}

static void testRenameRemoveAndPermissions(const fs::path& disk){
    ExtreamFS app;createMountedPartition(app,disk);requireContains(app.executeLine("mkfs -id=001A -fs=3fs"),"EXT3");requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");
    requireContains(app.executeLine("mkgrp -name=staff"),"OK:");requireContains(app.executeLine("mkusr -user=bob -pass=abc -grp=staff"),"OK:");
    requireContains(app.executeLine("mkdir -path=/tree/sub -p"),"OK:");requireContains(app.executeLine("mkfile -path=/tree/sub/a.txt -size=10"),"OK:");
    requireContains(app.executeLine("logout"),"OK:");requireContains(app.executeLine("login -user=bob -pass=abc -id=001A"),"OK:");
    require(app.executeLine("cat -file1=/tree/sub/a.txt")=="0123456789","CAT con permiso de lectura falló");
    requireContains(app.executeLine("rename -path=/tree/sub/a.txt -name=b.txt"),"sin permiso de escritura");
    requireContains(app.executeLine("remove -path=/tree"),"sin permiso de escritura");
    requireContains(app.executeLine("logout"),"OK:");requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");
    requireContains(app.executeLine("chmod -path=/tree/sub/a.txt -ugo=600"),"OK:");requireContains(app.executeLine("logout"),"OK:");requireContains(app.executeLine("login -user=bob -pass=abc -id=001A"),"OK:");requireContains(app.executeLine("cat -file1=/tree/sub/a.txt"),"sin permiso de lectura");requireContains(app.executeLine("logout"),"OK:");requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");requireContains(app.executeLine("chmod -path=/tree/sub/a.txt -ugo=664"),"OK:");
    requireContains(app.executeLine("mkdir -path=/nombre-demasiado-largo"),"nombre máximo 12");
    requireContains(app.executeLine("rename -path=/tree/sub/a.txt -name=b.txt"),"OK:");require(app.executeLine("cat -file1=/tree/sub/b.txt")=="0123456789","RENAME perdió el archivo");
    requireContains(app.executeLine("cat -file1=/tree/sub/a.txt"),"archivo no existe");requireContains(app.executeLine("remove -path=/tree"),"OK:");requireContains(app.executeLine("cat -file1=/tree/sub/b.txt"),"archivo no existe");
    const auto journal=app.executeLine("journaling -id=001A");requireContains(journal,"rename");requireContains(journal,"remove");requireContains(journal,"chmod");require(journal.find("-pass=abc")==std::string::npos,"El journal expuso una contraseña de usuario");
}

static void testCopyMoveFindAndChown(const fs::path& disk){
    ExtreamFS app;createMountedPartition(app,disk);requireContains(app.executeLine("mkfs -id=001A -fs=3fs"),"EXT3");requireContains(app.executeLine("login -user=root -pass=123 -id=001A"),"OK:");
    requireContains(app.executeLine("mkgrp -name=staff"),"OK:");requireContains(app.executeLine("mkusr -user=alice -pass=abc -grp=staff"),"OK:");
    requireContains(app.executeLine("mkdir -path=/src/sub -p"),"OK:");requireContains(app.executeLine("mkdir -path=/dst"),"OK:");requireContains(app.executeLine("mkfile -path=/src/sub/a.txt -size=20"),"OK:");
    requireContains(app.executeLine("copy -path=/src -destino=/dst"),"copia completada");require(app.executeLine("cat -file1=/dst/src/sub/a.txt")=="01234567890123456789","COPY no conservó el contenido");
    requireContains(app.executeLine("copy -path=/src -destino=/dst"),"ya existe");
    requireContains(app.executeLine("move -path=/dst/src/sub/a.txt -destino=/"),"OK:");require(app.executeLine("cat -file1=/a.txt")=="01234567890123456789","MOVE perdió el contenido");requireContains(app.executeLine("cat -file1=/dst/src/sub/a.txt"),"archivo no existe");
    const auto found=app.executeLine("find -path=/ -name=*.txt");requireContains(found,"/a.txt");requireContains(found,"/src/sub/a.txt");
    requireContains(app.executeLine("chown -path=/dst/src -usuario=alice -r"),"2 elemento(s)");requireContains(app.executeLine("chown -path=/a.txt -usuario=alice"),"1 elemento(s)");
    requireContains(app.executeLine("logout"),"OK:");requireContains(app.executeLine("login -user=alice -pass=abc -id=001A"),"OK:");require(app.executeLine("cat -file1=/a.txt")=="01234567890123456789","CHOWN dejó el archivo inaccesible al nuevo propietario");
    requireContains(app.executeLine("chown -path=/a.txt -usuario=root"),"OK:");requireContains(app.executeLine("rename -path=/a.txt -name=z.txt"),"sin permiso de escritura");
    const auto journal=app.executeLine("journaling -id=001A");requireContains(journal,"copy");requireContains(journal,"move");requireContains(journal,"find");requireContains(journal,"chown");
    const auto stateJson=app.stateJson();requireContains(stateJson,"\"active\":true");requireContains(stateJson,"\"user\":\"alice\"");requireContains(stateJson,"\"disks\"");requireContains(stateJson,"\"partitions\"");
    const auto explorerJson=app.explorerJson("/");requireContains(explorerJson,"\"kind\":\"directory\"");requireContains(explorerJson,"\"name\":\"a.txt\"");
    const auto fileJson=app.explorerJson("/a.txt");requireContains(fileJson,"\"kind\":\"file\"");requireContains(fileJson,"01234567890123456789");
    const auto journalJson=app.journalJson("001A");requireContains(journalJson,"\"operation\":\"copy\"");requireContains(journalJson,"\"operation\":\"move\"");
}

int main(){
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto ext3Disk=fs::temp_directory_path()/("extreamfs-ext3-"+std::to_string(stamp)+".mia");
    const auto ext2Disk=fs::temp_directory_path()/("extreamfs-ext2-"+std::to_string(stamp)+".mia");
    const auto partitionDisk=fs::temp_directory_path()/("extreamfs-fdisk-"+std::to_string(stamp)+".mia");
    const auto operationsDisk=fs::temp_directory_path()/("extreamfs-operations-"+std::to_string(stamp)+".mia");
    const auto advancedDisk=fs::temp_directory_path()/("extreamfs-advanced-"+std::to_string(stamp)+".mia");
    try{
        testExt3(ext3Disk);testExt2Compatibility(ext2Disk);testPartitionManagement(partitionDisk);testRenameRemoveAndPermissions(operationsDisk);testCopyMoveFindAndChown(advancedDisk);
        fs::remove(ext3Disk);fs::remove(ext2Disk);fs::remove(partitionDisk);fs::remove(operationsDisk);fs::remove(advancedDisk);
        std::cout<<"Pruebas EXT2/EXT3 completadas correctamente\n";
        return 0;
    }catch(const std::exception& error){
        fs::remove(ext3Disk);fs::remove(ext2Disk);fs::remove(partitionDisk);fs::remove(operationsDisk);fs::remove(advancedDisk);
        std::cerr<<"Fallo: "<<error.what()<<"\n";
        return 1;
    }
}
