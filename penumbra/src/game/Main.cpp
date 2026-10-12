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

// The Penumbra product's entry point (milestone 5 of docs/penumbra-levels.md):
// find the player's install and view its geometry, entities, areas and environment,
// with a free camera or a walking body. Without a map, check startup and exit.
//
//   Penumbra --game <install> [--map <file> --start <name>]
//            [--renderer standard|raytraced] [--frames N] [--walk]

#include "hpl.h"
#include "gui/Gui.h"
#include "resources/ConfigFile.h"
#include "resources/FileSearcher.h"
#include "resources/XmlHelper.h"
#include <tinyxml2.h>
#include "resources/MeshLoader.h"
#include "resources/WorldLoaderHandler.h"
#include "resources/WorldLoaderPenumbraCollada.h"
#include "resources/EntityLoader_PenumbraObject.h"
#include "scene/Camera.h"
#include "scene/World.h"
#include "scene/Viewport.h"
#include "engine/Updater.h"
#include "graphics/PostEffectComposite.h"
#include "graphics/PostEffect_ToneMap.h"

#include "GameInstall.h"
#include "FreeCamera.h"
#include "MapScripts.h"
#include "physics/Physics.h"
#include "physics/SurfaceData.h"

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
#include <charconv>
#include <cmath>

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

	class cAreaLoader : public iAreaLoader
	{
	public:
		cAreaLoader(const tString& type) : iAreaLoader(type) {}
		void Load(const tString& name, int, bool, const cVector3f& size, const cMatrixf& transform, cWorld* world) override
		{
			auto* area = world->CreateAreaEntity(name);
			area->msType = GetName(); area->mvSize = size; area->m_mtxTransform = transform;
		}
	};

	class cOptions
	{
	public:
		tString msGame;
		tString msMap;
		tString msStart;
		tString msRenderer;
		int mlFrames = 0;
		bool mbHelp = false;
		bool mbWalk = false;
		bool mbDebugAreas = false;
		bool mbScriptCheck = false;
		int mlScriptSteps = 120;
		std::vector<tString> mvScriptActions;
		tString msScriptApiStatus;
	};

	const char* kUsage =
		"Usage: Penumbra --game <install> [--map <file> --start <name>]\n"
		"\n"
		"  --game <install>  Penumbra install: the directory with resources.cfg, or\n"
		"                    the one above it when the data is in redist/\n"
		"  --map <file>      Map to view, e.g. level00_01_boat_cabin.dae\n"
		"  --start <name>    Start position in that map, e.g. link01\n"
		"  --renderer <name> standard or raytraced (overrides settings)\n"
		"  --frames <N>      Render N frames, then exit (smoke testing)\n"
		"  --walk            Walk with collision and gravity; Space jumps\n"
		"  --debug-areas     Show area bounds and labels (F3 toggles)\n"
		"  --script-check    Run scripts without graphics, audio or windows\n"
		"  --script-steps N  Fixed 60 Hz ticks for script checks\n"
		"  --script-action <tick:action:name>  Scheduled test event\n"
		"  --script-api-status <file>  Write the API status table\n"
		"  Controls: WASD fly, Q/E down/up, Shift faster, mouse look, Esc quit\n"
		"  Scripts: left mouse interact, right mouse examine, U use item, Space/Enter dismiss\n"
		"  --help            Show this text\n";

	bool ParseArgs(const std::vector<tString>& avArgs, cOptions& aOptions, tString& asError)
	{
		for(size_t i=0; i<avArgs.size(); ++i)
		{
			const tString& sArg = avArgs[i];
			tString* pValue = nullptr;
			if(sArg == "--help" || sArg == "-h")	{ aOptions.mbHelp = true; continue; }
			else if(sArg == "--walk") { aOptions.mbWalk = true; continue; }
			else if(sArg == "--debug-areas") { aOptions.mbDebugAreas = true; continue; }
			else if(sArg == "--script-check") { aOptions.mbScriptCheck = true; continue; }
			else if(sArg == "--script-api-status") pValue = &aOptions.msScriptApiStatus;
			else if(sArg == "--script-action")
			{
				if(i+1>=avArgs.size()) { asError="--script-action needs tick:action:name"; return false; }
				aOptions.mvScriptActions.push_back(avArgs[++i]); continue;
			}
			else if(sArg == "--game")				pValue = &aOptions.msGame;
			else if(sArg == "--map")				pValue = &aOptions.msMap;
			else if(sArg == "--start")				pValue = &aOptions.msStart;
			else if(sArg == "--renderer") pValue = &aOptions.msRenderer;
			else if(sArg == "--frames" || sArg == "--script-steps")
			{
				if(i+1 >= avArgs.size()) { asError = "--frames needs a positive integer"; return false; }
				const tString& sFrames = avArgs[++i];
				int& count = sArg=="--frames" ? aOptions.mlFrames : aOptions.mlScriptSteps;
				auto result = std::from_chars(sFrames.data(), sFrames.data()+sFrames.size(), count);
				if(result.ec != std::errc() || result.ptr != sFrames.data()+sFrames.size() || count <= 0)
				{ asError = "--frames needs a positive integer"; return false; }
				continue;
			}
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
		if(!aOptions.msRenderer.empty() && aOptions.msRenderer != "standard" && aOptions.msRenderer != "raytraced")
		{ asError = "--renderer must be standard or raytraced"; return false; }
		if(aOptions.mlFrames > 0 && aOptions.msMap.empty())
		{ asError = "--frames requires --map and --start"; return false; }
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
		if(options.mbScriptCheck)
		{
			penumbra::cMapScripts scripts(dataRoot,nullptr,mainInit.GetString("Language","File","English.lang"));
			if(!options.msScriptApiStatus.empty() && !scripts.WriteApiStatus(FromUtf8(options.msScriptApiStatus))) return 1;
			if(!scripts.Load(options.msMap) || !scripts.HasStart(options.msStart)) { Error("Penumbra: script-check map/start invalid\n"); return 1; }
			std::multimap<int,std::pair<tString,tString>> actions;
			for(const auto& action : options.mvScriptActions)
			{
				const auto colon=action.find(':'); const auto next=action.find(':',colon==tString::npos ? 0 : colon+1);
				int tick=0;
				const auto parsed = std::from_chars(action.data(),action.data()+(colon==tString::npos ? 0 : colon),tick);
				if(colon==tString::npos || parsed.ec!=std::errc() || parsed.ptr!=action.data()+colon || tick<0)
				{ Error("Invalid script action '%s'\n",action.c_str()); return 1; }
				actions.emplace(tick,std::make_pair(action.substr(colon+1,next==tString::npos ? tString::npos : next-colon-1),next==tString::npos ? "" : action.substr(next+1)));
			}
			for(int tick=0;tick<options.mlScriptSteps && !scripts.Failed();++tick)
			{
				const auto range=actions.equal_range(tick);
				for(auto it=range.first;it!=range.second;++it)
				{
					const auto& kind=it->second.first; const auto& name=it->second.second;
					if(kind=="dismiss") scripts.DismissMessage();
					else if(kind=="pickup") scripts.Pickup(name);
					else if(kind=="interact") scripts.Interact(name);
					else if(kind=="examine") scripts.Examine(name);
					else if(kind=="invoke") scripts.Invoke(name);
					else { Error("Unknown script action '%s'\n",kind.c_str()); return 1; }
				}
				scripts.Update(1.0f/60);
				if(scripts.TransitionReady())
				{
					const auto map=scripts.NextMap(); const auto start=scripts.NextStart();
					scripts.Leave();
					if(!scripts.Load(map) || !scripts.HasStart(start)) { Error("Penumbra: script transition map/start invalid\n"); return 1; }
					Log("Penumbra script transition complete: %s, start '%s'\n",map.c_str(),start.c_str());
				}
			}
			// Script errors don't stop the run, but they fail the check.
			scripts.Leave();
			const bool okay=!scripts.Failed() && scripts.Errors()==0;
			Log("Penumbra script check: %s, map '%s', %d ticks, %zu inventory items\n",okay ? "PASS" : "FAIL",scripts.CurrentMap().c_str(),options.mlScriptSteps,scripts.Inventory().size());
			return okay ? 0 : 1;
		}

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
			(options.msRenderer.empty() ? cString::ToLowerCase(settings.GetString("Graphics", "Renderer", "RayTraced")) : options.msRenderer) == "standard" ?
				eRendererBackend_Standard : eRendererBackend_RayTraced;

		// Bounded (test) runs stay in the background: windowed, shown without
		// taking focus, and without grabbing the mouse.
		const bool bBackground = options.mlFrames > 0;
		if(bBackground)
		{
			vars.mGraphics.mbFullscreen = false;
			SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
		}

		cEngine* pEngine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_All, &vars);
		if(pEngine == nullptr)
		{
			Error("Couldn't start the engine\n");
			return 1;
		}
		if(bBackground) pEngine->GetInput()->GetLowLevel()->LockInput(false);
		cResources* pResources = pEngine->GetResources();
		pResources->GetWorldLoaderHandler()->AddLoader(hplNew(cWorldLoaderPenumbraCollada, ()));
		for(const char* type : {"Object", "Item", "SwingDoor", "Lamp", "Enemy"})
			pResources->AddEntityLoader(hplNew(cEntityLoader_PenumbraObject, (type)));
		for(const char* type : {"script", "link", "save", "ladder", "damage", "force", "liquid", "stick"})
			pResources->AddAreaLoader(hplNew(cAreaLoader, (type)));

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

		// HPL1 language files also index localized textures, fonts and voices.
		const tString sLanguage = mainInit.GetString("Language", "File", "English.lang");
		const tWString sLanguagePath = pResources->GetFileSearcher()->GetFilePath(sLanguage);
		tinyxml2::XMLDocument language;
		if(!sLanguagePath.empty() && LoadXmlFile(language, sLanguagePath) && language.RootElement())
		{
			const auto* dirs = language.RootElement()->FirstChildElement("RESOURCES");
			for(const auto* dir = dirs ? dirs->FirstChildElement("Directory") : nullptr;
				dir; dir = dir->NextSiblingElement("Directory"))
			{
				const char* path = dir->Attribute("Path");
				if(!path) continue;
				const fs::path resolved = penumbra::ResolvePathNoCase(dataRoot, path);
				pResources->AddResourceDir(cString::To16Char(ToUtf8(resolved)), false, "*.*", kInstallPriority);
			}
		}

		// This product's own files, each directory with the priority set in
		// its resources.cfg (below the install's).
		Log(" Penumbra product (%s):\n", kProductResourcesConfig.c_str());
		if(pResources->LoadResourceDirsFile(kProductResourcesConfig) == false)
			Error("Couldn't load the product's %s\n", kProductResourcesConfig.c_str());
		pResources->AddLanguageFile("config/penumbra_english.lang", false);
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
		const fs::path materials = dataRoot / "materials.cfg";
		if(fs::exists(materials))
			if(!pEngine->GetPhysics()->LoadSurfaceData(ToUtf8(materials)))
				Error("Penumbra: could not load physics materials\n");
		// The CC test install omits retail impact sounds/particles named by
		// materials.cfg. Keep physical properties and report missing effects.
		auto hasEffect = [pResources](const tString& name, const char* ext) {
			if(name.empty() || !pResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(name, ext)).empty()) return true;
			Warning("Penumbra: missing optional surface effect '%s.%s'\n", name.c_str(), ext);
			return false;
		};
		auto surfaces = pEngine->GetPhysics()->GetSurfaceDataIterator();
		while(surfaces.HasNext())
		{
			auto* surface = surfaces.Next();
			if(!hasEffect(surface->GetScrapeSoundName(), "snt")) surface->SetScrapeSoundName("");
			if(!hasEffect(surface->GetRollSoundName(), "snt")) surface->SetRollSoundName("");
			for(int kind=0; kind<2; ++kind)
				for(int i=0; i<(kind ? surface->GetHitDataNum() : surface->GetImpactDataNum()); ++i)
				{
					auto* effect = kind ? surface->GetHitData(i) : surface->GetImpactData(i);
					if(!hasEffect(effect->GetSoundName(), "snt")) effect->SetSoundName("");
					if(!hasEffect(effect->GetPSName(), "ps")) effect->SetPSName("");
				}
		}
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
				Log("Map '%s' (start '%s'): %s\n", options.msMap.c_str(), options.msStart.c_str(),
					cString::To8Char(sMapPath).c_str());
				cWorld* pWorld = pEngine->GetScene()->LoadWorld(options.msMap, 0);
				cStartPosEntity* pStart = pWorld ? pWorld->GetStartPosEntity(options.msStart) : nullptr;
				if(pStart == nullptr)
				{
					Error("Couldn't load map or start '%s'\n", options.msStart.c_str());
					lResult = 1;
				}
				else
				{
					cCamera* pCamera = pEngine->GetScene()->CreateCamera(eCameraMoveMode_Fly);
					pCamera->SetPosition(pStart->GetWorldMatrix().GetTranslation() + cVector3f(0, 1.7f, 0));
					cVector3f forward = cMath::MatrixMul(pStart->GetWorldMatrix().GetRotation(), cVector3f(0, 0, -1));
					pCamera->SetYaw(std::atan2(-forward.x, -forward.z));
					pCamera->SetPitch(std::atan2(forward.y, std::sqrt(forward.x*forward.x + forward.z*forward.z)));
					pCamera->SetFOV(cMath::ToRad(70.0f));
					pCamera->SetNearClipPlane(0.05f);
					pCamera->SetFarClipPlane(1000.0f);
					cViewport* pViewport = pEngine->GetScene()->CreateViewport(pCamera, pWorld);
					pViewport->SetIsListener(true);
					pEngine->GetScene()->SetCurrentListener(pViewport);
					cPostEffectComposite* pEffects = pEngine->GetGraphics()->CreatePostEffectComposite();
					cPostEffectParams_ToneMap toneMap;
					pEffects->AddPostEffect(pEngine->GetGraphics()->CreatePostEffect(&toneMap), 0);
					pViewport->SetPostEffectComposite(pEffects);
					Log("Penumbra camera: %s, yaw %.3f, pitch %.3f\n", pCamera->GetPosition().ToString().c_str(), pCamera->GetYaw(), pCamera->GetPitch());
					{
						penumbra::cMapScripts scripts(dataRoot,pEngine,sLanguage);
						if(!scripts.Load(options.msMap,pWorld,pCamera)) lResult=1;
						else
						{
							penumbra::cFreeCamera camera(pEngine, pCamera, pViewport, options.mlFrames, options.mbWalk, options.mbDebugAreas,&scripts);
							pEngine->GetUpdater()->AddGlobalUpdate(&camera);
							pEngine->SetWaitIfAppOutOfFocus(options.mlFrames == 0);
							pEngine->Run();
							if(camera.Failed()) lResult=1;
						}
						scripts.Leave();
						if(scripts.Failed() || scripts.Errors()>0) lResult=1;
					}
				}
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
