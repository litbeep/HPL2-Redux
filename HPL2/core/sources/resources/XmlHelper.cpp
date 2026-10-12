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

#include "resources/XmlHelper.h"

#include "system/LowLevelSystem.h"
#include "system/Platform.h"
#include "system/String.h"

#include <tinyxml2.h>
#include <stdio.h>
#ifdef PENUMBRA
#include <ctype.h> // PENUMBRA: QuoteBareAttributes
#endif

namespace hpl {

	//-----------------------------------------------------------------------

	tString GetAttributeString(const tinyxml2::XMLElement* apElement, const tString& asName, const tString& asDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		if(pString)	return pString;
		else		return asDefault;
	}

	float GetAttributeFloat(const tinyxml2::XMLElement* apElement, const tString& asName, float afDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToFloat(pString, afDefault);
	}

	int GetAttributeInt(const tinyxml2::XMLElement* apElement, const tString& asName, int alDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToInt(pString, alDefault);
	}

	bool GetAttributeBool(const tinyxml2::XMLElement* apElement, const tString& asName, bool abDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToBool(pString, abDefault);
	}

	cVector2f GetAttributeVector2f(const tinyxml2::XMLElement* apElement, const tString& asName, const cVector2f& avDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToVector2f(pString, avDefault);
	}

	cVector3f GetAttributeVector3f(const tinyxml2::XMLElement* apElement, const tString& asName, const cVector3f& avDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToVector3f(pString, avDefault);
	}

	cColor GetAttributeColor(const tinyxml2::XMLElement* apElement, const tString& asName, const cColor& aDefault)
	{
		const char* pString = apElement ? apElement->Attribute(asName.c_str()) : NULL;
		return cString::ToColor(pString, aDefault);
	}

	//-----------------------------------------------------------------------

	void SetAttributeString(tinyxml2::XMLElement* apElement, const tString& asName, const tString& asVal)
	{
		apElement->SetAttribute(asName.c_str(), asVal.c_str());
	}

	void SetAttributeFloat(tinyxml2::XMLElement* apElement, const tString& asName, float afVal)
	{
		char buf[64];
		sprintf(buf, "%g", afVal);
		apElement->SetAttribute(asName.c_str(), buf);
	}

	void SetAttributeInt(tinyxml2::XMLElement* apElement, const tString& asName, int alVal)
	{
		char buf[64];
		sprintf(buf, "%d", alVal);
		apElement->SetAttribute(asName.c_str(), buf);
	}

	void SetAttributeBool(tinyxml2::XMLElement* apElement, const tString& asName, bool abVal)
	{
		apElement->SetAttribute(asName.c_str(), abVal ? "true" : "false");
	}

	void SetAttributeVector2f(tinyxml2::XMLElement* apElement, const tString& asName, const cVector2f& avVal)
	{
		apElement->SetAttribute(asName.c_str(), avVal.ToFileString().c_str());
	}

	void SetAttributeVector3f(tinyxml2::XMLElement* apElement, const tString& asName, const cVector3f& avVal)
	{
		apElement->SetAttribute(asName.c_str(), avVal.ToFileString().c_str());
	}

	void SetAttributeColor(tinyxml2::XMLElement* apElement, const tString& asName, const cColor& aVal)
	{
		apElement->SetAttribute(asName.c_str(), aVal.ToFileString().c_str());
	}

	//-----------------------------------------------------------------------

#ifdef PENUMBRA
	// PENUMBRA: HPL1's TinyXML accepted unquoted attribute values
	// ('MaxLinearSpeed = 1'), which some Penumbra files have. Quote them.
	static bool QuoteBareAttributes(std::string& asText)
	{
		const std::string& s = asText;
		std::string sOut;
		sOut.reserve(s.size() + 64);
		bool bInTag = false, bChanged = false;
		char lQuote = 0;
		for(size_t i=0; i<s.size(); ++i)
		{
			const char c = s[i];
			if(lQuote) { sOut += c; if(c == lQuote) lQuote = 0; continue; }
			if(!bInTag)
			{
				sOut += c;
				if(c == '<') bInTag = i+1 < s.size() && s[i+1] != '!' && s[i+1] != '?';
				continue;
			}
			if(c == '"' || c == '\'') { lQuote = c; sOut += c; continue; }
			if(c == '>') { bInTag = false; sOut += c; continue; }
			sOut += c;
			if(c != '=') continue;
			size_t j = i+1;
			while(j < s.size() && isspace((unsigned char)s[j])) sOut += s[j++];
			if(j < s.size() && s[j] != '"' && s[j] != '\'')
			{
				size_t k = j;
				while(k < s.size() && !isspace((unsigned char)s[k]) && s[k] != '>' &&
					  !(s[k] == '/' && k+1 < s.size() && s[k+1] == '>')) ++k;
				sOut += '"'; sOut.append(s, j, k-j); sOut += '"';
				bChanged = true;
				j = k;
			}
			i = j-1;
		}
		if(bChanged) asText.swap(sOut);
		return bChanged;
	}
#endif

	bool LoadXmlFile(tinyxml2::XMLDocument& aDoc, const tWString& asPath)
	{
		FILE *pFile = cPlatform::OpenFile(asPath, _W("rb"));
		if(pFile==NULL) return false;

		bool bRet = aDoc.LoadFile(pFile) == tinyxml2::XML_SUCCESS;

#ifdef PENUMBRA
		if(bRet==false && aDoc.ErrorID()==tinyxml2::XML_ERROR_PARSING_ATTRIBUTE)
		{
			std::string sText;
			fseek(pFile, 0, SEEK_SET);
			char vBuffer[4096];
			for(size_t lRead; (lRead = fread(vBuffer, 1, sizeof(vBuffer), pFile)) > 0;) sText.append(vBuffer, lRead);
			if(QuoteBareAttributes(sText) && aDoc.Parse(sText.c_str(), sText.size()) == tinyxml2::XML_SUCCESS)
			{
				Log("XML document '%s' has unquoted attribute values; read as HPL1 did\n", cString::To8Char(asPath).c_str());
				bRet = true;
			}
		}
#endif

		fclose(pFile);

		if(bRet==false)
		{
			Warning("Failed parsing of XML document '%s' at line %d: %s\n",
					cString::To8Char(asPath).c_str(), aDoc.ErrorLineNum(), aDoc.ErrorStr());
			return false;
		}

		// tinyxml2 accepts declaration/comment-only files; callers deref the root.
		if(aDoc.RootElement()==NULL)
		{
			Warning("XML document '%s' has no root element\n", cString::To8Char(asPath).c_str());
			return false;
		}

		return true;
	}

	bool SaveXmlFile(tinyxml2::XMLDocument& aDoc, const tWString& asPath)
	{
		if(asPath == _W("")) return false;

		FILE *pFile = cPlatform::OpenFile(asPath, _W("w+"));
		if(pFile==NULL) return false;

		bool bRet = aDoc.SaveFile(pFile) == tinyxml2::XML_SUCCESS;

		fclose(pFile);

		return bRet;
	}

	//-----------------------------------------------------------------------
}
