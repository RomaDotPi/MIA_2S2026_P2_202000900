#include "extreamfs.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static std::string jsonEscape(const std::string& value){
    std::string out;
    for(char ch:value){
        switch(ch){
            case '\\':out+="\\\\";break;
            case '"':out+="\\\"";break;
            case '\n':out+="\\n";break;
            case '\r':out+="\\r";break;
            case '\t':out+="\\t";break;
            default:out+=ch;
        }
    }
    return out;
}

static std::string extractJsonString(const std::string& body,const std::string& key){
    auto position=body.find("\""+key+"\"");
    if(position==std::string::npos)return"";
    position=body.find(':',position);
    position=body.find('"',position);
    if(position==std::string::npos)return"";
    std::string out;bool escaped=false;
    for(size_t i=position+1;i<body.size();i++){
        char ch=body[i];
        if(escaped){
            if(ch=='n')out+='\n';else if(ch=='r')out+='\r';else if(ch=='t')out+='\t';else out+=ch;
            escaped=false;
        }else if(ch=='\\')escaped=true;
        else if(ch=='"')break;
        else out+=ch;
    }
    return out;
}

int main(int argc,char** argv){
    int port=8080;
    if(argc>1)port=std::stoi(argv[1]);
    ExtreamFS app("202000900");
    int server=socket(AF_INET,SOCK_STREAM,0);
    int enabled=1;
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&enabled,sizeof(enabled));
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);
    if(bind(server,(sockaddr*)&address,sizeof(address))<0){perror("bind");return 1;}
    listen(server,16);
    std::cout<<"ExtreamFS API en http://localhost:"<<port<<"\n";
    while(true){
        int client=accept(server,nullptr,nullptr);
        if(client<0)continue;
        std::string request;char buffer[8192];int received;
        while((received=recv(client,buffer,sizeof(buffer),0))>0){
            request.append(buffer,received);
            auto headerEnd=request.find("\r\n\r\n");
            if(headerEnd!=std::string::npos){
                auto contentLength=request.find("Content-Length:");size_t length=0;
                if(contentLength!=std::string::npos)length=std::stoul(request.substr(contentLength+15));
                if(request.size()>=headerEnd+4+length)break;
            }
        }
        std::string status="200 OK",payload="{\"error\":\"solicitud inválida\"}";
        auto firstSpace=request.find(' ');
        auto secondSpace=request.find(' ',firstSpace==std::string::npos?firstSpace:firstSpace+1);
        if(firstSpace!=std::string::npos&&secondSpace!=std::string::npos){
            std::string method=request.substr(0,firstSpace);
            std::string path=request.substr(firstSpace+1,secondSpace-firstSpace-1),body;
            auto headerEnd=request.find("\r\n\r\n");
            if(headerEnd!=std::string::npos)body=request.substr(headerEnd+4);
            if(method=="OPTIONS")payload="{}";
            else if(method=="GET"&&path=="/health")payload="{\"ok\":true}";
            else if(method=="GET"&&path=="/api/state")payload=app.stateJson();
            else if(method=="POST"&&path=="/api/execute")payload="{\"output\":\""+jsonEscape(app.executeScript(extractJsonString(body,"commands")))+"\"}";
            else if(method=="POST"&&path=="/api/explorer")payload=app.explorerJson(extractJsonString(body,"path"));
            else if(method=="POST"&&path=="/api/journal")payload=app.journalJson(extractJsonString(body,"id"));
            else{status="404 Not Found";payload="{\"error\":\"not found\"}";}
        }
        std::ostringstream response;
        response<<"HTTP/1.1 "<<status<<"\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: POST,GET,OPTIONS\r\nContent-Length: "<<payload.size()<<"\r\nConnection: close\r\n\r\n"<<payload;
        auto text=response.str();send(client,text.data(),text.size(),0);close(client);
    }
    close(server);
}
