/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "resources/MeshLoaderHandler.h"

#include "resources/MeshLoader.h"
#include "system/String.h"
#include "system/LowLevelSystem.h"
#include "resources/Resources.h"
#include "scene/Scene.h"

#include <cstdint>
#include <cstdio>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	cMeshLoaderHandler::cMeshLoaderHandler(cResources* apResources, cScene *apScene)
	{
		mpResources = apResources;
		mpScene = apScene;
	}
	
	//-----------------------------------------------------------------------

	cMeshLoaderHandler::~cMeshLoaderHandler()
	{
		
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	// PENUMBRA: the .msh cache location (iMeshLoader::SetMeshCacheDir).
	tWString iMeshLoader::msMeshCacheDir;

	void iMeshLoader::SetMeshCacheDir(const tWString& asDir)
	{
		msMeshCacheDir = asDir.empty() ? asDir : cString::AddSlashAtEndW(asDir);
	}

	tWString iMeshLoader::GetMeshCachePath(const tWString& asFile)
	{
		if(msMeshCacheDir.empty()) return cString::SetFileExtW(asFile, _W("msh"));

		// One flat directory: the source's name plus a hash of its full path, so
		// meshes with the same name in different directories get their own cache.
		uint64_t lHash = 14695981039346656037ULL; // 64-bit FNV-1a
		for(size_t i=0; i<asFile.size(); ++i)
		{
			lHash ^= (uint64_t)asFile[i];
			lHash *= 1099511628211ULL;
		}
		char sHash[17];
		snprintf(sHash, sizeof(sHash), "%016llx", (unsigned long long)lHash);

		return msMeshCacheDir + cString::GetFileNameW(cString::SetFileExtW(asFile, _W(""))) +
			   _W("_") + cString::To16Char(sHash) + _W(".msh");
	}

	//-----------------------------------------------------------------------

	cMesh* cMeshLoaderHandler::LoadMesh(const tWString& asFile,tMeshLoadFlag aFlags)
	{
		iMeshLoader *pMeshLoader = static_cast<iMeshLoader*>(GetLoaderForFile(asFile));

		if(pMeshLoader)
		{
				return pMeshLoader->LoadMesh(asFile,aFlags);
		}
		return NULL;
	}

	//-----------------------------------------------------------------------

	bool cMeshLoaderHandler::SaveMesh(cMesh* apMesh,const tWString& asFile)
	{
		iMeshLoader *pMeshLoader = static_cast<iMeshLoader*>(GetLoaderForFile(asFile));

		if(pMeshLoader)
		{
				return pMeshLoader->SaveMesh(apMesh,asFile);
		}
		return false;
	}
	
	//-----------------------------------------------------------------------
	cAnimation* cMeshLoaderHandler::LoadAnimation(const tWString& asFile)
	{
		iMeshLoader *pMeshLoader = static_cast<iMeshLoader*>(GetLoaderForFile(asFile));

		if(pMeshLoader)
		{
				return pMeshLoader->LoadAnimation(asFile);
		}
		else
		{
			return NULL;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	
	void cMeshLoaderHandler::SetupLoader(iResourceLoader *apLoader)
	{
		iMeshLoader *pMeshLoader = static_cast<iMeshLoader*>(apLoader);

		pMeshLoader->mpMaterialManager = mpResources->GetMaterialManager();
		pMeshLoader->mpMeshManager = mpResources->GetMeshManager();
		pMeshLoader->mpAnimationManager = mpResources->GetAnimationManager();
	}

	//-----------------------------------------------------------------------
}
