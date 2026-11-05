#ifdef _WIN32
/* In-process MP4 capture using Windows Media Foundation. No desktop capture. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
static IMFSinkWriter* writer;
static DWORD videoStream,audioStream;
static std::atomic<bool> active{false};
static bool wanted,configured,comOwned,mfOwned;
static unsigned width,height,videoFrames;
static LONGLONG started,lastVideo=-1,audioRead;
static constexpr unsigned rate=32000,ring=rate*4;
static float audioRing[ring*2];
static LONGLONG audioCursor[2];
static std::mutex audioLock;
static std::wstring destination;
static LONGLONG now(){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);return (LONGLONG)((long double)n.QuadPart*10000000/f.QuadPart);}
static void configure(){if(configured)return;configured=true;wanted=getenv("SMB_RECORD_AUTO")&&atoi(getenv("SMB_RECORD_AUTO"));}
static std::wstring wide(const char* value){if(!value)return {};int n=MultiByteToWideChar(CP_UTF8,0,value,-1,NULL,0);std::wstring s(n,0);MultiByteToWideChar(CP_UTF8,0,value,-1,s.data(),n);s.resize(n-1);return s;}
template<class T>static void drop(T*& p){if(p){p->Release();p=nullptr;}}
static HRESULT sample(DWORD stream,const void* bytes,DWORD size,LONGLONG time,LONGLONG duration){
    IMFMediaBuffer* buffer=nullptr;IMFSample* packet=nullptr;BYTE* out=nullptr;
    HRESULT hr=MFCreateMemoryBuffer(size,&buffer);
    if(SUCCEEDED(hr))hr=buffer->Lock(&out,nullptr,nullptr);
    if(SUCCEEDED(hr)){memcpy(out,bytes,size);buffer->Unlock();hr=buffer->SetCurrentLength(size);}
    if(SUCCEEDED(hr))hr=MFCreateSample(&packet);
    if(SUCCEEDED(hr))hr=packet->AddBuffer(buffer);
    if(SUCCEEDED(hr))hr=packet->SetSampleTime(time);
    if(SUCCEEDED(hr))hr=packet->SetSampleDuration(duration);
    if(SUCCEEDED(hr))hr=writer->WriteSample(stream,packet);
    drop(packet);drop(buffer);return hr;
}
extern "C" void smb_record_stop();
extern "C" void smb_record_set_wanted(int enabled){configure();if(enabled)wanted=true;else smb_record_stop();}
static bool failure(HRESULT hr,const char* operation){if(SUCCEEDED(hr))return false;fprintf(stderr,"SMB RECORD FAIL operation=%s hresult=%08lx\n",operation,(unsigned long)hr);wanted=false;smb_record_stop();return true;}
static HRESULT video_type(IMFMediaType** type,GUID subtype,unsigned w,unsigned h){
    HRESULT hr=MFCreateMediaType(type);if(FAILED(hr))return hr;
    (*type)->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);(*type)->SetGUID(MF_MT_SUBTYPE,subtype);
    (*type)->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    MFSetAttributeSize(*type,MF_MT_FRAME_SIZE,w,h);MFSetAttributeRatio(*type,MF_MT_FRAME_RATE,30,1);MFSetAttributeRatio(*type,MF_MT_PIXEL_ASPECT_RATIO,1,1);return S_OK;
}
static HRESULT audio_type(IMFMediaType** type,GUID subtype){
    HRESULT hr=MFCreateMediaType(type);if(FAILED(hr))return hr;
    (*type)->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);(*type)->SetGUID(MF_MT_SUBTYPE,subtype);
    (*type)->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);(*type)->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,rate);
    (*type)->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
    (*type)->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,subtype==MFAudioFormat_PCM?4:1);
    (*type)->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,subtype==MFAudioFormat_PCM?rate*4:24000);return S_OK;
}
static bool begin(unsigned w,unsigned h){
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);comOwned=SUCCEEDED(hr);
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)return !failure(hr,"COM");
    hr=MFStartup(MF_VERSION);if(failure(hr,"MFStartup"))return false;mfOwned=true;
    destination=wide(getenv("SMB_RECORD_PATH"));
    if(destination.empty()){
        std::wstring dir=wide(getenv("SMB_RECORD_DIR"));if(dir.empty())dir=L"Recordings";CreateDirectoryW(dir.c_str(),nullptr);
        SYSTEMTIME t;GetLocalTime(&t);wchar_t file[120];swprintf(file,120,L"\\Super Melee 64 %04u-%02u-%02u %02u-%02u-%02u-%03u-%lu.mp4",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId());destination=dir+file;
    }
    if(GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES){failure(E_ACCESSDENIED,"refuse overwrite");return false;}
    hr=MFCreateSinkWriterFromURL(destination.c_str(),nullptr,nullptr,&writer);if(failure(hr,"MP4 writer"))return false;
    IMFMediaType *vout=nullptr,*vin=nullptr,*aout=nullptr,*ain=nullptr;
    hr=video_type(&vout,MFVideoFormat_H264,w,h);
    if(SUCCEEDED(hr))hr=vout->SetUINT32(MF_MT_AVG_BITRATE,12000000);
    if(SUCCEEDED(hr))hr=writer->AddStream(vout,&videoStream);
    if(SUCCEEDED(hr))hr=video_type(&vin,MFVideoFormat_RGB32,w,h);
    if(SUCCEEDED(hr))hr=vin->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4);
    if(SUCCEEDED(hr))hr=writer->SetInputMediaType(videoStream,vin,nullptr);
    if(SUCCEEDED(hr))hr=audio_type(&aout,MFAudioFormat_AAC);
    if(SUCCEEDED(hr))hr=writer->AddStream(aout,&audioStream);
    if(SUCCEEDED(hr))hr=audio_type(&ain,MFAudioFormat_PCM);
    if(SUCCEEDED(hr))hr=writer->SetInputMediaType(audioStream,ain,nullptr);
    if(SUCCEEDED(hr))hr=writer->BeginWriting();
    drop(vout);drop(vin);drop(aout);drop(ain);if(failure(hr,"encoder setup"))return false;
    width=w;height=h;videoFrames=0;audioRead=0;lastVideo=-1;started=now();
    {std::lock_guard<std::mutex> lock(audioLock);memset(audioRing,0,sizeof audioRing);audioCursor[0]=audioCursor[1]=-1;active=true;}
    fprintf(stderr,"SMB RECORD START platform=windows width=%u height=%u in_process=1\n",w,h);return true;
}
static void audio_write(const void* input,unsigned frames,bool native){
    if(!active)return;std::lock_guard<std::mutex> lock(audioLock);if(!active)return;
    LONGLONG wall=(now()-started)*rate/10000000,position=audioCursor[native];
    if(position<0||llabs(position-wall)>rate/4)position=wall;position=std::max(position,audioRead);
    for(unsigned i=0;i<frames;i++)if(position+i<audioRead+ring)for(unsigned c=0;c<2;c++)audioRing[((position+i)%ring)*2+c]+=native?((const float*)input)[i*2+c]:((const int16_t*)input)[i*2+c]/32768.f;
    audioCursor[native]=position+frames;
}
static HRESULT flush_audio(LONGLONG until){
    while(audioRead<until){unsigned count=(unsigned)std::min<LONGLONG>(1024,until-audioRead);int16_t out[2048];LONGLONG position;
        {std::lock_guard<std::mutex> lock(audioLock);position=audioRead;
         for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++){unsigned at=((position+i)%ring)*2+c;out[i*2+c]=(int16_t)(std::clamp(audioRing[at],-1.f,1.f)*32767);audioRing[at]=0;}audioRead+=count;}
        HRESULT hr=sample(audioStream,out,count*4,position*10000000/rate,count*10000000ll/rate);if(FAILED(hr))return hr;
    }return S_OK;
}
extern "C" int smb_record_wanted(){configure();return wanted;}
extern "C" int smb_record_active(){return active;}
extern "C" void smb_record_melee_audio(const float* data,unsigned frames){audio_write(data,frames,true);}
extern "C" void smb_record_sm64_audio(const int16_t* data,unsigned frames){audio_write(data,frames,false);}
extern "C" void smb_record_video(unsigned w,unsigned h,const unsigned char* bgra){
    configure();if(!wanted)return;if(!active&&!begin(w,h))return;if(w!=width||h!=height)return;
    LONGLONG time=now()-started;if(lastVideo>=0&&time-lastVideo<10000000/60)return;
    // Positive RGB stride describes bottom-up DIB rows, matching GL readback.
    if(failure(sample(videoStream,bgra,w*h*4,time,10000000/30),"video sample"))return;
    lastVideo=time;videoFrames++;failure(flush_audio(time*rate/10000000),"audio sample");
}
extern "C" void smb_record_stop(){
    bool was=active.exchange(false);if(writer){if(was)flush_audio((now()-started)*rate/10000000);HRESULT hr=writer->Finalize();fprintf(stderr,"SMB RECORD STOP video_frames=%u hresult=%08lx\n",videoFrames,(unsigned long)hr);drop(writer);}
    if(mfOwned){MFShutdown();mfOwned=false;}if(comOwned){CoUninitialize();comOwned=false;}
}
extern "C" void smb_record_toggle(){configure();wanted=!wanted;if(!wanted)smb_record_stop();}

#endif
