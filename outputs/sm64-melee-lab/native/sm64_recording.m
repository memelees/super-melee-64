/* In-process game capture: final GL image + the two engines' own PCM mixes. */
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static AVAssetWriter *writer;
static AVAssetWriterInput *videoInput,*audioInput;
static AVAssetWriterInputPixelBufferAdaptor *pixels;
static dispatch_queue_t encoder;
static _Atomic int active,pending;
static int wanted,configured;
static uint64_t started;
static double lastVideo;
static unsigned videoFrames,audioFrames;
static _Atomic unsigned dropped;
static NSString *destination;
static NSString *lastSaved,*recordError;
static uint64_t savedAt;
static NSMenuItem *recordMenuItem,*recordToggleItem,*recordLastItem;
#define RATE 32000
#define RING (RATE*4)
static float audioRing[RING*2];
static pthread_mutex_t audioLock=PTHREAD_MUTEX_INITIALIZER;
static _Atomic int64_t audioRead;
static int64_t audioCursor[2];
static unsigned long long sourceFrames[2];static double sourceEnergy[2];
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void configure(void){if(configured)return;configured=1;wanted=getenv("SMB_RECORD_AUTO")&&atoi(getenv("SMB_RECORD_AUTO"));encoder=dispatch_queue_create("super-melee-64.capture",DISPATCH_QUEUE_SERIAL);}
void smb_record_stop(void);
void smb_record_toggle(void);
void smb_record_set_wanted(int enabled){configure();if(enabled){wanted=1;recordError=nil;}else smb_record_stop();}
int smb_record_wanted(void){configure();return wanted;}
int smb_record_active(void){return atomic_load(&active);}
static void audio_write(const void* data,unsigned frames,int native){
    if(!atomic_load(&active))return;
    pthread_mutex_lock(&audioLock);
    if(!atomic_load(&active)){pthread_mutex_unlock(&audioLock);return;}
    int64_t wall=(now_ns()-started)*RATE/1000000000ull;
    int64_t pos=audioCursor[native];
    if(pos<0||llabs(pos-wall)>RATE/4)pos=wall;
    if(pos<audioRead)pos=audioRead;
    for(unsigned i=0;i<frames;i++)if(pos+i<audioRead+RING)for(unsigned c=0;c<2;c++) {
        float value=native?((const float*)data)[i*2+c]:((const int16_t*)data)[i*2+c]/32768.f;
        audioRing[((pos+i)%RING)*2+c]+=value;sourceEnergy[native]+=value*value;
    }
    sourceFrames[native]+=frames;audioCursor[native]=pos+frames;pthread_mutex_unlock(&audioLock);
}
void smb_record_melee_audio(const float* data,unsigned frames){audio_write(data,frames,1);}
void smb_record_sm64_audio(const int16_t* data,unsigned frames){audio_write(data,frames,0);}
static void flush_audio(int64_t until){
    while(audioRead<until&&audioInput.readyForMoreMediaData){
        unsigned count=(unsigned)MIN((int64_t)1024,until-audioRead);
        int16_t out[2048];
        pthread_mutex_lock(&audioLock);
        for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++){
            unsigned index=((audioRead+i)%RING)*2+c;float v=audioRing[index];audioRing[index]=0;
            out[i*2+c]=(int16_t)(fmaxf(-1,fminf(1,v))*32767);
        }
        pthread_mutex_unlock(&audioLock);
        AudioStreamBasicDescription format={.mSampleRate=RATE,.mFormatID=kAudioFormatLinearPCM,.mFormatFlags=kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,.mBytesPerPacket=4,.mFramesPerPacket=1,.mBytesPerFrame=4,.mChannelsPerFrame=2,.mBitsPerChannel=16};
        CMAudioFormatDescriptionRef desc=NULL;CMBlockBufferRef block=NULL;CMSampleBufferRef sample=NULL;
        OSStatus error=CMAudioFormatDescriptionCreate(kCFAllocatorDefault,&format,0,NULL,0,NULL,NULL,&desc);
        if(!error)error=CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,NULL,count*4,kCFAllocatorDefault,NULL,0,count*4,0,&block);
        if(!error)error=CMBlockBufferReplaceDataBytes(out,block,0,count*4);
        CMSampleTimingInfo timing={CMTimeMake(1,RATE),CMTimeMake(audioRead,RATE),kCMTimeInvalid};size_t size=4;
        if(!error)error=CMSampleBufferCreateReady(kCFAllocatorDefault,block,desc,count,1,&timing,1,&size,&sample);
        if(error||![audioInput appendSampleBuffer:sample])fprintf(stderr,"SMB RECORD AUDIO ERROR code=%d detail=%s\n",(int)error,writer.error.description.UTF8String);
        if(sample)CFRelease(sample);if(block)CFRelease(block);if(desc)CFRelease(desc);
        audioRead+=count;audioFrames+=count;
    }
}
static int begin(unsigned width,unsigned height){
    @autoreleasepool {
        NSString* folder=getenv("SMB_RECORD_DIR")?@(getenv("SMB_RECORD_DIR")):@".";
        [[NSFileManager defaultManager]createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
        NSString* requested=getenv("SMB_RECORD_PATH")?@(getenv("SMB_RECORD_PATH")):nil;
        NSDateFormatter* stamp=[NSDateFormatter new];stamp.dateFormat=@"yyyy-MM-dd_HH-mm-ss-SSS";
        NSString* path=requested?:[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"Super Melee 64_%@.mp4",[stamp stringFromDate:[NSDate date]]]];
        NSString* base=path.stringByDeletingPathExtension;unsigned suffix=1;
        while([[NSFileManager defaultManager]fileExistsAtPath:path])path=[NSString stringWithFormat:@"%@_%03u.mp4",base,suffix++];
        [[NSFileManager defaultManager]createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
        NSError* error=nil;writer=[[AVAssetWriter alloc]initWithURL:[NSURL fileURLWithPath:path] fileType:AVFileTypeMPEG4 error:&error];
        /* Commit a playable MP4 header and fragments during capture. Abrupt
         * process termination then loses only the unfinished tail, not moov. */
        writer.movieFragmentInterval=CMTimeMake(2,1);
        writer.initialMovieFragmentInterval=CMTimeMake(1,1);
        videoInput=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:@{AVVideoCodecKey:AVVideoCodecTypeH264,AVVideoWidthKey:@(width),AVVideoHeightKey:@(height),AVVideoCompressionPropertiesKey:@{AVVideoAverageBitRateKey:@8000000,AVVideoExpectedSourceFrameRateKey:@30,AVVideoMaxKeyFrameIntervalKey:@60,AVVideoAllowFrameReorderingKey:@NO}}];
        videoInput.mediaTimeScale=600;
        videoInput.expectsMediaDataInRealTime=YES;
        pixels=[AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoInput sourcePixelBufferAttributes:@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),(id)kCVPixelBufferWidthKey:@(width),(id)kCVPixelBufferHeightKey:@(height),(id)kCVPixelBufferIOSurfacePropertiesKey:@{}}];
        audioInput=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:@{AVFormatIDKey:@(kAudioFormatMPEG4AAC),AVSampleRateKey:@RATE,AVNumberOfChannelsKey:@2,AVEncoderBitRateKey:@160000}];audioInput.expectsMediaDataInRealTime=YES;
        if(!writer||![writer canAddInput:videoInput]||![writer canAddInput:audioInput]){recordError=error.localizedDescription?:@"The video encoder couldn't be configured.";fprintf(stderr,"SMB RECORD ERROR setup=%s\n",recordError.UTF8String);writer=nil;wanted=0;return 0;}
        [writer addInput:videoInput];[writer addInput:audioInput];
        if(![writer startWriting]){recordError=writer.error.localizedDescription;fprintf(stderr,"SMB RECORD ERROR start=%s\n",writer.error.description.UTF8String);writer=nil;wanted=0;return 0;}
        [writer startSessionAtSourceTime:kCMTimeZero];destination=path;
        pthread_mutex_lock(&audioLock);memset(audioRing,0,sizeof audioRing);audioRead=0;sourceFrames[0]=sourceFrames[1]=0;sourceEnergy[0]=sourceEnergy[1]=0;audioCursor[0]=audioCursor[1]=-1;started=now_ns();pthread_mutex_unlock(&audioLock);
        videoFrames=dropped=audioFrames=0;lastVideo=0;atomic_store(&active,1);
        fprintf(stderr,"SMB RECORD START path=%s width=%u height=%u audio=sm64+melee encoder=in-process\n",path.UTF8String,width,height);return 1;
    }
}
void smb_record_video(unsigned width,unsigned height,const unsigned char* bgra){
    configure();if(!wanted)return;if(!atomic_load(&active)&&!begin(width,height))return;
    if(atomic_load(&pending)>=3){dropped++;return;}
    CVPixelBufferRef buffer=NULL;
    if(CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,pixels.pixelBufferPool,&buffer)!=kCVReturnSuccess){dropped++;return;}
    CVPixelBufferLockBaseAddress(buffer,0);unsigned char* dst=CVPixelBufferGetBaseAddress(buffer);size_t stride=CVPixelBufferGetBytesPerRow(buffer);
    for(unsigned y=0;y<height;y++)memcpy(dst+y*stride,bgra+(height-1-y)*(size_t)width*4,width*4);
    CVPixelBufferUnlockBaseAddress(buffer,0);
    CMTime time=CMTimeMakeWithSeconds((now_ns()-started)/1e9,600);lastVideo=CMTimeGetSeconds(time);atomic_fetch_add(&pending,1);
    dispatch_async(encoder,^{@autoreleasepool {
        if(videoInput.readyForMoreMediaData&&[pixels appendPixelBuffer:buffer withPresentationTime:time])videoFrames++;else dropped++;
        flush_audio(MAX(0,(int64_t)((CMTimeGetSeconds(time)-.2)*RATE)));
        CVPixelBufferRelease(buffer);atomic_fetch_sub(&pending,1);
    }});
}
void smb_record_stop(void){
    configure();wanted=0;if(!atomic_exchange(&active,0))return;
    dispatch_sync(encoder,^{@autoreleasepool {
        int64_t end=(int64_t)((lastVideo+1./30)*RATE);
        for(unsigned i=0;audioRead<end&&i<200;i++){flush_audio(end);if(audioRead<end)usleep(10000);}
        [writer endSessionAtSourceTime:CMTimeMake(end,RATE)];[videoInput markAsFinished];[audioInput markAsFinished];
        dispatch_semaphore_t finished=dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{dispatch_semaphore_signal(finished);}];
        dispatch_semaphore_wait(finished,DISPATCH_TIME_FOREVER);
        fprintf(stderr,"SMB RECORD FINISH path=%s status=%ld frames=%u dropped=%u audio_frames=%u seconds=%.3f error=%s\n",destination.UTF8String,(long)writer.status,videoFrames,dropped,audioFrames,end/(double)RATE,writer.error.description.UTF8String?:"none");
        fprintf(stderr,"SMB RECORD SOURCES sm64_frames=%llu melee_frames=%llu sm64_rms=%.6f melee_rms=%.6f\n",sourceFrames[0],sourceFrames[1],sqrt(sourceEnergy[0]/fmax(1,sourceFrames[0]*2)),sqrt(sourceEnergy[1]/fmax(1,sourceFrames[1]*2)));
        if(writer.status==AVAssetWriterStatusCompleted&&videoFrames){lastSaved=destination;savedAt=now_ns();recordError=nil;}
        else recordError=writer.error.localizedDescription?:@"The recording couldn't be saved.";
        writer=nil;videoInput=nil;audioInput=nil;pixels=nil;
    }});
}
void smb_record_toggle(void){smb_record_set_wanted(!smb_record_wanted());}

static NSString* recordings_folder(void){return getenv("SMB_RECORD_DIR")?@(getenv("SMB_RECORD_DIR")):[NSHomeDirectory() stringByAppendingPathComponent:@"Movies/Super Melee 64"];}
extern int smb_host_launch_coop(const char*);
extern int smb_host_option(unsigned,int);
extern void smb_host_spawn_menu_request(void);
@interface SmbRecordingActions:NSObject
- (void)walls:(id)sender;
- (void)spawn:(id)sender;
- (void)host:(id)sender;
- (void)join:(id)sender;
- (void)toggle:(id)sender;
- (void)folder:(id)sender;
- (void)last:(id)sender;
@end
static void coop_launch(NSString* address) {
    if(smb_host_launch_coop(address?address.UTF8String:NULL)) {
        NSAlert* alert=[NSAlert new];alert.messageText=@"Co-op could not start";alert.informativeText=@"The multiplayer host or its local game files are missing. Rebuild Super Melee 64 with multiplayer included.";[alert runModal];
    }
}
@implementation SmbRecordingActions
- (void)host:(id)sender{(void)sender;coop_launch(nil);}
- (void)join:(id)sender{(void)sender;NSAlert* alert=[NSAlert new];alert.messageText=@"Join Super Melee 64";alert.informativeText=@"Enter the host’s IP address.";
    NSTextField* input=[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,280,24)];alert.accessoryView=input;
    [alert addButtonWithTitle:@"Join"];[alert addButtonWithTitle:@"Cancel"];
    if([alert runModal]==NSAlertFirstButtonReturn)coop_launch([input.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]);
}
- (void)walls:(id)sender{(void)sender;smb_host_option(0,1);}
- (void)spawn:(id)sender{(void)sender;smb_host_spawn_menu_request();}
- (void)toggle:(id)sender{(void)sender;smb_record_toggle();}
- (void)folder:(id)sender{(void)sender;NSString* folder=recordings_folder();[NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];[NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:folder isDirectory:YES]];}
- (void)last:(id)sender{(void)sender;if(lastSaved)[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:lastSaved]]];}
@end
static SmbRecordingActions* recordActions;
void smb_record_menu_update(void){
    configure();
    if(!NSApp.mainMenu)return;
    NSMenuItem* gameplay=[NSApp.mainMenu itemWithTitle:@"Gameplay"];
    if(!gameplay) {
        if(!recordActions)recordActions=[SmbRecordingActions new];
        gameplay=[[NSMenuItem alloc]initWithTitle:@"Gameplay" action:nil keyEquivalent:@""];
        NSMenu* menu=[[NSMenu alloc]initWithTitle:@"Gameplay"];menu.autoenablesItems=NO;gameplay.submenu=menu;
        NSArray* titles=@[@"Wall Jump with Every Character",@"Spawn Enemy…"];
        SEL actions[]={@selector(walls:),@selector(spawn:)};
        for(unsigned i=0;i<2;i++){NSMenuItem* item=[[NSMenuItem alloc]initWithTitle:titles[i] action:actions[i] keyEquivalent:@""];item.target=recordActions;[menu addItem:item];}
        [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem* host=[[NSMenuItem alloc]initWithTitle:@"Host Co-op (16 Players)" action:@selector(host:) keyEquivalent:@""];host.target=recordActions;[menu addItem:host];
        NSMenuItem* join=[[NSMenuItem alloc]initWithTitle:@"Join Co-op…" action:@selector(join:) keyEquivalent:@""];join.target=recordActions;[menu addItem:join];
        [NSApp.mainMenu addItem:gameplay];
    }
    [gameplay.submenu itemAtIndex:0].state=smb_host_option(0,0)?NSControlStateValueOn:NSControlStateValueOff;
    if(!recordMenuItem||![NSApp.mainMenu.itemArray containsObject:recordMenuItem]) {
        if(!recordActions)recordActions=[SmbRecordingActions new];NSMenu* menu=[[NSMenu alloc]initWithTitle:@"Recording"];menu.autoenablesItems=NO;
        recordMenuItem=[[NSMenuItem alloc]initWithTitle:@"Recording" action:nil keyEquivalent:@""];recordMenuItem.submenu=menu;
        recordToggleItem=[[NSMenuItem alloc]initWithTitle:@"Start Recording" action:@selector(toggle:) keyEquivalent:@"r"];
        recordToggleItem.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;recordToggleItem.target=recordActions;[menu addItem:recordToggleItem];
        [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem* folder=[[NSMenuItem alloc]initWithTitle:@"Open Recordings Folder" action:@selector(folder:) keyEquivalent:@"o"];
        folder.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;folder.target=recordActions;[menu addItem:folder];
        recordLastItem=[[NSMenuItem alloc]initWithTitle:@"Show Last Recording in Finder" action:@selector(last:) keyEquivalent:@""];recordLastItem.target=recordActions;[menu addItem:recordLastItem];
        [NSApp.mainMenu insertItem:recordMenuItem atIndex:MIN(1,NSApp.mainMenu.numberOfItems)];
        fprintf(stderr,"SMB RECORD MENU native=1 toggle=CmdShiftR folder=CmdShiftO\n");
    }
    recordToggleItem.title=wanted?@"Stop Recording":@"Start Recording";recordToggleItem.state=wanted?NSControlStateValueOn:NSControlStateValueOff;
    recordLastItem.enabled=lastSaved!=nil;
    NSWindow* window=NSApp.mainWindow?:NSApp.keyWindow;if(!window)return;
    NSString* title=@"Super Melee 64";
    if(atomic_load(&active)){unsigned seconds=(unsigned)((now_ns()-started)/1000000000ull);title=[NSString stringWithFormat:@"Super Melee 64 — REC %02u:%02u",seconds/60,seconds%60];}
    else if(recordError)title=@"Super Melee 64 — Recording couldn't be saved";
    else if(wanted)title=@"Super Melee 64 — Recording ready";
    else if(savedAt&&now_ns()-savedAt<6000000000ull)title=@"Super Melee 64 — Recording saved";
    if(![window.title isEqualToString:title])window.title=title;
}
