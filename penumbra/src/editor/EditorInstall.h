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

#ifndef PENUMBRA_EDITOR_INSTALL_H
#define PENUMBRA_EDITOR_INSTALL_H

#include "system/SystemTypes.h"

namespace tinyxml2 { class XMLElement; }
namespace hpl { class cResources; class cWorld; class cMeshEntity; }

// The level editor's use of the player's install (iEditorBase::Init and the
// editors' lookup directories, under PENUMBRA). The editor reads the install
// and never writes to it: its own maps and mesh caches go to the user
// directory.
namespace penumbra {

	//-----------------------------------------

	// asInstall is the editor's [Directories] PenumbraInstall setting; empty
	// means Overture in a Steam library. Adds the HPL1 loaders and the
	// install's directories and keeps mesh caches in the user directory.
	// False, after a message box, when there is no install.
	bool EditorUseInstall(hpl::cResources* apResources, const hpl::tWString& asInstall);

	// The install's data root, which the editor's object folders (models,
	// sounds, particles...) are relative to; with a trailing slash.
	const hpl::tWString& EditorContentDir();

	// <user dir>/maps/: where the project's own maps are made and saved.
	const hpl::tWString& EditorMapsDir();

	// True for an HPL1 .ent (Penumbra's), whose root has a MAIN element.
	bool IsHpl1Entity(const tinyxml2::XMLElement* apRoot);

	// What the editor shows of an HPL1 entity: its model, with the materials
	// the .ent sets on its submeshes. Bodies, joints, lights and the rest are
	// the game's: a map refers to the .ent, and the game loads all of it.
	hpl::cMeshEntity* EditorLoadHpl1Entity(const hpl::tString& asName, const tinyxml2::XMLElement* apRoot,
										   hpl::cWorld* apWorld, hpl::cResources* apResources);

	//-----------------------------------------

}

#endif // PENUMBRA_EDITOR_INSTALL_H
