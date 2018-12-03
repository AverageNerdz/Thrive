#include "thrive_version.h"
#include "ThriveServer.h"
#include "thrive_world_factory.h"

#include "resource.h"

#include "Define.h"
#include "Logger.h"
#include "GlobalCEFHandler.h"

#ifdef _WIN32
#include "include/cef_sandbox_win.h"
#endif

#include <string>
#include <iostream>



#define PROGRAMUSE_CUSTOMJS 0
#if PROGRAMUSE_CUSTOMJS == 1
// Define the actual macro //
#define LEVIATHAN_USES_CUSTOMJS
// Include required files //
#include "GlobalCEFHandler.h"
#include "GUI/GuiCEFApplication.h"
#endif

#ifdef LEVIATHAN_USES_CUSTOMJS
// Include the header //
#include "generated/thrive_v8_extension.h"
#include "thrive_js_interface.h"
#endif


// Breakpad is used to detect and report crashes
#ifdef LEVIATHAN_USING_BREAKPAD
#ifdef __linux
#include "client/linux/handler/exception_handler.h"
#elif defined(_WIN32)
#include "client/windows/handler/exception_handler.h"
#else
#error no breakpad on platform
#endif
#endif //USE_BREAKPAD

using namespace thrive;
// This is for easier logic with using standard classes instead of application specific
using namespace Leviathan;

// Don't look at the mess ahead, just set the variables in your cmake file //

#if LEVIATHAN_USING_BREAKPAD
#ifdef _WIN32
#ifndef _DEBUG
bool DumpCallback(const wchar_t* dump_path,
    const wchar_t* minidump_id, void* context, EXCEPTION_POINTERS* exinfo,
    MDRawAssertionInfo* assertion, bool succeeded)
{
    
    const string path = Convert::WstringToString(dump_path);
    
    printf("Dump path: %s\n", path.c_str());
    
    return succeeded;
}
#endif //_DEBUG
#else
bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context,
    bool succeeded)
{
    printf("Dump path: %s\n", descriptor.path());
    return succeeded;
}
#endif
#endif //USE_BREAKPAD

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
    int nCmdShow)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif

#else
int main(int argcount, char* args[]){
#endif
    
#ifdef _WIN32
    int argcount = 1;
    char* args[] = { lpCmdLine };
#endif
    
    int Return = 0;

    // We need to manage the lifetime of an object //
    std::shared_ptr<Leviathan::CEFApplicationKeeper> KeepThisSafe;

    // This needs to create the sandbox as otherwise this doesn't work
#ifdef CEF_ENABLE_SANDBOX
    // Manage the life span of the sandbox information object. This is necessary
    // for sandbox support on Windows. See cef_sandbox_win.h for complete details.
    CefScopedSandboxInfo sandbox = CefScopedSandboxInfo();
#endif
    
    
    // The default CEF handling needs to be the first thing in the program //
    if(Leviathan::GlobalCEFHandler::CEFFirstCheckChildProcess(argcount, args, Return,
            KeepThisSafe, "ThriveServer"
        #ifdef _WIN32
        #ifdef CEF_ENABLE_SANDBOX
            , sandbox
        #endif
            , hInstance
        #endif
        ))
    {
        // This was a child process, end it now //
        return Return;
    }
    // This is the main process, continue normally //
    
#ifndef _WIN32
    // We need to skip the program name
    args += 1;
    --argcount;
#endif // _WIN32
    
#ifdef _WIN32
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    if(SUCCEEDED(CoInitialize(NULL))){
#else

#endif

#if LEVIATHAN_USING_BREAKPAD
    // Crash handler //
#ifdef _WIN32
#ifndef _DEBUG
    google_breakpad::ExceptionHandler ExceptionHandler(L"C://tmp", NULL, DumpCallback, NULL, 
        google_breakpad::ExceptionHandler::HANDLER_ALL,
        (MINIDUMP_TYPE)(MiniDumpNormal & MiniDumpWithThreadInfo),
        (const wchar_t*)nullptr, NULL);
        
#endif //_DEBUG
#else
    google_breakpad::MinidumpDescriptor descriptor("/tmp");

    google_breakpad::ExceptionHandler ExceptionHandler(descriptor, NULL, DumpCallback, NULL,
        true, -1);
#endif
#endif //USE_BREAKPAD

    // Logger. This is here to make logging shutdown errors etc. possible //
    Logger mainLog("ThriveServer" + std::string("Log.txt"));

    // World creator object //
    ThriveWorldFactory worldFactory;
    
    // Create program object //
    ThriveServer app;

    std::unique_ptr<AppDef> ProgramDefinition(AppDef::GenerateAppdefine(
            "./EngineConf.conf", "./ThriveServer.conf", "./ThriveKeybindings.conf",
            &ThriveServer::CheckGameConfigurationVariables, &ThriveServer::CheckGameKeyConfigVariables));

    // Fail if no definition could be created //
    if(!ProgramDefinition){

        std::cout << "FATAL: failed to create AppDefine" << std::endl;
        return 2;
    }
    
    
    // customize values //
#ifdef _WIN32
    ProgramDefinition->SetHInstance(hInstance);
#endif
    ProgramDefinition->SetMasterServerParameters(MasterServerInformation()).
        SetApplicationIdentification(
        "Thrive server version " GAME_VERSIONS, "Thrive",
        "GAME_VERSIONS");
    
    // Create window last //
    ProgramDefinition->StoreWindowDetails(ThriveServer::GenerateWindowTitle(), true,
#ifdef _WIN32
        LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)),
#endif
        &app);

    if(!app.PassCommandLine(argcount, args)){

        std::cout << "Error: Invalid Command Line arguments. Shutting down" << std::endl;
        return 3;
    }

    // Register our custom JS object if we are using one //
#ifdef LEVIATHAN_USES_CUSTOMJS
    Leviathan::GlobalCEFHandler::RegisterCustomJavaScriptQueryHandler(
        std::make_shared<ThriveJSInterface>());
    
    // Register the custom file //
    Leviathan::GlobalCEFHandler::RegisterCustomExtension(
        std::make_shared<Leviathan::GUI::CustomExtension>(
            "ThriveJSInterface", thrive_v8_extensionStr, &makeThriveJSHandler,
            std::make_shared<ThriveJSMessageHandler>()));
    
#endif //LEVIATHAN_USES_CUSTOMJS
        
    
    if(app.Initialize(ProgramDefinition.get())){

        // this is where the game should customize the engine //
        app.CustomizeEnginePostLoad();

        LOG_INFO("Engine successfully initialized");
        Return = app.RunMessageLoop();
    } else {
        LOG_ERROR("App init failed, closing");
        app.ForceRelease();
        Return = 5;
    }
#ifdef _WIN32
    }
    //_CrtDumpMemoryLeaks();
    CoUninitialize();
#endif

    // The CEF application will go out of scope after this //
    KeepThisSafe.reset();

    // This needs to be called before quitting //
    Leviathan::GlobalCEFHandler::CEFLastThingInProgram();

    // Sandbox will go out of scope after this

    return Return;
}
