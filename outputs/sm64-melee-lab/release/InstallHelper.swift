import AppKit

struct SetupDiagnostic: Codable {
    let code: String, title: String, message: String, recovery: String
    var detail: String?
}
func toolDiagnostic(_ output: String) -> SetupDiagnostic? {
    let lower = output.lowercased()
    if lower.contains("license") && ["agree", "accept", "review"].contains(where: lower.contains) {
        return SetupDiagnostic(code:"xcode-license",title:"Finish Xcode setup",
            message:"Apple is blocking the build tools until the Xcode licence has been reviewed.",
            recovery:"Open Xcode, review its licence and complete the setup prompts yourself. Then choose Prepare Build Tools and Build / Resume. Existing downloads are kept.")
    }
    if lower.contains("command line tools are too outdated") || lower.contains("outdated command line tools") {
        return SetupDiagnostic(code:"mac-tools-outdated",title:"Update Apple Command Line Tools",
            message:"Homebrew cannot prepare the build because Apple's Command Line Tools are out of date.",
            recovery:"Open System Settings > General > Software Update and install the Command Line Tools update. If you use Xcode, open it and finish setup. Then retry Prepare Build Tools and Build / Resume in the same folder. Your game files and verified downloads are kept.")
    }
    if lower.contains("sys/types.h") && lower.contains("no such file") || lower.contains("invalid active developer path") || lower.contains("unable to find utility") {
        return SetupDiagnostic(code:"mac-sdk",title:"Mac build tools need attention",
            message:"The compiler cannot find the macOS system headers or selected Apple tools.",
            recovery:"Update Apple Command Line Tools in System Settings > General > Software Update. If you use Xcode, open it and finish setup. Then choose Prepare Build Tools and Build / Resume. Existing downloads are kept.")
    }
    return nil
}
func savedDiagnostic(_ url: URL) -> SetupDiagnostic? {
    guard let data=try? Data(contentsOf:url),data.count<=65536 else{return nil}
    return try? JSONDecoder().decode(SetupDiagnostic.self,from:data)
}

func identifyGame(_ url: URL) -> String? {
    guard let handle=try? FileHandle(forReadingFrom:url) else { return nil }
    let header=(try? handle.read(upToCount:8)) ?? Data();try? handle.close()
    if header.count==8 && header.prefix(6)==Data("GALE01".utf8) && header[7]==2 { return "melee" }
    if header.prefix(4)==Data([0x80,0x37,0x12,0x40]) { return "sm64" }
    return nil
}
func setupPython(_ kit: URL) -> String? {
    let home = FileManager.default.homeDirectoryForCurrentUser
    let candidates = [home.appendingPathComponent(".super-melee64/.venv/bin/python").path,
        kit.appendingPathComponent(".venv/bin/python").path, "/opt/homebrew/bin/python3",
        "/opt/homebrew/bin/python3.14", "/opt/homebrew/bin/python3.13", "/opt/homebrew/bin/python3.12",
        "/usr/local/bin/python3"]
    for path in candidates where FileManager.default.isExecutableFile(atPath: path) {
        let probe = Process(); probe.executableURL = URL(fileURLWithPath: path)
        probe.arguments = ["-c", "import sys; import PIL; sys.exit(0 if sys.version_info >= (3, 12) else 1)"]
        probe.standardOutput = FileHandle.nullDevice; probe.standardError = FileHandle.nullDevice
        do { try probe.run(); probe.waitUntilExit(); if probe.terminationStatus == 0 { return path } } catch { continue }
    }
    return nil
}
final class DropArea: NSView {
    var receive: (([URL]) -> Void)?
    override init(frame: NSRect) {
        super.init(frame: frame)
        registerForDraggedTypes([.fileURL])
        wantsLayer = true
        layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
        layer?.cornerRadius = 14
    }
    required init?(coder: NSCoder) { fatalError() }
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { .copy }
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let files = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        receive?(files)
        return !files.isEmpty
    }
}

final class Installer: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    var rom: URL?, melee: URL?
    var destination=FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".super-melee64")
    let status=NSTextField(wrappingLabelWithString:"Drop your Super Mario 64 ROM and Melee disc image here.")
    let inputs=NSTextField(wrappingLabelWithString:"SM64: not selected\nMelee: not selected")
    let log=NSTextView(), progress=NSProgressIndicator()
    let build=NSButton(title:"Build / Resume",target:nil,action:nil)
    let cancel=NSButton(title:"Cancel",target:nil,action:nil)
    var process: Process?, preparing=false, stopped=false, recentOutput=""
    var kit: URL { Bundle.main.resourceURL!.appendingPathComponent("SourceKit") }
    var cancelFile: URL {destination.appendingPathComponent("work/setup.cancel")}
    func applicationDidFinishLaunching(_ notification: Notification) {
        window=NSWindow(contentRect:NSRect(x:0,y:0,width:720,height:650),styleMask:[.titled,.closable,.miniaturizable],backing:.buffered,defer:false)
        window.title="Install Super Melee 64";window.center();window.delegate=self
        let content=window.contentView!
        let title=NSTextField(labelWithString:"Super Melee 64");title.font = .boldSystemFont(ofSize:28)
        title.frame=NSRect(x:26,y:592,width:660,height:38);content.addSubview(title)
        let drop=DropArea(frame:NSRect(x:24,y:404,width:672,height:170));drop.receive={ [weak self] in self?.accept($0) }
        status.frame=NSRect(x:18,y:108,width:636,height:48);status.font = .systemFont(ofSize:16,weight:.medium)
        inputs.frame=NSRect(x:18,y:16,width:636,height:82);inputs.textColor = .secondaryLabelColor
        drop.addSubview(status);drop.addSubview(inputs);content.addSubview(drop)
        func button(_ name:String,_ action:Selector,_ x:Double,_ y:Double,_ width:Double) {
            let b=NSButton(title:name,target:self,action:action);b.frame=NSRect(x:x,y:y,width:width,height:32);content.addSubview(b)
        }
        button("Choose Files…",#selector(chooseFiles),24,360,140)
        button("Build Folder…",#selector(chooseFolder),170,360,140)
        build.target=self;build.action=#selector(start);build.frame=NSRect(x:472,y:360,width:224,height:32);build.isEnabled=false;content.addSubview(build)
        button("Prepare Build Tools",#selector(prepareTools),24,316,184)
        button("Open Setup Log",#selector(openLog),220,316,152)
        cancel.target=self;cancel.action=#selector(stop);cancel.frame=NSRect(x:584,y:316,width:112,height:32);cancel.isEnabled=false;content.addSubview(cancel)
        progress.frame=NSRect(x:26,y:292,width:668,height:14);progress.minValue=0;progress.maxValue=12;progress.isIndeterminate=false;content.addSubview(progress)
        let scroll=NSScrollView(frame:NSRect(x:24,y:24,width:672,height:248));scroll.hasVerticalScroller=true
        log.isEditable=false;log.font = .monospacedSystemFont(ofSize:11,weight:.regular);scroll.documentView=log;content.addSubview(scroll)
        window.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)
        append("Your original US SM64 and Melee USA Rev 2 files stay on this Mac. Setup downloads official patches and builds the game locally.\nAllow at least 12 GiB free; 20 GiB is recommended. Cancel keeps verified downloads for Resume.\n")
        if CommandLine.arguments.count>1 {accept(CommandLine.arguments.dropFirst().filter{!$0.hasPrefix("--")}.map{URL(fileURLWithPath:$0)})}
    }
    func application(_ sender:NSApplication,openFiles filenames:[String]) {accept(filenames.map{URL(fileURLWithPath:$0)});sender.reply(toOpenOrPrint:.success)}
    func accept(_ files:[URL]) {
        for url in files {
            if identifyGame(url)=="melee" {melee=url} else if identifyGame(url)=="sm64" {rom=url}
            else {append("Unrecognized game file: \(url.lastPathComponent)\n")}
        }
        inputs.stringValue="SM64: \(rom?.lastPathComponent ?? "not selected")\nMelee: \(melee?.lastPathComponent ?? "not selected")\nBuild folder: \(destination.path)"
        build.isEnabled=rom != nil && melee != nil && process == nil
    }
    @objc func chooseFiles() {
        guard process==nil else{return};let panel=NSOpenPanel();panel.allowsMultipleSelection=true;panel.canChooseDirectories=false
        if panel.runModal() == .OK {accept(panel.urls)}
    }
    @objc func chooseFolder() {
        guard process==nil else{return};let panel=NSOpenPanel();panel.title="Choose a writable build folder without spaces"
        panel.canChooseDirectories=true;panel.canChooseFiles=false;panel.canCreateDirectories=true
        if panel.runModal() == .OK,let folder=panel.url {destination=folder;accept([])}
    }
    func append(_ text:String) {
        log.textStorage?.append(NSAttributedString(string:text,attributes:[.foregroundColor:NSColor.textColor,.font:NSFont.monospacedSystemFont(ofSize:11,weight:.regular)]));log.scrollToEndOfDocument(nil)
        if let range=text.range(of:#"\[(\d+)/12\] ([^\n]+)"#,options:.regularExpression) {
            let line=String(text[range]);status.stringValue=line
            if let n=Int(line.dropFirst().prefix(while:{$0.isNumber})) {progress.doubleValue=Double(n)}
        }
        if let data=text.data(using:.utf8) {
            let path=destination.appendingPathComponent("setup-gui.log")
            if FileManager.default.fileExists(atPath:destination.path) {
                if !FileManager.default.fileExists(atPath:path.path){FileManager.default.createFile(atPath:path.path,contents:nil)}
                if let handle=try? FileHandle(forWritingTo:path){_ = try? handle.seekToEnd();try? handle.write(contentsOf:data);try? handle.close()}
            }
        }
    }
    @objc func openLog() {
        let path=destination.appendingPathComponent("setup.log")
        let gui=destination.appendingPathComponent("setup-gui.log")
        NSWorkspace.shared.open(FileManager.default.fileExists(atPath:path.path) ? path : FileManager.default.fileExists(atPath:gui.path) ? gui : destination)
    }
    func failed(_ diagnostic: SetupDiagnostic?) {
        guard let diagnostic else {status.stringValue="Setup stopped. Open Setup Log for details. Verified downloads are kept.";return}
        status.stringValue=diagnostic.title+". Follow the repair instructions, then retry."
        append("\n"+diagnostic.title+"\n"+diagnostic.message+"\n"+diagnostic.recovery+"\n")
        let alert=NSAlert();alert.alertStyle = .warning;alert.messageText=diagnostic.title
        alert.informativeText=diagnostic.message+"\n\n"+diagnostic.recovery
        alert.addButton(withTitle:"OK");alert.addButton(withTitle:"Open Setup Log")
        alert.beginSheetModal(for:window) { [weak self] response in
            if response == .alertSecondButtonReturn {self?.openLog()}
        }
    }
    @objc func stop() {
        guard process != nil else{return};stopped=true;status.stringValue="Stopping safely. Verified downloads will be kept…";cancel.isEnabled=false
        if preparing {process?.interrupt()} else {try? FileManager.default.createDirectory(at:cancelFile.deletingLastPathComponent(),withIntermediateDirectories:true);try? Data().write(to:cancelFile)}
    }
    func windowShouldClose(_ sender:NSWindow)->Bool {if process != nil {stop();return false};return true}
    func execute(_ path:String,_ arguments:[String],_ completion:@escaping(Int32)->Void) {
        let task=Process(),pipe=Pipe();task.executableURL=URL(fileURLWithPath:path);task.arguments=arguments
        var environment=ProcessInfo.processInfo.environment
        environment["PATH"]="/opt/homebrew/opt/python@3.12/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
        environment["PYTHONUNBUFFERED"]="1";environment["SSL_CERT_FILE"]="/etc/ssl/cert.pem"
        task.environment=environment;task.standardOutput=pipe;task.standardError=pipe;recentOutput=""
        pipe.fileHandleForReading.readabilityHandler={ [weak self] handle in
            let data=handle.availableData;guard !data.isEmpty else{return}
            DispatchQueue.main.async{
                guard let self else{return};let text=String(decoding:data,as:UTF8.self)
                self.recentOutput=String((self.recentOutput+text).suffix(16384));self.append(text)
            }
        }
        task.terminationHandler={ [weak self] task in
            pipe.fileHandleForReading.readabilityHandler=nil
            let remainder=pipe.fileHandleForReading.readDataToEndOfFile()
            DispatchQueue.main.async {
                if !remainder.isEmpty,let self {
                    let text=String(decoding:remainder,as:UTF8.self)
                    self.recentOutput=String((self.recentOutput+text).suffix(16384));self.append(text)
                }
                self?.process=nil;completion(task.terminationStatus)
            }
        }
        process=task;build.isEnabled=false;cancel.isEnabled=true
        do {try task.run()} catch {process=nil;append(error.localizedDescription+"\n");completion(1)}
    }
    @objc func prepareTools() {
        guard process==nil else{return};stopped=false;preparing=true
        guard FileManager.default.fileExists(atPath:"/Library/Developer/CommandLineTools/usr/bin/clang") || FileManager.default.fileExists(atPath:"/Applications/Xcode.app") else {
            execute("/usr/bin/xcode-select",["--install"]) { [weak self] _ in self?.status.stringValue="Finish Apple's tools installer, then choose Prepare Build Tools again.";self?.cancel.isEnabled=false };return
        }
        guard FileManager.default.isExecutableFile(atPath:"/opt/homebrew/bin/brew") else {
            status.stringValue="Install Homebrew from its official site, then choose Prepare Build Tools."
            append("Opening Homebrew's official installation instructions. This helper does not run downloaded shell scripts.\n")
            NSWorkspace.shared.open(URL(string:"https://brew.sh")!);return
        }
        try? FileManager.default.createDirectory(at:destination,withIntermediateDirectories:true)
        let python="/opt/homebrew/opt/python@3.12/bin/python3.12",venv=destination.appendingPathComponent(".venv")
        let commands:[(String,[String])]=[("/opt/homebrew/bin/brew",["install","python@3.12","gcc","cmake","ninja","sdl2","sdl3","glew","pkg-config","xdelta","sevenzip"]),(python,["-m","venv",venv.path]),(venv.appendingPathComponent("bin/python").path,["-m","pip","install","Pillow==11.3.0"])]
        func next(_ index:Int) {
            if stopped || index==commands.count {preparing=false;cancel.isEnabled=false;accept([]);status.stringValue=stopped ? "Tool preparation stopped. Retry when ready." : "Build tools prepared. Choose Build / Resume.";return}
            status.stringValue="Preparing build tools…";let command=commands[index]
            execute(command.0,command.1) { [weak self] code in
                guard let self else{return}
                if code==0 {next(index+1)} else {
                    self.preparing=false;self.cancel.isEnabled=false;self.accept([])
                    if self.stopped {self.status.stringValue="Tool preparation stopped. Retry when ready."}
                    else {self.failed(toolDiagnostic(self.recentOutput))}
                }
            }
        }
        append("Preparing Homebrew build packages and a private Python environment with Pillow 11.3.0. Homebrew may show its own permission prompts.\n");next(0)
    }
    @objc func start() {
        guard let rom,let melee,process==nil else{return};stopped=false;preparing=false
        guard let python=setupPython(destination) ?? setupPython(kit) else {
            status.stringValue="Choose Prepare Build Tools first.";return
        }
        do {
            if destination.path.contains(where:{$0.isWhitespace}) {throw NSError(domain:"Setup",code:1,userInfo:[NSLocalizedDescriptionKey:"Choose a build folder without spaces. Game filenames may contain spaces."])}
            try FileManager.default.createDirectory(at:destination,withIntermediateDirectories:true)
            let source=kit.appendingPathComponent("outputs/sm64-melee-lab"),target=destination.appendingPathComponent("outputs/sm64-melee-lab")
            guard FileManager.default.fileExists(atPath:source.appendingPathComponent("release/Setup.py").path) else {throw NSError(domain:"Setup",code:2,userInfo:[NSLocalizedDescriptionKey:"The complete source kit is missing. Download and extract it again."])}
            for name in ["native","release","branding"] {
                let out=target.appendingPathComponent(name);try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true)
                let copy=Process();copy.executableURL=URL(fileURLWithPath:"/usr/bin/rsync");copy.arguments=["-a","--exclude=dist","--exclude=*.app","--exclude=__pycache__",source.appendingPathComponent(name).path+"/",out.path+"/"]
                try copy.run();copy.waitUntilExit();if copy.terminationStatus != 0 {throw NSError(domain:"Setup",code:3)}
            }
            var arguments=[target.appendingPathComponent("release/Setup.py").path,"--rom",rom.path,"--melee",melee.path,"--cancel-file",cancelFile.path,"--launch"]
            if CommandLine.arguments.contains("--check"){arguments=Array(arguments.dropLast())+["--check"]}
            try? FileManager.default.removeItem(at:cancelFile)
            try? FileManager.default.removeItem(at:destination.appendingPathComponent("work/setup-error.json"))
            status.stringValue="Checking your files and available build space…"
            execute(python,arguments) { [weak self] code in
                guard let self else{return};self.cancel.isEnabled=false;self.accept([])
                if code==0 {self.status.stringValue="Ready. Super Melee 64 has been built."}
                else if code==130 || self.stopped {self.status.stringValue="Stopped safely. Choose Build / Resume to continue."}
                else {self.failed(savedDiagnostic(self.destination.appendingPathComponent("work/setup-error.json")) ?? toolDiagnostic(self.recentOutput))}
            }
        } catch {status.stringValue=error.localizedDescription;append(error.localizedDescription+"\n");accept([])}
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {process==nil}
}
if let index=CommandLine.arguments.firstIndex(of:"--diagnose-output"),index+1<CommandLine.arguments.count {
    let output=(try? String(contentsOfFile:CommandLine.arguments[index+1],encoding:.utf8)) ?? ""
    if let diagnostic=toolDiagnostic(output) {
        print(String(decoding:try! JSONEncoder().encode(diagnostic),as:UTF8.self));exit(0)
    }
    exit(2)
}
if CommandLine.arguments.contains("--validate") {
    var results:[String:String]=[:]
    for path in CommandLine.arguments.dropFirst().filter({!$0.hasPrefix("--")}) {results[path]=identifyGame(URL(fileURLWithPath:path)) ?? "unsupported"}
    let data=try! JSONSerialization.data(withJSONObject:results,options:[.prettyPrinted,.sortedKeys]);print(String(decoding:data,as:UTF8.self))
    exit(Set(results.values) == Set(["melee", "sm64"]) ? 0 : 2)
}
let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = Installer(); app.delegate = delegate
app.run()
