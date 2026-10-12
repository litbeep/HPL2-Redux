// PENUMBRA: HPL1 .ent loading, based on Frictional's GPL HPL1 via Antumbra.
#ifndef HPL_ENTITY_LOADER_PENUMBRA_OBJECT_H
#define HPL_ENTITY_LOADER_PENUMBRA_OBJECT_H
#include "resources/EntityLoader_Object.h"

namespace hpl {

	class cEntityLoader_PenumbraObject : public cEntityLoader_Object
	{
	public:
		cEntityLoader_PenumbraObject(const tString& asType) : cEntityLoader_Object("Penumbra" + asType) {}
		iEntity3D* Load(const tString& asName, int alID, bool abActive, tinyxml2::XMLElement* apRoot,
			const cMatrixf& aTransform, const cVector3f& avScale, cWorld* apWorld,
			const tString& asFile, const tWString& asPath, cResourceVarsObject* apVars) override;
	private:
		void BeforeLoad(tinyxml2::XMLElement*, const cMatrixf&, cWorld*, cResourceVarsObject*) override {}
		void AfterLoad(tinyxml2::XMLElement*, const cMatrixf&, cWorld*, cResourceVarsObject*) override {}
	};
}
#endif
