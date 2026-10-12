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

#ifndef PENUMBRA_INSTALL_RESOURCES_H
#define PENUMBRA_INSTALL_RESOURCES_H

#include "GameInstall.h"
#include "system/SystemTypes.h"

namespace hpl { class cResources; }

// Giving an engine the player's install, as the game and the level editor
// both do: the HPL1 loaders, the install's directories and the user directory
// that caches go to instead of the install.
namespace penumbra {

	//-----------------------------------------

	// Higher priority shadows lower for files of the same name
	// (cFileSearcher::GetFilePath). The engine registers its own directories
	// (core/*, compiled_shaders/*) at the default priority 0 while it starts.
	const int kInstallPriority = 20;

	// <user dir>/maps: the project's own maps, made in the level editor and
	// found by the game above the install's.
	const hpl::tWString kUserMapsFolder = _W("maps");
	const int kUserMapsPriority = 30;

	std::string ToUtf8(const std::filesystem::path& aPath);
	std::filesystem::path FromUtf8(const std::string& asPath);

	// <Frictional's personal root>/<asFolder>/, created if missing: Documents
	// on Windows, ~/.frictionalgames/ on Linux, as Amnesia's (LuxBasePersonal.h).
	hpl::tWString UserDir(const hpl::tWString& asFolder);
	// Creates each missing directory of asRelative ("a/b/c") below asRoot.
	void CreateFolders(const hpl::tWString& asRoot, const hpl::tWString& asRelative);

	// The HPL1 loaders (Collada levels, .ent entities chosen by MAIN Type in
	// cWorld::CreateEntity, Penumbra's area types) and, for the project's own
	// HPL2 maps, PlayerStart areas as entrances.
	void AddPenumbraLoaders(hpl::cResources* apResources);

	// Indexes the directories the install's resources.cfg lists, and those its
	// language file lists, at kInstallPriority. Logs each one.
	void AddInstallDirs(hpl::cResources* apResources, const std::filesystem::path& aDataRoot,
						const std::vector<cInstallResourceDir>& avDirs, const std::string& asLanguageFile);

	//-----------------------------------------

}

#endif // PENUMBRA_INSTALL_RESOURCES_H
