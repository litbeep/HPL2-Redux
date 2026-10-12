// PENUMBRA: HPL1 scene-node conventions, based on Frictional's GPL HPL1
// MeshLoaderCollada (via Antumbra). Distributed under GPLv3 or later.
#ifndef HPL_WORLD_LOADER_PENUMBRA_COLLADA_H
#define HPL_WORLD_LOADER_PENUMBRA_COLLADA_H

#include "resources/WorldLoader.h"
#include "impl/MeshLoaderCollada.h"
#include <map>

namespace hpl {
	class cMeshEntity;

	class cWorldLoaderPenumbraCollada : public iWorldLoader
	{
	public:
		cWorldLoaderPenumbraCollada();
		cWorld* LoadWorld(const tWString& asFile, tWorldLoadFlag aFlags) override;
		void AddModelEffects(cMeshEntity* apEntity, cWorld* apWorld, cColladaScene& aScene,
			cMeshLoaderCollada* apParser, tColladaGeometryVec& avGeometry,
			tColladaLightVec& avLights, const cMatrixf& aTransform);

	private:
		void AddCollider(cColladaNode* apNode, cWorld* apWorld, cColladaGeometry* apGeometry,
			bool abCharacterOnly, const tString& asMaterial);
		void AddMeshBody(cColladaNode* apNode, cWorld* apWorld, cVertexBuffer* apBuffer,
			const tString& asMaterial, const tStringVec& avParams);
		void AddSceneObjects(cColladaNode* apNode, cWorld* apWorld,
			cMeshLoaderCollada* apParser, tColladaGeometryVec& avGeometry,
			tColladaLightVec& avLights, tColladaMaterialVec& avMaterials,
			tColladaTextureVec& avTextures, tColladaImageVec& avImages,
			tWorldLoadFlag aFlags);
		int mlMeshes = 0, mlLights = 0, mlStarts = 0, mlDeferred = 0, mlErrors = 0;
		int mlBodies = 0, mlSounds = 0, mlParticles = 0, mlBillboards = 0, mlBeams = 0;
		cColladaScene* mpColladaScene = nullptr;
		tString msEntityPrefix;
		std::map<tString, int> m_mapDeferredKinds; // Node kinds not loaded yet, for the log
		void Defer(const tString& asKind) { ++mlDeferred; ++m_mapDeferredKinds[asKind]; }
	};
}
#endif
