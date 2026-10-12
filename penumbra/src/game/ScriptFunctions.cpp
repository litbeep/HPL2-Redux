// The HPL1 script functions Penumbra's map scripts call. GPLv3 or later;
// behaviour reference: Antumbra's engine/game/ScriptFuncs.cpp and
// penumbra-overture/GameScripts.cpp. Functions without a handler here are
// stubs (cMapScripts::Generic).
#include "MapScripts.h"
#include "engine/Engine.h"
#include "scene/World.h"
#include "scene/Light.h"
#include "scene/LightBox.h"
#include "scene/LightParameters.h"
#include "scene/SoundEntity.h"
#include "scene/ParticleSystem.h"
#include "physics/PhysicsWorld.h"
#include "sound/Sound.h"
#include "sound/MusicHandler.h"
#include "sound/SoundHandler.h"
#include "system/LowLevelSystem.h"
#include "system/String.h"
#include <algorithm>
#include <cmath>
#include <sstream>

using namespace hpl;
namespace penumbra {
namespace {
	// HPL1 gives lights a radius; HPL2's physical light model an intensity.
	float LegacyLightValue(iLight* light, float radius, const cColor& color)
	{
		if(light->GetLightModel()==eLightModel_Legacy) return radius;
		cLegacyLightParameters legacy; legacy.mfRadius=radius;
		return PromoteLegacyLightParameters(legacy,color.r,color.g,color.b).mfIntensity;
	}
	bool Starts(const std::string& text, const char* prefix) { return text.rfind(prefix,0)==0; }
}

void cMapScripts::AddHandlers()
{
	////////////////////////////////
	// Strings and numbers

	Handle({"Print"}, [](cScriptCall& c) { Log("Penumbra script: %s\n",c.s(0).c_str()); });
	Handle({"FloatToString"}, [](cScriptCall& c) { std::ostringstream out; out<<c.f(0); c.Return(out.str()); });
	Handle({"IntToString"}, [](cScriptCall& c) { c.Return(std::to_string(c.n(0))); });
	Handle({"RandFloat"}, [](cScriptCall& c) { c.Return(cMath::RandRectf(c.f(0),c.f(1))); });
	Handle({"RandInt"}, [](cScriptCall& c) { c.Return(cMath::RandRectl(c.n(0),c.n(1))); });
	Handle({"StringContains"}, [](cScriptCall& c) { c.Return(c.s(0).find(c.s(1))!=std::string::npos); });
	Handle({"Translate"}, [this](cScriptCall& c) { c.Return(Translate(c.s(0),c.s(1))); });

	////////////////////////////////
	// Variables

	Handle({"CreateLocalVar","SetLocalVar","AddLocalVar","GetLocalVar",
	        "CreateGlobalVar","SetGlobalVar","AddGlobalVar","GetGlobalVar"}, [this](cScriptCall& c)
	{
		auto& vars=c.Name().find("Local")!=std::string::npos ? mLocalVars : mGlobalVars;
		if(Starts(c.Name(),"Get")) c.Return(vars[c.s(0)]);
		else if(Starts(c.Name(),"Add")) vars[c.s(0)]+=c.n(1);
		else if(Starts(c.Name(),"Create")) vars.emplace(c.s(0),c.n(1));
		else vars[c.s(0)]=c.n(1);
	});
	Handle({"CreateGameEntityVar","SetGameEntityVar","AddGameEntityVar","GetGameEntityVar"}, [this](cScriptCall& c)
	{
		const std::string key=c.s(0)+"/"+c.s(1);
		if(Starts(c.Name(),"Get")) c.Return(mEntityVars[key]);
		else if(Starts(c.Name(),"Add")) mEntityVars[key]+=c.n(2);
		else mEntityVars[key]=c.n(2);
	});

	////////////////////////////////
	// Timers

	Handle({"CreateTimer"}, [this](cScriptCall& c) { mTimers[c.s(0)]={std::max(0.0f,c.f(1)),c.s(2),c.b(3)}; });
	Handle({"DestroyTimer"}, [this](cScriptCall& c) { mTimers.erase(c.s(0)); });
	Handle({"GetTimerTime"}, [this](cScriptCall& c)
	{
		const auto it=mTimers.find(c.s(0));
		c.Return(it==mTimers.end() ? 0.0f : it->second.time);
	});
	Handle({"SetTimerPaused","SetTimerTime","AddTimerTime"}, [this](cScriptCall& c)
	{
		const auto it=mTimers.find(c.s(0));
		if(it==mTimers.end()) return;
		if(c.Name()=="SetTimerPaused") it->second.paused=c.b(1);
		else if(c.Name()=="SetTimerTime") it->second.time=c.f(1);
		else it->second.time+=c.f(1);
	});

	////////////////////////////////
	// Messages and subtitles

	Handle({"AddToTempString"}, [this](cScriptCall& c) { msTempString+=c.s(0); });
	Handle({"AddToTempStringTrans"}, [this](cScriptCall& c) { msTempString+=Translate(c.s(0),c.s(1)); });
	Handle({"AddToTempStringAction","GetActionKeyString"}, [this](cScriptCall& c)
	{
		const std::string& action=c.s(0);
		const std::string key=action=="Interact" ? Translate("ScriptUI","InteractKey") :
		                      action=="Examine" ? Translate("ScriptUI","ExamineKey") :
		                      action=="Inventory" ? Translate("ScriptUI","UseKey") : action;
		if(c.Name()=="GetActionKeyString") c.Return(key); else msTempString+=key;
	});
	Handle({"AddMessage"}, [this](cScriptCall& c) { if(!c.s(0).empty()) mMessages.push_back(c.s(0)); });
	Handle({"AddMessageTrans"}, [this](cScriptCall& c)
	{
		const auto message=Translate(c.s(0),c.s(1));
		if(!message.empty()) mMessages.push_back(message);
	});
	Handle({"AddMessageTempString"}, [this](cScriptCall& c)
	{
		if(!msTempString.empty()) mMessages.push_back(msTempString);
		msTempString.clear();
	});
	Handle({"SetMessagesOverCallback"}, [this](cScriptCall& c) { msMessagesCallback=c.s(0); });
	Handle({"AddSubTitle"}, [this](cScriptCall& c) { msSubtitle=c.s(0); mfSubtitleTime=c.f(1); });
	Handle({"AddSubTitleTrans"}, [this](cScriptCall& c) { msSubtitle=Translate(c.s(0),c.s(1)); mfSubtitleTime=c.f(2); });
	Handle({"AddSubTitleTempString"}, [this](cScriptCall& c) { msSubtitle=msTempString; mfSubtitleTime=c.f(0); msTempString.clear(); });

	////////////////////////////////
	// Player and screen

	Handle({"SetPlayerActive"}, [this](cScriptCall& c) { mbPlayerActive=c.b(0); });
	Handle({"StartPlayerLookAt"}, [this](cScriptCall& c) { msLookTarget=c.s(0); mfLookSpeed=c.f(1); mfLookMaxSpeed=c.f(2); });
	Handle({"StopPlayerLookAt"}, [this](cScriptCall&) { msLookTarget.clear(); });
	Handle({"SetupWaveGravity"}, [this](cScriptCall& c)
	{
		mfWaveAngle=c.f(0); mfWavePeriod=std::max(0.001f,c.f(1)); mfGravity=c.f(2);
		msWaveAxis=cString::ToLowerCase(c.s(3));
	});
	Handle({"SetWaveGravityActive"}, [this](cScriptCall& c)
	{
		mbWaveGravity=c.b(0);
		if(mpWorld && !mbWaveGravity) mpWorld->GetPhysicsWorld()->SetGravity(cVector3f(0,-mfGravity,0));
	});
	Handle({"SetWideScreenActive"}, [this](cScriptCall& c) { mbWideScreen=c.b(0); });
	Handle({"FadeIn","FadeOut"}, [this](cScriptCall& c)
	{
		const float time=c.f(0);
		mfFadeTarget=c.Name()=="FadeIn" ? 0.0f : 1.0f;
		mfFadeSpeed=time>0 ? std::abs(mfFadeTarget-mfFadeAlpha)/time : 0;
		if(time<=0) mfFadeAlpha=mfFadeTarget;
	});
	Handle({"IsFading"}, [this](cScriptCall& c) { c.Return(std::abs(mfFadeAlpha-mfFadeTarget)>1e-5f); });

	////////////////////////////////
	// Inventory and callbacks

	Handle({"HasItem"}, [this](cScriptCall& c) { c.Return(mInventory.count(c.s(0))!=0); });
	Handle({"GiveItem"}, [this](cScriptCall& c) { mInventory.insert(c.s(0)); });
	Handle({"RemoveItem"}, [this](cScriptCall& c) { mInventory.erase(c.s(0)); });
	Handle({"AddPickupCallback"}, [this](cScriptCall& c) { mPickupCallbacks[c.s(0)]=c.s(1); });
	Handle({"RemovePickupCallback"}, [this](cScriptCall& c) { mPickupCallbacks.erase(c.s(0)); });
	Handle({"AddUseCallback"}, [this](cScriptCall& c) { mUseCallbacks[c.s(0)+"/"+c.s(1)]=c.s(2); });
	Handle({"RemoveUseCallback"}, [this](cScriptCall& c)
	{
		for(auto it=mUseCallbacks.begin(); it!=mUseCallbacks.end();)
			if(it->second==c.s(0)) it=mUseCallbacks.erase(it); else ++it;
	});
	Handle({"AddEntityCallback"}, [this](cScriptCall& c) { mEntityCallbacks[c.s(0)+"/"+c.s(1)]=c.s(2); });
	Handle({"RemoveEntityCallback"}, [this](cScriptCall& c) { mEntityCallbacks.erase(c.s(0)+"/"+c.s(1)); });
	// Partial: point-in-area overlap rather than HPL1's entity collision.
	Handle({"AddEntityCollideCallback"}, [this](cScriptCall& c) { mCollideCallbacks.push_back({c.s(0),c.s(1),c.s(2),c.s(3)}); }, true);
	Handle({"RemoveEntityCollideCallback"}, [this](cScriptCall& c)
	{
		mCollideCallbacks.erase(std::remove_if(mCollideCallbacks.begin(),mCollideCallbacks.end(), [&](const cCollide& cb) {
			return cb.event==c.s(0) && cb.parent==c.s(1) && cb.child==c.s(2);
		}),mCollideCallbacks.end());
	});

	////////////////////////////////
	// Game entities

	Handle({"SetGameEntityActive","SetMeshActive"}, [this](cScriptCall& c) { SetActive(c.s(0),c.b(1)); });
	Handle({"GetGameEntityActive"}, [this](cScriptCall& c)
	{
		const auto it=mEntities.find(c.s(0));
		c.Return(it!=mEntities.end() && it->second.active);
	});
	Handle({"SetGameEntityDescription"}, [this](cScriptCall& c) { mEntities[c.s(0)].description=c.s(1); });
	Handle({"SetGameEntityDescriptionTrans","SetGameEntityDescriptionOnceTrans"}, [this](cScriptCall& c)
	{
		auto& entity=mEntities[c.s(0)];
		entity.description=Translate(c.s(1),c.s(2));
		entity.descriptionOnce=c.Name()=="SetGameEntityDescriptionOnceTrans";
	});

	////////////////////////////////
	// Links and areas

	// Partial: link sounds and loading text are kept but not used yet.
	Handle({"ChangeMap"}, [this](cScriptCall& c) { ChangeMap({c.s(0),c.s(1),c.s(2),c.s(3),c.s(6),c.s(7),c.f(4),c.f(5)}); }, true);
	Handle({"SetupLink","SetupLinkLoadText"}, [this](cScriptCall& c)
	{
		const bool text=c.Name()=="SetupLinkLoadText";
		mLinks[c.s(0)]={c.s(1),c.s(2),c.s(3),c.s(4),text ? c.s(7) : "",text ? c.s(8) : "",c.f(5),c.f(6)};
	}, true);
	// Partial: the settings are kept for the area behaviours to come.
	Handle({"SetupLadder","SetupDamageArea","SetupForceArea","SetupLiquidArea","SetupStickArea","SetupSaveArea","SetAreaCustomIcon"},
	       [this](cScriptCall& c)
	{
		std::vector<std::string> values;
		const int stringType=mpScriptEngine->GetTypeIdByDecl("string");
		for(asUINT i=1; i<asUINT(c.mpCall->GetArgCount()); ++i)
		{
			const int type=c.mpCall->GetArgTypeId(i);
			if(type==stringType) values.push_back(c.s(i));
			else if(type==asTYPEID_FLOAT) values.push_back(std::to_string(c.f(i)));
			else if(type==asTYPEID_BOOL) values.push_back(c.b(i) ? "true" : "false");
			else values.push_back(std::to_string(c.n(i)));
		}
		mAreaSettings[c.Name()+"/"+c.s(0)]=values;
		if(mWarned.insert(c.Name()).second) Warning("Penumbra script '%s': setup retained; area behavior pending\n",c.Name().c_str());
	}, true);

	////////////////////////////////
	// World: fog, sky, lights, particles

	// Partial: a legacy box light stands in for HPL1's ambient color.
	Handle({"SetAmbientColor"}, [this](cScriptCall& c)
	{
		if(!mpWorld) return;
		auto* ambient=static_cast<cLightBoxLegacy*>(mpWorld->GetLight("PenumbraScriptAmbient"));
		if(!ambient)
		{
			ambient=mpWorld->CreateLightBoxLegacy("PenumbraScriptAmbient");
			ambient->SetSize(cVector3f(2000));
			ambient->SetBlendFunc(eLightBoxBlendFunc_Add);
		}
		ambient->SetDiffuseColor(cColor(c.f(0),c.f(1),c.f(2),1));
	}, true);
	Handle({"SetFogActive"}, [this](cScriptCall& c) { if(mpWorld) mpWorld->SetFogActive(c.b(0)); });
	Handle({"SetFogCulling"}, [this](cScriptCall& c) { if(mpWorld) mpWorld->SetFogCulling(c.b(0)); });
	Handle({"SetFogProperties"}, [this](cScriptCall& c)
	{
		if(!mpWorld) return;
		mpWorld->SetFogStart(c.f(0)); mpWorld->SetFogEnd(c.f(1));
		mpWorld->SetFogColor(cColor(c.f(2),c.f(3),c.f(4),1));
	});
	Handle({"SetSkyboxActive"}, [this](cScriptCall& c) { if(mpWorld) mpWorld->SetSkyBoxActive(c.b(0)); });
	Handle({"SetSkyboxColor"}, [this](cScriptCall& c) { if(mpWorld) mpWorld->SetSkyBoxColor(cColor(c.f(0),c.f(1),c.f(2),c.f(3))); });
	Handle({"SetLight3DVisible"}, [this](cScriptCall& c) { if(auto* light=Light(c.s(0))) light->SetVisible(c.b(1)); });
	Handle({"SetLight3DFlickerActive"}, [this](cScriptCall& c) { if(auto* light=Light(c.s(0))) light->SetFlickerActive(c.b(1)); });
	Handle({"SetLight3DFlicker"}, [this](cScriptCall& c)
	{
		auto* light=Light(c.s(0)); if(!light) return;
		const cColor color(c.f(1),c.f(2),c.f(3),c.f(4));
		light->SetFlicker(color,LegacyLightValue(light,c.f(5),color),c.f(6),c.f(7),c.s(8),c.s(9),c.f(10),c.f(11),
		                  c.s(12),c.s(13),c.b(14),c.f(15),c.f(15),c.f(16),c.f(16));
	});
	Handle({"FadeLight3D"}, [this](cScriptCall& c)
	{
		auto* light=Light(c.s(0)); if(!light) return;
		const cColor color(c.f(1),c.f(2),c.f(3),c.f(4));
		light->FadeTo(color,LegacyLightValue(light,c.f(5),color),c.f(6));
	});
	Handle({"SetParticleSystemActive","KillParticleSystem"}, [this](cScriptCall& c)
	{
		auto* ps=mpWorld ? mpWorld->GetParticleSystem(c.s(0)) : nullptr; if(!ps) return;
		if(c.Name()=="KillParticleSystem") ps->Kill(); else ps->SetActive(c.b(1));
	});

	////////////////////////////////
	// Sound and music

	Handle({"PlaySoundEntity","StopSoundEntity","FadeInSoundEntity","FadeOutSoundEntity"}, [this](cScriptCall& c)
	{
		auto* sound=mpWorld ? mpWorld->GetSoundEntity(c.s(0)) : nullptr; if(!sound) return;
		if(c.Name()=="PlaySoundEntity") sound->Play(c.b(1));
		else if(c.Name()=="StopSoundEntity") sound->Stop(c.b(1));
		else if(c.Name()=="FadeInSoundEntity") sound->FadeIn(c.f(1));
		else sound->FadeOut(c.f(1));
	});
	auto playMusic=[this](cScriptCall& c) { if(mpEngine) mpEngine->GetSound()->GetMusicHandler()->Play(c.s(0),c.f(1),c.f(2),c.b(3),false); };
	auto stopMusic=[this](cScriptCall& c) { if(mpEngine) mpEngine->GetSound()->GetMusicHandler()->Stop(c.f(0)); };
	Handle({"PlayMusic"}, playMusic);
	Handle({"StopMusic"}, stopMusic);
	// Partial: one music channel, without HPL1's game music priorities.
	Handle({"PlayGameMusic"}, playMusic, true);
	Handle({"StopGameMusic"}, stopMusic, true);
	Handle({"PlayGuiSound"}, [this](cScriptCall& c)
	{
		if(mpEngine) mpEngine->GetSound()->GetSoundHandler()->PlayGui(SoundVariant(c.s(0)),false,c.f(1));
	});
}
}
