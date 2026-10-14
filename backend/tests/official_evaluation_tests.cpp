#include "extreamfs.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition,const std::string& message){
    if(!condition)throw std::runtime_error(message);
}

static void contains(const std::string& output,const std::string& expected){
    require(output.find(expected)!=std::string::npos,
            "No se encontró '"+expected+"' en: "+output);
}

static std::string run(ExtreamFS& app,const std::string& command){
    const auto output=app.executeLine(command);
    std::cout<<"> "<<command<<"\n"<<output<<"\n";
    return output;
}

int main(){
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory=fs::temp_directory_path()/("mia_p2_eval_"+std::to_string(stamp));
    const auto disk=directory/"disco.mia";
    ExtreamFS app;
    try{
        fs::create_directories(directory);
        const std::string path=" -path=\""+disk.string()+"\"";

        // Parte 1: discos, redimensionamiento, montaje y formatos.
        contains(run(app,"mkdisk -size=30 -unit=M"+path),"OK:");
        contains(run(app,"fdisk -size=8 -unit=M -name=ext2 -type=P"+path),"OK:");
        contains(run(app,"fdisk -size=10 -unit=M -name=ext3 -type=P"+path),"OK:");
        contains(run(app,"fdisk -size=1 -unit=M -name=ext2"+path),"ERROR:");
        contains(run(app,"fdisk -add=1 -unit=M -name=ext3"+path),"OK:");
        contains(run(app,"fdisk -add=-1 -unit=M -name=ext3"+path),"OK:");
        contains(run(app,"fdisk -add=-20 -unit=M -name=ext2"+path),"ERROR:");
        contains(run(app,"mount"+path+" -name=ext2"),"id=001A");
        contains(run(app,"mount"+path+" -name=ext3"),"id=002A");
        contains(run(app,"mounted"),"001A | ext2");
        contains(run(app,"mounted"),"002A | ext3");
        contains(run(app,"mkfs -id=001A -fs=2fs"),"EXT2");
        contains(run(app,"mkfs -id=002A -fs=3fs"),"EXT3");

        // Parte 2: login gráfico equivalente, carpetas, archivos y explorador.
        contains(run(app,"login -user=root -pass=123 -id=002A"),"OK:");
        contains(run(app,"mkdir -path=/evaluacion -p"),"OK:");
        contains(run(app,"mkdir -path=/evaluacion/origen"),"OK:");
        contains(run(app,"mkdir -path=/evaluacion/destino"),"OK:");
        contains(run(app,"mkdir -path=/evaluacion/movidos"),"OK:");
        contains(run(app,"mkdir -path=/evaluacion/borrar"),"OK:");
        contains(run(app,"mkfile -path=/evaluacion/origen/a.txt -size=15"),"OK:");
        contains(run(app,"mkfile -path=/evaluacion/origen/b.txt -size=70"),"OK:");
        contains(run(app,"mkfile -path=/evaluacion/borrar/temporal.txt -size=8"),"OK:");
        require(run(app,"cat -file1=/evaluacion/origen/a.txt")=="012345678901234",
                "CAT no devolvió los 15 bytes oficiales");
        contains(app.explorerJson("/evaluacion/origen"),"\"name\":\"a.txt\"");
        contains(app.explorerJson("/evaluacion/origen"),"\"name\":\"b.txt\"");

        // Parte 3: comandos nuevos y errores intencionales.
        contains(run(app,"rename -path=/evaluacion/origen/a.txt -name=nota.txt"),"OK:");
        contains(run(app,"rename -path=/evaluacion/origen/b.txt -name=nota.txt"),"ERROR:");
        contains(run(app,"copy -path=/evaluacion/origen/nota.txt -destino=/evaluacion/destino"),"OK:");
        contains(run(app,"copy -path=/evaluacion/origen/nota.txt -destino=/evaluacion/fantasma"),"ERROR:");
        contains(run(app,"move -path=/evaluacion/origen/b.txt -destino=/evaluacion/movidos"),"OK:");
        contains(run(app,"cat -file1=/evaluacion/origen/b.txt"),"ERROR:");
        require(run(app,"cat -file1=/evaluacion/movidos/b.txt")==
                    "0123456789012345678901234567890123456789012345678901234567890123456789",
                "MOVE no conservó los 70 bytes");
        const auto found=run(app,"find -path=/evaluacion -name=\"*.txt\"");
        contains(found,"/evaluacion/origen/nota.txt");
        contains(found,"/evaluacion/destino/nota.txt");
        contains(found,"/evaluacion/movidos/b.txt");
        contains(run(app,"chmod -path=/evaluacion/movidos/b.txt -ugo=640"),"OK:");
        contains(run(app,"chown -path=/evaluacion/movidos/b.txt -usuario=root"),"OK:");
        contains(run(app,"chown -path=/evaluacion/movidos/b.txt -usuario=noexiste"),"ERROR:");
        contains(run(app,"remove -path=/evaluacion/borrar/temporal.txt"),"OK:");
        contains(run(app,"remove -path=/evaluacion/borrar/temporal.txt"),"ERROR:");

        // Parte 4: journal, pérdida, logout gráfico, unmount y borrado confirmado.
        const auto journal=run(app,"journaling -id=002A");
        for(const auto* operation:{"mkdir","mkfile","rename","copy","move","remove"})
            contains(journal,operation);
        contains(app.journalJson("002A"),"\"operation\":\"move\"");
        contains(run(app,"loss -id=002A"),"OK: pérdida simulada");
        contains(app.stateJson(),"\"active\":true");
        contains(run(app,"logout"),"OK: sesión cerrada");
        contains(run(app,"unmount -id=001A"),"OK:");
        contains(run(app,"mounted"),"002A | ext3");
        contains(run(app,"unmount -id=001A"),"ERROR:");
        contains(run(app,"unmount -id=002A"),"OK:");
        contains(run(app,"mounted"),"No hay particiones montadas");
        contains(run(app,"fdisk -delete=fast -name=ext2"+path),"CONFIRMACION_REQUERIDA");
        contains(run(app,"fdisk -delete=fast -confirm -name=ext2"+path),"OK:");
        contains(run(app,"fdisk -delete=fast -name=ext2"+path),"ERROR: partición no existe");

        fs::remove_all(directory);
        std::cout<<"Las cuatro partes oficiales finalizaron correctamente.\n";
        return 0;
    }catch(const std::exception& error){
        fs::remove_all(directory);
        std::cerr<<"Fallo en archivos oficiales: "<<error.what()<<"\n";
        return 1;
    }
}
