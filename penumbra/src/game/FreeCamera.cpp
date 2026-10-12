// Penumbra map viewer. Distributed under GPLv3 or later.
#include "FreeCamera.h"
#include "MapScripts.h"
#include "scene/Scene.h"
#include "engine/Engine.h"
#include "scene/Camera.h"
#include "scene/Viewport.h"
#include "scene/World.h"
#include "physics/PhysicsWorld.h"
#include "physics/CharacterBody.h"
#include "physics/PhysicsBody.h"
#include "scene/ParticleSystem.h"
#include "scene/ParticleEmitter.h"
#include "scene/SoundEntity.h"
#include "scene/MeshEntity.h"
#include "graphics/BoneState.h"
#include "physics/PhysicsJoint.h"
#include "system/LowLevelSystem.h"
#include "math/Math.h"
#include "graphics/Graphics.h"
#include "graphics/DebugDraw.h"
#include "gui/Gui.h"
#include "gui/GuiSet.h"
#include "resources/Resources.h"
#include "system/String.h"
#include <algorithm>

namespace penumbra {
	cFreeCamera::cFreeCamera(hpl::cEngine* apEngine, hpl::cCamera* apCamera,
		hpl::cViewport* apViewport, int alFrames, bool abWalk, bool abDebugAreas, cMapScripts* scripts)
		: iUpdateable("PenumbraFreeCamera"), mpEngine(apEngine), mpCamera(apCamera),
		mpViewport(apViewport), mbDebugAreas(abDebugAreas), mbWalk(abWalk), mpScripts(scripts), mlFrameLimit(alFrames),
		mEvents([this](const SDL_Event& aEvent) { HandleEvent(aEvent); })
	{
		mDebugFont = mpEngine->GetResources()->GetFontManager()->CreateFontData("penumbra_default.fnt");
		mpDebugGui = mpEngine->GetGui()->CreateSet("PenumbraAreaDebug", nullptr);
		mpViewport->AddGuiSet(mpDebugGui);
		mpBlack = mpEngine->GetGui()->CreateGfxFilledRect(hpl::cColor(0,1),hpl::eGuiMaterial_Alpha);
		if(abWalk) CreatePlayerBody();
		mEvents.Connect(mpEngine->OnSDLEvent());
		if(mlFrameLimit == 0) SDL_SetRelativeMouseMode(SDL_TRUE);
	}

	void cFreeCamera::CreatePlayerBody()
	{
		mpBody = mpViewport->GetWorld()->GetPhysicsWorld()->CreateCharacterBody("PenumbraPlayer", hpl::cVector3f(0.6f, 1.8f, 0.6f));
		mpBody->SetMass(80);
		mpBody->SetFeetPosition(mpCamera->GetPosition() - hpl::cVector3f(0, 1.7f, 0));
		mpBody->SetYaw(mpCamera->GetYaw());
		mpBody->SetCamera(mpCamera);
		mpBody->SetCameraPosAdd(hpl::cVector3f(0, -0.1f, 0));
		mpBody->SetCameraSmoothPosNum(0);
		mpBody->SetDisableDiagSpeedBoost(true);
		mpBody->SetMaxStepSize(0.3f);
		mpBody->SetStepClimbSpeed(3);
		mpBody->SetMaxNoSlideSlopeAngle(hpl::cMath::ToRad(45));
		mpBody->SetMaxPushMass(20);
		mpBody->SetPushForce(100);
		for(int dir=0; dir<hpl::eCharDir_LastEnum; ++dir)
		{
			mpBody->SetMoveAcc(hpl::eCharDir(dir), 12);
			mpBody->SetMoveDeacc(hpl::eCharDir(dir), 12);
		}
	}

	bool cFreeCamera::ChangeMap()
	{
		const auto map=mpScripts->NextMap(), start=mpScripts->NextStart();
		auto* world=mpEngine->GetScene()->LoadWorld(map,0);
		auto* entrance=world ? world->GetStartPosEntity(start) : nullptr;
		if(!entrance)
		{
			hpl::Error("Penumbra: transition to '%s' start '%s' failed\n",map.c_str(),start.c_str());
			if(world) mpEngine->GetScene()->DestroyWorld(world);
			return false;
		}
		auto* oldWorld=mpViewport->GetWorld();
		mpScripts->Leave();
		if(mpBody) { oldWorld->GetPhysicsWorld()->DestroyCharacterBody(mpBody); mpBody=nullptr; }
		mpViewport->SetWorld(world);
		mpCamera->SetPosition(entrance->GetWorldMatrix().GetTranslation()+hpl::cVector3f(0,1.7f,0));
		const auto forward=hpl::cMath::MatrixMul(entrance->GetWorldMatrix().GetRotation(),hpl::cVector3f(0,0,-1));
		mpCamera->SetYaw(std::atan2(-forward.x,-forward.z));
		mpCamera->SetPitch(std::atan2(forward.y,std::sqrt(forward.x*forward.x+forward.z*forward.z)));
		if(mbWalk) CreatePlayerBody();
		mpEngine->GetScene()->DestroyWorld(oldWorld);
		if(!mpScripts->Load(map,world,mpCamera)) return false;
		hpl::Log("Penumbra transition complete: %s, start '%s'\n",map.c_str(),start.c_str());
		return true;
	}

	cFreeCamera::~cFreeCamera()
	{
		mEvents.Disconnect();
		mpViewport->RemoveGuiSet(mpDebugGui);
		mpEngine->GetGui()->DestroySet(mpDebugGui);
		mpEngine->GetGui()->DestroyGfx(mpBlack);
		if(mpBody) mpViewport->GetWorld()->GetPhysicsWorld()->DestroyCharacterBody(mpBody);
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}

	void cFreeCamera::HandleEvent(const SDL_Event& aEvent)
	{
		if(aEvent.type == SDL_QUIT || (aEvent.type == SDL_KEYDOWN && aEvent.key.keysym.sym == SDLK_ESCAPE))
			mpEngine->Exit();
		if(mlFrameLimit != 0) return;
		if(mpScripts)
		{
			if(aEvent.type==SDL_KEYDOWN && !aEvent.key.repeat && (aEvent.key.keysym.sym==SDLK_SPACE || aEvent.key.keysym.sym==SDLK_RETURN) && !mpScripts->Message().empty())
			{ mpScripts->DismissMessage(); return; }
			if(mpScripts->PlayerActive())
			{
				const auto target=mpScripts->FindTarget(mpCamera->GetPosition(),mpCamera->GetForward(),3.0f);
				if(aEvent.type==SDL_MOUSEBUTTONDOWN && aEvent.button.button==SDL_BUTTON_LEFT) mpScripts->Interact(target);
				if(aEvent.type==SDL_MOUSEBUTTONDOWN && aEvent.button.button==SDL_BUTTON_RIGHT) mpScripts->Examine(target);
				if(aEvent.type==SDL_KEYDOWN && !aEvent.key.repeat && aEvent.key.keysym.sym==SDLK_u)
				{
					const auto inventory=mpScripts->Inventory();
					for(const auto& item : inventory) mpScripts->UseItem(item,target);
				}
			}
		}
		if(aEvent.type == SDL_KEYDOWN && !aEvent.key.repeat && aEvent.key.keysym.sym == SDLK_F3)
			mbDebugAreas = !mbDebugAreas;
		if(aEvent.type == SDL_WINDOWEVENT)
		{
			if(aEvent.window.event == SDL_WINDOWEVENT_FOCUS_LOST) SDL_SetRelativeMouseMode(SDL_FALSE);
			if(aEvent.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) SDL_SetRelativeMouseMode(SDL_TRUE);
		}
		if(aEvent.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode() && (!mpScripts || mpScripts->PlayerActive()))
		{
			mpCamera->AddYaw(-aEvent.motion.xrel * 0.002f);
			mpCamera->SetPitch(std::clamp(mpCamera->GetPitch() - aEvent.motion.yrel * 0.002f, -1.55f, 1.55f));
			if(mpBody) mpBody->SetYaw(mpCamera->GetYaw());
		}
	}

	void cFreeCamera::Update(float afTimeStep)
	{
		if(mpScripts)
		{
			mpScripts->Update(afTimeStep);
			if(mpBody) mpBody->SetYaw(mpCamera->GetYaw());
			if(mpScripts->Failed()) { mpEngine->Exit(); return; }
			if(mpScripts->TransitionReady()) { if(!ChangeMap()) { mbFailed=true; mpEngine->Exit(); } return; }
			if(!mpScripts->PlayerActive()) return;
		}
		if(mlFrameLimit != 0 || SDL_GetKeyboardFocus() == nullptr) return;
		const Uint8* pKeys = SDL_GetKeyboardState(nullptr);
		const float fDistance = afTimeStep * (pKeys[SDL_SCANCODE_LSHIFT] ? 12.0f : 3.0f);
		if(mpBody)
		{
			const float speed = pKeys[SDL_SCANCODE_LSHIFT] ? 4.5f : 2.5f;
			for(int dir=0; dir<hpl::eCharDir_LastEnum; ++dir)
			{
				mpBody->SetMaxPositiveMoveSpeed(hpl::eCharDir(dir), speed);
				mpBody->SetMaxNegativeMoveSpeed(hpl::eCharDir(dir), -speed);
			}
			const int forward = pKeys[SDL_SCANCODE_W] - pKeys[SDL_SCANCODE_S];
			const int right = pKeys[SDL_SCANCODE_D] - pKeys[SDL_SCANCODE_A];
			// Move(0) still marks an axis as moving, preventing braking and
			// incorrectly applying the diagonal speed limit to single-axis input.
			if(forward != 0) mpBody->Move(hpl::eCharDir_Forward, float(forward));
			if(right != 0) mpBody->Move(hpl::eCharDir_Right, float(right));
			if(pKeys[SDL_SCANCODE_SPACE] && mpBody->IsOnGround()) mpBody->AddForceVelocity(hpl::cVector3f(0,4,0));
			return;
		}
		mpCamera->MoveForward((pKeys[SDL_SCANCODE_W] - pKeys[SDL_SCANCODE_S]) * fDistance);
		mpCamera->MoveRight((pKeys[SDL_SCANCODE_D] - pKeys[SDL_SCANCODE_A]) * fDistance);
		mpCamera->MoveUp((pKeys[SDL_SCANCODE_E] - pKeys[SDL_SCANCODE_Q]) * fDistance);
	}

	void cFreeCamera::OnDraw(float)
	{
		const hpl::cVector2l size = mpViewport->GetTargetSize();
		if(size.x > 0 && size.y > 0) mpCamera->SetAspect(float(size.x) / size.y);
		if(mpScripts && mDebugFont && size.x>0 && size.y>0)
		{
			mpDebugGui->SetVirtualSize(hpl::cVector2f(float(size.x),float(size.y)), -100, 100);
			if(mpScripts->WideScreen())
			{
				mpDebugGui->DrawGfx(mpBlack,hpl::cVector3f(0,0,20),hpl::cVector2f(float(size.x),size.y*0.1f));
				mpDebugGui->DrawGfx(mpBlack,hpl::cVector3f(0,size.y*0.9f,20),hpl::cVector2f(float(size.x),size.y*0.1f));
			}
			if(mpScripts->FadeAlpha()>0) mpDebugGui->DrawGfx(mpBlack,hpl::cVector3f(0,0,30),hpl::cVector2f(float(size.x),float(size.y)),hpl::cColor(1,mpScripts->FadeAlpha()));
			const auto& message=mpScripts->Message();
			const auto& subtitle=mpScripts->Subtitle();
			if(mpScripts->PlayerActive() && mlFrameLimit==0)
			{
				mpDebugGui->DrawFont(mpEngine->GetResources()->Translate("ScriptUI","Interact"),mDebugFont.Get(),hpl::cVector3f(size.x*0.5f,size.y-22,40),14,hpl::cColor(1,1),hpl::eFontAlign_Center);
				mpDebugGui->DrawFont(mpEngine->GetResources()->Translate("ScriptUI","Aim"),mDebugFont.Get(),hpl::cVector3f(size.x*0.5f,size.y*0.5f,40),16,hpl::cColor(1,1),hpl::eFontAlign_Center);
			}
			if(!message.empty() || !subtitle.empty())
			{
				mpDebugGui->DrawGfx(mpBlack,hpl::cVector3f(size.x*0.1f,size.y*0.65f,40),hpl::cVector2f(size.x*0.8f,size.y*0.3f),hpl::cColor(1,0.8f));
				const auto text=hpl::cString::To16Char(message.empty() ? subtitle : message);
				std::vector<hpl::tWString> rows;
				mDebugFont->GetWordWrapRows(size.x*0.75f,18,18,text,&rows);
				float y=size.y*0.68f;
				for(const auto& row : rows) { mpDebugGui->DrawFont(row,mDebugFont.Get(),hpl::cVector3f(size.x*0.12f,y,41),18,hpl::cColor(1,1)); y+=22; }
				if(!message.empty()) mpDebugGui->DrawFont(mpEngine->GetResources()->Translate("ScriptUI","Dismiss"),mDebugFont.Get(),hpl::cVector3f(size.x*0.12f,size.y*0.91f,42),16,hpl::cColor(1,1));
			}
		}
		if(!mbDebugAreas || !mDebugFont || size.x <= 0 || size.y <= 0) return;
		mpDebugGui->SetVirtualSize(hpl::cVector2f(float(size.x),float(size.y)), -100, 100);
		auto* resources = mpEngine->GetResources();
		mpDebugGui->DrawFont(resources->Translate("AreaDebug", "Hint"), mDebugFont.Get(), hpl::cVector3f(12,12,1), 16, hpl::cColor(1,1));
		auto* debug = mpEngine->GetGraphics()->GetDebugDraw();
		for(const auto& pair : *mpViewport->GetWorld()->GetAreaEntityMap())
		{
			auto* area = pair.second;
			hpl::DebugDraw::DebugDrawOptions options;
			options.m_transform = area->m_mtxTransform;
			options.m_depthTest = hpl::DebugDraw::DebugDepthTest::Always;
			unsigned hash = 0; for(char c : area->msType) hash = hash*31+unsigned(c);
			const hpl::cColor color(0.35f+(hash%3)*0.3f,0.35f+((hash/3)%3)*0.3f,0.35f+((hash/9)%3)*0.3f,1);
			debug->DebugDrawBoxMinMax(area->mvSize*-0.5f,area->mvSize*0.5f,color,options);
			const hpl::cVector3f view = hpl::cMath::MatrixMul(mpCamera->GetViewMatrix(),area->m_mtxTransform.GetTranslation());
			if(view.z >= -0.05f) continue;
			const auto projected = hpl::cMath::MatrixMulDivideW(mpCamera->GetProjectionMatrix(),view);
			if(std::abs(projected.x)>1 || std::abs(projected.y)>1) continue;
			auto type = resources->Translate("AreaDebug",area->msType.empty() ? "generic" : area->msType);
			if(type.empty()) type = hpl::cString::To16Char(area->msType);
			const float x = (projected.x+1)*size.x*0.5f, y = (1-projected.y)*size.y*0.5f;
			int line = 0;
			for(const auto& text : {hpl::cString::To16Char(area->msName), _W("[")+type+_W("]")})
			{
				const float labelY = y-8+line++*16;
				mpDebugGui->DrawFont(text,mDebugFont.Get(),hpl::cVector3f(x+1,labelY+1,1),14,hpl::cColor(0,1),hpl::eFontAlign_Center);
				mpDebugGui->DrawFont(text,mDebugFont.Get(),hpl::cVector3f(x,labelY,2),14,hpl::cColor(1,1),hpl::eFontAlign_Center);
			}
		}
	}

	void cFreeCamera::OnPostBufferSwap()
	{
		++mlFrames;
		if(mlFrameLimit > 0 && mlFrames >= mlFrameLimit)
		{
			hpl::Log("Penumbra rendered %d frames\n", mlFrames);
			if(mpBody) hpl::Log("Penumbra player feet: %s, grounded %d\n", mpBody->GetFeetPosition().ToString().c_str(), mpBody->IsOnGround());
			auto meshes = mpViewport->GetWorld()->GetDynamicMeshEntityIterator();
			while(meshes.HasNext())
			{
				auto* mesh = meshes.Next();
				for(int i=0; i<mesh->GetAnimationStateNum(); ++i)
				{
					auto* state = mesh->GetAnimationState(i);
					hpl::Log("Penumbra animation '%s/%s': time %.6f, active %d\n", mesh->GetName().c_str(), state->GetName().c_str(), state->GetTimePosition(), state->IsActive());
				}
				if(mesh->GetAnimationStateNum() > 0)
					for(int i=0; i<mesh->GetBoneStateNum(); ++i)
					{
						auto* bone = mesh->GetBoneState(i);
						hpl::Log("Penumbra animated bone '%s/%s': %s\n", mesh->GetName().c_str(), bone->GetName().c_str(), bone->GetWorldPosition().ToString().c_str());
					}
			}
			auto joints = mpViewport->GetWorld()->GetPhysicsWorld()->GetJointIterator();
			while(joints.HasNext())
			{
				auto* joint = joints.Next();
				hpl::Log("Penumbra joint '%s': type %d, angle %.6f, distance %.6f\n", joint->GetName().c_str(), int(joint->GetType()), joint->GetAngle(), joint->GetDistance());
			}
			auto bodies = mpViewport->GetWorld()->GetPhysicsWorld()->GetBodyIterator();
			while(bodies.HasNext())
			{
				auto* body = bodies.Next();
				// Entity bodies carry their entity in the user data.
				if(auto* owner = static_cast<hpl::cMeshEntity*>(body->GetUserData()); owner && body->GetMass()>0)
					hpl::Log("Penumbra rigid body '%s' of '%s': %s\n", body->GetName().c_str(), owner->GetName().c_str(), body->GetWorldPosition().ToString().c_str());
			}
			int particles = 0, sounds = 0;
			auto systems = mpViewport->GetWorld()->GetParticleSystemIterator();
			while(systems.HasNext())
			{
				auto* system = systems.Next();
				for(int i=0; i<system->GetEmitterNum(); ++i) particles += system->GetEmitter(i)->GetParticleNum();
			}
			auto entities = mpViewport->GetWorld()->GetSoundEntityIterator();
			while(entities.HasNext())
				if(entities.Next()->GetSoundEntry(hpl::eSoundEntityType_Main, true)) ++sounds;
			hpl::Log("Penumbra runtime: %d live particles, %d active main sounds\n", particles, sounds);
			mpEngine->Exit();
		}
	}
}
