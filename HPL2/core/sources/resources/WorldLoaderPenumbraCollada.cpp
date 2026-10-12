// PENUMBRA: HPL1 scene loading, based on Frictional's GPL HPL1
// MeshLoaderCollada (via Antumbra). Distributed under GPLv3 or later.
#include "resources/WorldLoaderPenumbraCollada.h"
#include "resources/Resources.h"
#include "resources/MeshLoaderHandler.h"
#include "resources/MeshManager.h"
#include "resources/MaterialManager.h"
#include "resources/FileSearcher.h"
#include "scene/Scene.h"
#include "scene/World.h"
#include "scene/MeshEntity.h"
#include "scene/Node3D.h"
#include "graphics/BoneState.h"
#include "scene/LightPoint.h"
#include "scene/LightSpot.h"
#include "graphics/Mesh.h"
#include "graphics/SubMesh.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "scene/LightParameters.h"
#include "graphics/VertexBuffer.h"
#include "system/String.h"
#include "system/LowLevelSystem.h"
#include "math/Math.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBody.h"
#include "scene/SoundEntity.h"
#include "scene/ParticleSystem.h"
#include "scene/BillBoard.h"
#include "scene/Beam.h"
#include "resources/XmlHelper.h"
#include "tinyxml2.h"
#include <algorithm>

namespace hpl {

	namespace {
		bool HasParam(const tStringVec& avParams, const tString& asParam)
		{
			for(const tString& sParam : avParams)
				if(cString::ToLowerCase(sParam) == asParam) return true;
			return false;
		}
	}

	void cWorldLoaderPenumbraCollada::AddMeshBody(cColladaNode* apNode, cWorld* apWorld,
		cVertexBuffer* apBuffer, const tString& asMaterial, const tStringVec& avParams)
	{
		auto* pPhysics = apWorld->GetPhysicsWorld();
		// Newton's tree collision crashes on a single face (its tree has no
		// inner node). Add the triangle's back face, as a second face that
		// collides the same. The buffer is the caller's temporary copy.
		if(apBuffer->GetIndexNum() == 3)
		{
			const unsigned int* pIndices = apBuffer->GetIndices();
			const unsigned int lA = pIndices[0], lB = pIndices[1], lC = pIndices[2];
			apBuffer->AddIndex(lA); apBuffer->AddIndex(lC); apBuffer->AddIndex(lB);
		}
		auto* pBody = pPhysics->CreateBody(apNode->msName, pPhysics->CreateMeshShape(apBuffer));
		pBody->SetMass(0);
		pBody->SetBlocksLight(!HasParam(avParams, "nodraw") && !HasParam(avParams, "noshadow"));
		bool bInRoom = false;
		for(auto* pParent=apNode->pParent; pParent; pParent=pParent->pParent)
			if(cString::ToLowerCase(pParent->msName).compare(0, 5, "_room") == 0) bInRoom = true;
		pBody->SetBlocksSound((bInRoom && !HasParam(avParams, "nosoundblock")) || HasParam(avParams, "soundblock"));
		pBody->SetCollideCharacter(!HasParam(avParams, "nocharcollide"));
		if(!asMaterial.empty())
			if(auto* pMat = pPhysics->GetMaterialFromName(mpResources->GetMaterialManager()->GetPhysicsMaterialName(asMaterial))) pBody->SetMaterial(pMat);
		++mlBodies;
	}

	void cWorldLoaderPenumbraCollada::AddCollider(cColladaNode* apNode, cWorld* apWorld,
		cColladaGeometry* apGeometry, bool abCharacterOnly, const tString& asMaterial)
	{
		if(!apGeometry || apGeometry->mvVertexVec.empty()) { Error("Penumbra: collider '%s' has no geometry\n", apNode->msName.c_str()); ++mlErrors; return; }
		cVector3f min = apGeometry->mvVertexVec.front().pos, max = min;
		for(const auto& v : apGeometry->mvVertexVec)
			for(int axis=0; axis<3; ++axis) { min.v[axis] = std::min(min.v[axis], v.pos.v[axis]); max.v[axis] = std::max(max.v[axis], v.pos.v[axis]); }
		cVector3f size = (max-min) * apNode->mvScale;
		tStringVec params; tString separator = "_";
		cString::GetStringVec(apNode->msName, params, &separator);
		if(params.size() < 2 || size.x <= 0 || size.y <= 0 || size.z <= 0)
		{ Error("Penumbra: invalid collider '%s'\n", apNode->msName.c_str()); ++mlErrors; return; }
		auto* pPhysics = apWorld->GetPhysicsWorld();
		iCollideShape* pShape = nullptr;
		cMatrixf offset = cMath::MatrixRotateZ(cMath::ToRad(90));
		const tString type = cString::ToLowerCase(params[1]);
		if(type == "sphere") pShape = pPhysics->CreateSphereShape(size*0.5f, nullptr);
		else if(type == "capsule") pShape = pPhysics->CreateCapsuleShape(size.x*0.5f, size.y, &offset);
		else if(type == "cylinder") pShape = pPhysics->CreateCylinderShape(size.x*0.5f, size.y, &offset);
		// HPL1 defaults to a box, including old _charcollider_<instance> names.
		else pShape = pPhysics->CreateBoxShape(size, nullptr);
		if(!pShape) { Error("Penumbra: unknown collider type in '%s'\n", apNode->msName.c_str()); ++mlErrors; return; }
		auto* pBody = pPhysics->CreateBody(apNode->msName, pShape);
		pBody->SetMatrix(apNode->m_mtxWorldTransform);
		pBody->SetMass(0);
		pBody->SetBlocksLight(false);
		pBody->SetBlocksSound(HasParam(params, "soundblock"));
		pBody->SetCollideCharacter(true);
		pBody->SetCollide(!abCharacterOnly);
		if(!asMaterial.empty())
			if(auto* mat = pPhysics->GetMaterialFromName(mpResources->GetMaterialManager()->GetPhysicsMaterialName(asMaterial))) pBody->SetMaterial(mat);
		++mlBodies;
	}

	void cWorldLoaderPenumbraCollada::AddModelEffects(cMeshEntity* entity, cWorld* world,
		cColladaScene& scene, cMeshLoaderCollada* parser, tColladaGeometryVec& geometry,
		tColladaLightVec& lights, const cMatrixf& transform)
	{
		// Reuse the map's HPL1 effect readers, but namespace instances and attach
		// them to their animated/physical model node rather than the map root.
		const tString oldPrefix = msEntityPrefix;
		auto* oldScene = mpColladaScene;
		msEntityPrefix = entity->GetName() + "_";
		mpColladaScene = &scene;
		std::vector<cMatrixf> matrices;
		for(auto* node : scene.mlstNodes)
		{
			matrices.push_back(node->m_mtxWorldTransform);
			node->m_mtxWorldTransform = cMath::MatrixMul(transform, node->m_mtxWorldTransform);
		}
		auto attach = [&](iEntity3D* child, cColladaNode* source) {
			if(!child) return;
			for(auto* parent = source->pParent; parent && parent != &scene.mRoot; parent = parent->pParent)
			{
				cNode3D* anchor = entity->GetBoneStateFromName(parent->msId);
				if(!anchor) anchor = entity->GetBoneStateFromName(parent->msName);
				if(!anchor) anchor = entity->GetNodeStateFromName(parent->msName);
				if(anchor)
				{
					const cMatrixf local = cMath::MatrixMul(cMath::MatrixInverse(anchor->GetWorldMatrix()), child->GetWorldMatrix());
					anchor->AddEntity(child); child->SetMatrix(local);
					return;
				}
			}
			const cMatrixf local = cMath::MatrixMul(cMath::MatrixInverse(entity->GetWorldMatrix()), child->GetWorldMatrix());
			entity->AddChild(child); child->SetMatrix(local);
		};
		tColladaMaterialVec materials; tColladaTextureVec textures; tColladaImageVec images;
		for(auto* node : scene.mlstNodes)
		{
			tStringVec parts; tString sep = "_"; cString::GetStringVec(node->msName, parts, &sep);
			if(parts.empty()) continue;
			const tString type = cString::ToLowerCase(parts[0]);
			const bool isLight = std::any_of(lights.begin(), lights.end(), [&](const cColladaLight& light) { return light.msId == node->msSource; });
			if(!isLight && type != "sound" && type != "ps" && type != "bb" && type != "beam") continue;
			AddSceneObjects(node, world, parser, geometry, lights, materials, textures, images, 0);
			tString effectName = msEntityPrefix + parts.back();
			if(!isLight && parts.size() >= 3)
			{
				tString resource;
				for(size_t i=1; i+1<parts.size(); ++i) resource += (i == 1 ? "" : "_") + parts[i];
				const char* ext = type == "sound" ? "snt" : type == "ps" ? "ps" : type == "bb" ? "bnt" : "beam";
				if(mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(resource, ext)).empty()) effectName = msEntityPrefix + parts[1];
			}
			if(isLight) attach(world->GetLight(effectName), node);
			else if(type == "sound") attach(world->GetSoundEntity(effectName), node);
			else if(type == "ps") attach(world->GetParticleSystem(effectName), node);
			else if(type == "bb") attach(world->GetBillboard(effectName), node);
			else if(type == "beam")
			{
				auto* beam = world->GetBeam(effectName);
				attach(beam, node);
				if(beam) if(auto* end = scene.GetNode("_beamend_" + parts.back())) attach(beam->GetEnd(), end);
			}
		}
		int i=0;
		for(auto* node : scene.mlstNodes) node->m_mtxWorldTransform = matrices[i++];
		msEntityPrefix = oldPrefix; mpColladaScene = oldScene;
	}

	cWorldLoaderPenumbraCollada::cWorldLoaderPenumbraCollada()
	{
		AddSupportedExtension("dae");
	}

	cWorld* cWorldLoaderPenumbraCollada::LoadWorld(const tWString& asFile, tWorldLoadFlag aFlags)
	{
		auto* pParser = dynamic_cast<cMeshLoaderCollada*>(
			mpResources->GetMeshLoaderHandler()->GetLoaderForFile(asFile));
		if(pParser == nullptr) return nullptr;

		tColladaImageVec vImages;
		tColladaTextureVec vTextures;
		tColladaMaterialVec vMaterials;
		tColladaGeometryVec vGeometry;
		tColladaLightVec vLights;
		cColladaScene scene;
		// Parse the original XML. HPL1's dae_cache is incompatible, and no
		// cache or converted map may ever be written beside the player's data.
		if(!pParser->FillStructures(asFile, &vImages, &vTextures, &vMaterials,
			&vLights, &vGeometry, nullptr, nullptr, &scene, false)) return nullptr;

		cWorld* pWorld = mpScene->CreateWorld(cString::To8Char(cString::GetFileNameW(asFile)));
		pWorld->SetFilePath(asFile);
		iPhysicsWorld* pPhysics = mpPhysics->CreateWorld(true);
		pPhysics->SetAccuracyLevel(ePhysicsAccuracy_Medium);
		pPhysics->SetMaxTimeStep(1.0f/60.0f);
		pPhysics->SetWorldSize(-300, 300);
		pWorld->SetPhysicsWorld(pPhysics);
		mlMeshes = mlLights = mlStarts = mlDeferred = mlErrors = 0; m_mapDeferredKinds.clear();
		mlBodies = mlSounds = mlParticles = mlBillboards = mlBeams = 0;
		mpColladaScene = &scene;
		for(cColladaNode* pNode : scene.mRoot.mlstChildren)
			AddSceneObjects(pNode, pWorld, pParser, vGeometry, vLights,
				vMaterials, vTextures, vImages, aFlags);
		mpColladaScene = nullptr;
		pWorld->Compile(true);
		mlBodies = 0;
		auto bodies = pPhysics->GetBodyIterator();
		while(bodies.HasNext()) { bodies.Next(); ++mlBodies; }
		Log("Penumbra world: %d static meshes, %d lights, %d starts, %d deferred nodes, %d errors\n",
			mlMeshes, mlLights, mlStarts, mlDeferred, mlErrors);
		Log("Penumbra environment: %d bodies, %d sounds, %d particles, %d billboards, %d beams\n",
			mlBodies, mlSounds, mlParticles, mlBillboards, mlBeams);
		for(const auto& kind : m_mapDeferredKinds)
			Log("Penumbra deferred: %s x%d\n", kind.first.c_str(), kind.second);
		// As in HPL1, a node that fails is logged and skipped; the level loads
		// without it. Only a level with no geometry at all is unusable.
		if(mlMeshes == 0)
		{
			Error("Penumbra world has no static geometry\n");
			mpScene->DestroyWorld(pWorld);
			return nullptr;
		}
		if(mlErrors != 0)
			Warning("Penumbra world loaded with %d failed nodes; see the errors above\n", mlErrors);
		return pWorld;
	}

	void cWorldLoaderPenumbraCollada::AddSceneObjects(cColladaNode* apNode, cWorld* apWorld,
		cMeshLoaderCollada* apParser, tColladaGeometryVec& avGeometry,
		tColladaLightVec& avLights, tColladaMaterialVec& avMaterials,
		tColladaTextureVec& avTextures, tColladaImageVec& avImages, tWorldLoadFlag aFlags)
	{
		tStringVec vParams;
		tString sSeparator = "_";
		cString::GetStringVec(apNode->msName, vParams, &sSeparator);
		const tString sType = vParams.empty() ? "" : cString::ToLowerCase(vParams[0]);
		const bool bSpecial = !apNode->msName.empty() && apNode->msName[0] == '_';

		if(bSpecial)
		{
			if(sType == "collider" || sType == "charcollider")
			{
				auto* geometry = apParser->GetGeometry(apNode->msSource, avGeometry);
				tString material;
				if(geometry)
				{
					const tString id = apNode->msInstanceMaterial.empty() ? geometry->msMaterial : apNode->msInstanceMaterial;
					for(const auto& mat : avMaterials) if(mat.msId == id)
						for(const auto& tex : avTextures)
							if(tex.msId == cString::Sub(mat.msTexture, !mat.msTexture.empty() && mat.msTexture[0]=='#' ? 1 : 0) && !tex.msImage.empty())
								material = apParser->GetMaterialTextureFile(id, avMaterials, avTextures, avImages);
				}
				AddCollider(apNode, apWorld, geometry, sType == "charcollider", material);
				return;
			}
			else if((sType == "sound" || sType == "ps" || sType == "bb" || sType == "beam") && !(aFlags & eWorldLoadFlag_NoEntities))
			{
				if(vParams.size() < 3) { Error("Penumbra: malformed effect node '%s'\n", apNode->msName.c_str()); ++mlErrors; return; }
				tString sFile;
				for(size_t i=1; i+1<vParams.size(); ++i) sFile += (i == 1 ? "" : "_") + vParams[i];
				tString sName = msEntityPrefix + vParams.back();
				// Older HPL1 exports put the instance name before the resource.
				const char* ext = sType == "sound" ? "snt" : sType == "ps" ? "ps" : sType == "bb" ? "bnt" : "beam";
				if(mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(sFile, ext)).empty())
				{
					sName = msEntityPrefix + vParams[1];
					sFile.clear();
					for(size_t i=2; i<vParams.size(); ++i) sFile += (i == 2 ? "" : "_") + vParams[i];
				}
				if(mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(sFile, ext)).empty())
				{
					Warning("Penumbra: missing optional environment resource '%s.%s'\n", sFile.c_str(), ext);
					Defer("missing " + tString(ext));
					return;
				}
				if(sType == "sound")
				{
					if(auto* pSound = apWorld->CreateSoundEntity(sName, sFile, false))
					{ pSound->SetMatrix(apNode->m_mtxWorldTransform); ++mlSounds; }
					else { Error("Penumbra: sound node '%s' failed\n", apNode->msName.c_str()); ++mlErrors; }
				}
				else if(sType == "ps")
				{
					if(auto* pPS = apWorld->CreateParticleSystem(sName, sFile, apNode->mvScale))
					{ pPS->SetMatrix(apNode->m_mtxWorldTransform); ++mlParticles; }
					else { Error("Penumbra: particle node '%s' failed\n", apNode->msName.c_str()); ++mlErrors; }
				}
				else if(sType == "beam")
				{
					auto* pEnd = mpColladaScene->GetNode("_beamend_" + vParams.back());
					if(pEnd)
					{
						auto* pBeam = apWorld->CreateBeam(sName);
						pBeam->SetPosition(apNode->m_mtxWorldTransform.GetTranslation());
						pBeam->GetEnd()->SetPosition(pEnd->m_mtxWorldTransform.GetTranslation());
						if(pBeam->LoadXMLProperties(sFile)) ++mlBeams; else ++mlErrors;
					}
					else { Error("Penumbra: beam end missing for '%s'\n", sName.c_str()); ++mlErrors; }
				}
				else
				{
					tinyxml2::XMLDocument doc;
					const tWString path = mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(sFile, "bnt"));
					if(!LoadXmlFile(doc, path) || !doc.RootElement() || !doc.RootElement()->FirstChildElement("MAIN"))
					{ Error("Penumbra: billboard '%s' failed\n", sFile.c_str()); ++mlErrors; return; }
					auto* pMain = doc.RootElement()->FirstChildElement("MAIN");
					auto material = mpResources->GetMaterialManager()->CreateMaterial(cString::ToString(pMain->Attribute("Material"), ""));
					if(!material) { ++mlErrors; return; }
					const auto type = cString::ToLowerCase(cString::ToString(pMain->Attribute("Type"), "point")) == "axis" ? eBillboardType_Axis : eBillboardType_Point;
					auto* pBill = apWorld->CreateBillboard(sName, cVector2f(apNode->mvScale.x, apNode->mvScale.y), type);
					pBill->SetMaterial(std::move(material));
					pBill->SetForwardOffset(cString::ToBool(pMain->Attribute("UseOffset"), false) ? apNode->mvScale.z : 0);
					pBill->SetMatrix(cMath::MatrixMul(apNode->m_mtxWorldTransform, cMath::MatrixInverse(cMath::MatrixScale(apNode->mvScale))));
					pBill->SetAxis(cVector3f(0,1,0));
					pBill->SetIsHalo(cString::ToBool(pMain->Attribute("IsHalo"), false));
					pBill->SetHaloSourceSize(cString::ToVector3f(pMain->Attribute("HaloSourceSize"), 1));
					++mlBillboards;
				}
				return;
			}
			else if(sType == "ref" && !(aFlags & eWorldLoadFlag_NoGameEntities) && !(aFlags & eWorldLoadFlag_NoDynamicGameEntities))
			{
				if(vParams.size() < 3) { Error("Penumbra: malformed ref node '%s'\n", apNode->msName.c_str()); ++mlErrors; return; }
				// HPL1 names the entity after the last part, as scripts refer to it:
				// _ref_<resource>_<name>, or the older _ref_<name>_<resource>.
				tString file, name = vParams.back();
				for(size_t i=1; i+1<vParams.size(); ++i) file += (i == 1 ? "" : "_") + vParams[i];
				if(mpResources->GetFileSearcher()->GetFilePath(cString::SetFileExt(file, "ent")).empty())
				{
					name = vParams[1];
					file.clear();
					for(size_t i=2; i<vParams.size(); ++i) file += (i == 2 ? "" : "_") + vParams[i];
				}
				if(!apWorld->CreateEntity(msEntityPrefix + name, apNode->m_mtxWorldTransform,
					cString::SetFileExt(file, "ent"), -1, true))
				{ Error("Penumbra: entity '%s' (%s.ent) failed\n", name.c_str(), file.c_str()); ++mlErrors; }
				return;
			}
			else if(sType == "beamend") return;
			else if(sType == "area" && !(aFlags & eWorldLoadFlag_NoEntities))
			{
				auto* geometry = apParser->GetGeometry(apNode->msSource, avGeometry);
				if(vParams.size() < 2 || !geometry || geometry->mvVertexVec.empty())
				{ Error("Penumbra: invalid area '%s'\n", apNode->msName.c_str()); ++mlErrors; return; }
				cVector3f min = geometry->mvVertexVec.front().pos, max = min;
				for(const auto& vertex : geometry->mvVertexVec)
					for(int axis=0; axis<3; ++axis) { min.v[axis]=std::min(min.v[axis],vertex.pos.v[axis]); max.v[axis]=std::max(max.v[axis],vertex.pos.v[axis]); }
				const tString type = vParams.size() >= 3 && !(aFlags & eWorldLoadFlag_NoGameEntities) ? cString::ToLowerCase(vParams[1]) : "";
				const tString name = vParams.size() < 3 ? cString::ToLowerCase(vParams[1]) : apNode->msName.substr(vParams[0].size()+vParams[1].size()+3);
				const cVector3f size = (max-min)*apNode->mvScale;
				if(apWorld->GetAreaEntity(name))
				{ Error("Penumbra: duplicate area '%s'\n", name.c_str()); ++mlErrors; return; }
				if(!type.empty())
				{
					auto* loader = mpResources->GetAreaLoader(type);
					if(loader) loader->Load(name, -1, true, size, apNode->m_mtxWorldTransform, apWorld);
					else Warning("Penumbra: unregistered area type '%s'; retaining bounds\n", type.c_str());
				}
				if(!apWorld->GetAreaEntity(name))
				{
					auto* area = apWorld->CreateAreaEntity(name);
					area->msType = type; area->mvSize = size; area->m_mtxTransform = apNode->m_mtxWorldTransform;
				}
				Log("Penumbra area '%s': type '%s', size %s, position %s\n", name.c_str(), type.c_str(), size.ToString().c_str(), apNode->m_mtxWorldTransform.GetTranslation().ToString().c_str());
				return;
			}
			else if(sType == "start" && !(aFlags & eWorldLoadFlag_NoEntities))
			{
				if(apNode->msName.size() > 7)
				{
					apWorld->CreateStartPos(apNode->msName.substr(7))->SetMatrix(apNode->m_mtxWorldTransform);
					++mlStarts;
				}
			}
			else if(sType.compare(0, 4, "room") != 0 && sType.compare(0, 6, "portal") != 0)
			{
				// AI/path helpers and other deferred nodes stay hidden.
				Defer("_" + sType + "_");
				return;
			}
			// Rooms are grouping nodes: retain their children and transforms.
		}
		else if(!apNode->msSource.empty())
		{
			if(apNode->mbSourceIsFile)
			{
				if(sType == "static")
				{
					tString sFile = cString::GetFileName(apNode->msSource);
					const size_t lFragment = sFile.find('#');
					if(lFragment != tString::npos) sFile.resize(lFragment);
					cMesh* pMesh = mpResources->GetMeshManager()->CreateMesh(sFile);
					if(pMesh)
					{
						cMeshEntity* pEntity = apWorld->CreateMeshEntity(apNode->msName, pMesh, true);
						pEntity->SetMatrix(apNode->m_mtxWorldTransform);
						pEntity->SetRenderFlagBit(eRenderableFlag_ShadowCaster, !HasParam(vParams, "noshadow"));
						if(!HasParam(vParams, "nocollide"))
							for(int i=0; i<pMesh->GetSubMeshNum(); ++i)
							{
								auto* sub = pMesh->GetSubMesh(i);
								auto* buffer = sub->GetVertexBuffer()->CreateCopy(eVertexBufferType_Software, eVertexBufferUsageType_Static, eVertexElementFlag_Position);
								buffer->Transform(apNode->m_mtxWorldTransform);
								AddMeshBody(apNode, apWorld, buffer, sub->GetMaterial() ? sub->GetMaterial()->GetName() : "", vParams);
								hplDelete(buffer);
							}
						++mlMeshes;
					}
					else { Error("Penumbra: static node '%s' failed\n", apNode->msName.c_str()); ++mlErrors; }
				}
				else { Defer("file " + sType); return; }
			}
			else if(cColladaGeometry* pGeometry = apParser->GetGeometry(apNode->msSource, avGeometry))
			{
				// Invisible static meshes still collide, unless explicitly disabled.
				if(!HasParam(vParams, "nocollide") && !pGeometry->mvVertexVec.empty() && !pGeometry->mvIndexVec.empty())
				{
					const tString id = apNode->msInstanceMaterial.empty() ? pGeometry->msMaterial : apNode->msInstanceMaterial;
					bool untextured = false;
					for(const auto& mat : avMaterials) if(mat.msId == id)
						for(const auto& tex : avTextures) if(tex.msId == cString::Sub(mat.msTexture, !mat.msTexture.empty() && mat.msTexture[0]=='#' ? 1 : 0) && tex.msImage.empty()) untextured = true;
					const tString mat = untextured ? "" : apParser->GetMaterialTextureFile(id, avMaterials, avTextures, avImages);
					auto* pBuffer = apParser->CreateVertexBuffer(*pGeometry, eVertexBufferUsageType_Static);
					pBuffer->Transform(apNode->m_mtxWorldTransform);
					AddMeshBody(apNode, apWorld, pBuffer, mat, vParams);
					hplDelete(pBuffer);
				}
				if(!HasParam(vParams, "nodraw") && !pGeometry->mvVertexVec.empty() && !pGeometry->mvIndexVec.empty())
				{
					const tString sMaterialId = apNode->msInstanceMaterial.empty() ? pGeometry->msMaterial : apNode->msInstanceMaterial;
					bool bUntextured = false;
					for(const cColladaMaterial& mat : avMaterials)
						if(mat.msId == sMaterialId)
							for(const cColladaTexture& tex : avTextures)
								if(tex.msId == cString::Sub(mat.msTexture, mat.msTexture[0] == '#' ? 1 : 0) && tex.msImage.empty()) bUntextured = true;
					tString sMaterial = bUntextured ? "" : apParser->GetMaterialTextureFile(sMaterialId, avMaterials, avTextures, avImages);
					auto material = bUntextured ? mpResources->GetMaterialManager()->CreateCustomMaterial(
						"penumbra_untextured_" + apNode->msName, mpGraphics->GetMaterialType("soliddiffuse")) :
						mpResources->GetMaterialManager()->CreateMaterial(sMaterial);
					if(!material)
					{
						// The install lacks it (some point at the artist's own disk).
						// Draw the geometry untextured rather than not at all.
						Warning("Penumbra: material '%s' for '%s' missing; drawn untextured\n", sMaterial.c_str(), apNode->msName.c_str());
						material = mpResources->GetMaterialManager()->CreateCustomMaterial(
							"penumbra_untextured_" + apNode->msName, mpGraphics->GetMaterialType("soliddiffuse"));
					}
					if(material)
					{
						cMesh* pMesh = hplNew(cMesh, (apNode->msName, _W(""), mpResources->GetMaterialManager(), mpResources->GetAnimationManager()));
						cSubMesh* pSub = pMesh->CreateSubMesh(apNode->msName);
						cVertexBuffer* pBuffer = apParser->CreateVertexBuffer(*pGeometry, eVertexBufferUsageType_Static);
						// Bake each instance's world transform, as HPL1 does. Recompute
						// tangent handedness after the parser's triangle winding flip.
						pBuffer->Transform(apNode->m_mtxWorldTransform);
						cMath::CreateTriTangentVectors(pBuffer->GetFloatArray(eVertexBufferElement_Texture1Tangent),
							pBuffer->GetIndices(), pBuffer->GetIndexNum(), pBuffer->GetFloatArray(eVertexBufferElement_Position), 4,
							pBuffer->GetFloatArray(eVertexBufferElement_Texture0), pBuffer->GetFloatArray(eVertexBufferElement_Normal), pBuffer->GetVertexNum());
						pBuffer->Compile(0);
						pSub->SetVertexBuffer(pBuffer);
						pSub->SetMaterial(std::move(material));
						pSub->Compile();
						pMesh->AddReference();
						cMeshEntity* pEntity = apWorld->CreateMeshEntity(apNode->msName, pMesh, true);
						pEntity->SetRenderFlagBit(eRenderableFlag_ShadowCaster, !HasParam(vParams, "noshadow"));
						++mlMeshes;
					}
					else { Error("Penumbra: no material for '%s'\n", apNode->msName.c_str()); ++mlErrors; }
				}
			}
			else if(!(aFlags & eWorldLoadFlag_NoLights))
			{
				cColladaLight* pSource = apParser->GetLight(apNode->msSource, avLights);
				if(pSource && !vParams.empty())
				{
					const size_t lFirst = sType == "dynamic" ? 1 : 0;
					tString sFile;
					if(vParams.size() > lFirst + 1)
					{
						for(size_t i=lFirst; i+1<vParams.size(); ++i)
							sFile += (i == lFirst ? "" : "_") + vParams[i];
						sFile += ".lnt";
					}
					iLight* pLight = nullptr;
					if(pSource->msType == "point")
						pLight = apWorld->CreateLightPoint(msEntityPrefix + vParams.back(), "", msEntityPrefix.empty() && lFirst == 0);
					else if(pSource->msType == "spot")
					{
						cLightSpot* pSpot = apWorld->CreateLightSpot(msEntityPrefix + vParams.back(), "", msEntityPrefix.empty() && lFirst == 0);
						pSpot->SetFOV(cMath::ToRad(pSource->mfAngle));
						pLight = pSpot;
					}
					if(pLight)
					{
						pLight->SetMatrix(apNode->m_mtxWorldTransform);
						cColor color = pSource->mDiffuseColor;
						if(pSource->msType == "point") color.a = apNode->mvScale.y;
						pLight->SetDiffuseColor(color);
						pLight->SetRadius(apNode->mvScale.x);
						if(pSource->msType == "point") pLight->SetCastShadows(apNode->mvScale.z >= 0.1f);
						if(!sFile.empty()) pLight->LoadXMLProperties(sFile);
						// HPL1 authors a radius, independent of the selected renderer.
						// Keep the retail tuning and promote it with the engine's usual
						// legacy-to-ray-traced rule, including any .lnt overrides.
						cLightTuningState standard = pLight->GetTuning(pLight->GetLightModel());
						standard.mfReach = standard.mfOnValue = apNode->mvScale.x;
						standard.mbAuthored = true;
						cLegacyLightParameters legacy;
						legacy.mfRadius = standard.mfReach;
						auto promoted = PromoteLegacyLightParameters(legacy, standard.mDiffuseColor.r, standard.mDiffuseColor.g, standard.mDiffuseColor.b);
						cLightTuningState rayTraced = standard;
						rayTraced.mbAuthored = false;
						rayTraced.mfIntensity = rayTraced.mfOnValue = promoted.mfIntensity;
						rayTraced.mfReach = promoted.mfRadius;
						rayTraced.mbReachFollowsIntensity = true;
						pLight->SetTuning(eLightModel_Legacy, standard);
						pLight->SetTuning(eLightModel_RayTraced, rayTraced);
						++mlLights;
					}
					// HPL1 also creates only point and spot lights.
					else { Warning("Penumbra: unsupported light '%s'\n", pSource->msType.c_str()); Defer("light " + pSource->msType); }
				}
				else { Warning("Penumbra: source '%s' not found\n", apNode->msSource.c_str()); ++mlErrors; }
			}
		}
		for(cColladaNode* pChild : apNode->mlstChildren)
			AddSceneObjects(pChild, apWorld, apParser, avGeometry, avLights, avMaterials, avTextures, avImages, aFlags);
	}
}
