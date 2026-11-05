/* Small host platform boundary; both simulations remain on the main thread. */
#ifndef SMB_PLATFORM_H
#define SMB_PLATFORM_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <io.h>
#include <direct.h>
#define getpid _getpid
#define setenv(name,value,overwrite) SDL_setenv(name,value,overwrite)
#define unsetenv(name) _putenv_s(name,"")
#define unlink _unlink
#define usleep(us) Sleep(((us)+999)/1000)
#define RTLD_NOW 0
#define RTLD_LOCAL 0
static void* dlopen(const char* path,int flags){(void)flags;return (void*)LoadLibraryA(path);}
static void* dlsym(void* module,const char* name){return (void*)GetProcAddress((HMODULE)module,name);}
static const char* dlerror(void){static char message[80];snprintf(message,sizeof message,"Windows loader error %lu",GetLastError());return message;}
#else
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#endif
