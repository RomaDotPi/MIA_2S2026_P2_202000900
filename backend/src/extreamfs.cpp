#include "extreamfs.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static std::string lower(std::string s){ for(char& c:s) c=(char)std::tolower((unsigned char)c); return s; }
static std::string trim(const std::string& s){ auto a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos)return""; auto b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1); }
static bool hasFlag(const Command& c,const std::string& f){ return std::find(c.flags.begin(),c.flags.end(),lower(f))!=c.flags.end(); }
static std::string getp(const Command& c,const std::string& k,const std::string& d=""){ auto it=c.params.find(lower(k)); return it==c.params.end()?d:it->second; }
static void copyFixed(char* dst,size_t n,const std::string& s){ std::memset(dst,0,n); std::memcpy(dst,s.data(),std::min(n,s.size())); }
static std::string fixed(const char* p,size_t n){ size_t l=0; while(l<n&&p[l])++l; return std::string(p,l); }
static std::vector<std::string> split(const std::string& s,char sep){ std::vector<std::string> v; std::stringstream ss(s); std::string x; while(std::getline(ss,x,sep))v.push_back(trim(x)); return v; }
static std::string html(const std::string& s){ std::string o; for(char c:s){ if(c=='&')o+="&amp;"; else if(c=='<')o+="&lt;"; else if(c=='>')o+="&gt;"; else if(c=='\"')o+="&quot;"; else o+=c;} return o; }
static std::string jsonQuoted(const std::string& s){std::string out="\"";for(unsigned char ch:s){switch(ch){case'\\':out+="\\\\";break;case'\"':out+="\\\"";break;case'\n':out+="\\n";break;case'\r':out+="\\r";break;case'\t':out+="\\t";break;default:if(ch<32){std::ostringstream hex;hex<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<(int)ch;out+=hex.str();}else out+=(char)ch;}}return out+="\"";}
static std::string redactSecrets(const std::string& value){return std::regex_replace(value,std::regex("(-pass\\s*=\\s*)(\\\"[^\\\"]*\\\"|[^\\s]+)",std::regex::icase),"$1***");}
static std::string timeText(std::time_t value);

ExtreamFS::ExtreamFS(std::string c):carnet(std::move(c)){}

Command ExtreamFS::parse(const std::string& line){
    Command c; c.raw=line; std::vector<std::string> tok; std::string cur; bool q=false;
    for(size_t i=0;i<line.size();++i){ char ch=line[i]; if(ch=='\"'){q=!q;continue;} if(std::isspace((unsigned char)ch)&&!q){if(!cur.empty()){tok.push_back(cur);cur.clear();}}else cur+=ch; }
    if(!cur.empty())tok.push_back(cur);
    if(tok.empty())return c;
    c.name=lower(tok[0]);
    for(size_t i=1;i<tok.size();++i){ std::string t=tok[i]; if(t.empty()||t[0]!='-')continue; t=t.substr(1); auto eq=t.find('='); if(eq==std::string::npos)c.flags.push_back(lower(t)); else c.params[lower(t.substr(0,eq))]=t.substr(eq+1); }
    return c;
}

std::string ExtreamFS::executeScript(const std::string& script){ std::stringstream ss(script); std::string line,out; while(std::getline(ss,line)){ std::string t=trim(line); if(t.empty()){out+="\n";continue;} if(t[0]=='#'){out+=t+"\n";continue;} out+="> "+redactSecrets(line)+"\n"+executeLine(line)+"\n"; } return out; }

std::string ExtreamFS::executeLine(const std::string& line){
    Command c=parse(line); if(c.name.empty())return"";
    try{
        Session previousSession=session;
        std::string result;
        if(c.name=="mkdisk")result=cmdMkdisk(c);
        else if(c.name=="rmdisk")result=cmdRmdisk(c);
        else if(c.name=="fdisk")result=cmdFdisk(c);
        else if(c.name=="mount")result=cmdMount(c);
        else if(c.name=="unmount")result=cmdUnmount(c);
        else if(c.name=="mounted")result=cmdMounted(c);
        else if(c.name=="mkfs")result=cmdMkfs(c);
        else if(c.name=="login")result=cmdLogin(c);
        else if(c.name=="logout")result=cmdLogout(c);
        else if(c.name=="mkgrp")result=cmdMkgrp(c);
        else if(c.name=="rmgrp")result=cmdRmgrp(c);
        else if(c.name=="mkusr")result=cmdMkusr(c);
        else if(c.name=="rmusr")result=cmdRmusr(c);
        else if(c.name=="chgrp")result=cmdChgrp(c);
        else if(c.name=="cat")result=cmdCat(c);
        else if(c.name=="mkdir")result=cmdMkdir(c);
        else if(c.name=="mkfile")result=cmdMkfile(c);
        else if(c.name=="remove")result=cmdRemove(c);
        else if(c.name=="rename")result=cmdRename(c);
        else if(c.name=="copy")result=cmdCopy(c);
        else if(c.name=="move")result=cmdMove(c);
        else if(c.name=="find")result=cmdFind(c);
        else if(c.name=="chown")result=cmdChown(c);
        else if(c.name=="chmod")result=cmdChmod(c);
        else if(c.name=="journaling")result=cmdJournaling(c);
        else if(c.name=="loss")result=cmdLoss(c);
        else if(c.name=="rep")result=cmdRep(c);
        else return "ERROR: comando no reconocido: "+c.name;

        static const std::set<std::string> journaled={
            "login","logout","mkgrp","rmgrp","mkusr","rmusr","chgrp","cat","mkdir","mkfile","remove","rename","copy","move","find","chown","chmod"
        };
        const bool succeeded=result.rfind("ERROR:",0)!=0;
        if(succeeded&&journaled.count(c.name)){
            const Session& journalSession=session.active?session:previousSession;
            if(journalSession.active){
                if(auto* mp=findMount(journalSession.mountId)){
                    std::string path=getp(c,"path");
                    if(path.empty()){
                        for(const auto& param:c.params)if(param.first.rfind("file",0)==0){path=param.second;break;}
                    }
                    if(path.empty()&&(c.name=="mkgrp"||c.name=="rmgrp"||c.name=="mkusr"||c.name=="rmusr"||c.name=="chgrp"))path="/users.txt";
                    if(path.empty())path="/";
                    appendJournal(*mp,c.name,path,redactSecrets(c.raw));
                }
            }
        }
        return result;
    }catch(const std::exception& e){return std::string("ERROR: ")+e.what();}
}

bool ExtreamFS::readMBR(const std::string& path,MBR& m){ std::ifstream f(path,std::ios::binary); if(!f)return false; f.read((char*)&m,sizeof(m)); return (bool)f; }
bool ExtreamFS::writeMBR(const std::string& path,const MBR& m){ std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out); if(!f)return false; f.seekp(0); f.write((const char*)&m,sizeof(m)); return (bool)f; }
Partition* ExtreamFS::findPrimary(MBR& m,const std::string& name){ for(auto& p:m.mbr_partitions)if(p.part_s>0&&fixed(p.part_name,16)==name)return &p; return nullptr; }

std::string ExtreamFS::cmdMkdisk(const Command& c){
    if(!c.params.count("size")||!c.params.count("path"))return"ERROR: -size y -path son obligatorios";
    long long size=std::stoll(getp(c,"size")); if(size<=0)return"ERROR: size debe ser mayor que cero";
    std::string unit=lower(getp(c,"unit","m")); long long mult=unit=="k"?1024LL:unit=="m"?1024LL*1024:0; if(!mult)return"ERROR: unit debe ser K o M";
    std::string fitv=lower(getp(c,"fit","ff")); char fit=fitv=="bf"?'B':fitv=="wf"?'W':fitv=="ff"?'F':0; if(!fit)return"ERROR: fit debe ser BF, FF o WF";
    std::string path=getp(c,"path"); fs::path pp(path); if(pp.has_parent_path())fs::create_directories(pp.parent_path());
    long long bytes=size*mult; if(bytes<(long long)sizeof(MBR)+1024)return"ERROR: disco demasiado pequeño";
    std::ofstream f(path,std::ios::binary|std::ios::trunc); if(!f)return"ERROR: no se pudo crear el disco";
    std::array<char,1024> zeros{}; long long left=bytes; while(left>0){ auto n=(std::streamsize)std::min<long long>(left,zeros.size()); f.write(zeros.data(),n); left-=n; } f.close();
    MBR m{}; m.mbr_tamano=(int)bytes; m.mbr_fecha_creacion=std::time(nullptr); m.mbr_dsk_signature=(int)std::random_device{}(); m.dsk_fit=fit;
    for(auto& p:m.mbr_partitions){p.part_status='0';p.part_type='0';p.part_fit='0';p.part_start=-1;p.part_s=0;p.part_correlative=-1;std::memset(p.part_name,0,16);std::memset(p.part_id,0,4);} writeMBR(path,m);
    if(std::find(knownDisks.begin(),knownDisks.end(),path)==knownDisks.end())knownDisks.push_back(path);
    return "OK: disco creado: "+path+" ("+std::to_string(bytes)+" bytes)";
}
std::string ExtreamFS::cmdRmdisk(const Command& c){ std::string p=getp(c,"path"); if(p.empty())return"ERROR: -path obligatorio"; if(!fs::exists(p))return"ERROR: disco no existe"; for(const auto& mounted:mounts)if(mounted.path==p)return"ERROR: desmonte las particiones antes de eliminar el disco"; fs::remove(p);knownDisks.erase(std::remove(knownDisks.begin(),knownDisks.end(),p),knownDisks.end()); return"OK: disco eliminado"; }

std::string ExtreamFS::cmdFdisk(const Command& c){
    std::string path=getp(c,"path"),name=getp(c,"name");
    if(path.empty()||name.empty())return"ERROR: -path y -name obligatorios";
    if(!fs::exists(path))return"ERROR: disco no existe";
    if(std::find(knownDisks.begin(),knownDisks.end(),path)==knownDisks.end())knownDisks.push_back(path);
    MBR m{};if(!readMBR(path,m))return"ERROR: no se pudo leer MBR";
    std::string unit=lower(getp(c,"unit","k"));long long multiplier=unit=="b"?1:unit=="k"?1024LL:unit=="m"?1024LL*1024:0;
    if(!multiplier)return"ERROR: unit debe ser B, K o M";
    auto mounted=[&](){for(const auto& item:mounts)if(item.path==path&&item.name==name)return true;return false;};
    auto wipe=[&](long long start,long long size){
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);if(!file)return false;
        std::array<char,4096> zeros{};file.seekp(start);
        while(size>0){auto amount=(std::streamsize)std::min<long long>(size,zeros.size());file.write(zeros.data(),amount);if(!file)return false;size-=amount;}return true;
    };
    auto extendedPartition=[&]()->Partition*{for(auto& p:m.mbr_partitions)if(p.part_s>0&&p.part_type=='E')return &p;return nullptr;};
    auto logicalNameExists=[&](const std::string& candidate){Partition* extended=extendedPartition();if(!extended)return false;std::ifstream file(path,std::ios::binary);int position=extended->part_start;EBR entry{};while(position>=extended->part_start&&position<extended->part_start+extended->part_s){file.clear();file.seekg(position);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;if(fixed(entry.part_name,16)==candidate)return true;if(entry.part_next<0)break;position=entry.part_next;}return false;};

    if(c.params.count("delete")){
        std::string mode=lower(getp(c,"delete"));if(mode!="fast"&&mode!="full")return"ERROR: -delete debe ser fast o full";
        if(!findPrimary(m,name)&&!logicalNameExists(name))return"ERROR: partición no existe";
        const std::string confirm=lower(getp(c,"confirm"));
        if(!hasFlag(c,"confirm")&&confirm!="true"&&confirm!="yes"&&confirm!="1")return"CONFIRMACION_REQUERIDA: repita el comando con -confirm para eliminar "+name;
        if(mounted())return"ERROR: desmonte la partición antes de eliminarla";
        if(Partition* p=findPrimary(m,name)){
            const int start=p->part_start,size=p->part_s;
            if(mode=="full"&&!wipe(start,size))return"ERROR: no se pudo limpiar la partición";
            *p=Partition{};p->part_start=-1;p->part_s=0;p->part_correlative=-1;
            return writeMBR(path,m)?"OK: partición eliminada ("+mode+")":"ERROR: no se pudo actualizar el MBR";
        }
        Partition* ext=extendedPartition();if(!ext)return"ERROR: partición no existe";
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);int pos=ext->part_start,previous=-1;EBR entry{};
        while(pos>=ext->part_start&&pos<ext->part_start+ext->part_s){file.clear();file.seekg(pos);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;
            if(fixed(entry.part_name,16)==name){
                if(mode=="full"&&!wipe(entry.part_start,entry.part_s))return"ERROR: no se pudo limpiar la partición lógica";
                if(previous<0){
                    if(entry.part_next<0){EBR empty{};empty.part_start=ext->part_start+(int)sizeof(EBR);empty.part_next=-1;file.clear();file.seekp(ext->part_start);file.write((char*)&empty,sizeof(empty));}
                    else{EBR next{};file.clear();file.seekg(entry.part_next);file.read((char*)&next,sizeof(next));file.clear();file.seekp(ext->part_start);file.write((char*)&next,sizeof(next));}
                }else{EBR prev{};file.clear();file.seekg(previous);file.read((char*)&prev,sizeof(prev));prev.part_next=entry.part_next;file.clear();file.seekp(previous);file.write((char*)&prev,sizeof(prev));}
                return file?"OK: partición lógica eliminada ("+mode+")":"ERROR: no se pudo actualizar EBR";
            }
            previous=pos;if(entry.part_next<0)break;pos=entry.part_next;
        }
        return"ERROR: partición no existe";
    }

    if(c.params.count("add")){
        long long value=std::stoll(getp(c,"add"));if(value==0)return"ERROR: -add no puede ser cero";long long delta=value*multiplier;
        if(delta<std::numeric_limits<int>::min()||delta>std::numeric_limits<int>::max())return"ERROR: ajuste fuera de rango";
        if(mounted())return"ERROR: desmonte la partición antes de redimensionarla";
        if(Partition* p=findPrimary(m,name)){
            long long newSize=(long long)p->part_s+delta;if(newSize<=0)return"ERROR: la reducción dejaría un tamaño inválido";
            if(delta>0){int limit=m.mbr_tamano;for(const auto& other:m.mbr_partitions)if(other.part_s>0&&other.part_start>p->part_start)limit=std::min(limit,other.part_start);if((long long)p->part_start+newSize>limit)return"ERROR: no hay espacio contiguo posterior";}
            if(delta<0&&p->part_type=='E'){
                std::ifstream file(path,std::ios::binary);int pos=p->part_start;EBR entry{};while(pos>=p->part_start&&pos<p->part_start+p->part_s){file.clear();file.seekg(pos);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;if((long long)entry.part_start+entry.part_s>p->part_start+newSize)return"ERROR: la reducción dejaría particiones lógicas fuera";if(entry.part_next<0)break;pos=entry.part_next;}
            }
            p->part_s=(int)newSize;return writeMBR(path,m)?"OK: partición redimensionada":"ERROR: no se pudo actualizar el MBR";
        }
        Partition* ext=extendedPartition();if(!ext)return"ERROR: partición no existe";
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);int pos=ext->part_start;EBR entry{};
        while(pos>=ext->part_start&&pos<ext->part_start+ext->part_s){file.clear();file.seekg(pos);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;if(fixed(entry.part_name,16)==name){long long newSize=(long long)entry.part_s+delta;if(newSize<=0)return"ERROR: la reducción dejaría un tamaño inválido";long long limit=entry.part_next<0?(long long)ext->part_start+ext->part_s:entry.part_next;if(delta>0&&(long long)entry.part_start+newSize>limit)return"ERROR: no hay espacio contiguo posterior";entry.part_s=(int)newSize;file.clear();file.seekp(pos);file.write((char*)&entry,sizeof(entry));return file?"OK: partición lógica redimensionada":"ERROR: no se pudo actualizar EBR";}if(entry.part_next<0)break;pos=entry.part_next;}
        return"ERROR: partición no existe";
    }

    if(!c.params.count("size"))return"ERROR: -size obligatorio para crear";
    long long requested=std::stoll(getp(c,"size"));if(requested<=0)return"ERROR: size inválido";long long requestedBytes=requested*multiplier;if(requestedBytes>std::numeric_limits<int>::max())return"ERROR: tamaño fuera de rango";int bytes=(int)requestedBytes;
    std::string typeValue=lower(getp(c,"type","p"));char type=typeValue=="p"?'P':typeValue=="e"?'E':typeValue=="l"?'L':0;if(!type)return"ERROR: type debe ser P, E o L";
    std::string fitValue=lower(getp(c,"fit","wf"));char fit=fitValue=="bf"?'B':fitValue=="ff"?'F':fitValue=="wf"?'W':0;if(!fit)return"ERROR: fit inválido";
    if(findPrimary(m,name)||logicalNameExists(name))return"ERROR: nombre de partición repetido";
    if(type=='L'){
        Partition* ext=extendedPartition();if(!ext)return"ERROR: no existe partición extendida";
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);EBR entry{};file.seekg(ext->part_start);file.read((char*)&entry,sizeof(entry));
        if(entry.part_s==0){if((int)sizeof(EBR)+bytes>ext->part_s)return"ERROR: sin espacio en extendida";entry.part_mount='0';entry.part_fit=fit;entry.part_start=ext->part_start+(int)sizeof(EBR);entry.part_s=bytes;entry.part_next=-1;copyFixed(entry.part_name,16,name);file.seekp(ext->part_start);file.write((char*)&entry,sizeof(entry));return"OK: partición lógica creada";}
        int ebrPos=ext->part_start;while(true){if(fixed(entry.part_name,16)==name)return"ERROR: nombre repetido";if(entry.part_next<0)break;ebrPos=entry.part_next;file.clear();file.seekg(ebrPos);file.read((char*)&entry,sizeof(entry));}
        int newPos=entry.part_start+entry.part_s;if(newPos+(int)sizeof(EBR)+bytes>ext->part_start+ext->part_s)return"ERROR: sin espacio en extendida";entry.part_next=newPos;file.clear();file.seekp(ebrPos);file.write((char*)&entry,sizeof(entry));EBR next{};next.part_mount='0';next.part_fit=fit;next.part_start=newPos+(int)sizeof(EBR);next.part_s=bytes;next.part_next=-1;copyFixed(next.part_name,16,name);file.seekp(newPos);file.write((char*)&next,sizeof(next));return"OK: partición lógica creada";
    }
    int used=0;bool hasExtended=false;for(auto& p:m.mbr_partitions)if(p.part_s>0){used++;if(p.part_type=='E')hasExtended=true;}if(used>=4)return"ERROR: máximo 4 particiones primarias/extendidas";if(type=='E'&&hasExtended)return"ERROR: solo puede existir una extendida";
    struct Hole{int start,size;};std::vector<Hole> holes;std::vector<Partition*> partitions;for(auto& p:m.mbr_partitions)if(p.part_s>0)partitions.push_back(&p);std::sort(partitions.begin(),partitions.end(),[](auto a,auto b){return a->part_start<b->part_start;});int cursor=sizeof(MBR);for(auto* p:partitions){if(p->part_start>cursor)holes.push_back({cursor,p->part_start-cursor});cursor=p->part_start+p->part_s;}if(cursor<m.mbr_tamano)holes.push_back({cursor,m.mbr_tamano-cursor});
    std::vector<Hole> candidates;for(auto hole:holes)if(hole.size>=bytes)candidates.push_back(hole);if(candidates.empty())return"ERROR: espacio insuficiente";Hole selected=candidates[0];if(fit=='B')selected=*std::min_element(candidates.begin(),candidates.end(),[](auto a,auto b){return a.size<b.size;});else if(fit=='W')selected=*std::max_element(candidates.begin(),candidates.end(),[](auto a,auto b){return a.size<b.size;});
    Partition* slot=nullptr;for(auto& p:m.mbr_partitions)if(p.part_s==0){slot=&p;break;}slot->part_status='0';slot->part_type=type;slot->part_fit=fit;slot->part_start=selected.start;slot->part_s=bytes;slot->part_correlative=-1;copyFixed(slot->part_name,16,name);std::memset(slot->part_id,0,4);writeMBR(path,m);
    if(type=='E'){std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);EBR empty{};empty.part_mount='0';empty.part_fit=fit;empty.part_start=selected.start+(int)sizeof(EBR);empty.part_next=-1;file.seekp(selected.start);file.write((char*)&empty,sizeof(empty));}
    return std::string("OK: partición ")+(type=='P'?"primaria":"extendida")+" creada";
}

MountedPartition* ExtreamFS::findMount(const std::string& id){ for(auto& m:mounts)if(lower(m.id)==lower(id))return &m; return nullptr; }
std::string ExtreamFS::cmdMount(const Command& c){
    std::string path=getp(c,"path"),name=getp(c,"name");
    if(path.empty()||name.empty())return"ERROR: -path y -name obligatorios";
    for(auto& item:mounts)if(item.path==path&&item.name==name)return"ERROR: partición ya montada";
    MBR m{};if(!readMBR(path,m))return"ERROR: disco no existe o inválido";
    int start=-1,size=0,logicalHeader=-1;Partition* primary=findPrimary(m,name);
    if(primary&&primary->part_type=='P'){start=primary->part_start;size=primary->part_s;}
    else{
        Partition* extended=nullptr;for(auto& partition:m.mbr_partitions)if(partition.part_s>0&&partition.part_type=='E')extended=&partition;
        if(extended){std::ifstream file(path,std::ios::binary);int position=extended->part_start;EBR entry{};while(position>=extended->part_start&&position<extended->part_start+extended->part_s){file.clear();file.seekg(position);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;if(fixed(entry.part_name,16)==name){start=entry.part_start;size=entry.part_s;logicalHeader=position;break;}if(entry.part_next<0)break;position=entry.part_next;}}
    }
    if(start<0)return"ERROR: partición primaria o lógica no existe";
    char letter='A';std::map<std::string,char> letters;for(auto& item:mounts)if(!letters.count(item.path))letters[item.path]=item.id.back();if(letters.count(path))letter=letters[path];else{std::set<char> used;for(auto& pair:letters)used.insert(pair.second);while(used.count(letter))letter++;}
    std::string suffix=carnet.size()>=2?carnet.substr(carnet.size()-2):carnet;int number=1;std::string id;
    while(true){id=suffix+std::to_string(number)+letter;bool used=false;for(const auto& item:mounts)if(lower(item.id)==lower(id)){used=true;break;}if(!used)break;number++;}
    if(logicalHeader>=0){std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);EBR entry{};file.seekg(logicalHeader);file.read((char*)&entry,sizeof(entry));entry.part_mount='1';file.clear();file.seekp(logicalHeader);file.write((char*)&entry,sizeof(entry));if(!file)return"ERROR: no se pudo actualizar EBR";}
    else{primary->part_status='1';primary->part_correlative=number;std::memset(primary->part_id,0,4);std::memcpy(primary->part_id,id.data(),std::min<size_t>(4,id.size()));if(!writeMBR(path,m))return"ERROR: no se pudo actualizar el MBR";}
    mounts.push_back({path,name,id,start,size});if(std::find(knownDisks.begin(),knownDisks.end(),path)==knownDisks.end())knownDisks.push_back(path);return"OK: partición montada con id="+id;
}
std::string ExtreamFS::cmdUnmount(const Command& c){
    std::string id=getp(c,"id");if(id.empty())return"ERROR: -id obligatorio";
    auto mounted=std::find_if(mounts.begin(),mounts.end(),[&](const MountedPartition& item){return lower(item.id)==lower(id);});
    if(mounted==mounts.end())return"ERROR: id no montado";
    MBR m{};if(!readMBR(mounted->path,m))return"ERROR: no se pudo leer el MBR";Partition* p=findPrimary(m,mounted->name);bool updated=false;
    if(p&&p->part_type=='P'){p->part_status='0';p->part_correlative=0;std::memset(p->part_id,0,sizeof(p->part_id));updated=writeMBR(mounted->path,m);}
    else{Partition* extended=nullptr;for(auto& partition:m.mbr_partitions)if(partition.part_s>0&&partition.part_type=='E')extended=&partition;if(extended){std::fstream file(mounted->path,std::ios::binary|std::ios::in|std::ios::out);int position=extended->part_start;EBR entry{};while(position>=extended->part_start&&position<extended->part_start+extended->part_s){file.clear();file.seekg(position);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;if(fixed(entry.part_name,16)==mounted->name){entry.part_mount='0';file.clear();file.seekp(position);file.write((char*)&entry,sizeof(entry));updated=(bool)file;break;}if(entry.part_next<0)break;position=entry.part_next;}}}
    if(!updated)return"ERROR: no se pudo actualizar la partición";
    if(session.active&&lower(session.mountId)==lower(id))session=Session{};
    mounts.erase(mounted);return"OK: partición desmontada: "+id;
}
std::string ExtreamFS::cmdMounted(const Command&){ if(mounts.empty())return"No hay particiones montadas"; std::string o="Particiones montadas:\n";for(auto&m:mounts)o+="- "+m.id+" | "+m.name+" | "+m.path+"\n";return o; }

bool ExtreamFS::loadSB(MountedPartition& mp,SuperBlock& sb){std::ifstream f(mp.path,std::ios::binary);if(!f)return false;f.seekg(mp.start);f.read((char*)&sb,sizeof(sb));return f&&sb.s_magic==0xEF53;}
void ExtreamFS::saveSB(std::fstream& f,MountedPartition& mp,const SuperBlock& sb){f.seekp(mp.start);f.write((const char*)&sb,sizeof(sb));}
bool ExtreamFS::readInode(std::fstream& f,const SuperBlock& sb,int idx,Inode& ino){if(idx<0||idx>=sb.s_inodes_count)return false;f.seekg(sb.s_inode_start+(long long)idx*sizeof(Inode));f.read((char*)&ino,sizeof(ino));return(bool)f;}
bool ExtreamFS::writeInode(std::fstream& f,const SuperBlock& sb,int idx,const Inode& ino){f.seekp(sb.s_inode_start+(long long)idx*sizeof(Inode));f.write((const char*)&ino,sizeof(ino));return(bool)f;}
bool ExtreamFS::readPointerBlock(std::fstream& f,const SuperBlock& sb,int idx,PointerBlock& block){if(idx<0||idx>=sb.s_blocks_count)return false;f.clear();f.seekg(sb.s_block_start+(long long)idx*64);f.read((char*)&block,64);return(bool)f;}
bool ExtreamFS::writePointerBlock(std::fstream& f,const SuperBlock& sb,int idx,const PointerBlock& block){if(idx<0||idx>=sb.s_blocks_count)return false;f.clear();f.seekp(sb.s_block_start+(long long)idx*64);f.write((const char*)&block,64);return(bool)f;}
int ExtreamFS::allocInode(std::fstream& f,SuperBlock& sb){ for(int i=0;i<sb.s_inodes_count;i++){char b;f.seekg(sb.s_bm_inode_start+i);f.read(&b,1);if(b=='0'||b==0){b='1';f.seekp(sb.s_bm_inode_start+i);f.write(&b,1);sb.s_free_inodes_count--;sb.s_first_ino=i+1;return i;}}return-1; }
int ExtreamFS::allocBlock(std::fstream& f,SuperBlock& sb){ for(int i=0;i<sb.s_blocks_count;i++){char b;f.seekg(sb.s_bm_block_start+i);f.read(&b,1);if(b=='0'||b==0){b='1';f.seekp(sb.s_bm_block_start+i);f.write(&b,1);sb.s_free_blocks_count--;sb.s_first_blo=i+1;return i;}}return-1; }

static PointerBlock emptyPointerBlock(){PointerBlock p{};for(int&i:p.b_pointers)i=-1;return p;}

std::vector<int> ExtreamFS::fileDataBlocks(std::fstream& f,const SuperBlock& sb,const Inode& ino){
    std::vector<int> out;
    for(int i=0;i<12;i++)if(ino.i_block[i]>=0)out.push_back(ino.i_block[i]);
    std::function<void(int,int)> walk=[&](int block,int depth){
        if(block<0)return;
        PointerBlock p{};if(!readPointerBlock(f,sb,block,p))return;
        for(int next:p.b_pointers)if(next>=0){if(depth==1)out.push_back(next);else walk(next,depth-1);}
    };
    walk(ino.i_block[12],1);walk(ino.i_block[13],2);walk(ino.i_block[14],3);
    return out;
}

std::vector<int> ExtreamFS::filePointerBlocks(std::fstream& f,const SuperBlock& sb,const Inode& ino){
    std::vector<int> out;
    std::function<void(int,int)> walk=[&](int block,int depth){
        if(block<0)return;
        out.push_back(block);PointerBlock p{};if(!readPointerBlock(f,sb,block,p)||depth==1)return;
        for(int next:p.b_pointers)if(next>=0)walk(next,depth-1);
    };
    walk(ino.i_block[12],1);walk(ino.i_block[13],2);walk(ino.i_block[14],3);
    return out;
}

bool ExtreamFS::writeFileContent(std::fstream& f,SuperBlock& sb,Inode& ino,const std::string& content,std::string& err){
    const size_t dataNeeded=(content.size()+63)/64;
    const size_t maxData=12+16+16*16+16*16*16;
    if(dataNeeded>maxData){err="archivo excede la capacidad de apuntadores del inodo";return false;}
    size_t remaining=dataNeeded>12?dataNeeded-12:0;
    size_t pointerNeeded=0;
    if(remaining){pointerNeeded++;remaining-=std::min<size_t>(remaining,16);}
    if(remaining){size_t used=std::min<size_t>(remaining,256);pointerNeeded+=1+(used+15)/16;remaining-=used;}
    if(remaining){pointerNeeded+=1+(remaining+255)/256+(remaining+15)/16;}
    if(dataNeeded+pointerNeeded>(size_t)sb.s_free_blocks_count){err="sin bloques suficientes";return false;}

    std::vector<int> dataBlocks;dataBlocks.reserve(dataNeeded);
    for(size_t i=0;i<dataNeeded;i++){int b=allocBlock(f,sb);if(b<0){err="sin bloques";return false;}dataBlocks.push_back(b);}
    size_t cursor=0;
    for(int i=0;i<12&&cursor<dataBlocks.size();i++)ino.i_block[i]=dataBlocks[cursor++];
    std::function<int(int,size_t)> build=[&](int depth,size_t count)->int{
        int root=allocBlock(f,sb);if(root<0)return-1;PointerBlock p=emptyPointerBlock();
        size_t childCapacity=1;for(int i=1;i<depth;i++)childCapacity*=16;
        for(int i=0;i<16&&count>0;i++){
            size_t take=std::min(count,childCapacity);
            if(depth==1)p.b_pointers[i]=dataBlocks[cursor++];
            else{p.b_pointers[i]=build(depth-1,take);if(p.b_pointers[i]<0)return-1;}
            count-=take;
        }
        if(!writePointerBlock(f,sb,root,p))return-1;
        return root;
    };
    size_t left=dataBlocks.size()-cursor;
    if(left){size_t take=std::min<size_t>(left,16);ino.i_block[12]=build(1,take);left-=take;}
    if(left){size_t take=std::min<size_t>(left,256);ino.i_block[13]=build(2,take);left-=take;}
    if(left){ino.i_block[14]=build(3,left);left=0;}
    if((dataNeeded&&cursor!=dataBlocks.size())||left){err="fallo al construir apuntadores indirectos";return false;}

    size_t pos=0;for(int block:dataBlocks){FileBlock b{};size_t n=std::min<size_t>(64,content.size()-pos);if(n)std::memcpy(b.b_content,content.data()+pos,n);f.clear();f.seekp(sb.s_block_start+(long long)block*64);f.write((char*)&b,64);if(!f){err="fallo al escribir bloque";return false;}pos+=n;}
    ino.i_s=(int)content.size();return true;
}

void ExtreamFS::releaseFileBlocks(std::fstream& f,SuperBlock& sb,Inode& ino){
    auto data=fileDataBlocks(f,sb,ino);auto pointers=filePointerBlocks(f,sb,ino);
    for(int block:data){char z='0';f.clear();f.seekp(sb.s_bm_block_start+block);f.write(&z,1);sb.s_free_blocks_count++;}
    for(int block:pointers){char z='0';f.clear();f.seekp(sb.s_bm_block_start+block);f.write(&z,1);sb.s_free_blocks_count++;}
    for(int&i:ino.i_block)i=-1;
}

std::string ExtreamFS::cmdMkfs(const Command& c){
    std::string id=getp(c,"id"); if(id.empty())return"ERROR: -id obligatorio"; auto* mp=findMount(id); if(!mp)return"ERROR: id no montado";
    std::string type=lower(getp(c,"type","full"));if(type!="full")return"ERROR: -type debe ser full";
    std::string fsType=lower(getp(c,"fs","2fs"));if(fsType!="2fs"&&fsType!="3fs")return"ERROR: -fs debe ser 2fs o 3fs";
    std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out); if(!f)return"ERROR: no se pudo abrir disco";
    const bool ext3=fsType=="3fs";const long long journalBytes=ext3?(long long)sizeof(Journaling):0;
    long long avail=mp->size; long long den=1+3+sizeof(Inode)+3*64; int n=(int)((avail-sizeof(SuperBlock)-journalBytes)/den); if(n<2)return"ERROR: partición demasiado pequeña";
    SuperBlock sb{};sb.s_filesystem_type=ext3?3:2;sb.s_inodes_count=n;sb.s_blocks_count=3*n;sb.s_free_inodes_count=n;sb.s_free_blocks_count=3*n;sb.s_mtime=std::time(nullptr);sb.s_umtime=0;sb.s_mnt_count=1;sb.s_magic=0xEF53;sb.s_inode_s=sizeof(Inode);sb.s_block_s=64;sb.s_first_ino=0;sb.s_first_blo=0;sb.s_bm_inode_start=mp->start+sizeof(SuperBlock)+(int)journalBytes;sb.s_bm_block_start=sb.s_bm_inode_start+n;sb.s_inode_start=sb.s_bm_block_start+3*n;sb.s_block_start=sb.s_inode_start+n*sizeof(Inode);
    f.seekp(mp->start); std::vector<char> zero(mp->size,0); f.write(zero.data(),zero.size()); f.seekp(mp->start);f.write((char*)&sb,sizeof(sb)); std::vector<char> bmI(n,'0'),bmB(3*n,'0');f.seekp(sb.s_bm_inode_start);f.write(bmI.data(),bmI.size());f.seekp(sb.s_bm_block_start);f.write(bmB.data(),bmB.size());
    int root=allocInode(f,sb),users=allocInode(f,sb),broot=allocBlock(f,sb),busers=allocBlock(f,sb); Inode ri{},ui{};for(int&i:ri.i_block)i=-1;for(int&i:ui.i_block)i=-1;auto now=std::time(nullptr);ri.i_uid=1;ri.i_gid=1;ri.i_s=0;ri.i_atime=ri.i_ctime=ri.i_mtime=now;ri.i_block[0]=broot;ri.i_type='0';std::memcpy(ri.i_perm,"777",3);ui.i_uid=1;ui.i_gid=1;std::string uc="1, G, root\n1, U, root, root, 123\n";ui.i_s=uc.size();ui.i_atime=ui.i_ctime=ui.i_mtime=now;ui.i_block[0]=busers;ui.i_type='1';std::memcpy(ui.i_perm,"664",3);writeInode(f,sb,root,ri);writeInode(f,sb,users,ui);
    FolderBlock fb{};for(auto&e:fb.b_content)e.b_inodo=-1;copyFixed(fb.b_content[0].b_name,12,".");fb.b_content[0].b_inodo=root;copyFixed(fb.b_content[1].b_name,12,"..");fb.b_content[1].b_inodo=root;copyFixed(fb.b_content[2].b_name,12,"users.txt");fb.b_content[2].b_inodo=users;f.seekp(sb.s_block_start+(long long)broot*64);f.write((char*)&fb,64);FileBlock fl{};std::memcpy(fl.b_content,uc.data(),std::min<size_t>(64,uc.size()));f.seekp(sb.s_block_start+(long long)busers*64);f.write((char*)&fl,64);saveSB(f,*mp,sb);return std::string("OK: partición formateada ")+(ext3?"EXT3":"EXT2")+" y users.txt creado";
}

int ExtreamFS::resolvePath(MountedPartition& mp,const std::string& path,int* parentOut,std::string* leafOut){
    SuperBlock sb{};if(!loadSB(mp,sb))return-1;std::fstream f(mp.path,std::ios::binary|std::ios::in|std::ios::out);auto parts=split(path,'/');std::vector<std::string> seg;for(auto&s:parts)if(!s.empty())seg.push_back(s);int cur=0,parent=-1;
    if(seg.empty()){if(parentOut)*parentOut=-1;if(leafOut)*leafOut="/";return 0;} for(size_t si=0;si<seg.size();++si){Inode ino{};if(!readInode(f,sb,cur,ino)||ino.i_type!='0')return-1;parent=cur;int nxt=-1;for(int bi=0;bi<12&&nxt<0;bi++){if(ino.i_block[bi]<0)continue;FolderBlock fb{};f.seekg(sb.s_block_start+(long long)ino.i_block[bi]*64);f.read((char*)&fb,64);for(auto&e:fb.b_content)if(e.b_inodo>=0&&fixed(e.b_name,12)==seg[si]){nxt=e.b_inodo;break;}} if(nxt<0){if(parentOut&&si==seg.size()-1)*parentOut=cur;if(leafOut&&si==seg.size()-1)*leafOut=seg[si];return-1;}cur=nxt;}
    if(parentOut)*parentOut=parent;
    if(leafOut)*leafOut=seg.back();
    return cur;
}

bool ExtreamFS::appendChild(std::fstream& f,SuperBlock& sb,int parent,const std::string& name,int child){Inode ino{};if(!readInode(f,sb,parent,ino))return false;for(int i=0;i<12;i++){if(ino.i_block[i]<0){int nb=allocBlock(f,sb);if(nb<0)return false;ino.i_block[i]=nb;FolderBlock z{};for(auto&e:z.b_content)e.b_inodo=-1;f.seekp(sb.s_block_start+(long long)nb*64);f.write((char*)&z,64);writeInode(f,sb,parent,ino);}FolderBlock fb{};f.seekg(sb.s_block_start+(long long)ino.i_block[i]*64);f.read((char*)&fb,64);for(auto&e:fb.b_content)if(e.b_inodo<0){copyFixed(e.b_name,12,name);e.b_inodo=child;f.seekp(sb.s_block_start+(long long)ino.i_block[i]*64);f.write((char*)&fb,64);ino.i_mtime=std::time(nullptr);writeInode(f,sb,parent,ino);return true;}}return false;}

bool ExtreamFS::canRead(const Inode& inode) const{
    if(!session.active)return false;
    if(session.user=="root")return true;
    int position=session.uid==inode.i_uid?0:session.gid==inode.i_gid?1:2;
    char value=inode.i_perm[position];
    if(value<'0'||value>'7')return false;
    return ((value-'0')&4)!=0;
}

bool ExtreamFS::canWrite(const Inode& inode) const{
    if(!session.active)return false;
    if(session.user=="root")return true;
    int position=session.uid==inode.i_uid?0:session.gid==inode.i_gid?1:2;
    char value=inode.i_perm[position];
    if(value<'0'||value>'7')return false;
    return ((value-'0')&2)!=0;
}

bool ExtreamFS::appendJournal(MountedPartition& mp,const std::string& operation,const std::string& path,const std::string& content){
    SuperBlock sb{};if(!loadSB(mp,sb)||sb.s_filesystem_type!=3)return false;
    std::fstream f(mp.path,std::ios::binary|std::ios::in|std::ios::out);if(!f)return false;
    Journaling journal{};const auto journalStart=(long long)mp.start+sizeof(SuperBlock);
    f.seekg(journalStart);f.read((char*)&journal,sizeof(journal));
    if(!f||journal.j_count<0||journal.j_count>JOURNAL_CAPACITY){f.clear();journal=Journaling{};}
    int index=journal.j_count;
    if(index==JOURNAL_CAPACITY){
        for(int i=1;i<JOURNAL_CAPACITY;i++)journal.j_content[i-1]=journal.j_content[i];
        index=JOURNAL_CAPACITY-1;
    }else journal.j_count++;
    auto& entry=journal.j_content[index];entry=JournalInformation{};
    copyFixed(entry.i_operation,sizeof(entry.i_operation),operation);
    copyFixed(entry.i_path,sizeof(entry.i_path),path);
    copyFixed(entry.i_content,sizeof(entry.i_content),content);
    entry.i_date=(float)std::time(nullptr);
    f.clear();f.seekp(journalStart);f.write((char*)&journal,sizeof(journal));return(bool)f;
}

bool ExtreamFS::createDirectory(MountedPartition& mp,const std::string& path,bool recursive,std::string& err){
    if(resolvePath(mp,path)>=0){err="la carpeta ya existe";return false;}
    auto segments=split(path,'/');std::string current;
    for(auto& segment:segments){
        if(segment.empty())continue;
        current+="/"+segment;
        if(resolvePath(mp,current)>=0)continue;
        int parent=-1;std::string leaf;resolvePath(mp,current,&parent,&leaf);
        if(parent<0){err="carpeta padre no existe";return false;}
        if(!recursive&&current!=path){err="carpeta padre no existe; use -p";return false;}
        if(leaf.size()>12){err="nombre máximo 12 caracteres por bloque carpeta";return false;}
        SuperBlock sb{};loadSB(mp,sb);std::fstream f(mp.path,std::ios::binary|std::ios::in|std::ios::out);Inode parentInode{};
        if(!readInode(f,sb,parent,parentInode)){err="no se pudo leer la carpeta padre";return false;}
        if(!canWrite(parentInode)){err="el usuario no tiene permiso de escritura en la carpeta padre";return false;}
        int inodeIndex=allocInode(f,sb),blockIndex=allocBlock(f,sb);
        if(inodeIndex<0||blockIndex<0){
            if(inodeIndex>=0){char free='0';f.clear();f.seekp(sb.s_bm_inode_start+inodeIndex);f.write(&free,1);sb.s_free_inodes_count++;}
            if(blockIndex>=0){char free='0';f.clear();f.seekp(sb.s_bm_block_start+blockIndex);f.write(&free,1);sb.s_free_blocks_count++;}
            saveSB(f,mp,sb);err="sin espacio";return false;
        }
        Inode inode{};for(int& pointer:inode.i_block)pointer=-1;inode.i_uid=session.uid>0?session.uid:1;inode.i_gid=session.gid>0?session.gid:1;inode.i_atime=inode.i_ctime=inode.i_mtime=std::time(nullptr);inode.i_block[0]=blockIndex;inode.i_type='0';std::memcpy(inode.i_perm,"664",3);writeInode(f,sb,inodeIndex,inode);
        FolderBlock folder{};for(auto& entry:folder.b_content)entry.b_inodo=-1;copyFixed(folder.b_content[0].b_name,12,".");folder.b_content[0].b_inodo=inodeIndex;copyFixed(folder.b_content[1].b_name,12,"..");folder.b_content[1].b_inodo=parent;f.seekp(sb.s_block_start+(long long)blockIndex*64);f.write((char*)&folder,64);
        if(!appendChild(f,sb,parent,leaf,inodeIndex)){
            char free='0';f.clear();f.seekp(sb.s_bm_inode_start+inodeIndex);f.write(&free,1);f.seekp(sb.s_bm_block_start+blockIndex);f.write(&free,1);sb.s_free_inodes_count++;sb.s_free_blocks_count++;saveSB(f,mp,sb);err="carpeta padre llena";return false;
        }
        saveSB(f,mp,sb);
    }
    return true;
}

bool ExtreamFS::createFile(MountedPartition& mp,const std::string& path,const std::string& content,bool recursive,std::string& err){
    if(resolvePath(mp,path)>=0){err="el archivo ya existe";return false;}fs::path pp(path);std::string parentPath=pp.parent_path().string();if(parentPath.empty())parentPath="/";if(resolvePath(mp,parentPath)<0){if(!recursive||!createDirectory(mp,parentPath,true,err))return false;}int parent=resolvePath(mp,parentPath);std::string leaf=pp.filename().string();if(leaf.size()>12){err="nombre máximo 12 caracteres por bloque carpeta";return false;}SuperBlock sb{};if(!loadSB(mp,sb)){err="partición sin formato";return false;}std::fstream f(mp.path,std::ios::binary|std::ios::in|std::ios::out);Inode parentInode{};if(!readInode(f,sb,parent,parentInode)){err="no se pudo leer la carpeta padre";return false;}if(!canWrite(parentInode)){err="el usuario no tiene permiso de escritura en la carpeta padre";return false;}int ni=allocInode(f,sb);if(ni<0){err="sin inodos";return false;}Inode ino{};for(int&i:ino.i_block)i=-1;ino.i_uid=session.uid>0?session.uid:1;ino.i_gid=session.gid>0?session.gid:1;ino.i_atime=ino.i_ctime=ino.i_mtime=std::time(nullptr);ino.i_type='1';std::memcpy(ino.i_perm,"664",3);if(!writeFileContent(f,sb,ino,content,err)){char z='0';f.seekp(sb.s_bm_inode_start+ni);f.write(&z,1);return false;}writeInode(f,sb,ni,ino);if(!appendChild(f,sb,parent,leaf,ni)){releaseFileBlocks(f,sb,ino);char z='0';f.seekp(sb.s_bm_inode_start+ni);f.write(&z,1);err="carpeta padre llena";return false;}saveSB(f,mp,sb);return true;
}

std::string ExtreamFS::readFile(MountedPartition& mp,const std::string& path,bool* ok){if(ok)*ok=false;int idx=resolvePath(mp,path);if(idx<0)return"";SuperBlock sb{};if(!loadSB(mp,sb))return"";std::fstream f(mp.path,std::ios::binary|std::ios::in);Inode ino{};if(!readInode(f,sb,idx,ino)||ino.i_type!='1')return"";std::string out;int remaining=ino.i_s;for(int block:fileDataBlocks(f,sb,ino)){if(remaining<=0)break;FileBlock b{};f.clear();f.seekg(sb.s_block_start+(long long)block*64);f.read((char*)&b,64);int n=std::min(64,remaining);out.append(b.b_content,b.b_content+n);remaining-=n;}if(remaining!=0)return"";if(ok)*ok=true;return out;}

std::string ExtreamFS::usersRead(MountedPartition& mp){bool ok=false;auto x=readFile(mp,"/users.txt",&ok);return ok?x:"";}
bool ExtreamFS::usersWrite(MountedPartition& mp,const std::string& content){int idx=resolvePath(mp,"/users.txt");if(idx<0)return false;SuperBlock sb{};if(!loadSB(mp,sb))return false;std::fstream f(mp.path,std::ios::binary|std::ios::in|std::ios::out);Inode ino{};readInode(f,sb,idx,ino);auto oldData=fileDataBlocks(f,sb,ino);auto oldPointers=filePointerBlocks(f,sb,ino);int oldCount=(int)(oldData.size()+oldPointers.size());int neededData=(int)((content.size()+63)/64);int rem=std::max(0,neededData-12),neededPointers=0;if(rem){neededPointers++;rem-=std::min(rem,16);}if(rem){int used=std::min(rem,256);neededPointers+=1+(used+15)/16;rem-=used;}if(rem)neededPointers+=1+(rem+255)/256+(rem+15)/16;if(neededData+neededPointers>sb.s_free_blocks_count+oldCount)return false;releaseFileBlocks(f,sb,ino);std::string err;if(!writeFileContent(f,sb,ino,content,err))return false;ino.i_mtime=std::time(nullptr);writeInode(f,sb,idx,ino);saveSB(f,mp,sb);return true;}

std::string ExtreamFS::cmdLogin(const Command& c){if(session.active)return"ERROR: ya existe una sesión activa";std::string u=getp(c,"user"),p=getp(c,"pass"),id=getp(c,"id");if(u.empty()||p.empty()||id.empty())return"ERROR: -user -pass -id obligatorios";auto*mp=findMount(id);if(!mp)return"ERROR: id no montado";std::string data=usersRead(*mp);for(auto&line:split(data,'\n')){auto v=split(line,',');if(v.size()>=5&&v[0]!="0"&&trim(v[1])=="U"&&trim(v[3])==u){if(trim(v[4])!=p)return"ERROR: autenticación fallida";session.active=true;session.user=u;session.group=trim(v[2]);session.uid=std::stoi(v[0]);session.mountId=id;int gid=1;for(auto&gl:split(data,'\n')){auto g=split(gl,',');if(g.size()>=3&&g[0]!="0"&&trim(g[1])=="G"&&trim(g[2])==session.group){gid=std::stoi(g[0]);break;}}session.gid=gid;return"OK: sesión iniciada como "+u;}}return"ERROR: usuario no existe";}
std::string ExtreamFS::cmdLogout(const Command&){if(!session.active)return"ERROR: no hay sesión activa";session=Session{};return"OK: sesión cerrada";}
static bool rootOnly(const Session&s){return s.active&&s.user=="root";}
std::string ExtreamFS::cmdMkgrp(const Command& c){if(!rootOnly(session))return"ERROR: solo root puede ejecutar mkgrp";auto*mp=findMount(session.mountId);std::string n=getp(c,"name");if(n.empty())return"ERROR: -name obligatorio";auto d=usersRead(*mp);int mx=0;for(auto&l:split(d,'\n')){auto v=split(l,',');if(v.size()>=3&&trim(v[1])=="G"){mx=std::max(mx,std::stoi(v[0]));if(v[0]!="0"&&trim(v[2])==n)return"ERROR: grupo ya existe";}}d+=std::to_string(mx+1)+", G, "+n+"\n";return usersWrite(*mp,d)?"OK: grupo creado":"ERROR: no se pudo actualizar users.txt";}
std::string ExtreamFS::cmdRmgrp(const Command& c){if(!rootOnly(session))return"ERROR: solo root";auto*mp=findMount(session.mountId);std::string n=getp(c,"name");auto lines=split(usersRead(*mp),'\n');bool found=false;std::string out;for(auto l:lines){auto v=split(l,',');if(v.size()>=3&&v[0]!="0"&&trim(v[1])=="G"&&trim(v[2])==n){v[0]="0";l=v[0]+", G, "+trim(v[2]);found=true;}if(!trim(l).empty())out+=l+"\n";}if(!found)return"ERROR: grupo no existe";return usersWrite(*mp,out)?"OK: grupo eliminado":"ERROR: fallo users.txt";}
std::string ExtreamFS::cmdMkusr(const Command& c){if(!rootOnly(session))return"ERROR: solo root";std::string u=getp(c,"user"),p=getp(c,"pass"),g=getp(c,"grp");if(u.empty()||p.empty()||g.empty())return"ERROR: parámetros obligatorios";if(u.size()>10||p.size()>10||g.size()>10)return"ERROR: máximo 10 caracteres";auto*mp=findMount(session.mountId);auto d=usersRead(*mp);bool grp=false;int mx=0;for(auto&l:split(d,'\n')){auto v=split(l,',');if(v.size()>=3&&v[0]!="0"&&trim(v[1])=="G"&&trim(v[2])==g)grp=true;if(v.size()>=5&&trim(v[1])=="U"){mx=std::max(mx,std::stoi(v[0]));if(v[0]!="0"&&trim(v[3])==u)return"ERROR: usuario ya existe";}}if(!grp)return"ERROR: grupo no existe";d+=std::to_string(mx+1)+", U, "+g+", "+u+", "+p+"\n";return usersWrite(*mp,d)?"OK: usuario creado":"ERROR: users.txt lleno";}
std::string ExtreamFS::cmdRmusr(const Command& c){if(!rootOnly(session))return"ERROR: solo root";std::string u=getp(c,"user");auto*mp=findMount(session.mountId);auto lines=split(usersRead(*mp),'\n');bool found=false;std::string out;for(auto l:lines){auto v=split(l,',');if(v.size()>=5&&v[0]!="0"&&trim(v[1])=="U"&&trim(v[3])==u){v[0]="0";l="0, U, "+trim(v[2])+", "+trim(v[3])+", "+trim(v[4]);found=true;}if(!trim(l).empty())out+=l+"\n";}if(!found)return"ERROR: usuario no existe";return usersWrite(*mp,out)?"OK: usuario eliminado":"ERROR: fallo users.txt";}
std::string ExtreamFS::cmdChgrp(const Command& c){if(!rootOnly(session))return"ERROR: solo root";std::string u=getp(c,"user"),g=getp(c,"grp");auto*mp=findMount(session.mountId);auto lines=split(usersRead(*mp),'\n');bool ge=false,uf=false;for(auto&l:lines){auto v=split(l,',');if(v.size()>=3&&v[0]!="0"&&trim(v[1])=="G"&&trim(v[2])==g)ge=true;}if(!ge)return"ERROR: grupo destino no existe";std::string out;for(auto l:lines){auto v=split(l,',');if(v.size()>=5&&v[0]!="0"&&trim(v[1])=="U"&&trim(v[3])==u){l=trim(v[0])+", U, "+g+", "+trim(v[3])+", "+trim(v[4]);uf=true;}if(!trim(l).empty())out+=l+"\n";}if(!uf)return"ERROR: usuario no existe";return usersWrite(*mp,out)?"OK: grupo cambiado":"ERROR";}

std::string ExtreamFS::cmdCat(const Command& c){if(!session.active)return"ERROR: requiere sesión";auto*mp=findMount(session.mountId);std::vector<std::pair<int,std::string>> files;for(auto&kv:c.params)if(kv.first.rfind("file",0)==0){int n=1;try{n=std::stoi(kv.first.substr(4));}catch(...){ }files.push_back({n,kv.second});}std::sort(files.begin(),files.end());if(files.empty())return"ERROR: use -file1=...";SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream disk(mp->path,std::ios::binary|std::ios::in);std::string out;for(size_t i=0;i<files.size();i++){int idx=resolvePath(*mp,files[i].second);Inode inode{};if(idx<0||!readInode(disk,sb,idx,inode)||inode.i_type!='1')return"ERROR: archivo no existe: "+files[i].second;if(!canRead(inode))return"ERROR: sin permiso de lectura: "+files[i].second;bool ok=false;auto s=readFile(*mp,files[i].second,&ok);if(!ok)return"ERROR: no se pudo leer: "+files[i].second;out+=s;if(i+1<files.size())out+="\n";}return out;}
std::string ExtreamFS::cmdMkdir(const Command& c){if(!session.active)return"ERROR: requiere sesión";auto*mp=findMount(session.mountId);std::string p=getp(c,"path");if(p.empty())return"ERROR: -path obligatorio";std::string e;return createDirectory(*mp,p,hasFlag(c,"p"),e)?"OK: carpeta creada":"ERROR: "+e;}
std::string ExtreamFS::cmdMkfile(const Command& c){if(!session.active)return"ERROR: requiere sesión";auto*mp=findMount(session.mountId);std::string p=getp(c,"path");if(p.empty())return"ERROR: -path obligatorio";std::string content;if(c.params.count("cont")){std::ifstream in(getp(c,"cont"),std::ios::binary);if(!in)return"ERROR: archivo -cont no existe";content.assign((std::istreambuf_iterator<char>(in)),{});}else{long long sz=0;if(c.params.count("size"))sz=std::stoll(getp(c,"size"));if(sz<0)return"ERROR: size negativo";content.reserve(sz);for(long long i=0;i<sz;i++)content.push_back('0'+(i%10));}std::string e;return createFile(*mp,p,content,hasFlag(c,"r"),e)?"OK: archivo creado":"ERROR: "+e;}

std::string ExtreamFS::cmdRemove(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string path=getp(c,"path");
    if(path.empty())return"ERROR: -path obligatorio";
    if(path=="/"||path=="/users.txt")return"ERROR: no se puede eliminar una ruta protegida";
    auto*mp=findMount(session.mountId);int parent=-1;std::string leaf;int target=resolvePath(*mp,path,&parent,&leaf);if(target<0||parent<0)return"ERROR: ruta no existe";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);Inode parentInode{};if(!readInode(f,sb,parent,parentInode)||!canWrite(parentInode))return"ERROR: sin permiso de escritura en la carpeta padre";
    std::vector<std::pair<int,Inode>> postorder;std::set<int> visited;std::string validationError;
    std::function<bool(int)> inspect=[&](int index){
        if(visited.count(index))return true;
        visited.insert(index);Inode inode{};
        if(!readInode(f,sb,index,inode)){validationError="no se pudo leer el árbol";return false;}
        if(!canWrite(inode)){validationError="sin permiso de escritura en "+path;return false;}
        if(inode.i_type=='0')for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);if(!f){validationError="no se pudo leer una carpeta";return false;}for(const auto& entry:folder.b_content){auto name=fixed(entry.b_name,12);if(entry.b_inodo>=0&&name!="."&&name!=".."&&!inspect(entry.b_inodo))return false;}}
        postorder.push_back({index,inode});return true;
    };
    if(!inspect(target))return"ERROR: "+validationError;
    bool removedEntry=false;
    for(int blockIndex=0;blockIndex<12&&!removedEntry;blockIndex++)if(parentInode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(auto& entry:folder.b_content)if(entry.b_inodo==target&&fixed(entry.b_name,12)==leaf){entry.b_inodo=-1;std::memset(entry.b_name,0,sizeof(entry.b_name));f.clear();f.seekp(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.write((char*)&folder,64);removedEntry=true;break;}}
    if(!removedEntry)return"ERROR: no se encontró la referencia en la carpeta padre";
    for(auto& item:postorder){int index=item.first;Inode inode=item.second;if(inode.i_type=='1')releaseFileBlocks(f,sb,inode);else for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){char free='0';f.clear();f.seekp(sb.s_bm_block_start+inode.i_block[blockIndex]);f.write(&free,1);sb.s_free_blocks_count++;}char free='0';f.clear();f.seekp(sb.s_bm_inode_start+index);f.write(&free,1);sb.s_free_inodes_count++;}
    parentInode.i_mtime=std::time(nullptr);writeInode(f,sb,parent,parentInode);saveSB(f,*mp,sb);return"OK: ruta eliminada";
}

std::string ExtreamFS::cmdRename(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string path=getp(c,"path"),newName=getp(c,"name");
    if(path.empty()||newName.empty())return"ERROR: -path y -name obligatorios";
    if(newName.size()>12||newName.find('/')!=std::string::npos)return"ERROR: nombre inválido; máximo 12 caracteres";
    if(path=="/"||path=="/users.txt")return"ERROR: no se puede renombrar una ruta protegida";
    auto*mp=findMount(session.mountId);int parent=-1;std::string oldName;int target=resolvePath(*mp,path,&parent,&oldName);if(target<0||parent<0)return"ERROR: ruta no existe";SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);Inode parentInode{},targetInode{};if(!readInode(f,sb,parent,parentInode)||!readInode(f,sb,target,targetInode))return"ERROR: no se pudieron leer los inodos";if(!canWrite(parentInode)||!canWrite(targetInode))return"ERROR: sin permiso de escritura";
    for(int blockIndex=0;blockIndex<12;blockIndex++)if(parentInode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(const auto& entry:folder.b_content)if(entry.b_inodo>=0&&fixed(entry.b_name,12)==newName)return"ERROR: ya existe un elemento con ese nombre";}
    for(int blockIndex=0;blockIndex<12;blockIndex++)if(parentInode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(auto& entry:folder.b_content)if(entry.b_inodo==target&&fixed(entry.b_name,12)==oldName){copyFixed(entry.b_name,12,newName);f.clear();f.seekp(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.write((char*)&folder,64);parentInode.i_mtime=std::time(nullptr);writeInode(f,sb,parent,parentInode);return"OK: ruta renombrada";}}
    return"ERROR: no se encontró la referencia en la carpeta padre";
}

std::string ExtreamFS::cmdCopy(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string sourcePath=getp(c,"path"),destinationPath=getp(c,"destino");
    if(sourcePath.empty()||destinationPath.empty())return"ERROR: -path y -destino obligatorios";
    if(sourcePath=="/")return"ERROR: no se puede copiar la raíz";
    while(destinationPath.size()>1&&destinationPath.back()=='/')destinationPath.pop_back();
    if(destinationPath==sourcePath||destinationPath.rfind(sourcePath+"/",0)==0)return"ERROR: el destino no puede estar dentro del origen";
    auto*mp=findMount(session.mountId);int source=resolvePath(*mp,sourcePath),destination=resolvePath(*mp,destinationPath);if(source<0)return"ERROR: origen no existe";if(destination<0)return"ERROR: destino no existe";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);Inode sourceInode{},destinationInode{};if(!readInode(f,sb,source,sourceInode)||!readInode(f,sb,destination,destinationInode)||destinationInode.i_type!='0')return"ERROR: destino debe ser una carpeta";if(!canRead(sourceInode))return"ERROR: sin permiso de lectura en el origen";if(!canWrite(destinationInode))return"ERROR: sin permiso de escritura en el destino";
    std::string baseName=fs::path(sourcePath).filename().string();std::string targetPath=destinationPath=="/"?"/"+baseName:destinationPath+"/"+baseName;if(resolvePath(*mp,targetPath)>=0)return"ERROR: ya existe un elemento con ese nombre en el destino";
    int copied=0,skipped=0;std::string failure;
    std::function<bool(int,const std::string&,const std::string&,bool)> clone=[&](int index,const std::string& from,const std::string& to,bool required){
        Inode inode{};if(!readInode(f,sb,index,inode)){failure="no se pudo leer "+from;return false;}if(!canRead(inode)){if(required){failure="sin permiso de lectura en "+from;return false;}skipped++;return true;}
        if(inode.i_type=='1'){bool ok=false;std::string content=readFile(*mp,from,&ok);if(!ok){failure="no se pudo leer "+from;return false;}std::string error;if(!createFile(*mp,to,content,false,error)){failure=error;return false;}copied++;return true;}
        std::string error;if(!createDirectory(*mp,to,false,error)){failure=error;return false;}copied++;
        std::vector<std::pair<std::string,int>> children;
        for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);if(!f){failure="no se pudo leer la carpeta "+from;return false;}for(const auto& entry:folder.b_content){auto name=fixed(entry.b_name,12);if(entry.b_inodo>=0&&name!="."&&name!="..")children.push_back({name,entry.b_inodo});}}
        for(const auto& child:children){std::string childFrom=from=="/"?"/"+child.first:from+"/"+child.first;std::string childTo=to=="/"?"/"+child.first:to+"/"+child.first;if(!clone(child.second,childFrom,childTo,false))return false;}
        return true;
    };
    if(!clone(source,sourcePath,targetPath,true))return"ERROR: copia incompleta: "+failure;
    return"OK: copia completada; elementos copiados="+std::to_string(copied)+", omitidos sin lectura="+std::to_string(skipped);
}

std::string ExtreamFS::cmdMove(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string sourcePath=getp(c,"path"),destinationPath=getp(c,"destino");
    if(sourcePath.empty()||destinationPath.empty())return"ERROR: -path y -destino obligatorios";
    if(sourcePath=="/"||sourcePath=="/users.txt")return"ERROR: no se puede mover una ruta protegida";
    while(destinationPath.size()>1&&destinationPath.back()=='/')destinationPath.pop_back();
    if(destinationPath==sourcePath||destinationPath.rfind(sourcePath+"/",0)==0)return"ERROR: el destino no puede estar dentro del origen";
    auto*mp=findMount(session.mountId);int sourceParent=-1;std::string sourceName;int source=resolvePath(*mp,sourcePath,&sourceParent,&sourceName),destination=resolvePath(*mp,destinationPath);if(source<0)return"ERROR: origen no existe";if(destination<0)return"ERROR: destino no existe";if(sourceParent<0)return"ERROR: origen inválido";
    std::string targetPath=destinationPath=="/"?"/"+sourceName:destinationPath+"/"+sourceName;if(resolvePath(*mp,targetPath)>=0)return"ERROR: ya existe un elemento con ese nombre en el destino";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);Inode sourceInode{},sourceParentInode{},destinationInode{};if(!readInode(f,sb,source,sourceInode)||!readInode(f,sb,sourceParent,sourceParentInode)||!readInode(f,sb,destination,destinationInode)||destinationInode.i_type!='0')return"ERROR: destino debe ser una carpeta";if(!canWrite(sourceInode)||!canWrite(sourceParentInode)||!canWrite(destinationInode))return"ERROR: sin permiso de escritura en origen o destino";
    if(!appendChild(f,sb,destination,sourceName,source))return"ERROR: no hay espacio en la carpeta destino";
    auto clearReference=[&](int parentIndex,int childIndex,const std::string& name){Inode parentInode{};if(!readInode(f,sb,parentIndex,parentInode))return false;for(int blockIndex=0;blockIndex<12;blockIndex++)if(parentInode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(auto& entry:folder.b_content)if(entry.b_inodo==childIndex&&fixed(entry.b_name,12)==name){entry.b_inodo=-1;std::memset(entry.b_name,0,sizeof(entry.b_name));f.clear();f.seekp(sb.s_block_start+(long long)parentInode.i_block[blockIndex]*64);f.write((char*)&folder,64);parentInode.i_mtime=std::time(nullptr);writeInode(f,sb,parentIndex,parentInode);return true;}}return false;};
    if(!clearReference(sourceParent,source,sourceName)){clearReference(destination,source,sourceName);return"ERROR: no se pudo retirar la referencia del origen";}
    if(sourceInode.i_type=='0')for(int blockIndex=0;blockIndex<12;blockIndex++)if(sourceInode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)sourceInode.i_block[blockIndex]*64);f.read((char*)&folder,64);bool changed=false;for(auto& entry:folder.b_content)if(fixed(entry.b_name,12)==".."){entry.b_inodo=destination;changed=true;break;}if(changed){f.clear();f.seekp(sb.s_block_start+(long long)sourceInode.i_block[blockIndex]*64);f.write((char*)&folder,64);break;}}
    saveSB(f,*mp,sb);return"OK: ruta movida";
}

std::string ExtreamFS::cmdFind(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string path=getp(c,"path"),pattern=getp(c,"name");
    if(path.empty()||pattern.empty())return"ERROR: -path y -name obligatorios";
    auto*mp=findMount(session.mountId);int start=resolvePath(*mp,path);if(start<0)return"ERROR: ruta no existe";SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in);Inode startInode{};if(!readInode(f,sb,start,startInode)||!canRead(startInode))return"ERROR: sin permiso de lectura en la ruta inicial";
    std::string regexText="^";for(char ch:pattern){if(ch=='*')regexText+=".+";else if(ch=='?')regexText+='.';else{if(std::string(".^$|()[]{}+\\").find(ch)!=std::string::npos)regexText+='\\';regexText+=ch;}}regexText+='$';std::regex matcher(regexText,std::regex::ECMAScript|std::regex::icase);
    std::vector<std::string> matches;std::set<int> visited;
    std::function<void(int,const std::string&)> walk=[&](int index,const std::string& current){if(visited.count(index))return;visited.insert(index);Inode inode{};if(!readInode(f,sb,index,inode)||!canRead(inode))return;std::string name=fs::path(current).filename().string();if(current=="/")name="/";if(std::regex_match(name,matcher))matches.push_back(current);if(inode.i_type!='0')return;for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(const auto& entry:folder.b_content){auto childName=fixed(entry.b_name,12);if(entry.b_inodo>=0&&childName!="."&&childName!=".."){std::string childPath=current=="/"?"/"+childName:current+"/"+childName;walk(entry.b_inodo,childPath);}}}};
    walk(start,path);if(matches.empty())return"No se encontraron coincidencias";std::ostringstream out;out<<path<<"\n";for(size_t i=0;i<matches.size();i++)out<<(i+1==matches.size()?"└── ":"├── ")<<matches[i]<<"\n";return out.str();
}

std::string ExtreamFS::cmdChown(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string path=getp(c,"path"),user=getp(c,"usuario",getp(c,"user"));
    if(path.empty()||user.empty())return"ERROR: -path y -usuario obligatorios";
    auto*mp=findMount(session.mountId);int target=resolvePath(*mp,path);if(target<0)return"ERROR: ruta no existe";
    std::string users=usersRead(*mp),targetGroup;int targetUid=-1,targetGid=-1;for(const auto& line:split(users,'\n')){auto values=split(line,',');if(values.size()>=5&&values[0]!="0"&&trim(values[1])=="U"&&trim(values[3])==user){targetUid=std::stoi(values[0]);targetGroup=trim(values[2]);break;}}if(targetUid<0)return"ERROR: usuario destino no existe";for(const auto& line:split(users,'\n')){auto values=split(line,',');if(values.size()>=3&&values[0]!="0"&&trim(values[1])=="G"&&trim(values[2])==targetGroup){targetGid=std::stoi(values[0]);break;}}if(targetGid<0)return"ERROR: grupo del usuario destino no existe";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);std::vector<int> targets;std::set<int> visited;std::string error;
    std::function<bool(int)> collect=[&](int index){if(visited.count(index))return true;visited.insert(index);Inode inode{};if(!readInode(f,sb,index,inode)){error="no se pudo leer el árbol";return false;}if(session.user!="root"&&inode.i_uid!=session.uid){error="solo root o el propietario pueden cambiar el dueño";return false;}targets.push_back(index);if(hasFlag(c,"r")&&inode.i_type=='0')for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(const auto& entry:folder.b_content){auto name=fixed(entry.b_name,12);if(entry.b_inodo>=0&&name!="."&&name!=".."&&!collect(entry.b_inodo))return false;}}return true;};
    if(!collect(target))return"ERROR: "+error;
    for(int index:targets){Inode inode{};readInode(f,sb,index,inode);inode.i_uid=targetUid;inode.i_gid=targetGid;inode.i_mtime=std::time(nullptr);writeInode(f,sb,index,inode);}
    return"OK: propietario actualizado en "+std::to_string(targets.size())+" elemento(s)";
}

std::string ExtreamFS::cmdChmod(const Command& c){
    if(!session.active)return"ERROR: requiere sesión";
    std::string path=getp(c,"path"),ugo=getp(c,"ugo");
    if(path.empty()||ugo.empty())return"ERROR: -path y -ugo obligatorios";
    if(ugo.size()!=3||!std::all_of(ugo.begin(),ugo.end(),[](char value){return value>='0'&&value<='7';}))return"ERROR: -ugo debe contener tres dígitos entre 0 y 7";
    auto*mp=findMount(session.mountId);int target=resolvePath(*mp,path);if(target<0)return"ERROR: ruta no existe";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);std::vector<int> targets;std::set<int> visited;std::string error;
    std::function<bool(int)> collect=[&](int index){
        if(visited.count(index))return true;
        visited.insert(index);Inode inode{};
        if(!readInode(f,sb,index,inode)){error="no se pudo leer el árbol";return false;}
        if(session.user!="root"&&inode.i_uid!=session.uid){error="solo root o el propietario pueden cambiar permisos";return false;}
        targets.push_back(index);
        if(hasFlag(c,"r")&&inode.i_type=='0')for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(const auto& entry:folder.b_content){auto name=fixed(entry.b_name,12);if(entry.b_inodo>=0&&name!="."&&name!=".."&&!collect(entry.b_inodo))return false;}}
        return true;
    };
    if(!collect(target))return"ERROR: "+error;
    for(int index:targets){Inode inode{};readInode(f,sb,index,inode);std::memcpy(inode.i_perm,ugo.data(),3);inode.i_mtime=std::time(nullptr);writeInode(f,sb,index,inode);}
    return"OK: permisos actualizados en "+std::to_string(targets.size())+" elemento(s)";
}

std::string ExtreamFS::cmdJournaling(const Command& c){
    std::string id=getp(c,"id");if(id.empty())return"ERROR: -id obligatorio";auto*mp=findMount(id);if(!mp)return"ERROR: id no montado";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";if(sb.s_filesystem_type!=3)return"ERROR: journaling solo está disponible en EXT3";
    std::ifstream f(mp->path,std::ios::binary);if(!f)return"ERROR: no se pudo abrir disco";Journaling journal{};f.seekg((long long)mp->start+sizeof(SuperBlock));f.read((char*)&journal,sizeof(journal));if(!f||journal.j_count<0||journal.j_count>JOURNAL_CAPACITY)return"ERROR: journal inválido";
    std::ostringstream out;out<<"Journal EXT3 ("<<journal.j_count<<"/"<<JOURNAL_CAPACITY<<")\n";
    out<<"# | Operación | Ruta | Contenido | Fecha\n";
    for(int i=0;i<journal.j_count;i++){
        const auto& entry=journal.j_content[i];
        out<<i+1<<" | "<<fixed(entry.i_operation,sizeof(entry.i_operation))<<" | "<<fixed(entry.i_path,sizeof(entry.i_path))<<" | "<<fixed(entry.i_content,sizeof(entry.i_content))<<" | "<<timeText((std::time_t)entry.i_date)<<"\n";
    }
    return out.str();
}

std::string ExtreamFS::cmdLoss(const Command& c){
    std::string id=getp(c,"id");if(id.empty())return"ERROR: -id obligatorio";auto*mp=findMount(id);if(!mp)return"ERROR: id no montado";
    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";if(sb.s_filesystem_type!=3)return"ERROR: loss solo está disponible en EXT3";
    const long long partitionEnd=(long long)mp->start+mp->size;
    const long long dataEnd=(long long)sb.s_block_start+(long long)sb.s_blocks_count*64;
    if(sb.s_bm_inode_start<(int)(mp->start+sizeof(SuperBlock)+sizeof(Journaling))||sb.s_bm_block_start<sb.s_bm_inode_start||sb.s_inode_start<sb.s_bm_block_start||sb.s_block_start<sb.s_inode_start||dataEnd>partitionEnd)return"ERROR: layout EXT3 inválido; se canceló loss";
    std::fstream f(mp->path,std::ios::binary|std::ios::in|std::ios::out);if(!f)return"ERROR: no se pudo abrir disco";
    auto bitmapText=[&](long long start,int count){
        std::string out;out.reserve(count+(count/20)+1);f.clear();f.seekg(start);
        for(int i=0;i<count;i++){char bit=0;f.read(&bit,1);if(!f)return std::string("<error de lectura>");out+=(bit=='1'?'1':'0');if((i+1)%20==0)out+='\n';else if(i+1<count)out+=' ';}
        return out;
    };
    const std::string inodeBefore=bitmapText(sb.s_bm_inode_start,sb.s_inodes_count);
    const std::string blockBefore=bitmapText(sb.s_bm_block_start,sb.s_blocks_count);
    appendJournal(*mp,"loss","/","loss -id="+id);
    auto wipe=[&](long long start,long long bytes){
        std::array<char,4096> zeros{};f.clear();f.seekp(start);
        while(bytes>0){const auto amount=(std::streamsize)std::min<long long>(bytes,zeros.size());f.write(zeros.data(),amount);if(!f)return false;bytes-=amount;}
        return true;
    };
    if(!wipe(sb.s_bm_inode_start,sb.s_inodes_count)||!wipe(sb.s_bm_block_start,sb.s_blocks_count)||!wipe(sb.s_inode_start,(long long)sb.s_inodes_count*sizeof(Inode))||!wipe(sb.s_block_start,(long long)sb.s_blocks_count*64))return"ERROR: no se pudo completar loss";
    f.flush();
    const std::string inodeAfter=bitmapText(sb.s_bm_inode_start,sb.s_inodes_count);
    const std::string blockAfter=bitmapText(sb.s_bm_block_start,sb.s_blocks_count);
    std::ostringstream out;out<<"OK: pérdida simulada en "<<id<<"\n"
        <<"Bitmap de inodos antes:\n"<<inodeBefore<<"\nBitmap de inodos después:\n"<<inodeAfter
        <<"\nBitmap de bloques antes:\n"<<blockBefore<<"\nBitmap de bloques después:\n"<<blockAfter;
    return out.str();
}

std::string ExtreamFS::graphvizRender(const std::string& dot,const std::string& out){fs::path p(out);if(p.has_parent_path())fs::create_directories(p.parent_path());std::string ext=lower(p.extension().string());if(ext==".txt"){std::ofstream f(out);f<<dot;return"OK: reporte creado: "+out;}std::string fmt=ext.empty()?"png":ext.substr(1);if(fmt=="jpg")fmt="jpg";std::string dotfile=out+".dot";{std::ofstream f(dotfile);f<<dot;}std::string cmd="dot -T"+fmt+" \""+dotfile+"\" -o \""+out+"\"";int rc=std::system(cmd.c_str());fs::remove(dotfile);return rc==0?"OK: reporte creado: "+out:"ERROR: Graphviz falló (¿está instalado?)";}

static std::string timeText(std::time_t value){
    if(value==0)return "-";
    std::tm tm{};localtime_r(&value,&tm);std::ostringstream out;out<<std::put_time(&tm,"%d/%m/%Y %H:%M:%S");return out.str();
}

static std::string permissionText(const char perm[3],char type){
    std::string out=type=='0'?"d":"-";
    for(int i=0;i<3;i++){int value=(perm[i]>='0'&&perm[i]<='7')?perm[i]-'0':0;out+=(value&4)?'r':'-';out+=(value&2)?'w':'-';out+=(value&1)?'x':'-';}
    return out;
}

std::string ExtreamFS::stateJson(){
    std::vector<std::string> disks=knownDisks;for(const auto& mounted:mounts)if(std::find(disks.begin(),disks.end(),mounted.path)==disks.end())disks.push_back(mounted.path);
    std::ostringstream out;out<<"{\"ok\":true,\"session\":{\"active\":"<<(session.active?"true":"false")<<",\"user\":"<<jsonQuoted(session.user)<<",\"group\":"<<jsonQuoted(session.group)<<",\"mountId\":"<<jsonQuoted(session.mountId)<<"},\"mounts\":[";
    for(size_t i=0;i<mounts.size();i++){if(i)out<<',';const auto& mounted=mounts[i];out<<"{\"id\":"<<jsonQuoted(mounted.id)<<",\"name\":"<<jsonQuoted(mounted.name)<<",\"path\":"<<jsonQuoted(mounted.path)<<",\"start\":"<<mounted.start<<",\"size\":"<<mounted.size<<'}';}
    out<<"],\"disks\":[";bool firstDisk=true;
    for(const auto& path:disks){MBR mbr{};if(!readMBR(path,mbr))continue;if(!firstDisk)out<<',';firstDisk=false;out<<"{\"path\":"<<jsonQuoted(path)<<",\"size\":"<<mbr.mbr_tamano<<",\"fit\":"<<jsonQuoted(std::string(1,mbr.dsk_fit))<<",\"created\":"<<jsonQuoted(timeText(mbr.mbr_fecha_creacion))<<",\"partitions\":[";bool firstPartition=true;
        auto emitPartition=[&](const std::string& name,char type,char fit,int start,int size){if(!firstPartition)out<<',';firstPartition=false;std::string mountedId;for(const auto& mounted:mounts)if(mounted.path==path&&mounted.name==name){mountedId=mounted.id;break;}out<<"{\"name\":"<<jsonQuoted(name)<<",\"type\":"<<jsonQuoted(std::string(1,type))<<",\"fit\":"<<jsonQuoted(std::string(1,fit))<<",\"start\":"<<start<<",\"size\":"<<size<<",\"mounted\":"<<(!mountedId.empty()?"true":"false")<<",\"id\":"<<jsonQuoted(mountedId)<<'}';};
        for(const auto& partition:mbr.mbr_partitions)if(partition.part_s>0){emitPartition(fixed(partition.part_name,16),partition.part_type,partition.part_fit,partition.part_start,partition.part_s);if(partition.part_type=='E'){std::ifstream file(path,std::ios::binary);int position=partition.part_start;EBR entry{};while(position>=partition.part_start&&position<partition.part_start+partition.part_s){file.clear();file.seekg(position);file.read((char*)&entry,sizeof(entry));if(!file||entry.part_s<=0)break;emitPartition(fixed(entry.part_name,16),'L',entry.part_fit,entry.part_start,entry.part_s);if(entry.part_next<0)break;position=entry.part_next;}}}
        out<<"]}";
    }
    out<<"]}";return out.str();
}

std::string ExtreamFS::explorerJson(const std::string& requestedPath){
    auto error=[](const std::string& message){return std::string("{\"ok\":false,\"error\":")+jsonQuoted(message)+"}";};
    if(!session.active)return error("Se requiere una sesión activa");
    auto*mp=findMount(session.mountId);if(!mp)return error("La partición de la sesión no está montada");std::string path=requestedPath.empty()?"/":requestedPath;int target=resolvePath(*mp,path);if(target<0)return error("La ruta no existe");SuperBlock sb{};if(!loadSB(*mp,sb))return error("La partición no está formateada");std::fstream f(mp->path,std::ios::binary|std::ios::in);Inode inode{};if(!readInode(f,sb,target,inode))return error("No se pudo leer el inodo");if(!canRead(inode))return error("Sin permiso de lectura");
    if(inode.i_type=='1'){bool ok=false;std::string content=readFile(*mp,path,&ok);if(!ok)return error("No se pudo leer el archivo");return std::string("{\"ok\":true,\"kind\":\"file\",\"path\":")+jsonQuoted(path)+",\"size\":"+std::to_string(inode.i_s)+",\"permissions\":"+jsonQuoted(permissionText(inode.i_perm,inode.i_type))+",\"content\":"+jsonQuoted(content)+"}";}
    std::map<int,std::string> users,groups;for(const auto& line:split(usersRead(*mp),'\n')){auto values=split(line,',');if(values.size()>=3&&values[0]!="0"&&trim(values[1])=="G")groups[std::stoi(values[0])]=trim(values[2]);if(values.size()>=5&&values[0]!="0"&&trim(values[1])=="U")users[std::stoi(values[0])]=trim(values[3]);}
    struct EntryView{std::string name;int index;Inode inode;};std::vector<EntryView> entries;
    for(int blockIndex=0;blockIndex<12;blockIndex++)if(inode.i_block[blockIndex]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)inode.i_block[blockIndex]*64);f.read((char*)&folder,64);for(const auto& entry:folder.b_content){auto name=fixed(entry.b_name,12);if(entry.b_inodo>=0&&name!="."&&name!=".."){Inode child{};if(readInode(f,sb,entry.b_inodo,child))entries.push_back({name,entry.b_inodo,child});}}}
    std::sort(entries.begin(),entries.end(),[](const EntryView& a,const EntryView& b){if(a.inode.i_type!=b.inode.i_type)return a.inode.i_type<b.inode.i_type;return lower(a.name)<lower(b.name);});
    std::ostringstream out;out<<"{\"ok\":true,\"kind\":\"directory\",\"path\":"<<jsonQuoted(path)<<",\"entries\":[";for(size_t i=0;i<entries.size();i++){if(i)out<<',';const auto& entry=entries[i];std::string childPath=path=="/"?"/"+entry.name:path+"/"+entry.name;out<<"{\"name\":"<<jsonQuoted(entry.name)<<",\"path\":"<<jsonQuoted(childPath)<<",\"type\":"<<jsonQuoted(entry.inode.i_type=='0'?"directory":"file")<<",\"size\":"<<entry.inode.i_s<<",\"permissions\":"<<jsonQuoted(permissionText(entry.inode.i_perm,entry.inode.i_type))<<",\"owner\":"<<jsonQuoted(users.count(entry.inode.i_uid)?users[entry.inode.i_uid]:std::to_string(entry.inode.i_uid))<<",\"group\":"<<jsonQuoted(groups.count(entry.inode.i_gid)?groups[entry.inode.i_gid]:std::to_string(entry.inode.i_gid))<<",\"modified\":"<<jsonQuoted(timeText(entry.inode.i_mtime))<<",\"readable\":"<<(canRead(entry.inode)?"true":"false")<<'}';}out<<"]}";return out.str();
}

std::string ExtreamFS::journalJson(const std::string& id){
    auto error=[](const std::string& message){return std::string("{\"ok\":false,\"error\":")+jsonQuoted(message)+"}";};auto*mp=findMount(id);if(!mp)return error("ID no montado");SuperBlock sb{};if(!loadSB(*mp,sb))return error("Partición no formateada");if(sb.s_filesystem_type!=3)return error("El journal solo existe en EXT3");std::ifstream f(mp->path,std::ios::binary);Journaling journal{};f.seekg((long long)mp->start+sizeof(SuperBlock));f.read((char*)&journal,sizeof(journal));if(!f||journal.j_count<0||journal.j_count>JOURNAL_CAPACITY)return error("Journal inválido");std::ostringstream out;out<<"{\"ok\":true,\"id\":"<<jsonQuoted(id)<<",\"count\":"<<journal.j_count<<",\"capacity\":"<<JOURNAL_CAPACITY<<",\"entries\":[";for(int i=0;i<journal.j_count;i++){if(i)out<<',';const auto& entry=journal.j_content[i];out<<"{\"operation\":"<<jsonQuoted(fixed(entry.i_operation,sizeof(entry.i_operation)))<<",\"path\":"<<jsonQuoted(fixed(entry.i_path,sizeof(entry.i_path)))<<",\"content\":"<<jsonQuoted(fixed(entry.i_content,sizeof(entry.i_content)))<<",\"date\":"<<jsonQuoted(timeText((std::time_t)entry.i_date))<<'}';}out<<"]}";return out.str();
}

std::string ExtreamFS::cmdRep(const Command& c){
    std::string id=getp(c,"id"),name=lower(getp(c,"name")),out=getp(c,"path");
    if(id.empty()||name.empty()||out.empty())return"ERROR: -id -name -path obligatorios";
    static const std::set<std::string> valid={"mbr","disk","inode","block","bm_inode","bm_block","tree","sb","file","ls"};
    if(!valid.count(name))return"ERROR: reporte no reconocido";
    auto*mp=findMount(id);if(!mp)return"ERROR: id no montado";

    if(name=="mbr"||name=="disk"){
        MBR m{};if(!readMBR(mp->path,m))return"ERROR: no se pudo leer el MBR";
        std::vector<Partition> parts;for(auto&p:m.mbr_partitions)if(p.part_s>0)parts.push_back(p);
        std::sort(parts.begin(),parts.end(),[](const Partition&a,const Partition&b){return a.part_start<b.part_start;});
        if(name=="mbr"){
            std::ostringstream d;d<<"digraph G{rankdir=TB;node[shape=plaintext fontname=Helvetica];mbr[label=< <table border='1' cellborder='1' cellspacing='0'>"
              <<"<tr><td colspan='2' bgcolor='#1f4e78'><font color='white'><b>MASTER BOOT RECORD</b></font></td></tr>"
              <<"<tr><td>mbr_tamano</td><td>"<<m.mbr_tamano<<"</td></tr><tr><td>mbr_fecha_creacion</td><td>"<<timeText(m.mbr_fecha_creacion)<<"</td></tr>"
              <<"<tr><td>mbr_dsk_signature</td><td>"<<m.mbr_dsk_signature<<"</td></tr><tr><td>dsk_fit</td><td>"<<m.dsk_fit<<"</td></tr>";
            int pi=1;for(const auto&p:parts)d<<"<tr><td colspan='2' bgcolor='#d9eaf7'><b>PARTICION "<<pi++<<"</b></td></tr>"
              <<"<tr><td>part_status</td><td>"<<p.part_status<<"</td></tr><tr><td>part_type</td><td>"<<p.part_type<<"</td></tr><tr><td>part_fit</td><td>"<<p.part_fit<<"</td></tr>"
              <<"<tr><td>part_start</td><td>"<<p.part_start<<"</td></tr><tr><td>part_s</td><td>"<<p.part_s<<"</td></tr><tr><td>part_name</td><td>"<<html(fixed(p.part_name,16))<<"</td></tr>"
              <<"<tr><td>part_correlative</td><td>"<<p.part_correlative<<"</td></tr><tr><td>part_id</td><td>"<<html(fixed(p.part_id,4))<<"</td></tr>";
            d<<"</table> >];";
            std::ifstream file(mp->path,std::ios::binary);int ei=0;for(const auto&p:parts)if(p.part_type=='E'){
                int pos=p.part_start;while(pos>=p.part_start&&pos<p.part_start+p.part_s){EBR e{};file.clear();file.seekg(pos);file.read((char*)&e,sizeof(e));if(!file||e.part_s<=0)break;d<<"ebr"<<ei<<"[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='2' bgcolor='#e2f0d9'><b>EBR "<<ei+1<<"</b></td></tr>"
                  <<"<tr><td>part_mount</td><td>"<<e.part_mount<<"</td></tr><tr><td>part_fit</td><td>"<<e.part_fit<<"</td></tr><tr><td>part_start</td><td>"<<e.part_start<<"</td></tr>"
                  <<"<tr><td>part_s</td><td>"<<e.part_s<<"</td></tr><tr><td>part_next</td><td>"<<e.part_next<<"</td></tr><tr><td>part_name</td><td>"<<html(fixed(e.part_name,16))<<"</td></tr></table> >];";
                if(ei==0)d<<"mbr->ebr0;";else d<<"ebr"<<ei-1<<"->ebr"<<ei<<";";ei++;if(e.part_next<0)break;pos=e.part_next;
                }
            }d<<"}";return graphvizRender(d.str(),out);
        }
        struct Segment{int start;int size;std::string label;char type;};std::vector<Segment> segs;int cursor=0;
        if(sizeof(MBR)>0){segs.push_back({0,(int)sizeof(MBR),"MBR",'M'});cursor=sizeof(MBR);}
        for(const auto&p:parts){if(p.part_start>cursor)segs.push_back({cursor,p.part_start-cursor,"LIBRE",'F'});segs.push_back({p.part_start,p.part_s,fixed(p.part_name,16),p.part_type});cursor=p.part_start+p.part_s;}
        if(cursor<m.mbr_tamano)segs.push_back({cursor,m.mbr_tamano-cursor,"LIBRE",'F'});

        auto extendedSegments=[&](const Segment& extended){
            std::vector<Segment> internal;
            std::ifstream diskFile(mp->path,std::ios::binary);
            int ebrPosition=extended.start;
            int internalCursor=extended.start;
            const int extendedEnd=extended.start+extended.size;
            while(ebrPosition>=extended.start&&ebrPosition+(int)sizeof(EBR)<=extendedEnd){
                EBR e{};diskFile.clear();diskFile.seekg(ebrPosition);diskFile.read((char*)&e,sizeof(e));
                if(!diskFile)break;
                if(ebrPosition>internalCursor)internal.push_back({internalCursor,ebrPosition-internalCursor,"LIBRE",'F'});
                internal.push_back({ebrPosition,(int)sizeof(EBR),"EBR",'B'});
                int afterEbr=ebrPosition+(int)sizeof(EBR);
                if(e.part_s<=0){internalCursor=afterEbr;break;}
                if(e.part_start>afterEbr)internal.push_back({afterEbr,e.part_start-afterEbr,"LIBRE",'F'});
                if(e.part_start<afterEbr||e.part_start+e.part_s>extendedEnd)break;
                internal.push_back({e.part_start,e.part_s,fixed(e.part_name,16),'L'});
                internalCursor=e.part_start+e.part_s;
                if(e.part_next<0)break;
                if(e.part_next<internalCursor||e.part_next>=extendedEnd)break;
                ebrPosition=e.part_next;
            }
            if(internalCursor<extendedEnd)internal.push_back({internalCursor,extendedEnd-internalCursor,"LIBRE",'F'});
            return internal;
        };

        std::ostringstream d;d<<"digraph G{node[shape=plaintext fontname=Helvetica];disk[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='"<<segs.size()<<"' bgcolor='#1f4e78'><font color='white'><b>ESTRUCTURA DEL DISCO - 100%</b></font></td></tr><tr>";
        for(const auto&s:segs){
            double pct=100.0*s.size/m.mbr_tamano;
            if(s.type!='E'){
                std::string color=s.type=='F'?"#eeeeee":s.type=='P'?"#bdd7ee":"#d9e2f3";
                d<<"<td bgcolor='"<<color<<"'><b>"<<html(s.label)<<"</b><br/>"<<(s.type=='P'?"Primaria":s.type=='F'?"Espacio libre":"MBR")<<"<br/>"<<std::fixed<<std::setprecision(4)<<pct<<"%<br/>"<<s.size<<" bytes</td>";
                continue;
            }
            auto internal=extendedSegments(s);
            d<<"<td cellpadding='0'><table border='0' cellborder='1' cellspacing='0'><tr><td colspan='"<<internal.size()<<"' bgcolor='#ffe699'><b>"<<html(s.label)<<"</b><br/>Extendida<br/>"<<std::fixed<<std::setprecision(4)<<pct<<"%<br/>"<<s.size<<" bytes</td></tr><tr>";
            for(const auto&inside:internal){
                double insidePct=100.0*inside.size/m.mbr_tamano;
                std::string color=inside.type=='B'?"#f4b183":inside.type=='L'?"#fff2cc":"#eeeeee";
                std::string kind=inside.type=='B'?"EBR":inside.type=='L'?"Lógica":"Espacio libre";
                d<<"<td bgcolor='"<<color<<"'><b>"<<html(inside.label)<<"</b><br/>"<<kind<<"<br/>"<<std::fixed<<std::setprecision(4)<<insidePct<<"%<br/>"<<inside.size<<" bytes</td>";
            }
            d<<"</tr></table></td>";
        }
        d<<"</tr></table> >];}";return graphvizRender(d.str(),out);
    }

    SuperBlock sb{};if(!loadSB(*mp,sb))return"ERROR: partición no formateada";
    if(name=="bm_inode"||name=="bm_block"){
        int start=name=="bm_inode"?sb.s_bm_inode_start:sb.s_bm_block_start,n=name=="bm_inode"?sb.s_inodes_count:sb.s_blocks_count;
        if(fs::path(out).has_parent_path())fs::create_directories(fs::path(out).parent_path());
        std::ifstream f(mp->path,std::ios::binary);std::ofstream o(out);if(!o)return"ERROR: no se pudo crear el reporte";f.seekg(start);
        for(int i=0;i<n;i++){char bit='0';f.read(&bit,1);o<<(bit=='1'?'1':'0');if((i+1)%20==0)o<<'\n';else if(i+1<n)o<<' ';}return"OK: bitmap creado: "+out;
    }
    if(name=="file"){
        std::string path=getp(c,"path_file_ls");if(path.empty())return"ERROR: -path_file_ls obligatorio para FILE";bool ok=false;auto data=readFile(*mp,path,&ok);if(!ok)return"ERROR: archivo no existe";
        if(fs::path(out).has_parent_path())fs::create_directories(fs::path(out).parent_path());
        std::ofstream file(out,std::ios::binary);if(!file)return"ERROR: no se pudo crear el reporte";file<<"Archivo: "<<path<<"\n"<<data;return"OK: reporte file creado: "+out;
    }

    std::fstream f(mp->path,std::ios::binary|std::ios::in);std::ifstream bitmap(mp->path,std::ios::binary);std::vector<std::pair<int,Inode>> inodes;
    for(int i=0;i<sb.s_inodes_count;i++){char used='0';bitmap.clear();bitmap.seekg(sb.s_bm_inode_start+i);bitmap.read(&used,1);if(used=='1'){Inode ino{};if(readInode(f,sb,i,ino))inodes.push_back({i,ino});}}
    if(name=="ls"){
        std::string path=getp(c,"path_file_ls");if(path.empty())return"ERROR: -path_file_ls obligatorio para LS";int target=resolvePath(*mp,path);if(target<0)return"ERROR: ruta no existe";
        std::map<int,std::string> users,groups;for(auto&line:split(usersRead(*mp),'\n')){auto v=split(line,',');if(v.size()>=3&&v[0]!="0"&&trim(v[1])=="G")groups[std::stoi(v[0])]=trim(v[2]);if(v.size()>=5&&v[0]!="0"&&trim(v[1])=="U")users[std::stoi(v[0])]=trim(v[3]);}
        std::vector<std::pair<std::string,int>> entries;Inode base{};readInode(f,sb,target,base);
        if(base.i_type=='1')entries.push_back({fs::path(path).filename().string(),target});else for(int block:std::vector<int>(base.i_block,base.i_block+12))if(block>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)block*64);f.read((char*)&folder,64);for(auto&e:folder.b_content){auto n=fixed(e.b_name,12);if(e.b_inodo>=0&&n!="."&&n!="..")entries.push_back({n,e.b_inodo});}}
        std::ostringstream d;d<<"digraph G{node[shape=plaintext fontname=Helvetica];ls[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='8' bgcolor='#1f4e78'><font color='white'><b>LS "<<html(path)<<"</b></font></td></tr><tr><td><b>Permisos</b></td><td><b>Propietario</b></td><td><b>Grupo</b></td><td><b>Tamano</b></td><td><b>Modificacion</b></td><td><b>Creacion</b></td><td><b>Tipo</b></td><td><b>Nombre</b></td></tr>";
        for(auto&e:entries){Inode ino{};if(!readInode(f,sb,e.second,ino))continue;d<<"<tr><td>"<<permissionText(ino.i_perm,ino.i_type)<<"</td><td>"<<html(users.count(ino.i_uid)?users[ino.i_uid]:std::to_string(ino.i_uid))<<"</td><td>"<<html(groups.count(ino.i_gid)?groups[ino.i_gid]:std::to_string(ino.i_gid))<<"</td><td>"<<ino.i_s<<"</td><td>"<<timeText(ino.i_mtime)<<"</td><td>"<<timeText(ino.i_ctime)<<"</td><td>"<<(ino.i_type=='0'?"Carpeta":"Archivo")<<"</td><td>"<<html(e.first)<<"</td></tr>";}
        d<<"</table> >];}";return graphvizRender(d.str(),out);
    }

    std::ostringstream d;d<<"digraph G{rankdir=LR;node[shape=plaintext fontname=Helvetica];";
    if(name=="sb"){
        d<<"sb[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='2' bgcolor='#1f4e78'><font color='white'><b>SUPERBLOQUE</b></font></td></tr>";
        const std::vector<std::pair<std::string,long long>> fields={{"s_filesystem_type",sb.s_filesystem_type},{"s_inodes_count",sb.s_inodes_count},{"s_blocks_count",sb.s_blocks_count},{"s_free_blocks_count",sb.s_free_blocks_count},{"s_free_inodes_count",sb.s_free_inodes_count},{"s_mnt_count",sb.s_mnt_count},{"s_magic",sb.s_magic},{"s_inode_s",sb.s_inode_s},{"s_block_s",sb.s_block_s},{"s_first_ino",sb.s_first_ino},{"s_first_blo",sb.s_first_blo},{"s_bm_inode_start",sb.s_bm_inode_start},{"s_bm_block_start",sb.s_bm_block_start},{"s_inode_start",sb.s_inode_start},{"s_block_start",sb.s_block_start}};
        for(auto&field:fields)d<<"<tr><td>"<<field.first<<"</td><td>"<<field.second<<"</td></tr>";
        d<<"<tr><td>s_mtime</td><td>"<<timeText(sb.s_mtime)<<"</td></tr><tr><td>s_umtime</td><td>"<<timeText(sb.s_umtime)<<"</td></tr></table> >];}";return graphvizRender(d.str(),out);
    }

    std::map<int,char> blockTypes;std::map<int,std::vector<int>> pointerEdges;
    for(auto&item:inodes){const Inode&ino=item.second;if(ino.i_type=='0'){for(int i=0;i<12;i++)if(ino.i_block[i]>=0)blockTypes[ino.i_block[i]]='D';}else{
        for(int b:fileDataBlocks(f,sb,ino))blockTypes[b]='F';
        for(int b:filePointerBlocks(f,sb,ino))blockTypes[b]='P';
        std::function<void(int,int)> mapPointers=[&](int block,int depth){if(block<0)return;PointerBlock p{};if(!readPointerBlock(f,sb,block,p))return;for(int next:p.b_pointers)if(next>=0){pointerEdges[block].push_back(next);if(depth>1)mapPointers(next,depth-1);}};mapPointers(ino.i_block[12],1);mapPointers(ino.i_block[13],2);mapPointers(ino.i_block[14],3);
    }}
    auto emitInode=[&](int idx,const Inode&ino){d<<"i"<<idx<<"[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='2' bgcolor='#bdd7ee'><b>INODO "<<idx<<"</b></td></tr><tr><td>i_uid</td><td>"<<ino.i_uid<<"</td></tr><tr><td>i_gid</td><td>"<<ino.i_gid<<"</td></tr><tr><td>i_s</td><td>"<<ino.i_s<<"</td></tr><tr><td>i_atime</td><td>"<<timeText(ino.i_atime)<<"</td></tr><tr><td>i_ctime</td><td>"<<timeText(ino.i_ctime)<<"</td></tr><tr><td>i_mtime</td><td>"<<timeText(ino.i_mtime)<<"</td></tr>";for(int j=0;j<15;j++)d<<"<tr><td port='p"<<j<<"'>i_block["<<j<<"]</td><td>"<<ino.i_block[j]<<"</td></tr>";d<<"<tr><td>i_type</td><td>"<<ino.i_type<<"</td></tr><tr><td>i_perm</td><td>"<<std::string(ino.i_perm,3)<<"</td></tr></table> >];";};
    auto emitBlock=[&](int block,char type){if(type=='D'){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)block*64);f.read((char*)&folder,64);d<<"b"<<block<<"[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='2' bgcolor='#e2f0d9'><b>BLOQUE CARPETA "<<block<<"</b></td></tr><tr><td>Nombre</td><td>Inodo</td></tr>";for(auto&e:folder.b_content)d<<"<tr><td>"<<html(fixed(e.b_name,12))<<"</td><td>"<<e.b_inodo<<"</td></tr>";d<<"</table> >];";}else if(type=='P'){PointerBlock p{};readPointerBlock(f,sb,block,p);d<<"b"<<block<<"[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td colspan='2' bgcolor='#ffe699'><b>BLOQUE APUNTADORES "<<block<<"</b></td></tr>";for(int i=0;i<16;i++)d<<"<tr><td>b_pointer["<<i<<"]</td><td>"<<p.b_pointers[i]<<"</td></tr>";d<<"</table> >];";}else{FileBlock file{};f.clear();f.seekg(sb.s_block_start+(long long)block*64);f.read((char*)&file,64);std::string text(file.b_content,file.b_content+64);for(char&ch:text)if((unsigned char)ch<32&&ch!='\n'&&ch!='\t')ch=' ';d<<"b"<<block<<"[label=< <table border='1' cellborder='1' cellspacing='0'><tr><td bgcolor='#f4cccc'><b>BLOQUE ARCHIVO "<<block<<"</b></td></tr><tr><td>"<<html(text)<<"</td></tr></table> >];";}};
    if(name=="inode"){for(auto&item:inodes)emitInode(item.first,item.second);d<<"}";return graphvizRender(d.str(),out);}
    if(name=="block"){for(auto&item:blockTypes)emitBlock(item.first,item.second);d<<"}";return graphvizRender(d.str(),out);}
    for(auto&item:inodes)emitInode(item.first,item.second);
    for(auto&item:blockTypes)emitBlock(item.first,item.second);
    for(auto&item:inodes){int idx=item.first;const Inode&ino=item.second;for(int j=0;j<15;j++)if(ino.i_block[j]>=0)d<<"i"<<idx<<":p"<<j<<"->b"<<ino.i_block[j]<<";";if(ino.i_type=='0')for(int j=0;j<12;j++)if(ino.i_block[j]>=0){FolderBlock folder{};f.clear();f.seekg(sb.s_block_start+(long long)ino.i_block[j]*64);f.read((char*)&folder,64);for(auto&e:folder.b_content){auto n=fixed(e.b_name,12);if(e.b_inodo>=0&&n!="."&&n!="..")d<<"b"<<ino.i_block[j]<<"->i"<<e.b_inodo<<";";}}}
    for(auto&edge:pointerEdges)for(int next:edge.second)d<<"b"<<edge.first<<"->b"<<next<<";";
    d<<"}";return graphvizRender(d.str(),out);
}
