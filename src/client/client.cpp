// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <api.h>
#include <command.h>
#include <database.h>
#include <directories.h>
#include <exceptions.h>
#include <file.h>
#include <package_data.h>
#include <resolver.h>
#include <settings.h>

// globals
#include <command_storage.h>
#include <db.h>
#include <db_file.h>
#include <file_storage.h>

#include <sw/builder/build.h>
#include <sw/builder/driver.h>
#include <sw/driver/cpp/driver.h>
#include <jumppad.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string_regex.hpp>
#include <boost/dll.hpp>
#include <boost/regex.hpp>
#include <primitives/executor.h>
#include <primitives/file_monitor.h>
#include <primitives/lock.h>
#include <primitives/pack.h>
#include <primitives/sw/settings.h>
#include <primitives/sw/main.h>
#include <primitives/thread.h>
#include <primitives/win32helpers.h>

#include <iostream>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "main");

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <Objbase.h>
#include <Shlobj.h>

#include <WinReg.hpp>
#endif

#if _MSC_VER
#if defined(SW_USE_TBBMALLOC)
#include "tbb/include/tbb/tbbmalloc_proxy.h"
#elif defined(SW_USE_TCMALLOC)
//"libtcmalloc_minimal.lib"
//#pragma comment(linker, "/include:__tcmalloc")
#else
#endif
#endif

using namespace sw;

bool bConsoleMode = true;
bool bUseSystemPause = false;

namespace sw::driver::cpp { SW_REGISTER_PACKAGE_DRIVER(CppDriver); }

/*
// check args here to see if we want gui or not!

// 1. if 'uri' arg - console depends
// 2. if no args, no sw.cpp, *.sw files in cwd - gui
*/

#pragma push_macro("main")
#undef main
int main(int argc, char **argv);
#pragma pop_macro("main")

int sw_main(const Strings &args);
void stop();
void setup_log(const std::string &log_level);
void self_upgrade();
void self_upgrade_copy(const path &dst);

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    const std::wstring s = GetCommandLine();
    bConsoleMode = s.find(L"uri sw:") == -1;
    if (bConsoleMode)
    {
        SetupConsole();
    }
    else
    {
        CoInitialize(0);
    }

#pragma push_macro("main")
#undef main
    return main(__argc, __argv);
#pragma pop_macro("main")
}
#endif

static cl::opt<path> working_directory("d", cl::desc("Working directory"));
extern bool gVerbose;
static cl::opt<bool> trace("trace", cl::desc("Trace output"));
static cl::opt<int> jobs("j", cl::desc("Number of jobs"), cl::init(-1));

static cl::opt<int> sleep_seconds("sleep", cl::desc("Sleep on startup"), cl::Hidden);

static cl::opt<bool> cl_self_upgrade("self-upgrade", cl::desc("Upgrade client"));
static cl::opt<path> cl_self_upgrade_copy("internal-self-upgrade-copy", cl::desc("Upgrade client: copy file"), cl::ReallyHidden);

extern cl::opt<bool> useFileMonitor;

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

int setup_main(const Strings &args)
{
    // some initial stuff

    if (sleep_seconds > 0)
        std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));

    if (!cl_self_upgrade_copy.empty())
    {
        self_upgrade_copy(cl_self_upgrade_copy);
        return 0;
    }

    if (cl_self_upgrade)
    {
        self_upgrade();
        return 0;
    }

    if (!working_directory.empty())
    {
        if (fs::is_regular_file(working_directory))
            fs::current_path(working_directory.parent_path());
        else
            fs::current_path(working_directory);
    }

    if (trace)
        setup_log("TRACE");
    else if (gVerbose)
        setup_log("DEBUG");
    else
        setup_log("INFO");

    // init
    getServiceDatabase();

    primitives::filesystem::FileMonitor fm;
    sw::getFileMonitor(&fm);
    joining_thread_with_object_run_stop fmt(fm);

    // before storages
    // Create QSBR context for the main thread.
    //auto context = createConcurrentContext();
    //getConcurrentContext(&context);

    SCOPE_EXIT
    {
        // Destroy the QSBR context for the main thread.
        //destroyConcurrentContext(context);
    };

    // before CommandStorage and FileStorages
    sw::FileDb db;
    sw::getDb(&db);

    sw::CommandStorage cs;
    sw::getCommandStorage(&cs);

    // before FileStorages
    sw::FileDataHashMap fshm;
    sw::getFileData(&fshm);

    sw::FileStorages fs;
    sw::getFileStorages(&fs);

    // after everything
    Executor e(select_number_of_threads(jobs));
    getExecutor(&e);

    /*boost::asio::io_context io_service;
    boost::asio::signal_set signals(io_service, SIGINT);
    signals.async_wait([&e]()
    {
        e.stop();
    });
    io_service.run();*/

    // actual execution
    return sw_main(args);
}

int parse_main(int argc, char **argv)
{
    //args::ValueFlag<int> configuration(parser, "configuration", "Configuration to build", { 'c' });

    String overview = "SW: Software Network Client\n"
        "\n"
        "  SW is a Universal Package Manager and Build System...\n";
    if (auto &driver = getDrivers(); !driver.empty())
    {
        overview += "\n  Available drivers:\n";
        for (auto &d : driver)
            overview += "    - " + d->getName() + "\n";
    }

    const std::vector<std::string> args0(argv + 1, argv + argc);
    Strings args;
    args.push_back(argv[0]);
    for (auto &a : args0)
    {
        std::vector<std::string> t;
        boost::split_regex(t, a, boost::regex("%20"));
        args.insert(args.end(), t.begin(), t.end());
    }

    if (args.size() > 1 && args[1] == "internal-call-builtin-function")
    {
        return jumppad_call(args);
    }

    //useFileMonitor = false;

    //
    cl::ParseCommandLineOptions(args, overview);

    return setup_main(args);
}

int main(int argc, char **argv)
{
    int r = 0;
    String error;
    bool supress = false;
    try
    {
        r = parse_main(argc, argv);
    }
    catch (SupressOutputException &)
    {
        supress = true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
    }

    stop();

    if (!error.empty() || supress)
    {
        if (!supress)
        {
            LOG_ERROR(logger, error);
#ifdef _WIN32
            //if (IsDebuggerPresent())
                //system("pause");
#else
            //std::cout << "Press any key to continue..." << std::endl;
            //getchar();
#endif
        }
        r = 1;

        if (!bConsoleMode)
        {
#ifdef _WIN32
            if (bUseSystemPause)
            {
                system("pause");
            }
            else
                message_box(sw::getProgramName(), error);
#endif
        }
    }

    LOG_FLUSH();

    return r;
}

#define SUBCOMMAND_DECL(n) void cli_##n()
#define SUBCOMMAND(n, d) SUBCOMMAND_DECL(n);
#include <commands.inl>
#undef SUBCOMMAND

#define SUBCOMMAND(n, d) cl::SubCommand subcommand_##n(#n, d);
#include <commands.inl>
#undef SUBCOMMAND

//
//static cl::list<path> build_arg0(cl::Positional, cl::desc("Files or directoris to build"));

// build commands
// must be opt<String>!
static cl::opt<String> build_arg(cl::Positional, cl::desc("File or directory to build"), cl::init("."), cl::sub(subcommand_build));
static cl::opt<String> build_arg_generate(cl::Positional, cl::desc("File or directory to use to generate projects"), cl::init("."), cl::sub(subcommand_generate));
static cl::opt<String> build_arg_update(cl::Positional, cl::desc("File or directory to use to generate projects"), cl::init("."), cl::sub(subcommand_update));
static cl::opt<String> build_arg_test(cl::Positional, cl::desc("File or directory to use to generate projects"), cl::init("."), cl::sub(subcommand_test));
static cl::opt<String> list_arg(cl::Positional, cl::desc("Package regex to list"), cl::init("."), cl::sub(subcommand_list));
static cl::opt<String> install_arg(cl::Positional, cl::desc("Packages to add"), cl::sub(subcommand_install));
static cl::list<String> install_args(cl::ConsumeAfter, cl::desc("Packages to add"), cl::sub(subcommand_install));

// upload
static cl::opt<String> upload_prefix(cl::Positional, cl::desc("Prefix path"), cl::sub(subcommand_upload), cl::Required);
static cl::opt<bool> build_before_upload("build", cl::desc("Build before upload"), cl::sub(subcommand_upload));

// ide commands
static cl::opt<String> target_build("target", cl::desc("Target to build")/*, cl::sub(subcommand_ide)*/);
static cl::opt<String> ide_rebuild("rebuild", cl::desc("Rebuild target"), cl::sub(subcommand_ide));
static cl::opt<String> ide_clean("clean", cl::desc("Clean target"), cl::sub(subcommand_ide));

//static cl::list<String> override_package("override-remote-package", cl::value_desc("prefix sdir"), cl::desc("Provide a local copy of remote package"), cl::multi_val(2));
static cl::opt<String> override_package("override-remote-package", cl::value_desc("prefix"), cl::desc("Provide a local copy of remote package(s)"));
static cl::alias override_package2("override", cl::desc("Alias for -override-remote-package"), cl::aliasopt(override_package));
static cl::opt<bool> list_overridden_packages("list-overridden-remote-packages", cl::desc("List overridden packages"));
static cl::opt<String> delete_overridden_package("delete-overridden-remote-package", cl::value_desc("package"), cl::desc("Delete overridden package from index"));
static cl::opt<path> delete_overridden_package_dir("delete-overridden-remote-package-dir", cl::value_desc("sdir"), cl::desc("Delete overridden dir packages"));

// uri commands
extern bool gRunAppInContainer;
static cl::opt<bool, true> run_app_in_container("in-container", cl::desc("Print file with build graph"), cl::location(gRunAppInContainer), cl::sub(subcommand_uri));

extern bool gUseLockFile;

//static cl::list<String> builtin_function("internal-call-builtin-function", cl::desc("Call built-in function"), cl::Hidden);

void override_package_perform();

int sw_main(const Strings &args)
{
    if (list_overridden_packages)
    {
        std::map<sw::PackageId, path> pkgs;
        for (auto &[pkg, p] : getServiceDatabase().getOverriddenPackages())
            pkgs[pkg] = p.sdir;
        for (auto &[n, p] : pkgs)
            std::cout << n.toString() << " " << p << "\n";
        return 0;
    }

    if (!override_package.empty())
    {
        override_package_perform();
        return 0;
    }

    if (!delete_overridden_package.empty())
    {
        sw::PackageId pkg{ delete_overridden_package };
        LOG_INFO(logger, "Delete override for " + pkg.toString());
        getServiceDatabase().deleteOverriddenPackage(pkg);
        return 0;
    }

    if (!delete_overridden_package_dir.empty())
    {
        LOG_INFO(logger, "Delete override for sdir " + delete_overridden_package_dir.u8string());

        auto d = fs::canonical(fs::absolute(delete_overridden_package_dir));

        std::map<sw::PackageId, path> pkgs;
        for (auto &[pkg, p] : getServiceDatabase().getOverriddenPackages())
        {
            if (p.sdir == d)
                pkgs[pkg] = p.sdir;
        }
        for (auto &[n, p] : pkgs)
            std::cout << "Deleting " << n.toString() << "\n";

        getServiceDatabase().deleteOverriddenPackageDir(delete_overridden_package_dir);
        return 0;
    }

    if (gUseLockFile && fs::exists(fs::current_path() / "sw.lock"))
        getPackageStore().loadLockFile(fs::current_path() / "sw.lock");

    /*if (!build_arg0.empty())
    {
        sw::build(Files{ build_arg0.begin(), build_arg0.end() });
        return 0;
    }*/

    if (0);
#define SUBCOMMAND(n, d) else if (subcommand_##n) { cli_##n(); return 0; }
#include <commands.inl>
#undef SUBCOMMAND

    LOG_WARN(logger, "No command was issued");

    return 0;
}

void stop()
{
    //getFileMonitor().stop();
    //getExecutor().join();
    //getFileStorages().clear();
    if (gUseLockFile)
        getPackageStore().saveLockFile(fs::current_path() / "sw.lock");
}

static cl::opt<bool> write_log_to_file("log-to-file");

void setup_log(const std::string &log_level)
{
    LoggerSettings log_settings;
    log_settings.log_level = log_level;
    if (write_log_to_file && bConsoleMode)
        log_settings.log_file = (get_root_directory() / "sw").string();
    log_settings.simple_logger = true;
    log_settings.print_trace = true;
    initLogger(log_settings);

    // first trace message
    LOG_TRACE(logger, "----------------------------------------");
    LOG_TRACE(logger, "Starting sw...");
}

#define SUBCOMMAND_DECL_URI(c) SUBCOMMAND_DECL(uri_ ## c)

SUBCOMMAND_DECL(build)
{
    sw::build(build_arg);
}

static cl::list<String> uri_args(cl::Positional, cl::desc("sw uri arguments"), cl::sub(subcommand_uri));
//static cl::opt<String> uri_sdir("sw:sdir", cl::desc("Open source dir in file browser"), cl::sub(subcommand_uri));

SUBCOMMAND_DECL(uri)
{
    if (uri_args.empty())
        return;
    if (uri_args.size() == 1)
        return;

    try
    {
        auto p = extractFromStringPackageId(uri_args[1]);
        auto &sdb = getServiceDatabase();

        if (uri_args[0] == "sw:sdir" || uri_args[0] == "sw:bdir")
        {
#ifdef _WIN32
            if (sdb.isPackageInstalled(p))
            {
                auto pidl = uri_args[0] == "sw:sdir" ?
                    ILCreateFromPath(p.getDirSrc2().wstring().c_str()) :
                    ILCreateFromPath(p.getDirObj().wstring().c_str())
                    ;
                if (pidl)
                {
                    CoInitialize(0);
                    auto r = SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
                    if (FAILED(r))
                    {
                        message_box(sw::getProgramName(), "Error in SHOpenFolderAndSelectItems");
                    }
                    ILFree(pidl);
                }
            }
            else
            {
                message_box(sw::getProgramName(), "Package '" + p.toString() + "' not installed");
            }
#endif
        }

        if (uri_args[0] == "sw:install")
        {
#ifdef _WIN32
            if (!sdb.isPackageInstalled(p))
            {
                SetupConsole();
                bUseSystemPause = true;
                Resolver r;
                r.resolve_dependencies({ {p.ppath, p.version} });
            }
            else
            {
                message_box(sw::getProgramName(), "Package '" + p.toString() + "' is already installed");
            }
#endif
        }

        if (uri_args[0] == "sw:remove")
        {
            sdb.removeInstalledPackage(p);
            error_code ec;
            fs::remove_all(p.getDir(), ec);
        }

        if (uri_args[0] == "sw:build")
        {
#ifdef _WIN32
            SetupConsole();
            bUseSystemPause = true;
#endif
            auto d = getUserDirectories().storage_dir_tmp / "build";// / fs::unique_path();
            fs::create_directories(d);
            ScopedCurrentPath scp(d, CurrentPathScope::All);
            sw::build(p);
        }

        if (uri_args[0] == "sw:run")
        {
#ifdef _WIN32
            SetupConsole();
            bUseSystemPause = true;
#endif
            auto d = getUserDirectories().storage_dir_tmp / "build";// / fs::unique_path();
            fs::create_directories(d);
            ScopedCurrentPath scp(d, CurrentPathScope::All);
            sw::run(p);
        }

        if (uri_args[0] == "sw:upload")
        {
            if (uri_args.size() != 4)
                return;

            PackageId pkg(uri_args[1]);
            Version new_version(uri_args[2]);

            String url = "https://raw.githubusercontent.com/SoftwareNetwork/specifications/master/";
            url += normalize_path(pkg.getHashPathFull() / "sw.cpp");
            auto fn = get_temp_filename("uploads") / "sw.cpp";
            auto spec_data = download_file(url);
            boost::replace_all(spec_data, pkg.version.toString(), new_version.toString());
            write_file(fn, spec_data);

            primitives::Command c;
            c.program = "sw";
            c.working_directory = fn.parent_path();
            c.args.push_back("upload");
            c.args.push_back(".");
            c.args.push_back(pkg.ppath.slice(0, std::stoi(uri_args[3])));
            c.out.inherit = true;
            c.err.inherit = true;
            c.execute();

            fs::remove_all(fn.parent_path());
        }
    }
    catch (std::exception &e)
    {
#ifdef _WIN32
        message_box(sw::getProgramName(), e.what());
#endif
    }
    catch (...)
    {
#ifdef _WIN32
        message_box(sw::getProgramName(), "Unknown exception");
#endif
    }
}

#include <solution.h>

void override_package_perform()
{
    auto s = sw::load(".");
    auto &b = *((sw::Build*)s.get());
    b.prepareStep();

    //auto s = sw::load(override_package[1]);
    for (auto &[pkg, desc] : s->getPackages())
    {
        sw::PackagePath prefix = override_package;
        sw::PackageId pkg2{ prefix / pkg.ppath, pkg.version };
        auto dir = fs::absolute(".");
        //auto dir = fs::absolute(override_package[1]);
        LOG_INFO(logger, "Overriding " + pkg2.toString() + " to " + dir.u8string());
        // fix deps' prefix
        sw::UnresolvedPackages deps;
        for (auto &d : desc->getData().dependencies)
        {
            if (d.ppath.isAbsolute())
                deps.insert(d);
            else
                deps.insert({ prefix / d.ppath, d.range });
        }
        getServiceDatabase().overridePackage(pkg2, { dir, deps, 0, (int)prefix.size() });
    }
}

SUBCOMMAND_DECL(ide)
{
    //useFileMonitor = false;

    if (!target_build.empty())
    {
        try_single_process_job(fs::current_path() / SW_BINARY_DIR / "ide", []()
        {
            auto s = sw::load(working_directory);
            auto &b = *((sw::Build*)s.get());
            b.ide = true;
            auto pkg = sw::extractFromStringPackageId(target_build);
            b.TargetsToBuild[pkg] = b.children[pkg];
            s->execute();
        });
    }
    else
    {
        single_process_job(fs::current_path() / SW_BINARY_DIR / "ide", []()
        {
            auto s = sw::load(working_directory);
            auto &b = *((sw::Build*)s.get());
            b.ide = true;
            s->execute();
        });
    }
}

extern ::cl::opt<String> generator;

SUBCOMMAND_DECL(generate)
{
    if (generator.empty())
    {
#ifdef _WIN32
        generator = "vs";
#endif
    }
    build_arg = build_arg_generate.getValue();
    cli_build();
}

SUBCOMMAND_DECL(init)
{
    elevate();

#ifdef _WIN32
    auto prog = boost::dll::program_location().wstring();

    // set common environment variable
    //winreg::RegKey env(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    //env.SetStringValue(L"SW_TOOL", boost::dll::program_location().wstring());

    // set up protocol handler
    {
        const std::wstring id = L"sw";

        winreg::RegKey url(HKEY_CLASSES_ROOT, id);
        url.SetStringValue(L"URL Protocol", L"");

        winreg::RegKey icon(HKEY_CLASSES_ROOT, id + L"\\DefaultIcon");
        icon.SetStringValue(L"", prog);

        winreg::RegKey open(HKEY_CLASSES_ROOT, id + L"\\shell\\open\\command");
        open.SetStringValue(L"", prog + L" uri %1");
    }

    // register .sw extension
    {
        const std::wstring id = L"sw.1";

        winreg::RegKey ext(HKEY_CLASSES_ROOT, L".sw");
        ext.SetStringValue(L"", id);

        winreg::RegKey icon(HKEY_CLASSES_ROOT, id + L"\\DefaultIcon");
        icon.SetStringValue(L"", prog);

        winreg::RegKey p(HKEY_CLASSES_ROOT, id + L"\\shell\\open\\command");
        p.SetStringValue(L"", prog + L" build %1");
    }
#endif
}

SUBCOMMAND_DECL(list)
{
    getPackagesDatabase().listPackages(list_arg);
}

SUBCOMMAND_DECL(pack)
{
    // http://www.king-foo.com/2011/11/creating-debianubuntu-deb-packages/
}

extern bool gWithTesting;

SUBCOMMAND_DECL(test)
{
    gWithTesting = true;
    build_arg = build_arg_test.getValue();
    cli_build();
}

SUBCOMMAND_DECL(install)
{
    sw::UnresolvedPackages pkgs;
    install_args.push_back(install_arg);
    for (auto &p : install_args)
        pkgs.insert(extractFromString(p));
    resolveAllDependencies(pkgs);
    for (auto &[p1, d] : getPackageStore().resolved_packages)
    {
        for (auto &p2 : install_args)
            if (p1 == p2)
                d.installed = true;
    }
}

extern ::cl::opt<bool> dry_run;
SUBCOMMAND_DECL(update)
{
    getPackageStore() = sw::PackageStore();
    dry_run = true;
    build_arg = build_arg_update.getValue();
    cli_build();
}

SUBCOMMAND_DECL(upload)
{
    sw::FetchOptions opts;
    //opts.name_prefix = upload_prefix;
    opts.root_dir = fs::current_path() / SW_BINARY_DIR;
    opts.ignore_existing_dirs = true;
    opts.existing_dirs_age = std::chrono::hours(8);
    //opts.apply_version_to_source = true;
    auto s = sw::fetch_and_load(build_arg_update.getValue(), opts);
    if (build_before_upload)
        s->execute();

    auto &us = Settings::get_user_settings();
    auto cr = us.remotes.begin();
    auto current_remote = &*cr++;

    sw::Api api(*current_remote);
    api.addVersion(upload_prefix, s->getPackages(), sw::read_config(build_arg_update.getValue()).value());
}

EXPORT_FROM_EXECUTABLE
std::string getVersionString()
{
    std::string s;
    s += ::sw::getProgramName();
    s += " version ";
    s += PACKAGE_VERSION;
    s += "\n";
    s += "assembled " __DATE__ " " __TIME__;
    return s;
}

EXPORT_FROM_EXECUTABLE
std::string getProgramName()
{
    return PACKAGE_NAME_CLEAN;
}

void self_upgrade()
{
#ifdef _WIN32
    path client = "/client/sw-master-windows-client.zip";
#elif __APPLE__
    path client = "/client/sw-master-macos-client.zip";
#else
    path client = "/client/.service/sw-master-linux-client.zip";
#endif

    auto &s = Settings::get_user_settings();

    std::cout << "Downloading checksum file" << "\n";
    auto md5sum = boost::algorithm::trim_copy(download_file(s.remotes[0].url + client.u8string() + ".md5"));

    auto fn = fs::temp_directory_path() / (unique_path() += client.extension());
    std::cout << "Downloading the latest client" << "\n";
    download_file(s.remotes[0].url + client.u8string(), fn, 50_MB);
    if (md5sum != md5(fn))
        throw std::runtime_error("Downloaded bad file (md5 check failed)");

    std::cout << "Unpacking" << "\n";
    auto tmp_dir = fs::temp_directory_path() / "sw.bak";
    unpack_file(fn, tmp_dir);
    fs::remove(fn);

    // self update
    auto program = path(boost::dll::program_location().wstring());
#ifdef _WIN32
    auto exe = (tmp_dir / "sw.exe").wstring();
    auto arg0 = L"\"" + exe + L"\"";
    auto dst = L"\"" + program.wstring() + L"\"";
    std::cout << "Replacing client" << "\n";
    auto cmd_line = arg0 + L" -internal-self-upgrade-copy " + dst;
    STARTUPINFO si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcess(exe.c_str(), &cmd_line[0], 0, 0, 0, 0, 0, 0, &si, &pi))
    {
        throw std::runtime_error("errno = "s + std::to_string(errno) + "\n" +
            "Cannot do a self upgrade. Replace this file with newer SW client manually.");
    }
#else
    auto cppan = tmp_dir / "sw";
    fs::permissions(cppan, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
    fs::remove(program);
    fs::copy_file(cppan, program);
    fs::remove(cppan);
#endif
}

void self_upgrade_copy(const path &dst)
{
    int n = 3;
    while (n--)
    {
        std::cout << "Waiting old program to exit...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
        try
        {
            fs::copy_file(boost::dll::program_location().wstring(), dst, fs::copy_options::overwrite_existing);
            break;
        }
        catch (std::exception &e)
        {
            std::cerr << "Cannot replace program with new executable: " << e.what() << "\n";
            if (n == 0)
                throw;
            std::cerr << "Retrying... (" << n << ")\n";
        }
    }
    std::cout << "Success!\n";
}

#if _MSC_VER
#if defined(SW_USE_JEMALLOC)
#define JEMALLOC_NO_PRIVATE_NAMESPACE
#include <jemalloc-5.1.0/include/jemalloc/jemalloc.h>
//#include <jemalloc-5.1.0/src/jemalloc_cpp.cpp>
#endif
#endif
