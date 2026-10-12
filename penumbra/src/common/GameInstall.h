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

#ifndef PENUMBRA_GAME_INSTALL_H
#define PENUMBRA_GAME_INSTALL_H

#include <filesystem>
#include <string>
#include <vector>

// The player's Penumbra install: where its data is and which directories its
// resources.cfg lists. Nothing here writes to the install.
namespace penumbra {

	//-----------------------------------------

	// One directory listed by the install's resources.cfg.
	class cInstallResourceDir
	{
	public:
		std::string msConfigPath;        // Path as written in resources.cfg
		std::filesystem::path mPath;     // Resolved under the data root
		bool mbExists = false;
	};

	//-----------------------------------------

	// The directory holding resources.cfg: <install>/redist if it has one,
	// else <install> itself (the same rule as Antumbra's findDataRoot: retail
	// and GOG installs keep the data in redist/). Matching is case-insensitive.
	// Returns an empty path when neither holds resources.cfg.
	std::filesystem::path FindDataRoot(const std::filesystem::path& aInstall);

	// Reads <aDataRoot>/resources.cfg the way HPL1 does: every child element of
	// the root with a non-empty Path is one directory, a leading slash is
	// dropped, the path is relative to the data root and its subdirectories
	// are not added. Path components are matched case-insensitively, as Linux
	// installs are not consistent about case. Returns false (with asError set)
	// if the file can't be read.
	bool ReadResourceDirs(const std::filesystem::path& aDataRoot,
						  std::vector<cInstallResourceDir>& avDirs, std::string& asError);

	// aBase / asRelative, with each component of asRelative matched
	// case-insensitively when the exact name does not exist. Components that
	// are not found are kept as written.
	std::filesystem::path ResolvePathNoCase(const std::filesystem::path& aBase, const std::string& asRelative);

	// The install of asGameFolder ("Penumbra Overture") in any Steam library,
	// or an empty path. Used when no install is given.
	std::filesystem::path FindSteamInstall(const std::string& asGameFolder);

	//-----------------------------------------

}

#endif // PENUMBRA_GAME_INSTALL_H
