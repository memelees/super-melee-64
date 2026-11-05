// Native launcher only. Game data is prepared locally; no network requests.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cwchar>
#include <string>
#include <vector>

static std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\'){slashes++;continue;}
        if(c==L'\"')result.append(slashes*2+1,L'\\');else result.append(slashes,L'\\');
        slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');return result+L"\"";
}
static void replace(std::wstring& value,const std::wstring& key,const std::wstring& replacement) {
    size_t at=0;while((at=value.find(key,at))!=std::wstring::npos){value.replace(at,key.size(),replacement);at+=replacement.size();}
}
static bool exists(const std::wstring& path){return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
static void fail(const std::wstring& message){MessageBoxW(nullptr,message.c_str(),L"Super Melee 64",MB_OK|MB_ICONERROR);}
static std::wstring folder(REFKNOWNFOLDERID id) {
    PWSTR path=nullptr;std::wstring value;
    if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&path))){value=path;CoTaskMemFree(path);}return value;
}
static std::wstring continuation(const std::wstring& support) {
    std::wstring fallback=support+L"/Continue",latest=support+L"/Playthroughs/latest.txt";
    HANDLE file=CreateFileW(latest.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return fallback;
    char bytes[32768];DWORD size=0;bool read=ReadFile(file,bytes,sizeof(bytes),&size,nullptr);CloseHandle(file);
    if(!read||!size||size==sizeof(bytes))return fallback;
    while(size&&(bytes[size-1]=='\n'||bytes[size-1]=='\r'))size--;
    int needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,size,nullptr,0);
    if(!needed)return fallback;
    std::wstring path(needed,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,size,path.data(),needed);
    // A damaged latest.txt must never redirect game saves outside this profile.
    std::wstring prefix=support+L"/Playthroughs/";
    if(path.find(prefix)!=0||path.find(L"..")!=std::wstring::npos||!exists(path))return fallback;
    return path;
}
static std::wstring newProfile(const std::wstring& support) {
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[80];
    swprintf(stamp,80,L"/%04u-%02u-%02u_%02u-%02u-%02u-%lu",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    return support+L"/Playthroughs"+stamp;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int count=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&count);
    bool check=false;
    if(count==2&&std::wstring(argv[1])==L"--self-test") {
        bool ok=quote(L"a b")==L"\"a b\""&&quote(L"x\\")==L"\"x\\\\\""&&quote(L"x\"y")==L"\"x\\\"y\"";
        std::wstring tokens=L"${ROOT}/x";replace(tokens,L"${ROOT}",L"C:/Game");
        LocalFree(argv);return ok&&tokens==L"C:/Game/x"?0:3;
    }
    if(count==2&&std::wstring(argv[1])==L"--check")check=true;
    else if(count>1){LocalFree(argv);fail(L"Unknown launcher option. Use the game menus to choose a character or multiplayer session.");return 2;}
    LocalFree(argv);
    std::vector<wchar_t> path(32768);DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!length||length>=path.size()){fail(L"Cannot locate this launcher.");return 1;}
    std::wstring root(path.data(),length);root.resize(root.find_last_of(L"\\/"));
    std::wstring support=folder(FOLDERID_RoamingAppData);
    if(support.empty()){fail(L"Windows could not locate your save folder.");return 1;}
    support+=L"/Super Melee 64";
    std::wstring movies=folder(FOLDERID_Videos);if(movies.empty())movies=support+L"/Recordings";
    const std::wstring fresh=newProfile(support),continued=continuation(support);
    const std::wstring config=root+L"/SuperMelee64.ini",game=root+L"/work/SuperMelee64-Windows/SuperMelee64.exe";
    if(!exists(config)||!exists(game)){fail(L"The local game build is incomplete. Open Setup Windows.cmd, supply your original game files, then choose Build. Keep the launcher with its complete game folder.");return 1;}
    std::vector<wchar_t> data(65536);
    DWORD size=GetPrivateProfileSectionW(L"Environment",data.data(),DWORD(data.size()),config.c_str());
    if(!size||size>=data.size()-2){fail(L"The launcher configuration is missing or too large. Run setup again.");return 1;}
    for(wchar_t* row=data.data();*row;row+=wcslen(row)+1) {
        std::wstring entry(row);size_t split=entry.find(L'=');if(split==std::wstring::npos)return 1;
        std::wstring key=entry.substr(0,split),value=entry.substr(split+1);
        if(key.find(L"SMB_")!=0&&key.find(L"SM64_MELEE_")!=0&&key.find(L"MELEE_")!=0&&key.find(L"MELEE3D_")!=0){fail(L"Unexpected launcher setting. Run setup again.");return 1;}
        replace(value,L"${ROOT}",root);replace(value,L"${SUPPORT}",support);replace(value,L"${MOVIES}",movies);
        replace(value,L"${NEW_PROFILE}",fresh);replace(value,L"${CONTINUE_PROFILE}",continued);
        if(value.find(L"${")!=std::wstring::npos||!SetEnvironmentVariableW(key.c_str(),value.c_str())){fail(L"Cannot configure the local game.");return 1;}
    }
    for(const wchar_t* key:{L"SM64_MELEE_LIBRARY",L"SM64_MELEE_DISC",L"SM64_MELEE_RESOURCES",L"SMB_SM64_ROM",L"SMB_SELECTION_ART"}) {
        DWORD needed=GetEnvironmentVariableW(key,nullptr,0);std::vector<wchar_t> value(needed?needed:1);
        if(!needed||!GetEnvironmentVariableW(key,value.data(),needed)||!exists(value.data())){fail(L"A prepared game file is missing. Run setup again with the original game files. Existing saves will be kept.");return 1;}
    }
    if(check)return 0;
    const std::wstring profile=support+L"/Unlocked";
    for(const std::wstring& dir:{support,profile,support+L"/Native",support+L"/Coop",support+L"/Cache",support+L"/Continue",support+L"/Playthroughs",fresh,support+L"/Recommended"}) {
        int status=SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);
        if(status!=ERROR_SUCCESS&&status!=ERROR_ALREADY_EXISTS&&status!=ERROR_FILE_EXISTS){fail(L"Cannot create your save folder. Check permissions for your Windows account.");return 1;}
    }
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    HANDLE output=CreateFileW((support+L"/latest-host.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&inherit,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,0,nullptr);
    if(output==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){fail(L"Cannot open the game log in your save folder.");return 1;}
    std::wstring command=quote(game)+L" --windowed --savepath "+quote(profile);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=output;startup.hStdError=output;startup.hStdInput=input;PROCESS_INFORMATION process{};
    if(!CreateProcessW(game.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,root.c_str(),&startup,&process)) {
        fail(L"Windows could not start the game (error "+std::to_wstring(GetLastError())+L"). Check the Visual C++ x64 runtime, GPU drivers and setup.log.");return 1;
    }
    CloseHandle(output);CloseHandle(input);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
