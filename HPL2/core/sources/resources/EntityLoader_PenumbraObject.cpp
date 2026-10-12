// PENUMBRA: HPL1 entity loading, based on Frictional's GPL HPL1 via Antumbra.
#include "resources/EntityLoader_PenumbraObject.h"
#include "resources/WorldLoaderPenumbraCollada.h"
#include "resources/WorldLoaderHandler.h"
#include "resources/MeshLoaderHandler.h"
#include "resources/MeshManager.h"
#include "resources/MaterialManager.h"
#include "resources/AnimationManager.h"
#include "resources/FileSearcher.h"
#include "resources/XmlHelper.h"
#include "graphics/Mesh.h"
#include "graphics/Animation.h"
#include "graphics/SubMesh.h"
#include "graphics/BoneState.h"
#include "graphics/VertexBuffer.h"
#include "scene/World.h"
#include "scene/MeshEntity.h"
#include "scene/SubMeshEntity.h"
#include "scene/Node3D.h"
#include "scene/Light.h"
#include "scene/LightSpot.h"
#include "scene/BillBoard.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBody.h"
#include "physics/PhysicsJointBall.h"
#include "physics/PhysicsJointHinge.h"
#include "physics/PhysicsJointSlider.h"
#include "physics/PhysicsJointScrew.h"
#include "physics/PhysicsController.h"
#include "math/Math.h"
#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "tinyxml2.h"
#include <algorithm>
#include <map>

namespace hpl {

	namespace {
		struct cBodyGroup
		{
			cColladaNode* mpAnchor = nullptr;
			std::vector<cMeshCollider> mvColliders;
		};

		// Removes a partly loaded entity again, so that a failed entity leaves
		// nothing behind and the level goes on loading without it.
		struct cPartialEntity
		{
			cWorld* mpWorld;
			cMeshEntity* mpEntity = nullptr;
			std::vector<iPhysicsBody*> mvBodies;
			std::vector<iPhysicsJoint*> mvJoints;
			bool mbDone = false;

			explicit cPartialEntity(cWorld* world) : mpWorld(world) {}
			~cPartialEntity()
			{
				if(mbDone) return;
				auto* physics = mpWorld->GetPhysicsWorld();
				for(auto* joint : mvJoints) physics->DestroyJoint(joint);
				if(mpEntity) mpWorld->DestroyMeshEntity(mpEntity);
				for(auto* body : mvBodies) physics->DestroyBody(body);
			}
		};

		void BodyProperties(iPhysicsBody* body, tinyxml2::XMLElement* xml, iPhysicsWorld* world)
		{
			body->SetMass(GetAttributeFloat(xml, "Mass", 1));
			body->SetLinearDamping(GetAttributeFloat(xml, "LinearDamping", 0.1f));
			body->SetAngularDamping(GetAttributeFloat(xml, "AngularDamping", 0.1f));
			body->SetGravity(GetAttributeBool(xml, "HasGravity", true));
			body->SetContinuousCollision(GetAttributeBool(xml, "ContinuousCollision", true));
			body->SetBlocksSound(GetAttributeBool(xml, "BlocksSound", false));
			body->SetCollideCharacter(GetAttributeBool(xml, "CollideCharacter", true));
			body->SetCollide(GetAttributeBool(xml, "CollideNonCharacter", true));
			body->SetPushedByCharacterGravity(GetAttributeBool(xml, "PushedByCharacterGravity", false));
			body->SetGravityCanAttachCharacter(GetAttributeBool(xml, "CanAttachCharacter", false));
			body->SetVolatile(GetAttributeBool(xml, "Volatile", false));
			// HPL1's attribute is intentionally misspelled.
			body->SetMaxAngularSpeed(GetAttributeFloat(xml, "MaxAngluarSpeed", 0));
			body->SetMaxLinearSpeed(GetAttributeFloat(xml, "MaxLinearSpeed", 0));
			const tString material = GetAttributeString(xml, "Material", "Default");
			if(auto* mat = world->GetMaterialFromName(material)) body->SetMaterial(mat);
			else Warning("Penumbra: physics material '%s' missing for '%s'\n", material.c_str(), body->GetName().c_str());
		}

		void JointLimits(iPhysicsJoint* joint, const cVector3f& pin, float min, float max)
		{
			if(joint->GetType() == ePhysicsJointType_Hinge)
			{
				auto* hinge = static_cast<iPhysicsJointHinge*>(joint);
				hinge->SetMinAngle(cMath::ToRad(-min));
				hinge->SetMaxAngle(cMath::ToRad(max));
			}
			else if(joint->GetType() == ePhysicsJointType_Ball)
				static_cast<iPhysicsJointBall*>(joint)->SetConeLimits(cMath::ToRad(min), cMath::ToRad(max));
			else if(joint->GetType() == ePhysicsJointType_Slider)
			{
				auto* slider = static_cast<iPhysicsJointSlider*>(joint);
				slider->SetMinDistance(-min/100.0f); slider->SetMaxDistance(max/100.0f);
			}
			else if(joint->GetType() == ePhysicsJointType_Screw)
			{
				auto* screw = static_cast<iPhysicsJointScrew*>(joint);
				screw->SetMinDistance(-min/100.0f); screw->SetMaxDistance(max/100.0f);
			}
		}

		bool LoadHpl1Controller(iPhysicsJoint* joint, iPhysicsWorld* world, tinyxml2::XMLElement* xml)
		{
			const tString type = GetAttributeString(xml, "Type"), input = GetAttributeString(xml, "Input");
			const tString output = GetAttributeString(xml, "Output");
			auto axis = [](const tString& value) {
				return value == "X" ? ePhysicsControllerAxis_X : value == "Y" ? ePhysicsControllerAxis_Y : value == "Z" ? ePhysicsControllerAxis_Z : ePhysicsControllerAxis_LastEnum;
			};
			const auto inAxis = axis(GetAttributeString(xml, "InputAxis")), outAxis = axis(GetAttributeString(xml, "OutputAxis"));
			const auto in = input == "JointAngle" ? ePhysicsControllerInput_JointAngle : input == "JointDist" ? ePhysicsControllerInput_JointDist : input == "LinearSpeed" ? ePhysicsControllerInput_LinearSpeed : input == "AngularSpeed" ? ePhysicsControllerInput_AngularSpeed : ePhysicsControllerInput_LastEnum;
			if((type != "Pid" && type != "Spring") || in == ePhysicsControllerInput_LastEnum ||
				(output != "Force" && output != "Torque") || inAxis == ePhysicsControllerAxis_LastEnum || outAxis == ePhysicsControllerAxis_LastEnum)
			{ Error("Penumbra: invalid joint controller '%s'\n", GetAttributeString(xml, "Name").c_str()); return false; }
			auto* controller = world->CreateController(GetAttributeString(xml, "Name"));
			controller->SetType(type == "Pid" ? ePhysicsControllerType_Pid : ePhysicsControllerType_Spring);
			controller->SetA(GetAttributeFloat(xml, "A", 0));
			controller->SetB(GetAttributeFloat(xml, "B", 0));
			controller->SetC(GetAttributeFloat(xml, "C", 0));
			controller->SetPidIntegralSize(GetAttributeInt(xml, "IntegralSize", 1));
			controller->SetActive(GetAttributeBool(xml, "Active", false));
			controller->SetInputType(in, inAxis);
			const float dest = GetAttributeFloat(xml, "DestValue", 0);
			controller->SetDestValue(in == ePhysicsControllerInput_JointAngle ? cMath::ToRad(dest) : dest);
			controller->SetOutputType(output == "Force" ? ePhysicsControllerOutput_Force : ePhysicsControllerOutput_Torque, outAxis);
			controller->SetMaxOutput(GetAttributeFloat(xml, "MaxOutput", 0));
			controller->SetMulMassWithOutput(GetAttributeBool(xml, "MulMassWithOutput", false));
			const tString end = GetAttributeString(xml, "EndType");
			controller->SetEndType(end == "OnMin" ? ePhysicsControllerEnd_OnMin : end == "OnMax" ? ePhysicsControllerEnd_OnMax : end == "OnDest" ? ePhysicsControllerEnd_OnDest : ePhysicsControllerEnd_Null);
			controller->SetNextController(GetAttributeString(xml, "NextController"));
			controller->SetLogInfo(GetAttributeBool(xml, "LogInfo", false));
			joint->AddController(controller);
			return true;
		}
	}

	iEntity3D* cEntityLoader_PenumbraObject::Load(const tString& name, int id, bool active,
		tinyxml2::XMLElement* root, const cMatrixf& transform, const cVector3f& scale,
		cWorld* world, const tString& file, const tWString& path, cResourceVarsObject* vars)
	{
		cPartialEntity partial(world);
		auto* graphics = root->FirstChildElement("GRAPHICS");
		if(!root->FirstChildElement("MAIN") || !graphics) return nullptr;
		auto* resources = world->GetResources();
		const tString model = GetAttributeString(graphics, "ModelFile");
		const tWString modelPath = resources->GetFileSearcher()->GetFilePath(cString::SetFileExt(model, "dae"));
		auto* parser = dynamic_cast<cMeshLoaderCollada*>(resources->GetMeshLoaderHandler()->GetLoaderForFile(modelPath));
		tColladaGeometryVec geometry; tColladaLightVec lights; cColladaScene scene;
		if(!parser || !parser->FillStructures(modelPath, nullptr, nullptr, nullptr, &lights,
			&geometry, nullptr, nullptr, &scene, false))
		{
			Error("Penumbra: could not read original entity model '%s'\n", model.c_str());
			return nullptr;
		}
		cMesh* mesh = resources->GetMeshManager()->CreateMesh(model);
		if(!mesh) return nullptr;
		cMeshEntity* entity = world->CreateMeshEntity(name, mesh, false);
		partial.mpEntity = entity;
		// HPL2 normally builds node states only for animations. Multipart HPL1
		// props also need them so physics can move a whole authored body group.
		entity->CreateNodes();
		entity->SetUniqueID(id); entity->SetActive(active);
		entity->SetRenderFlagBit(eRenderableFlag_ShadowCaster, GetAttributeBool(graphics, "CastShadows", true));
		entity->SetMatrix(transform);
		entity->UpdateLogic(0);
		auto findSubmesh = [&](const tString& subName) -> cSubMeshEntity* {
			if(auto* sub = entity->GetSubMeshEntityName(subName)) return sub;
			// HPL1 names submeshes after geometry; HPL2 uses scene-node names.
			for(const auto& geom : geometry)
				if(geom.msName == subName)
					if(auto* node = scene.GetNodeFromSource(geom.msId)) return entity->GetSubMeshEntityName(node->msName);
			return nullptr;
		};
		auto findBone = [&](cColladaNode* node) -> cBoneState* {
			// HPL2's skeleton stores Collada IDs, which can differ from names.
			if(auto* bone = entity->GetBoneStateFromName(node->msId)) return bone;
			return entity->GetBoneStateFromName(node->msName);
		};
		for(auto* xml = root->FirstChildElement("SUBMESH"); xml; xml = xml->NextSiblingElement("SUBMESH"))
		{
			auto* sub = findSubmesh(GetAttributeString(xml, "Name"));
			const tString material = GetAttributeString(xml, "MaterialFile");
			if(!sub) { Error("Penumbra: submesh missing in '%s'\n", file.c_str()); return nullptr; }
			if(!material.empty())
			{
				auto handle = resources->GetMaterialManager()->CreateMaterial(material);
				if(!handle) return nullptr;
				sub->SetCustomMaterial(std::move(handle));
			}
		}
		if(auto* animations = root->FirstChildElement("ANIMATIONS"))
		{
			entity->ClearAnimations();
			for(auto* xml = animations->FirstChildElement("Animation"); xml; xml = xml->NextSiblingElement("Animation"))
			{
				auto animation = resources->GetAnimationManager()->CreateAnimation(GetAttributeString(xml, "File"));
				if(!animation) return nullptr;
				// AnimationState owns the manager reference until the world is destroyed.
				auto* state = entity->AddAnimation(animation.Release(), GetAttributeString(xml, "Name", "Unknown"), GetAttributeFloat(xml, "Speed", 1));
				state->SetSpecialEventTime(GetAttributeFloat(xml, "SpecialEventTime", 0));
				for(auto* event = xml->FirstChildElement("Event"); event; event = event->NextSiblingElement("Event"))
				{
					auto* entry = state->CreateEvent();
					entry->mfTime = GetAttributeFloat(event, "Time", 1);
					entry->mType = GetAnimationEventType(event->Attribute("Type"));
					entry->msValue = GetAttributeString(event, "Value");
				}
			}
		}

		std::map<tString, tinyxml2::XMLElement*> properties;
		for(auto* xml = root->FirstChildElement("PHYSICS"); xml; xml = xml->NextSiblingElement("PHYSICS"))
			properties[GetAttributeString(xml, "SubName")] = xml;
		const bool skeleton = mesh->GetSkeleton() != nullptr;
		bool multipart = skeleton || properties.size() > 1;
		for(auto* node : scene.mlstNodes)
			if(node->msName.compare(0, 7, "_joint_") == 0) multipart = true;
		std::map<tString, cBodyGroup> groups;
		std::map<tString, tString> nodeBodies;
		for(auto* node : scene.mlstNodes)
		{
			tStringVec parts; tString sep = "_"; cString::GetStringVec(node->msName, parts, &sep);
			if(parts.size() < 2 || (parts[0] != "collider" && parts[0] != "charcollider")) continue;
			auto* geom = parser->GetGeometry(node->msSource, geometry);
			if(!geom || geom->mvVertexVec.empty() || parts[1] == "mesh") continue;
			cVector3f min = geom->mvVertexVec.front().pos, max = min;
			for(const auto& vertex : geom->mvVertexVec)
				for(int axis=0; axis<3; ++axis) { min.v[axis]=std::min(min.v[axis],vertex.pos.v[axis]); max.v[axis]=std::max(max.v[axis],vertex.pos.v[axis]); }
			cMeshCollider collider;
			collider.mbCharCollider = parts[0] == "charcollider";
			collider.mvSize = (max-min)*node->mvScale*scale;
			collider.m_mtxOffset = node->m_mtxWorldTransform;
			collider.m_mtxOffset.SetTranslation(cMath::MatrixMul(node->m_mtxWorldTransform, (min+max)*0.5f*node->mvScale)*scale);
			if(parts[1] == "box") collider.mType = eCollideShapeType_Box;
			else if(parts[1] == "sphere") { collider.mType = eCollideShapeType_Sphere; collider.mvSize = collider.mvSize * 0.5f; }
			else if(parts[1] == "cylinder" || parts[1] == "capsule")
			{
				collider.mType = parts[1] == "cylinder" ? eCollideShapeType_Cylinder : eCollideShapeType_Capsule;
				collider.mvSize.x *= 0.5f;
				collider.m_mtxOffset = cMath::MatrixMul(collider.m_mtxOffset, cMath::MatrixRotateZ(kPi2f));
			}
			else { Error("Penumbra: unknown model collider '%s'\n", node->msName.c_str()); return nullptr; }
			tString key;
			cColladaNode* anchor = nullptr;
			if(multipart || (!properties.empty() && !properties.begin()->first.empty()))
				for(auto* parent = node->pParent; parent; parent = parent->pParent)
				{
					if(skeleton && findBone(parent))
					{ key = parent->msName; anchor = parent; break; }
					if(skeleton) continue;
					auto* meshNode = parser->GetGeometry(parent->msSource, geometry) ? parent : nullptr;
					if(!meshNode)
						for(auto* child : parent->mlstChildren)
							if(!child->msName.empty() && child->msName[0] != '_' && parser->GetGeometry(child->msSource, geometry)) { meshNode = child; break; }
					if(meshNode)
					{
						key = parser->GetGeometry(meshNode->msSource, geometry)->msName;
						anchor = parent == &scene.mRoot ? meshNode : parent;
						nodeBodies[meshNode->msName] = key;
						break;
					}
				}
			if(!multipart && !properties.empty()) key = properties.begin()->first;
			auto& group = groups[key]; group.mpAnchor = anchor; group.mvColliders.push_back(collider);
		}
		auto* physics = world->GetPhysicsWorld();
		std::map<tString, iPhysicsBody*> bodies;
		for(auto& pair : groups)
		{
			auto& group = pair.second;
			auto prop = properties.find(pair.first);
			if(prop != properties.end() && !GetAttributeBool(prop->second, "Collides", false)) continue;
			cMatrixf local = group.mpAnchor && multipart ? group.mpAnchor->m_mtxWorldTransform : cMatrixf::Identity;
			local.SetTranslation(local.GetTranslation()*scale);
			tCollideShapeVec shapes;
			for(auto collider : group.mvColliders)
			{
				collider.m_mtxOffset = cMath::MatrixMul(cMath::MatrixInverse(local), collider.m_mtxOffset);
				shapes.push_back(cSubMesh::CreateCollideShapeFromCollider(&collider, physics, 1, nullptr));
			}
			auto* body = physics->CreateBody(name + (pair.first.empty() ? "" : "_" + pair.first),
				shapes.size() == 1 ? shapes.front() : physics->CreateCompundShape(shapes));
			body->SetMass(1);
			if(prop != properties.end())
			{
				BodyProperties(body, prop->second, physics);
				if(!multipart && !GetAttributeBool(prop->second, "HasPhysics", false)) body->SetMass(0);
			}
			body->SetMatrix(cMath::MatrixMul(transform, local));
			bodies[pair.first] = body; partial.mvBodies.push_back(body);
			if(skeleton && group.mpAnchor)
			{
				auto* bone = findBone(group.mpAnchor);
				bone->SetBody(body);
				bone->SetBodyMatrix(cMath::MatrixMul(cMath::MatrixInverse(bone->GetWorldMatrix()), body->GetWorldMatrix()));
				body->SetIsRagDoll(true); body->SetCollideRagDoll(false); body->SetActive(false);
				auto* colliderBody = physics->CreateBody(body->GetName() + "_collider", body->GetShape());
				colliderBody->SetMass(0); colliderBody->SetCollideCharacter(false); colliderBody->SetActive(false);
				partial.mvBodies.push_back(colliderBody);
				bone->SetColliderBody(colliderBody);
			}
			else if(multipart && group.mpAnchor)
			{
				auto* node = entity->GetNodeStateFromName(group.mpAnchor->msName);
				if(node)
				{
					if(node->GetParent()) node->GetParent()->RemoveChild(node); else entity->RemoveNodeChild(node);
					node->SetParent(nullptr); node->SetMatrix(cMatrixf::Identity);
					body->AddNodeChild(node);
				}
			}
			else
			{
				entity->SetMatrix(cMatrixf::Identity); body->AddChild(entity); entity->SetBody(body);
			}
		}
		for(const auto& pair : properties)
		{
			if(bodies.count(pair.first) || !GetAttributeBool(pair.second, "Collides", false)) continue;
			if(GetAttributeBool(pair.second, "StaticMeshCollider", false))
			{
				auto* sub = findSubmesh(GetAttributeString(pair.second, "ColliderMesh"));
				if(!sub) { Error("Penumbra: static collider submesh missing in '%s'\n", file.c_str()); return nullptr; }
				auto* buffer = sub->GetVertexBuffer()->CreateCopy(eVertexBufferType_Software, eVertexBufferUsageType_Static, eVertexElementFlag_Position);
				buffer->Transform(sub->GetWorldMatrix());
				auto* body = physics->CreateBody(name + "_" + pair.first, physics->CreateMeshShape(buffer));
				hplDelete(buffer); BodyProperties(body, pair.second, physics); body->SetMass(0);
				bodies[pair.first] = body; partial.mvBodies.push_back(body);
			}
			// HPL1 keeps the entity when authored physics settings refer to a
			// group with no collider (Overture's hanging lamp includes one).
			else Warning("Penumbra: no authored collider for body '%s' in '%s'; skipped\n", pair.first.c_str(), file.c_str());
		}
		for(auto* node : scene.mlstNodes)
		{
			if(node->msName.compare(0, 7, "_joint_") != 0) continue;
			tStringVec parts; tString sep = "_"; cString::GetStringVec(node->msName, parts, &sep);
			if(parts.size() < 5) { Error("Penumbra: malformed joint '%s' in '%s'\n", node->msName.c_str(), file.c_str()); return nullptr; }
			auto findBody = [&](const tString& nodeName) -> iPhysicsBody* {
				if(bodies.count(nodeName)) return bodies[nodeName];
				if(nodeBodies.count(nodeName) && bodies.count(nodeBodies[nodeName])) return bodies[nodeBodies[nodeName]];
				return nullptr;
			};
			auto* child = findBody(parts[2]); auto* parent = findBody(parts[3]);
			if(!child || (!parent && parts[3] != "null"))
			{
				Warning("Penumbra: joint '%s' in '%s' has an unauthored body; skipped\n", node->msName.c_str(), file.c_str());
				continue;
			}
			const cVector3f pivot = cMath::MatrixMul(transform, node->m_mtxWorldTransform.GetTranslation()*scale);
			const cVector3f pin = cMath::Vector3Normalize(cMath::MatrixMul3x3(transform, node->m_mtxWorldTransform.GetUp()));
			const tString jointName = name + "_" + parts.back();
			iPhysicsJoint* joint = nullptr;
			if(parts[1] == "hinge") joint = physics->CreateJointHinge(jointName, pivot, pin, parent, child);
			else if(parts[1] == "ball") joint = physics->CreateJointBall(jointName, pivot, pin, parent, child);
			else if(parts[1] == "slider") joint = physics->CreateJointSlider(jointName, pivot, pin, parent, child);
			else if(parts[1] == "screw") joint = physics->CreateJointScrew(jointName, pivot, pin, parent, child);
			if(!joint) { Error("Penumbra: unknown joint type in '%s'\n", node->msName.c_str()); return nullptr; }
			partial.mvJoints.push_back(joint);
			float min = parts.size() >= 7 ? cString::ToFloat(parts[4].c_str(), 0) : 0;
			float max = parts.size() >= 7 ? cString::ToFloat(parts[5].c_str(), 0) : 0;
			if(parts.size() < 7 && (parts[1] == "slider" || parts[1] == "screw"))
			{
				if(auto* geom = parser->GetGeometry(node->msSource, geometry))
					for(const auto& vertex : geom->mvVertexVec) { min = std::max(min, -vertex.pos.y*100); max = std::max(max, vertex.pos.y*100); }
			}
			for(auto* xml = root->FirstChildElement("JOINT"); xml; xml = xml->NextSiblingElement("JOINT"))
				if(GetAttributeString(xml, "Name") == parts.back())
				{
					SetJointProperties(joint, xml, world);
					for(auto* ctrl = xml->FirstChildElement("Controller"); ctrl; ctrl = ctrl->NextSiblingElement("Controller"))
						if(!LoadHpl1Controller(joint, physics, ctrl)) return nullptr;
					const float xmlMin = GetAttributeFloat(xml, "MinValue", -1), xmlMax = GetAttributeFloat(xml, "MaxValue", -1);
					if(xmlMin >= 0 && xmlMax >= 0) { min = xmlMin; max = xmlMax; }
					for(auto* limit : {joint->GetMinLimit(), joint->GetMaxLimit()})
					{
						const bool isMin = limit == joint->GetMinLimit();
						limit->msSound = GetAttributeString(xml, isMin ? "MinLimit_Sound" : "MaxLimit_Sound");
						limit->mfMinSpeed = GetAttributeFloat(xml, isMin ? "MinLimit_MinSpeed" : "MaxLimit_MinSpeed", 20);
						limit->mfMaxSpeed = GetAttributeFloat(xml, isMin ? "MinLimit_MaxSpeed" : "MaxLimit_MaxSpeed", 10);
					}
				}
			JointLimits(joint, pin, min, max);
			joint->SetCollideBodies(std::find(parts.begin(), parts.end(), "nocollide") == parts.end());
		}

		auto* loader = dynamic_cast<cWorldLoaderPenumbraCollada*>(resources->GetWorldLoaderHandler()->GetLoaderForFile(modelPath));
		if(loader) loader->AddModelEffects(entity, world, scene, parser, geometry, lights, transform);
		for(auto* xml = root->FirstChildElement("LIGHT"); xml; xml = xml->NextSiblingElement("LIGHT"))
		{
			auto* light = world->GetLight(name + "_" + GetAttributeString(xml, "Name"));
			if(!light) { Warning("Penumbra: entity light '%s' missing in '%s'; skipped\n", GetAttributeString(xml, "Name").c_str(), file.c_str()); continue; }
			light->SetRadius(GetAttributeFloat(xml, "Attenuation", light->GetRadius()));
			light->SetDiffuseColor(GetAttributeColor(xml, "Color", light->GetDiffuseColor()));
			light->SetCastShadows(GetAttributeBool(xml, "CastShadows", light->GetCastShadows()));
			if(light->GetLightType() == eLightType_Spot)
			{
				auto* spot = static_cast<cLightSpot*>(light);
				spot->SetFOV(GetAttributeFloat(xml, "FOV", spot->GetFOV()));
				spot->SetAspect(GetAttributeFloat(xml, "Aspect", spot->GetAspect()));
			}
		}
		if(auto* attach = root->FirstChildElement("ATTACH_BILLBOARDS"))
			for(auto* pair = attach->FirstChildElement(); pair; pair = pair->NextSiblingElement())
			{
				auto* light = world->GetLight(name + "_" + GetAttributeString(pair, "Light"));
				auto* bill = world->GetBillboard(name + "_" + GetAttributeString(pair, "Billboard"));
				if(light && bill) light->AttachBillboard(bill, bill->GetColor());
				else Warning("Penumbra: light/billboard attachment missing in '%s'\n", file.c_str());
			}
		if(entity->GetAnimationStateNum())
		{
			auto* state = entity->GetAnimationState(0);
			state->SetActive(true); state->SetLoop(GetAttributeBool(graphics, "AnimationLoop", true));
			state->SetTimePosition(GetAttributeBool(graphics, "AnimationRandStart", true) ? cMath::RandRectf(0, state->GetLength()) : 0);
			if(skeleton) entity->SetSkeletonCollidersActive(true);
		}
		else if(skeleton) entity->SetSkeletonPhysicsActive(true);
		// cWorld's entity cache retains the original .ent XML, including GAME,
		// and SourceFile identifies it for the later product gameplay layer.
		// The bodies' user data names their entity, for the game code (scripts
		// enable, disable and push an entity's bodies through it).
		for(auto* body : partial.mvBodies) body->SetUserData(entity);
		partial.mbDone = true;
		Log("Penumbra entity '%s': %s, %zu bodies, %zu joints, %d animations, %d bones\n",
			name.c_str(), file.c_str(), bodies.size(), partial.mvJoints.size(), entity->GetAnimationStateNum(), entity->GetBoneStateNum());
		return entity;
	}
}
