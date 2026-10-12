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

#include "InstallResources.h"

#include "resources/Resources.h"
#include "resources/FileSearcher.h"
#include "resources/WorldLoaderHandler.h"
#include "resources/WorldLoaderPenumbraCollada.h"
#include "resources/EntityLoader_PenumbraObject.h"
#include "resources/XmlHelper.h"
#include "scene/World.h"
#include "system/LowLevelSystem.h"
#include "system/Platform.h"
#include "system/String.h"

#include <tinyxml2.h>

using namespace hpl;
namespace fs = std::filesystem;

namespace penumbra {

	//-----------------------------------------------------------------------

	namespace {
		// Same personal root as Amnesia (LuxBasePersonal.h).
#if defined(_WIN32)
		const tWString kPersonalRoot = _W("");
#elif defined(__linux__)
		const tWString kPersonalRoot = _W(".frictionalgames/");
#else
		const tWString kPersonalRoot = _W("Library/Application Support/Frictional Games/");
#endif

		// An area keeps its type, size and transform for the game code.
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

		// The project's own maps (HPL2 .map, made in the level editor) mark an
		// entrance with a PlayerStart area, as Amnesia's do.
		class cStartAreaLoader : public iAreaLoader
		{
		public:
			cStartAreaLoader() : iAreaLoader("PlayerStart") {}
			void Load(const tString& name, int, bool, const cVector3f&, const cMatrixf& transform, cWorld* world) override
			{
				world->CreateStartPos(name)->SetMatrix(transform);
			}
		};
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

	//-----------------------------------------------------------------------

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

	tWString UserDir(const tWString& asFolder)
	{
		const tWString sPersonalDir = cString::AddSlashAtEndW(cString::ReplaceCharToW(
			cPlatform::GetSystemSpecialPath(eSystemPath_Personal), _W("\\"), _W("/")));
		CreateFolders(sPersonalDir, kPersonalRoot + asFolder);
		return cString::AddSlashAtEndW(sPersonalDir + kPersonalRoot + asFolder);
	}

	//-----------------------------------------------------------------------

	void AddPenumbraLoaders(cResources* apResources)
	{
		apResources->GetWorldLoaderHandler()->AddLoader(hplNew(cWorldLoaderPenumbraCollada, ()));
		for(const char* type : {"Object", "Item", "SwingDoor", "Lamp", "Enemy"})
			apResources->AddEntityLoader(hplNew(cEntityLoader_PenumbraObject, (type)));
		for(const char* type : {"script", "link", "save", "ladder", "damage", "force", "liquid", "stick"})
			apResources->AddAreaLoader(hplNew(cAreaLoader, (type)));
		apResources->AddAreaLoader(hplNew(cStartAreaLoader, ()));
	}

	//-----------------------------------------------------------------------

	void AddInstallDirs(cResources* apResources, const fs::path& aDataRoot,
						const std::vector<cInstallResourceDir>& avDirs, const std::string& asLanguageFile)
	{
		Log(" Install (%s/resources.cfg, priority %d):\n", ToUtf8(aDataRoot).c_str(), kInstallPriority);
		for(const cInstallResourceDir& dir : avDirs)
		{
			if(dir.mbExists == false)
			{
				Warning("  '%s' listed in resources.cfg does not exist: %s\n",
						dir.msConfigPath.c_str(), ToUtf8(dir.mPath).c_str());
				continue;
			}
			Log("  %s\n", ToUtf8(dir.mPath).c_str());
			apResources->AddResourceDir(cString::To16Char(ToUtf8(dir.mPath)), false, "*.*", kInstallPriority);
		}

		// HPL1 language files also index localized textures, fonts and voices.
		const tWString sLanguagePath = apResources->GetFileSearcher()->GetFilePath(asLanguageFile);
		tinyxml2::XMLDocument language;
		if(sLanguagePath.empty() || !LoadXmlFile(language, sLanguagePath) || !language.RootElement()) return;
		const auto* dirs = language.RootElement()->FirstChildElement("RESOURCES");
		for(const auto* dir = dirs ? dirs->FirstChildElement("Directory") : nullptr;
			dir; dir = dir->NextSiblingElement("Directory"))
		{
			const char* path = dir->Attribute("Path");
			if(!path) continue;
			const fs::path resolved = ResolvePathNoCase(aDataRoot, path);
			apResources->AddResourceDir(cString::To16Char(ToUtf8(resolved)), false, "*.*", kInstallPriority);
		}
	}

	//-----------------------------------------------------------------------

}
