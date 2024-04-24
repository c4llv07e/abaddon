#include "platform.hpp"
#include <config.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace std::literals::string_literals;

#if defined(_WIN32)
    #include <pango/pangocairo.h>
    #include <pango/pangofc-fontmap.h>
    #if defined(_MSC_VER)
        #include <ShlObj_core.h>
    #else
        #include <shlobj.h>
    #endif
    #include <Shlwapi.h>
    #include <Windows.h>
    #pragma comment(lib, "Shlwapi.lib")
bool Platform::SetupFonts() {
    using namespace std::string_literals;

    char buf[MAX_PATH] { 0 };
    GetCurrentDirectoryA(MAX_PATH, buf);
    {
        // thanks @WorkingRobot for da help :^))

        std::ifstream template_stream(buf + R"(\fonts\fonts.template.conf)"s);
        std::ofstream conf_stream(buf + R"(\fonts\fonts.conf)"s);
        if (!template_stream.good()) {
            printf("can't open fonts/fonts.template.conf\n");
            return false;
        }
        if (!conf_stream.good()) {
            printf("can't open write to fonts.conf\n");
            return false;
        }

        std::string line;
        while (std::getline(template_stream, line)) {
            if (line == "<!--(CONFD)-->")
                conf_stream << "<include ignore_missing=\"no\">" << (buf + R"(\fonts\conf.d)"s) << "</include>";
            else
                conf_stream << line;
            conf_stream << '\n';
        }
    }

    auto fc = FcConfigCreate();
    FcConfigSetCurrent(fc);
    FcConfigParseAndLoad(fc, const_cast<FcChar8 *>(reinterpret_cast<const FcChar8 *>((buf + R"(\fonts\fonts.conf)"s).c_str())), true);
    FcConfigAppFontAddDir(fc, const_cast<FcChar8 *>(reinterpret_cast<const FcChar8 *>((buf + "\\fonts"s).c_str())));

    char fonts_path[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_FONTS, nullptr, SHGFP_TYPE_CURRENT, fonts_path) == S_OK) {
        FcConfigAppFontAddDir(fc, reinterpret_cast<FcChar8 *>(fonts_path));
    }

    auto map = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
    pango_fc_font_map_set_config(reinterpret_cast<PangoFcFontMap *>(map), fc);
    pango_cairo_font_map_set_default(reinterpret_cast<PangoCairoFontMap *>(map));

    return true;
}
#else
bool Platform::SetupFonts() {
    return true;
}
#endif

#if defined(_WIN32)
std::string Platform::FindResourceFolder() {
    return ".";
}

std::string Platform::FindConfigFile() {
    const auto x = std::getenv("ABADDON_CONFIG");
    if (x != nullptr)
        return x;
    return "./abaddon.ini";
}

std::string Platform::FindStateCacheFolder() {
    return ".";
}

#elif defined(__linux__)
static std::string TryUseEnvForResurceFolder(std::string env) {
    const static std::string data_path = env + "/abaddon"s;
    for (const auto &path : { "."s, data_path, std::string(ABADDON_DEFAULT_RESOURCE_DIR) }) {
        if (util::IsFolder(path + "/res") && util::IsFolder(path + "/css")) {
            return path;
        }
    }
    return "";
}

std::string Platform::FindResourceFolder() {
    static std::string found_path;
    static bool found = false;
    if (found) return found_path;

    const auto data_env = std::getenv("XDG_DATA_HOME");
    const auto path = TryUseEnvForResurceFolder(data_env);
    if (path != "") {
        found_path = path;
        found = true;
        return path;
    }

    const auto home_env = std::getenv("HOME");
    if (home_env != nullptr) {
        const static std::string home_path = home_env + "/.local/share"s;
        const auto path = TryUseEnvForResurceFolder(home_path);
        if (path != "") {
            found_path = path;
            found = true;
            return found_path;
        }
    }

    spdlog::get("discord")->warn("cant find a resources folder, will try to load from cwd");
    found_path = ".";
    found = true;
    return found_path;
}

static std::string FindOrCreateConfigDirInPath(std::string path) {
    if (auto config_path = path + "/abaddon/abaddon.ini"s; util::IsFile(config_path)) {
        return config_path;
    }
    std::error_code ec;
    const auto config_path = path + "/abaddon"s;
    if (!util::IsFolder(config_path))
        std::filesystem::create_directories(config_path, ec);
    if (util::IsFolder(config_path))
        return config_path + "/abaddon.ini";
    return "";
}

std::string Platform::FindConfigFile() {
    const auto cfg = std::getenv("ABADDON_CONFIG");
    if (cfg != nullptr) return cfg;

    // use config present in cwd first
    if (util::IsFile("./abaddon.ini"))
        return "./abaddon.ini";

    if (const auto config_env = std::getenv("XDG_CONFIG_HOME")) {
        const auto path = FindOrCreateConfigDirInPath(config_env);
        if (path != "")
            return path;
    }

    if (const auto home_env = std::getenv("HOME")) {
        const auto config_path = std::string(home_env) + "/.config";
        const auto path = FindOrCreateConfigDirInPath(config_path);
        if (path != "")
            return path;
    }

    // fallback to cwd if cant find + cant make in ~/.config
    spdlog::get("discord")->warn("can't find configuration file!");
    return "./abaddon.ini";
}

static std::string FindOrCreateCacheDirInPath(std::string path) {
    auto cache_path = path + "/abaddon"s;
    std::error_code ec;
    if (!util::IsFolder(cache_path))
        std::filesystem::create_directories(cache_path, ec);
    if (util::IsFolder(cache_path))
        return cache_path;
    return "";
}

std::string Platform::FindStateCacheFolder() {
    if (const auto cache_env = std::getenv("XDG_CACHE_HOME")) {
        const auto path = FindOrCreateCacheDirInPath(cache_env);
        if (path != "")
            return path;
    }
    if (const auto home_env = std::getenv("HOME")) {
        const auto cache_path = std::string(home_env) + "/.cache"s;
        const auto path = FindOrCreateCacheDirInPath(cache_path);
        if (path != "")
            return path;
    }
    spdlog::get("discord")->warn("can't find cache folder!");
    return ".";
}

#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#include <sys/param.h>
#include <sys/stat.h>
std::string Platform::FindResourceFolder() {
    static std::string found_path;
    static bool found = false;
    if (found) return found_path;

    CFURLRef resourceURL = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
    char resourcePath[PATH_MAX];
    if (CFURLGetFileSystemRepresentation(resourceURL, true, (UInt8 *)resourcePath, PATH_MAX)) {
        if (resourceURL != NULL) {
            CFRelease(resourceURL);
        }
        found_path = resourcePath;
        found = true;
        return found_path;
    }

    spdlog::get("discord")->warn("cant find a resources folder, will try to load from cwd");
    found_path = ".";
    found = true;
    return found_path;
}

std::string Platform::FindConfigFile() {
    const auto cfg = std::getenv("ABADDON_CONFIG");
    if (cfg != nullptr) return cfg;

    passwd *home = getpwuid(getuid());
    const char *homeDir = home->pw_dir;

    char appSupportPath[PATH_MAX];
    snprintf(appSupportPath, sizeof(appSupportPath), "%s/Library/Application Support", homeDir);

    char homefolder_path[PATH_MAX];
    snprintf(homefolder_path, sizeof(homefolder_path), "%s/%s", appSupportPath, "com.github.uowuo.abaddon");

    if (mkdir(homefolder_path, 0755) == 0) {
        spdlog::get("discord")->warn("created Application Support dir");
    }
    
    char home_path[PATH_MAX];
    snprintf(home_path, sizeof(home_path), "%s/%s", homefolder_path, "/abaddon.ini");
    
    return home_path;
}

std::string Platform::FindStateCacheFolder() {
    
    passwd *home = getpwuid(getuid());
    const char *homeDir = home->pw_dir;

    char appSupportPath[PATH_MAX];
    snprintf(appSupportPath, sizeof(appSupportPath), "%s/Library/Application Support", homeDir);

    char home_path[PATH_MAX];
    snprintf(home_path, sizeof(home_path), "%s/%s", appSupportPath, "com.github.uowuo.abaddon");

    return home_path;
}


#else
std::string Platform::FindResourceFolder() {
    spdlog::get("discord")->warn("unknown OS, trying to load resources from cwd");
    return ".";
}

std::string Platform::FindConfigFile() {
    const auto x = std::getenv("ABADDON_CONFIG");
    if (x != nullptr)
        return x;
    spdlog::get("discord")->warn("unknown OS, trying to load config from cwd");
    return "./abaddon.ini";
}

std::string Platform::FindStateCacheFolder() {
    spdlog::get("discord")->warn("unknown OS, setting state cache folder to cwd");
    return ".";
}
#endif
