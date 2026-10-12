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

#include "EditorInstall.h"
#include "CrashLog.h"
#include "InstallResources.h"

#include "graphics/Mesh.h"
#include "resources/MaterialManager.h"
#include "resources/MeshLoader.h"
#include "resources/MeshManager.h"
#include "resources/Resources.h"
#include "resources/XmlHelper.h"
#include "scene/MeshEntity.h"
#include "scene/SubMeshEntity.h"
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
		// The game's defaults (penumbra/resources/config/main_init.cfg).
		const tWString kUserFolder = _W("Penumbra Redux");
		const tWString kMeshCacheFolder = _W("cache/meshes");
		const tString kLanguageFile = "English.lang";

		tWString gsContentDir;
		tWString gsMapsDir;
	}

	//-----------------------------------------------------------------------

	bool EditorUseInstall(cResources* apResources, const tWString& asInstall)
	{
		// A crash writes its stack to the editor's log, as the game's does.
		InstallCrashLog();

		// The user directory first: no mesh may be cached next to its source.
		const tWString sUserDir = UserDir(kUserFolder);
		CreateFolders(sUserDir, kMeshCacheFolder);
		iMeshLoader::SetMeshCacheDir(sUserDir + kMeshCacheFolder);
		CreateFolders(sUserDir, kUserMapsFolder);
		gsMapsDir = sUserDir + kUserMapsFolder + _W("/");

		const fs::path install = asInstall.empty() ? FindSteamInstall("Penumbra Overture") :
			FromUtf8(cString::To8Char(asInstall));
		const fs::path dataRoot = install.empty() ? fs::path() : FindDataRoot(install);
		std::vector<cInstallResourceDir> vDirs;
		std::string sError;
		if(dataRoot.empty() || ReadResourceDirs(dataRoot, vDirs, sError) == false)
		{
			cPlatform::CreateMessageBox(_W("Penumbra"),
				_W("No Penumbra install found. Set [Directories] PenumbraInstall in MainEditorSettings.cfg next to the editor."));
			return false;
		}

		gsContentDir = cString::AddSlashAtEndW(cString::To16Char(ToUtf8(dataRoot)));
		Log("Penumbra install: %s\n", ToUtf8(dataRoot).c_str());
		Log(" Mesh cache: %s\n", cString::To8Char(sUserDir + kMeshCacheFolder).c_str());
		Log(" Maps: %s\n", cString::To8Char(gsMapsDir).c_str());
		AddPenumbraLoaders(apResources);
		AddInstallDirs(apResources, dataRoot, vDirs, kLanguageFile);
		return true;
	}

	//-----------------------------------------------------------------------

	const tWString& EditorContentDir() { return gsContentDir; }
	const tWString& EditorMapsDir() { return gsMapsDir; }

	//-----------------------------------------------------------------------

	bool IsHpl1Entity(const tinyxml2::XMLElement* apRoot)
	{
		return apRoot && apRoot->FirstChildElement("MAIN");
	}

	cMeshEntity* EditorLoadHpl1Entity(const tString& asName, const tinyxml2::XMLElement* apRoot,
									  cWorld* apWorld, cResources* apResources)
	{
		const tString sModel = GetAttributeString(apRoot->FirstChildElement("GRAPHICS"), "ModelFile");
		if(sModel.empty()) return nullptr;
		// The .dae explicitly: other files share the model's name (.anm).
		cMesh* pMesh = apResources->GetMeshManager()->CreateMesh(cString::SetFileExt(sModel, "dae"));
		if(pMesh == nullptr) return nullptr;

		cMeshEntity* pEntity = apWorld->CreateMeshEntity(asName, pMesh, true);
		for(const auto* pSub = apRoot->FirstChildElement("SUBMESH"); pSub; pSub = pSub->NextSiblingElement("SUBMESH"))
		{
			// HPL1 names a submesh after its geometry, HPL2 after its node; the
			// names usually agree. Where they don't, the model's own material shows.
			cSubMeshEntity* pSubEntity = pEntity->GetSubMeshEntityName(GetAttributeString(pSub, "Name"));
			const tString sMaterial = GetAttributeString(pSub, "MaterialFile");
			if(pSubEntity == nullptr || sMaterial.empty()) continue;
			if(auto material = apResources->GetMaterialManager()->CreateMaterial(sMaterial))
				pSubEntity->SetCustomMaterial(std::move(material));
		}
		return pEntity;
	}

	//-----------------------------------------------------------------------

}
