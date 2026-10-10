/*
 * This file is part of HPL2-Redux's Penumbra product.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// The Penumbra product's entry point (milestone 0 of docs/penumbra-levels.md):
// find the player's install, start the engine with a window, register the
// resource directories and log them, then shut down.
//
//   Penumbra --game <install> [--map <file> --start <name>]

#include "hpl.h"
#include "gui/Gui.h"
#include "resources/ConfigFile.h"
#include "resources/FileSearcher.h"
#include "resources/MeshLoader.h"

#include "GameInstall.h"

#include <SDL.h>

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

using namespace hpl;
namespace fs = std::filesystem;

//-----------------------------------------------------------------------

namespace {

	// Same personal root as Amnesia (LuxBasePersonal.h), own folder below it.
#if defined(_WIN32)
	const tWString kPersonalRoot = _W("");
#elif defined(__linux__)
	const tWString kPersonalRoot = _W(".frictionalgames/");
#else
	const tWString kPersonalRoot = _W("Library/Application Support/Frictional Games/");
#endif

	// This product's files, relative to the executable (staged from
	// penumbra/resources by premake/penumbra.lua).
	const tWString kMainInitConfig = _W("config/main_init.cfg");
	const tString kProductResourcesConfig = "resources.cfg";

	// Higher priority shadows lower for files of the same name
	// (cFileSearcher::GetFilePath). The engine registers its own directories
	// (core/*, compiled_shaders/*) at the default priority 0 while it starts.
	const int kInstallPriority = 20;

	//-----------------------------------------------------------------------

	class cOptions
	{
	public:
		tString msGame;
		tString msMap;
		tString msStart;
		bool mbHelp = false;
	};

	const char* kUsage =
		"Usage: Penumbra --game <install> [--map <file> --start <name>]\n"
		"\n"
		"  --game <install>  Penumbra install: the directory with resources.cfg, or\n"
		"                    the one above it when the data is in redist/\n"
		"  --map <file>      Map to load, e.g. level01_entrance.dae (milestone 1)\n"
		"  --start <name>    Start position in that map, e.g. link01\n"
		"  --help            Show this text\n";

	bool ParseArgs(const std::vector<tString>& avArgs, cOptions& aOptions, tString& asError)
	{
		for(size_t i=0; i<avArgs.size(); ++i)
		{
			const tString& sArg = avArgs[i];
			tString* pValue = nullptr;
			if(sArg == "--help" || sArg == "-h")	{ aOptions.mbHelp = true; continue; }
			else if(sArg == "--game")				pValue = &aOptions.msGame;
			else if(sArg == "--map")				pValue = &aOptions.msMap;
			else if(sArg == "--start")				pValue = &aOptions.msStart;
			else
			{
				asError = "unknown argument '" + sArg + "'";
				return false;
			}

			if(i+1 >= avArgs.size())
			{
				asError = sArg + " needs a value";
				return false;
			}
			*pValue = avArgs[++i];
		}

		if(aOptions.mbHelp) return true;
		if(aOptions.msGame.empty())
		{
			asError = "--game <install> is required";
			return false;
		}
		if(aOptions.msMap.empty() != aOptions.msStart.empty())
		{
			asError = "--map and --start go together";
			return false;
		}
		return true;
	}

	//-----------------------------------------------------------------------

	tString ToUtf8(const fs::path& aPath)
	{
		const std::u8string sPath = aPath.generic_u8string();
		return tString(sPath.begin(), sPath.end());
	}

	fs::path FromUtf8(const tString& asPath)
	{
		return fs::path(std::u8string(asPath.begin(), asPath.end()));
	}

	void CreateFolders(const tWString& asRoot, const tWString& asRelative)
	{
		tWString sPath = asRoot;
		tWStringVec vParts;
		tWString sSepp = _W("/");
		cString::GetStringVecW(asRelative, vParts, &sSepp);
		for(const tWString& sPart : vParts)
		{
			sPath = cString::AddSlashAtEndW(sPath) + sPart;
			if(cPlatform::FolderExists(sPath) == false) cPlatform::CreateFolder(sPath);
		}
	}

	// Every directory the file searcher indexed files from, highest priority
	// first: the order in which a name is looked up.
	void LogIndexedDirs(cResources* apResources)
	{
		std::map<tWString, std::pair<int, int>> mapDirs; // dir -> (priority, files)
		for(const auto& entry : apResources->GetFileSearcher()->GetAllFiles())
		{
			auto& dir = mapDirs[cString::GetFilePathW(entry.second.msPath)];
			dir.first = std::max(dir.first, entry.second.mlPriority);
			dir.second++;
		}

		std::vector<std::tuple<int, tWString, int>> vDirs;
		for(const auto& dir : mapDirs) vDirs.emplace_back(-dir.second.first, dir.first, dir.second.second);
		std::sort(vDirs.begin(), vDirs.end());

		Log("Indexed resource directories (priority, files, directory):\n");
		for(const auto& dir : vDirs)
			Log(" %3d %5d  %s\n", -std::get<0>(dir), std::get<2>(dir), cString::To8Char(std::get<1>(dir)).c_str());
	}

	//-----------------------------------------------------------------------

	int RunPenumbra(const std::vector<tString>& avArgs)
	{
		cOptions options;
		tString sError;
		if(ParseArgs(avArgs, options, sError) == false)
		{
			fprintf(stderr, "Penumbra: %s\n\n%s", sError.c_str(), kUsage);
			return 1;
		}
		if(options.mbHelp)
		{
			printf("%s", kUsage);
			return 0;
		}

		/////////////////////////
		// Find the install (relative to where we were started from)
		std::error_code ec;
		fs::path install = fs::absolute(FromUtf8(options.msGame), ec).lexically_normal();
		if(install.has_filename() == false) install = install.parent_path(); // trailing slash
		const fs::path dataRoot = penumbra::FindDataRoot(install);
		if(dataRoot.empty())
		{
			fprintf(stderr,
					"Penumbra: no Penumbra game data in '%s'.\n"
					"Expected resources.cfg in that directory or in its 'redist' subdirectory.\n",
					ToUtf8(install).c_str());
			return 1;
		}

		std::vector<penumbra::cInstallResourceDir> vInstallDirs;
		if(penumbra::ReadResourceDirs(dataRoot, vInstallDirs, sError) == false)
		{
			fprintf(stderr, "Penumbra: %s\n", sError.c_str());
			return 1;
		}

		/////////////////////////
		// The engine finds its own files relative to the working directory:
		// work from the executable's directory, wherever we were started from.
		if(char* pBasePath = SDL_GetBasePath())
		{
			fs::current_path(FromUtf8(pBasePath), ec);
			SDL_free(pBasePath);
		}

		/////////////////////////
		// Startup configs
		cConfigFile mainInit(kMainInitConfig);
		if(mainInit.Load() == false)
		{
			fprintf(stderr, "Penumbra: can't read %s next to the executable\n", cString::To8Char(kMainInitConfig).c_str());
			return 1;
		}

		/////////////////////////
		// User directory: log, settings and caches; never the install.
		const tWString sPersonalDir = cString::AddSlashAtEndW(cString::ReplaceCharToW(
			cPlatform::GetSystemSpecialPath(eSystemPath_Personal), _W("\\"), _W("/")));
		const tWString sUserFolder = kPersonalRoot + mainInit.GetStringW("Directories", "UserFolder", _W("Penumbra Redux"));
		const tWString sUserDir = cString::AddSlashAtEndW(sPersonalDir + sUserFolder);
		CreateFolders(sPersonalDir, sUserFolder);

		SetLogFile(sUserDir + mainInit.GetStringW("Directories", "LogFile", _W("penumbra.log")));

		const tWString sCacheFolder = mainInit.GetStringW("Directories", "MeshCache", _W("cache/meshes"));
		CreateFolders(sUserDir, sCacheFolder);
		iMeshLoader::SetMeshCacheDir(sUserDir + sCacheFolder);

		// The player's settings once they exist; until then the defaults.
		tWString sSettingsFile = sUserDir + mainInit.GetStringW("ConfigFiles", "UserSettings", _W("main_settings.cfg"));
		if(cPlatform::FileExists(sSettingsFile) == false)
			sSettingsFile = mainInit.GetStringW("ConfigFiles", "DefaultSettings", _W("config/default_main_settings.cfg"));
		cConfigFile settings(sSettingsFile);
		if(settings.Load() == false)
			Warning("Couldn't load settings '%s', using built-in defaults\n", cString::To8Char(sSettingsFile).c_str());

		Log("Penumbra\n");
		Log("--------------------------------------------------------\n");
		Log(" Install: %s\n", ToUtf8(install).c_str());
		Log(" Data root: %s\n", ToUtf8(dataRoot).c_str());
		Log(" User directory: %s\n", cString::To8Char(sUserDir).c_str());
		Log(" Mesh cache: %s\n", cString::To8Char(sUserDir + sCacheFolder).c_str());
		Log(" Settings: %s\n", cString::To8Char(sSettingsFile).c_str());
		Log("--------------------------------------------------------\n\n");

		/////////////////////////
		// Start the engine (this opens the window)
		cEngineInitVars vars;
		vars.mGraphics.mvScreenSize.x = settings.GetInt("Screen", "Width", 1280);
		vars.mGraphics.mvScreenSize.y = settings.GetInt("Screen", "Height", 720);
		vars.mGraphics.mlDisplay = settings.GetInt("Screen", "Display", 0);
		vars.mGraphics.mbFullscreen = settings.GetBool("Screen", "FullScreen", false);
		vars.mGraphics.mbVsync = settings.GetBool("Screen", "Vsync", true);
		vars.mGraphics.msWindowCaption = mainInit.GetString("Window", "Caption", "Penumbra");
		vars.mGraphics.mRendererBackend =
			cString::ToLowerCase(settings.GetString("Graphics", "Renderer", "RayTraced")) == "standard" ?
				eRendererBackend_Standard : eRendererBackend_RayTraced;

		cEngine* pEngine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_All, &vars);
		if(pEngine == nullptr)
		{
			Error("Couldn't start the engine\n");
			return 1;
		}
		cResources* pResources = pEngine->GetResources();

		/////////////////////////
		// Resource directories, highest priority first. The engine has already
		// added its own (core/shaders, core/textures, core/models and
		// compiled_shaders/*, relative to the executable) at priority 0.
		Log("Registering resource directories\n");
		Log("--------------------------------------------------------\n");
		Log(" Install (%s/resources.cfg, priority %d):\n", ToUtf8(dataRoot).c_str(), kInstallPriority);
		for(const penumbra::cInstallResourceDir& dir : vInstallDirs)
		{
			if(dir.mbExists == false)
			{
				Warning("  '%s' listed in resources.cfg does not exist: %s\n",
						dir.msConfigPath.c_str(), ToUtf8(dir.mPath).c_str());
				continue;
			}
			Log("  %s\n", ToUtf8(dir.mPath).c_str());
			pResources->AddResourceDir(cString::To16Char(ToUtf8(dir.mPath)), false, "*.*", kInstallPriority);
		}

		// This product's own files, each directory with the priority set in
		// its resources.cfg (below the install's).
		Log(" Penumbra product (%s):\n", kProductResourcesConfig.c_str());
		if(pResources->LoadResourceDirsFile(kProductResourcesConfig) == false)
			Error("Couldn't load the product's %s\n", kProductResourcesConfig.c_str());
		Log("--------------------------------------------------------\n\n");

		LogIndexedDirs(pResources);
		Log("\n");

		/////////////////////////
		// This product's GUI skin, and through it its default font.
		const tString sSkin = mainInit.GetString("Gui", "DefaultSkin", "penumbra_default.skin");
		if(pEngine->GetGui()->CreateSkin(sSkin))
			Log("Loaded GUI skin '%s'\n", sSkin.c_str());
		else
			Error("Couldn't load GUI skin '%s'\n", sSkin.c_str());

		int lResult = 0;
		if(options.msMap.empty() == false)
		{
			const tWString sMapPath = pResources->GetFileSearcher()->GetFilePath(options.msMap);
			if(sMapPath.empty())
			{
				Error("Map '%s' not found in the game data\n", options.msMap.c_str());
				lResult = 1;
			}
			else
			{
				// Loading the map comes with milestone 1.
				Log("Map '%s' (start '%s'): %s\n", options.msMap.c_str(), options.msStart.c_str(),
					cString::To8Char(sMapPath).c_str());
			}
		}

		/////////////////////////
		// Shut down
		DestroyHPLEngine(pEngine);
		Log("Penumbra exited cleanly\n");
		return lResult;
	}

}

//-----------------------------------------------------------------------

// Arguments arrive one by one, so install paths with spaces need no quoting
// rules of our own. On Linux this main replaces the engine's (HplMainShim.cpp
// joins argv into one string); on Windows WinMain still comes from the engine.
#ifdef _WIN32
int hplMain(const tString& asCommandLine)
{
	std::vector<tString> vArgs;
	for(int i=1; i<__argc; ++i) vArgs.push_back(__argv[i]);
	return RunPenumbra(vArgs);
}
#else
int main(int argc, char* argv[])
{
	setlocale(LC_CTYPE, "");
	std::vector<tString> vArgs;
	for(int i=1; i<argc; ++i) vArgs.push_back(argv[i]);
	return RunPenumbra(vArgs);
}
#endif
