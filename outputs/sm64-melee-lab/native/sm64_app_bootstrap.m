/* Finder launches the game itself. No shell, Python or donor app is involved. */
#import <Cocoa/Cocoa.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int app_lock=-1;
static void app_fail(NSString* message) {
    fprintf(stderr,"SMB APP FAIL %s\n",message.UTF8String);
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert* alert=[NSAlert new];
    alert.messageText=@"Super Melee 64 couldn't start";
    alert.informativeText=message;
    [alert addButtonWithTitle:@"Close"];
    [alert runModal];exit(2);
}
static void app_env(const char* key,NSString* value) {setenv(key,value.fileSystemRepresentation,1);}

void smb_app_bootstrap(int* argc,char*** argv) {
    /* Development/verification launches already supply an isolated runtime. */
    if(getenv("SM64_MELEE_LIBRARY"))return;
    @autoreleasepool {
        NSBundle* bundle=NSBundle.mainBundle;
        NSString* resources=bundle.resourcePath;
        NSDictionary* config=[NSDictionary dictionaryWithContentsOfFile:[resources stringByAppendingPathComponent:@"Game.plist"]];
        if(!config) {
            NSString* relay=[[NSString stringWithContentsOfFile:[resources stringByAppendingPathComponent:@"GameApp.path"] encoding:NSUTF8StringEncoding error:nil] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if(relay.length) {
                NSString* target=[relay stringByAppendingPathComponent:@"Contents/MacOS/SuperMelee64"];
                (*argv)[0]=strdup(target.fileSystemRepresentation);execv((*argv)[0],*argv);
                app_fail(@"The installed Super Melee 64 app could not be opened. Rebuild the native app.");
            }
            return;
        }
        NSFileManager* fm=NSFileManager.defaultManager;
        NSString* home=NSHomeDirectory();
        NSString* support=[home stringByAppendingPathComponent:@"Library/Application Support/Super Melee 64"];
        NSString* logs=[home stringByAppendingPathComponent:@"Library/Logs/Super Melee 64"];
        NSError* error=nil;
        for(NSString* path in @[support,logs])if(![fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:&error])app_fail(error.localizedDescription);
        /* Kernel ownership disappears on exit/crash; stale PID files cannot
         * prevent launch. The bundled runtime is independent of dev builds. */
        app_lock=open([[support stringByAppendingPathComponent:@"game.lock"] fileSystemRepresentation],O_CREAT|O_RDWR,0600);
        if(app_lock<0)app_fail(@"The game couldn't open its save folder.");
        if(flock(app_lock,LOCK_EX|LOCK_NB)) {
            for(NSRunningApplication* running in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundle.bundleIdentifier])
                if(running.processIdentifier!=getpid())[running activateWithOptions:NSApplicationActivateIgnoringOtherApps];
            exit(0);
        }
        ftruncate(app_lock,0);dprintf(app_lock,"%d\n",getpid());
        NSString* log=[logs stringByAppendingPathComponent:@"latest.log"];
        freopen(log.fileSystemRepresentation,"w",stdout);
        dup2(fileno(stdout),STDERR_FILENO);
        setvbuf(stdout,NULL,_IOLBF,0);setvbuf(stderr,NULL,_IOLBF,0);
        fprintf(stderr,"SMB APP native_bootstrap=1 pid=%d bundle=%s\n",getpid(),bundle.bundlePath.fileSystemRepresentation);
        NSDictionary* tokens=@{@"${RESOURCES}":resources,@"${FRAMEWORKS}":bundle.privateFrameworksPath,@"${SUPPORT}":support,@"${LOGS}":logs,@"${HOME}":home};
        NSDictionary* environment=config[@"Environment"];
        if(![environment isKindOfClass:NSDictionary.class])app_fail(@"This app is missing its startup settings. Rebuild the installed app.");
        for(NSString* key in environment) {
            NSString* value=environment[key];
            for(NSString* token in tokens)value=[value stringByReplacingOccurrencesOfString:token withString:tokens[token]];
            app_env(key.UTF8String,value);
        }
        for(const char** key=(const char*[]){"SM64_MELEE_LIBRARY","SM64_MELEE_DISC","SMB_SELECTION_ART",NULL};*key;key++)
            if(!getenv(*key)||access(getenv(*key),R_OK))app_fail([NSString stringWithFormat:@"A required game file is missing (%s). Rebuild the app from your local installation. Details: %@",*key,log]);

        NSString* playthroughs=[support stringByAppendingPathComponent:@"Playthroughs"];
        NSString* latest=[playthroughs stringByAppendingPathComponent:@"latest.txt"];
        NSString* continued=[[NSString stringWithContentsOfFile:latest encoding:NSUTF8StringEncoding error:nil] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if(!continued.length||![fm fileExistsAtPath:continued])continued=[support stringByAppendingPathComponent:@"Continue"];
        NSDateFormatter* formatter=[NSDateFormatter new];formatter.dateFormat=@"yyyy-MM-dd_HH-mm-ss";
        NSString* fresh=[playthroughs stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%d",[formatter stringFromDate:NSDate.date],getpid()]];
        NSString* unlocked=[support stringByAppendingPathComponent:@"Unlocked"];
        NSString* recommended=[support stringByAppendingPathComponent:@"Recommended Stages"];
        NSString* recordings=[home stringByAppendingPathComponent:@"Movies/Super Melee 64"];
        app_env("SMB_PROFILE_UNLOCKED",unlocked);app_env("SMB_PROFILE_CONTINUE",continued);
        app_env("SMB_PROFILE_RECOMMENDED",recommended);
        app_env("SMB_PROFILE_NEW",fresh);app_env("SMB_PLAYTHROUGH_LATEST",latest);app_env("SMB_RECORD_DIR",recordings);
        NSString* profile=unlocked;
        NSMutableArray<NSString*>* arguments=[NSMutableArray arrayWithObject:@((*argv)[0])];
        for(int i=1;i<*argc;i++) {
            if(!strcmp((*argv)[i],"--app-smoke")) {
                profile=[support stringByAppendingPathComponent:@"App Verification"];
                app_env("SMB_SMOKE",@"1");app_env("SMB_SMOKE_FRAMES",@"180");
                app_env("SMB_EMBED_TEST_PAUSE",@"1");app_env("SMB_SELECT_AUTOSTART",@"1");
                app_env("SMB_SCENE_CAPTURE",[logs stringByAppendingPathComponent:@"app-smoke.rgba"]);
                app_env("SMB_PROFILE_UNLOCKED",profile);app_env("SMB_PROFILE_CONTINUE",profile);app_env("SMB_PROFILE_NEW",profile);
                app_env("SMB_PROFILE_RECOMMENDED",profile);
            } else if(strncmp((*argv)[i],"-psn_",5)) [arguments addObject:@((*argv)[i])];
        }
        for(NSString* path in @[profile,unlocked,recommended,continued,fresh,recordings,[support stringByAppendingPathComponent:@"Cache"],[support stringByAppendingPathComponent:@"Native"]])
            if(![fm createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:&error])app_fail(error.localizedDescription);
        [arguments addObjectsFromArray:@[@"--windowed",@"--savepath",profile]];
        char** values=calloc(arguments.count+1,sizeof(char*));
        for(NSUInteger i=0;i<arguments.count;i++)values[i]=strdup(arguments[i].fileSystemRepresentation);
        *argc=(int)arguments.count;*argv=values;chdir(profile.fileSystemRepresentation);
        [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"ApplePersistenceIgnoreState"];
        [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"NSQuitAlwaysKeepsWindows"];
        app_env("SMB_OPTIONS_PATH",[support stringByAppendingPathComponent:@"Gameplay.options"]);
        fprintf(stderr,"SMB APP READY python=0 shell=0 save=%s recordings=%s\n",profile.fileSystemRepresentation,recordings.fileSystemRepresentation);
    }
}
