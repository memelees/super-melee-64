import AppKit

struct SetupDiagnostic: Codable {
    let code: String, title: String, message: String, recovery: String
    var detail: String?
}
func toolDiagnostic(_ output: String) -> SetupDiagnostic? {
    let lower = output.lowercased()
    let missingSoundTool = lower.range(of:"i686-w64-mingw32-(?:as|objcopy|objdump)['\"]?:\\s*(?:no such file|(?:command )?not found)",options:.regularExpression) != nil
    if missingSoundTool || lower.contains("sm64 sound build tools are missing") || lower.contains("prerequisites") && lower.contains("i686-w64-mingw32-") {
        return SetupDiagnostic(code:"mac-sound-tools",title:"Install the SM64 sound build tools",
            message:"The Mac build is missing tools used to assemble SM64's sound data. Homebrew provides them in mingw-w64; the game still runs natively on Mac.",
            recovery:"Choose Prepare Build Tools in the latest installer. Manual repair: run brew install mingw-w64 in Terminal. Then choose Build / Resume in the same folder. Verified downloads and build outputs are kept.")
    }
    if lower.contains("gmake") && lower.contains("prerequisites") || lower.contains("level_rules.mk") && lower.contains("no such file") || lower.contains("gnu make is required") {
        return SetupDiagnostic(code:"gnu-make",title:"Install GNU Make",
            message:"The SM64 build needs GNU Make. Apple's bundled Make 3.81 skips required preparation steps.",
            recovery:"Choose Prepare Build Tools in the latest installer. Manual repair: run brew install make in Terminal. Then choose Build / Resume in the same folder. Verified downloads are kept.")
    }
    if lower.contains("gib is available") || lower.contains("no space left on device") || lower.contains("disk full") {
        return SetupDiagnostic(code:"disk-space",title:"More free disk space is needed",
            message:output.components(separatedBy:"\n").last(where:{$0.lowercased().contains("gib is available")}) ?? "The build drive ran out of free space.",
            recovery:"Free space on the build drive, or use Build Folder to choose another writable drive. At least 12 GiB free is required; 20 GiB is recommended. Then choose Build / Resume. Verified downloads are kept.")
    }
    if lower.contains("without spaces") {
        return SetupDiagnostic(code:"build-path",title:"Choose a build folder without spaces",message:"The source build tools cannot use this build-folder path.",recovery:"Use Build Folder to choose a writable path such as ~/SuperMelee64. Game filenames may contain spaces.")
    }
    if lower.contains("permission denied") || lower.contains("not writable") {
        return SetupDiagnostic(code:"folder-access",title:"Setup cannot write a required file",message:"The current account does not have access to the selected build folder or file.",recovery:"Choose a writable build folder owned by your account. Do not run setup as root or administrator. Your original game files are unchanged.")
    }
    if ["could not resolve host","connection timed out","failed to connect","curl: ("].contains(where:lower.contains) {
        return SetupDiagnostic(code:"download-network",title:"A required download could not finish",message:"The download server or network connection was unavailable.",recovery:"Check your connection and retry the same operation. If the server is temporarily unavailable, wait a few minutes. Existing verified downloads are kept.")
    }
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
func installedGame(_ receipt: URL) -> URL? {
    struct Receipt: Decodable {let app:String,installed:Bool,signature_verified:Bool,startup_verified:Bool}
    guard let data=try? Data(contentsOf:receipt),data.count<=65536,
          let result=try? JSONDecoder().decode(Receipt.self,from:data),
          result.installed,result.signature_verified,result.startup_verified,result.app.hasPrefix("/") else {return nil}
    let url=URL(fileURLWithPath:result.app)
    guard let bundle=Bundle(url:url),bundle.bundleIdentifier=="dev.supermelee64.launcher",
          let executable=bundle.executableURL,FileManager.default.isExecutableFile(atPath:executable.path) else {return nil}
    return url
}

func identifyGame(_ url: URL) -> String? {
    guard let handle=try? FileHandle(forReadingFrom:url) else { return nil }
    let header=(try? handle.read(upToCount:8)) ?? Data();try? handle.close()
    if header.count==8 && header.prefix(6)==Data("GALE01".utf8) && header[7]==2 { return "melee" }
    if header.prefix(4)==Data([0x80,0x37,0x12,0x40]) { return "sm64" }
    return nil
}
func setupEnvironment() -> [String:String] {
    var environment=ProcessInfo.processInfo.environment
    let paths=(environment["PATH"] ?? "").split(separator:":").map(String.init)
    environment["PATH"]=(paths+["/opt/homebrew/opt/python@3.12/bin","/opt/homebrew/bin","/usr/local/bin","/usr/bin","/bin","/usr/sbin","/sbin"]).joined(separator:":")
    environment["PYTHONUNBUFFERED"]="1"
    if environment["SSL_CERT_FILE"] == nil {environment["SSL_CERT_FILE"]="/etc/ssl/cert.pem"}
    return environment
}
func setupProgram(_ name: String) -> String? {
    for directory in (setupEnvironment()["PATH"] ?? "").split(separator:":") {
        let path=URL(fileURLWithPath:String(directory)).appendingPathComponent(name).path
        if FileManager.default.isExecutableFile(atPath:path) {return path}
    }
    return nil
}
func setupPython(_ kit: URL) -> String? {
    let candidates = [kit.appendingPathComponent(".venv/bin/python").path] +
        ["python3","python3.14","python3.13","python3.12"].compactMap(setupProgram)
    for path in candidates where FileManager.default.isExecutableFile(atPath: path) {
        let probe = Process(); probe.executableURL = URL(fileURLWithPath: path)
        probe.environment=setupEnvironment()
        probe.arguments = ["-c", "import sys; import PIL; sys.exit(0 if sys.version_info >= (3, 12) else 1)"]
        probe.standardOutput = FileHandle.nullDevice; probe.standardError = FileHandle.nullDevice
        do { try probe.run(); probe.waitUntilExit(); if probe.terminationStatus == 0 { return path } } catch { continue }
    }
    return nil
}
func synchronizeSourceKit(_ source: URL, to target: URL) throws {
    guard FileManager.default.fileExists(atPath:source.appendingPathComponent("release/Setup.py").path) else {
        throw NSError(domain:"Setup",code:2,userInfo:[NSLocalizedDescriptionKey:"The complete source kit is missing. Download and extract it again."])
    }
    for name in ["native","release","branding"] {
        let out=target.appendingPathComponent(name)
        try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true)
        let copy=Process();copy.executableURL=URL(fileURLWithPath:"/usr/bin/rsync")
        // Release ZIPs use reproducible timestamps. Size/mtime alone can retain
        // older source when an update changes bytes without changing length.
        copy.arguments=["-a","--checksum","--exclude=dist","--exclude=*.app","--exclude=__pycache__",source.appendingPathComponent(name).path+"/",out.path+"/"]
        try copy.run();copy.waitUntilExit()
        if copy.terminationStatus != 0 {throw NSError(domain:"Setup",code:3,userInfo:[NSLocalizedDescriptionKey:"The installer could not copy its source kit into the build folder. Check the folder is writable and the drive has space, then extract the complete release ZIP again if needed."])}
        // Python's timestamp/size bytecode check has the same upgrade hazard.
        let cache=out.appendingPathComponent("__pycache__")
        if FileManager.default.fileExists(atPath:cache.path) {try FileManager.default.removeItem(at:cache)}
    }
}
struct SetupToolReport: Decodable {
    let missing: [String]
    struct Toolchain: Decodable {
        let ok: Bool
        var code: String?, title: String?, message: String?, recovery: String?, detail: String?
        var diagnostic: SetupDiagnostic? {
            guard !ok,let code,let title,let message,let recovery else {return nil}
            return SetupDiagnostic(code:code,title:title,message:message,recovery:recovery,detail:detail)
        }
    }
    var mac_toolchain: Toolchain?
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
    let openInstalled=NSButton(title:"Open Game",target:nil,action:nil)
    let showInstalled=NSButton(title:"Show in Finder",target:nil,action:nil)
    var installedApp:URL?
    var process: Process?, preparing=false, stopped=false, recentOutput=""
    var kit: URL { Bundle.main.resourceURL!.appendingPathComponent("SourceKit") }
    var cancelFile: URL {destination.appendingPathComponent("work/setup.cancel")}
    var installerVersion: String {Bundle.main.object(forInfoDictionaryKey:"SMBInstallerVersion") as? String ?? "development"}
    func attemptHeader(_ action:String) {
        append("\nSUPER MELEE 64 INSTALLER \(installerVersion) — \(ISO8601DateFormatter().string(from:Date())) — \(action)\n")
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        window=NSWindow(contentRect:NSRect(x:0,y:0,width:720,height:650),styleMask:[.titled,.closable,.miniaturizable],backing:.buffered,defer:false)
        window.title="Install Super Melee 64 — \(installerVersion)";window.center();window.delegate=self
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
        openInstalled.target=self;openInstalled.action=#selector(openGame);openInstalled.frame=NSRect(x:320,y:360,width:140,height:32);openInstalled.isHidden=true;content.addSubview(openInstalled)
        build.target=self;build.action=#selector(start);build.frame=NSRect(x:472,y:360,width:224,height:32);build.isEnabled=false;content.addSubview(build)
        button("Prepare Build Tools",#selector(prepareTools),24,316,184)
        button("Open Setup Log",#selector(openLog),220,316,152)
        showInstalled.target=self;showInstalled.action=#selector(showGame);showInstalled.frame=NSRect(x:388,y:316,width:180,height:32);showInstalled.isHidden=true;content.addSubview(showInstalled)
        cancel.target=self;cancel.action=#selector(stop);cancel.frame=NSRect(x:584,y:316,width:112,height:32);cancel.isEnabled=false;content.addSubview(cancel)
        progress.frame=NSRect(x:26,y:292,width:668,height:14);progress.minValue=0;progress.maxValue=12;progress.isIndeterminate=false;content.addSubview(progress)
        let scroll=NSScrollView(frame:NSRect(x:24,y:24,width:672,height:248));scroll.hasVerticalScroller=true
        log.isEditable=false;log.font = .monospacedSystemFont(ofSize:11,weight:.regular);scroll.documentView=log;content.addSubview(scroll)
        window.makeKeyAndOrderFront(nil);NSApp.activate(ignoringOtherApps:true)
        attemptHeader("Opened installer")
        append("Your original US SM64 and Melee USA Rev 2 files stay on this Mac. Setup downloads official patches and builds the game locally.\nAllow at least 12 GiB free; 20 GiB is recommended. Cancel keeps verified downloads for Resume.\n")
        if CommandLine.arguments.count>1 {accept(CommandLine.arguments.dropFirst().filter{!$0.hasPrefix("--")}.map{URL(fileURLWithPath:$0)})}
        _ = refreshInstalledApp()
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
        if panel.runModal() == .OK,let folder=panel.url {destination=folder;accept([]);_ = refreshInstalledApp()}
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
        // GUI output includes failures before the backend Session opens setup.log.
        NSWorkspace.shared.open(FileManager.default.fileExists(atPath:gui.path) ? gui : FileManager.default.fileExists(atPath:path.path) ? path : destination)
    }
    @objc func openGame() {if process==nil,let installedApp {NSWorkspace.shared.open(installedApp)}}
    @objc func showGame() {if let installedApp {NSWorkspace.shared.activateFileViewerSelecting([installedApp])}}
    @discardableResult func refreshInstalledApp() -> Bool {
        installedApp=installedGame(destination.appendingPathComponent("work/native-app-build.json"))
        openInstalled.isHidden=installedApp==nil;showInstalled.isHidden=installedApp==nil
        if let installedApp {status.stringValue="Installed: "+installedApp.path;return true}
        return false
    }
    func failed(_ diagnostic: SetupDiagnostic?) {
        let diagnostic=diagnostic ?? SetupDiagnostic(code:"setup-failed",title:"Setup could not continue",
            message:"The setup process stopped without a recognised error. The complete output is in the setup log.",
            recovery:"Open Setup Log and share the latest attempt with the project maintainer. Include the installer version and error code below. Build / Resume keeps verified downloads and completed outputs.")
        status.stringValue=diagnostic.title+". Follow the repair instructions, then retry."
        let details=diagnostic.title+"\n"+diagnostic.message+"\n\n"+diagnostic.recovery+"\n\nError code: "+diagnostic.code+"\nInstaller version: "+installerVersion
        append("\n"+details+"\n")
        let alert=NSAlert();alert.alertStyle = .warning;alert.messageText=diagnostic.title
        alert.informativeText=diagnostic.message+"\n\n"+diagnostic.recovery+"\n\nError code: "+diagnostic.code+"\nInstaller version: "+installerVersion
        alert.addButton(withTitle:"OK");alert.addButton(withTitle:"Open Setup Log")
        alert.addButton(withTitle:"Copy Error Details")
        alert.beginSheetModal(for:window) { [weak self] response in
            if response == .alertSecondButtonReturn {self?.openLog()}
            if response == .alertThirdButtonReturn {NSPasteboard.general.clearContents();NSPasteboard.general.setString(details,forType:.string)}
        }
    }
    @objc func stop() {
        guard process != nil else{return};stopped=true;status.stringValue="Stopping safely. Verified downloads will be kept…";cancel.isEnabled=false
        if preparing {process?.interrupt()} else {try? FileManager.default.createDirectory(at:cancelFile.deletingLastPathComponent(),withIntermediateDirectories:true);try? Data().write(to:cancelFile)}
    }
    func windowShouldClose(_ sender:NSWindow)->Bool {if process != nil {stop();return false};return true}
    func execute(_ path:String,_ arguments:[String],_ completion:@escaping(Int32)->Void) {
        let task=Process(),pipe=Pipe();task.executableURL=URL(fileURLWithPath:path);task.arguments=arguments
        task.environment=setupEnvironment();task.standardOutput=pipe;task.standardError=pipe;recentOutput=""
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
    @objc func prepareTools() {prepareRequiredTools(then:nil)}
    func prepareRequiredTools(then ready:(()->Void)?) {
        guard process==nil else{return};stopped=false;preparing=true
        try? FileManager.default.createDirectory(at:destination,withIntermediateDirectories:true)
        attemptHeader("Prepare Build Tools")
        let selection=Process();selection.executableURL=URL(fileURLWithPath:"/usr/bin/xcode-select");selection.arguments=["-p"]
        selection.standardOutput=FileHandle.nullDevice;selection.standardError=FileHandle.nullDevice
        var toolsSelected=false
        do {try selection.run();selection.waitUntilExit();toolsSelected=selection.terminationStatus==0} catch {append(error.localizedDescription+"\n")}
        guard toolsSelected else {
            execute("/usr/bin/xcode-select",["--install"]) { [weak self] _ in self?.preparing=false;self?.status.stringValue="Finish Apple's tools installer, then choose Build / Resume.";self?.cancel.isEnabled=false;self?.accept([]) };return
        }
        guard let brew=setupProgram("brew") else {
            preparing=false
            status.stringValue="Install Homebrew from its official site, then choose Prepare Build Tools."
            append("Opening Homebrew's official installation instructions. This helper does not run downloaded shell scripts.\n")
            NSWorkspace.shared.open(URL(string:"https://brew.sh")!);return
        }
        execute(brew,["--prefix"]) { [weak self] code in
            guard let self else{return}
            let prefix=self.recentOutput.trimmingCharacters(in:.whitespacesAndNewlines)
            guard code==0,!self.stopped,prefix.hasPrefix("/"),!prefix.contains("\n") else {
                self.preparing=false;self.cancel.isEnabled=false;self.accept([])
                if self.stopped {self.status.stringValue="Tool preparation stopped. Retry when ready."}
                else {self.failed(toolDiagnostic(self.recentOutput))};return
            }
            self.preparePackages(brew:brew,prefix:prefix,then:ready)
        }
    }
    func preparePackages(brew:String,prefix:String,then ready:(()->Void)?) {
        let python=prefix+"/opt/python@3.12/bin/python3.12",venv=destination.appendingPathComponent(".venv")
        let commands:[(String,[String])]=[(brew,["install","python@3.12","gcc","make","mingw-w64","cmake","ninja","sdl2","sdl3","glew","pkg-config","xdelta","sevenzip"]),(python,["-m","venv",venv.path]),(venv.appendingPathComponent("bin/python").path,["-m","pip","install","Pillow==11.3.0"])]
        func next(_ index:Int) {
            if stopped {preparing=false;cancel.isEnabled=false;accept([]);status.stringValue="Tool preparation stopped. Retry when ready.";return}
            if index==commands.count {
                checkTools(venv.appendingPathComponent("bin/python").path,script:kit.appendingPathComponent("outputs/sm64-melee-lab/release/Setup.py"),prepareIfNeeded:false) {
                    self.status.stringValue="Build tools verified. Choose Build / Resume."
                    ready?()
                };return
            }
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
    func checkTools(_ python:String,script:URL,prepareIfNeeded:Bool,ready:@escaping()->Void) {
        preparing=true;status.stringValue="Checking build tools…"
        execute(python,[script.path,"--check-tools"]) { [weak self] code in
            guard let self else{return};self.preparing=false;self.cancel.isEnabled=false;self.accept([])
            if self.stopped {self.status.stringValue="Stopped safely. Choose Build / Resume to continue.";return}
            guard let report=try? JSONDecoder().decode(SetupToolReport.self,from:Data(self.recentOutput.utf8)) else {
                self.failed(toolDiagnostic(self.recentOutput));return
            }
            if !report.missing.isEmpty {
                self.append("Required build tools: "+report.missing.joined(separator:", ")+"\n")
                if prepareIfNeeded {self.prepareRequiredTools(then:{self.startBuild(prepareIfNeeded:false)})}
                else {self.failed(toolDiagnostic("Install the prerequisites: "+report.missing.joined(separator:", ")) ?? SetupDiagnostic(code:"missing-tools",title:"Build tools are still missing",message:report.missing.joined(separator:", "),recovery:"Open Setup Log for the tool preparation output, resolve its reported error, then choose Build / Resume. Verified downloads are kept."))}
                return
            }
            guard code==0,report.mac_toolchain?.ok != false else {self.failed(report.mac_toolchain?.diagnostic ?? toolDiagnostic(self.recentOutput));return}
            ready()
        }
    }
    @objc func start() {startBuild(prepareIfNeeded:!CommandLine.arguments.contains("--check"))}
    func startBuild(prepareIfNeeded:Bool) {
        guard let rom,let melee,process==nil else{return};stopped=false;preparing=false
        installedApp=nil;openInstalled.isHidden=true;showInstalled.isHidden=true
        try? FileManager.default.createDirectory(at:destination,withIntermediateDirectories:true)
        attemptHeader("Build / Resume")
        guard let python=setupPython(destination) ?? setupPython(kit) else {
            if prepareIfNeeded {prepareRequiredTools(then:{self.startBuild(prepareIfNeeded:false)});return}
            failed(SetupDiagnostic(code:"missing-python",title:"Prepare the Python build tools",message:"Setup needs Python 3.12 or newer with Pillow to build the game.",recovery:"Choose Prepare Build Tools, let it finish, then choose Build / Resume. The finished game does not need Python during gameplay."));return
        }
        do {
            if destination.path.contains(where:{$0.isWhitespace}) {throw NSError(domain:"Setup",code:1,userInfo:[NSLocalizedDescriptionKey:"Choose a build folder without spaces. Game filenames may contain spaces."])}
            try FileManager.default.createDirectory(at:destination,withIntermediateDirectories:true)
            let source=kit.appendingPathComponent("outputs/sm64-melee-lab"),target=destination.appendingPathComponent("outputs/sm64-melee-lab")
            try synchronizeSourceKit(source,to:target)
            var arguments=[target.appendingPathComponent("release/Setup.py").path,"--rom",rom.path,"--melee",melee.path,"--cancel-file",cancelFile.path,"--launch"]
            if CommandLine.arguments.contains("--check"){arguments=Array(arguments.dropLast())+["--check"]}
            try? FileManager.default.removeItem(at:cancelFile)
            try? FileManager.default.removeItem(at:destination.appendingPathComponent("work/setup-error.json"))
            let launchBuild = { [self] in
                self.preparing=false;self.status.stringValue="Checking your files and available build space…"
                self.execute(python,arguments) { [weak self] code in
                guard let self else{return};self.cancel.isEnabled=false;self.accept([])
                if code==0 {
                    if CommandLine.arguments.contains("--check") {self.status.stringValue="Files and tools checked. No game was installed.";return}
                    guard self.refreshInstalledApp() else {
                        self.failed(SetupDiagnostic(code:"mac-app-install",title:"The installed app could not be found",message:"Setup finished without a valid installed app. It has not reported a successful installation.",recovery:"Choose Build / Resume in this installer to retry installing the app into Applications. Verified build outputs are kept."));return
                    }
                }
                else if code==130 || self.stopped {self.status.stringValue="Stopped safely. Choose Build / Resume to continue."}
                else {self.failed(savedDiagnostic(self.destination.appendingPathComponent("work/setup-error.json")) ?? toolDiagnostic(self.recentOutput))}
                }
            }
            if CommandLine.arguments.contains("--check") {launchBuild()}
            else {checkTools(python,script:target.appendingPathComponent("release/Setup.py"),prepareIfNeeded:prepareIfNeeded,ready:launchBuild)}
        } catch {
            append(error.localizedDescription+"\n");accept([])
            failed(toolDiagnostic(error.localizedDescription) ?? SetupDiagnostic(code:"prepare-folder",title:"The installer could not prepare the build folder",message:error.localizedDescription,recovery:"Check the build folder is writable and the complete release ZIP has been extracted. Choose the same folder to resume verified downloads, or a new empty folder if you want a fresh install."))
        }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool {process==nil}
}
if let index=CommandLine.arguments.firstIndex(of:"--installed-app"),index+1<CommandLine.arguments.count {
    if let app=installedGame(URL(fileURLWithPath:CommandLine.arguments[index+1])) {print(app.path);exit(0)}
    exit(2)
}
if let index=CommandLine.arguments.firstIndex(of:"--sync-source-kit"),index+2<CommandLine.arguments.count {
    do {try synchronizeSourceKit(URL(fileURLWithPath:CommandLine.arguments[index+1]),to:URL(fileURLWithPath:CommandLine.arguments[index+2]));exit(0)}
    catch {fputs(error.localizedDescription+"\n",stderr);exit(1)}
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
