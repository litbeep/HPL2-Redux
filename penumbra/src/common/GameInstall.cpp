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

#include "GameInstall.h"

#include <tinyxml2.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace penumbra {

	//-----------------------------------------------------------------------

	static bool EqualsNoCase(const std::string& asA, const std::string& asB)
	{
		if(asA.size() != asB.size()) return false;
		for(size_t i=0; i<asA.size(); ++i)
		{
			if(std::tolower((unsigned char)asA[i]) != std::tolower((unsigned char)asB[i])) return false;
		}
		return true;
	}

	static std::string FileName(const fs::path& aPath)
	{
		const std::u8string sName = aPath.filename().u8string();
		return std::string(sName.begin(), sName.end());
	}

	static bool IsDirectory(const fs::path& aPath)
	{
		std::error_code ec;
		return fs::is_directory(aPath, ec);
	}

	// The entry of aDir named asName, ignoring case; empty if there is none.
	static fs::path FindEntryNoCase(const fs::path& aDir, const std::string& asName)
	{
		std::error_code ec;
		if(fs::exists(aDir / asName, ec)) return aDir / asName;

		for(fs::directory_iterator it(aDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if(EqualsNoCase(FileName(it->path()), asName)) return it->path();
		}
		return fs::path();
	}

	//-----------------------------------------------------------------------

	fs::path ResolvePathNoCase(const fs::path& aBase, const std::string& asRelative)
	{
		fs::path result = aBase;
		size_t lStart = 0;
		while(lStart <= asRelative.size())
		{
			size_t lEnd = asRelative.find_first_of("/\\", lStart);
			if(lEnd == std::string::npos) lEnd = asRelative.size();
			std::string sPart = asRelative.substr(lStart, lEnd - lStart);
			lStart = lEnd + 1;

			if(sPart.empty() || sPart == ".") continue;

			fs::path entry = sPart == ".." ? fs::path() : FindEntryNoCase(result, sPart);
			result = entry.empty() ? result / fs::path(std::u8string(sPart.begin(), sPart.end())) : entry;
		}
		return result;
	}

	//-----------------------------------------------------------------------

	fs::path FindDataRoot(const fs::path& aInstall)
	{
		const fs::path candidates[] = { ResolvePathNoCase(aInstall, "redist"), aInstall };
		for(const fs::path& candidate : candidates)
		{
			if(IsDirectory(candidate) && FindEntryNoCase(candidate, "resources.cfg").empty() == false)
				return candidate;
		}
		return fs::path();
	}

	//-----------------------------------------------------------------------

	bool ReadResourceDirs(const fs::path& aDataRoot, std::vector<cInstallResourceDir>& avDirs, std::string& asError)
	{
		const fs::path cfgPath = FindEntryNoCase(aDataRoot, "resources.cfg");
		if(cfgPath.empty())
		{
			asError = "no resources.cfg in '" + aDataRoot.string() + "'";
			return false;
		}

		// Through a FILE* so non-ASCII install paths open on Windows too.
#ifdef _WIN32
		FILE* pFile = _wfopen(cfgPath.wstring().c_str(), L"rb");
#else
		FILE* pFile = fopen(cfgPath.c_str(), "rb");
#endif
		if(pFile == nullptr)
		{
			asError = "can't open '" + cfgPath.string() + "'";
			return false;
		}
		tinyxml2::XMLDocument xmlDoc;
		const tinyxml2::XMLError result = xmlDoc.LoadFile(pFile);
		fclose(pFile);
		if(result != tinyxml2::XML_SUCCESS || xmlDoc.RootElement() == nullptr)
		{
			asError = "can't parse '" + cfgPath.string() + "': " + xmlDoc.ErrorStr();
			return false;
		}

		for(const tinyxml2::XMLElement* pElem = xmlDoc.RootElement()->FirstChildElement();
			pElem != nullptr; pElem = pElem->NextSiblingElement())
		{
			const char* pPath = pElem->Attribute("Path");
			std::string sPath = pPath ? pPath : "";
			if(sPath.empty()) continue;

			cInstallResourceDir dir;
			dir.msConfigPath = sPath;
			if(sPath[0] == '/' || sPath[0] == '\\') sPath = sPath.substr(1);
			dir.mPath = ResolvePathNoCase(aDataRoot, sPath);
			dir.mbExists = IsDirectory(dir.mPath);
			avDirs.push_back(dir);
		}
		return true;
	}

	//-----------------------------------------------------------------------

	// Where Steam itself is installed; empty if it isn't.
	static fs::path SteamDir()
	{
#ifdef _WIN32
		wchar_t sPath[1024];
		DWORD lSize = sizeof(sPath);
		if(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, sPath, &lSize) == ERROR_SUCCESS)
			return fs::path(sPath);
		return fs::path();
#else
		const char* pHome = std::getenv("HOME");
		if(pHome == nullptr) return fs::path();
		for(const char* pRelative : {".local/share/Steam", ".steam/steam"})
			if(IsDirectory(fs::path(pHome) / pRelative)) return fs::path(pHome) / pRelative;
		return fs::path();
#endif
	}

	fs::path FindSteamInstall(const std::string& asGameFolder)
	{
		const fs::path steam = SteamDir();
		if(steam.empty()) return fs::path();

		// Steam's own library, then those libraryfolders.vdf lists ("path" "<dir>").
		std::vector<fs::path> vLibraries = { steam };
		std::ifstream folders(ResolvePathNoCase(steam, "steamapps/libraryfolders.vdf"));
		for(std::string sLine; std::getline(folders, sLine);)
		{
			const size_t lKey = sLine.find("\"path\"");
			if(lKey == std::string::npos) continue;
			const size_t lOpen = sLine.find('"', lKey + 6), lClose = sLine.rfind('"');
			if(lOpen == std::string::npos || lClose <= lOpen) continue;
			std::string sPath;
			for(size_t i = lOpen + 1; i < lClose; ++i)
			{
				if(sLine[i] == '\\' && i + 1 < lClose) ++i; // "C:\\Games" in the file
				sPath += sLine[i];
			}
			vLibraries.push_back(fs::path(std::u8string(sPath.begin(), sPath.end())));
		}

		for(const fs::path& library : vLibraries)
		{
			const fs::path install = ResolvePathNoCase(library, "steamapps/common/" + asGameFolder);
			if(FindDataRoot(install).empty() == false) return install;
		}
		return fs::path();
	}

	//-----------------------------------------------------------------------

}
