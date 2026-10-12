// GPLv3 or later. HPL1 script API adapted from Frictional's releases via Antumbra.
#include "MapScripts.h"
#include "InstallResources.h"
#include "impl/scriptarray.h"
#include "impl/scriptstdstring.h"
#include "engine/Engine.h"
#include "resources/Resources.h"
#include "resources/FileSearcher.h"
#include "scene/World.h"
#include "scene/Camera.h"
#include "scene/MeshEntity.h"
#include "scene/Light.h"
#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBody.h"
#include "system/LowLevelSystem.h"
#include "system/String.h"
#include <tinyxml2.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <new>

using namespace hpl;
namespace fs = std::filesystem;
namespace penumbra {
namespace {
	struct cApi { const char* name; const char* result; const char* declaration; };
	const cApi kApi[] = {
#include "ScriptApi.inc"
	};
	// HPL1's AngelScript accepted an if with an empty statement, "if(x);",
	// which later versions reject (several Overture scripts have one). Rewrite
	// it as "if(x){}", which means the same: the code after it always runs.
	int FixEmptyIfs(std::string& source)
	{
		int count=0;
		for(size_t i=0; (i=source.find("if",i))!=std::string::npos; i+=2)
		{
			if(i>0 && (std::isalnum(static_cast<unsigned char>(source[i-1])) || source[i-1]=='_')) continue;
			size_t open=i+2;
			while(open<source.size() && std::isspace(static_cast<unsigned char>(source[open]))) ++open;
			if(open>=source.size() || source[open]!='(') continue;
			size_t close=open; int depth=0;
			for(; close<source.size(); ++close)
			{
				if(source[close]=='(') ++depth;
				else if(source[close]==')' && --depth==0) break;
			}
			if(close>=source.size()) break;
			size_t next=close+1;
			while(next<source.size() && (source[next]==' ' || source[next]=='\t')) ++next;
			if(next<source.size() && source[next]==';') { source.replace(next,1,"{}"); ++count; }
		}
		return count;
	}
	std::string Lower(std::string text) { for(char& c : text) c = char(std::tolower(static_cast<unsigned char>(c))); return text; }
	std::string Attr(const tinyxml2::XMLElement* element, const char* name) { const auto* s = element->Attribute(name); return s ? s : ""; }
	bool Ends(const std::string& text, const std::string& ending) { return text.size() >= ending.size() && text.compare(text.size()-ending.size(),ending.size(),ending) == 0; }
}

void cScriptCall::Return(const std::string& value) { new(mpCall->GetAddressOfReturnLocation()) std::string(value); }

const char* cMapScripts::Status(const std::string& name) const { return mPartial.count(name) ? "partial" : mHandlers.count(name) ? "done" : "stub"; }
bool cMapScripts::WriteApiStatus(const fs::path& path) const
{
	std::ofstream out(path);
	out << "# Penumbra script API\n\nGPL HPL1 reference: Antumbra `engine/game/ScriptFuncs.cpp` and `penumbra-overture/GameScripts.cpp`.\n\n"
		"This checkout registers 173 functions (59 engine + 114 game), rather than the plan's expected 174.\n"
		"`done` means implemented for the current map viewer; `partial` is an approximation or retains setup data for later gameplay; `stub` warns once and returns a neutral value.\n"
		"Area setup functions retain their arguments. Collision callbacks currently use point-in-area overlap (Enter/Leave/During), rather than HPL1's full entity collision checks. Ambient color uses a legacy box light.\n"
		"Map travel implements the destination, entrance and fade-out; link audio/loading text and the requested arrival fade remain pending (arrival currently fades in over 0.5 seconds). Game music uses a single channel without HPL1 priorities.\n"
		"Enemy AI, full inventory UI, save games and advanced player effects remain later gameplay work.\n\n| Declaration | Status |\n|---|---|\n";
	for(const auto& api : kApi) out << "| `" << api.declaration << "` | " << Status(api.name) << " |\n";
	return bool(out);
}

cMapScripts::cMapScripts(const fs::path& root, cEngine* engine, const std::string& languageFile) : mRoot(root), mpEngine(engine)
{
	std::vector<cInstallResourceDir> dirs; std::string error;
	ReadResourceDirs(root, dirs, error);
	for(const auto& dir : dirs)
		if(dir.mbExists) for(const auto& file : fs::directory_iterator(dir.mPath))
			if(file.is_regular_file()) mFiles.emplace(Lower(file.path().filename().string()),file.path());
	// Config is also needed for translated messages in headless script checks.
	for(const auto& languagePath : {ResolvePathNoCase(root,"config/"+languageFile), fs::current_path()/"config/penumbra_english.lang"})
	{
		tinyxml2::XMLDocument language;
		if(language.LoadFile(languagePath.string().c_str()) == tinyxml2::XML_SUCCESS && language.RootElement())
			for(auto* category=language.RootElement()->FirstChildElement("CATEGORY"); category; category=category->NextSiblingElement("CATEGORY"))
				for(auto* entry=category->FirstChildElement("Entry"); entry; entry=entry->NextSiblingElement("Entry"))
					mTranslations[Attr(category,"Name")+"/"+Attr(entry,"Name")] = entry->GetText() ? entry->GetText() : "";
	}
	mpScriptEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
	if(!mpScriptEngine) { mbFailed = true; return; }
	mpScriptEngine->SetUserData(this);
	mpScriptEngine->SetMessageCallback(asFUNCTION(CompileMessage),this,asCALL_CDECL);
	// As HPL1 (Antumbra) did: arrays, with T[] as array<T>, then strings.
	RegisterScriptArray(mpScriptEngine,true);
	RegisterStdString(mpScriptEngine);
	AddHandlers();
	for(const auto& api : kApi)
	{
		const int id=mpScriptEngine->RegisterGlobalFunction(api.declaration,asFUNCTION(Generic),asCALL_GENERIC);
		if(id<0) { Error("Penumbra: failed script registration '%s'\n",api.declaration); mbFailed=true; continue; }
		const auto handler=mHandlers.find(api.name);
		mHandlerById[id]=handler==mHandlers.end() ? nullptr : &handler->second;
	}
}

cMapScripts::~cMapScripts() { if(mpScriptEngine) mpScriptEngine->Release(); }
void cMapScripts::CompileMessage(const asSMessageInfo* message, void* runtime)
{
	// Build() and registration report the failure; this only logs the detail.
	if(message->type == asMSGTYPE_ERROR) Error("Penumbra script %s (%d:%d): %s\n",message->section,message->row,message->col,message->message);
	else Log("Penumbra script %s (%d:%d): %s\n",message->section,message->row,message->col,message->message);
}
void cMapScripts::Budget(asIScriptContext* context, void* runtime)
{
	if(--static_cast<cMapScripts*>(runtime)->mlBudget<=0) context->Abort();
}
fs::path cMapScripts::FindFile(const std::string& name) const
{
	auto it = mFiles.find(Lower(fs::path(name).filename().string()));
	if(it != mFiles.end()) return it->second;
	// Files outside the install, such as the project's own maps in the user
	// directory, through the engine's resource directories.
	const tWString sPath = mpEngine ? mpEngine->GetResources()->GetFileSearcher()->GetFilePath(fs::path(name).filename().string()) : tWString();
	return sPath.empty() ? fs::path() : penumbra::FromUtf8(cString::To8Char(sPath));
}
std::string cMapScripts::Translate(const std::string& category, const std::string& entry) const
{
	auto it = mTranslations.find(category+"/"+entry); return it == mTranslations.end() ? "" : it->second;
}

std::string cMapScripts::EntityType(const std::string& entFile)
{
	auto cached=mEntityTypes.find(Lower(entFile));
	if(cached!=mEntityTypes.end()) return cached->second;
	std::string type="Object";
	tinyxml2::XMLDocument ent;
	const auto path=FindFile(entFile);
	if(!path.empty() && ent.LoadFile(path.string().c_str())==tinyxml2::XML_SUCCESS && ent.RootElement())
		if(auto* main=ent.RootElement()->FirstChildElement("MAIN")) type=Attr(main,"Type");
	return mEntityTypes[Lower(entFile)]=type;
}

void cMapScripts::ReadEntities(const fs::path& map)
{
	// With a world loaded, its areas and entities are the ones scripts see.
	// Headless checks have no world, so they read the map file using the
	// world loader's naming rules. Start positions always come from the file.
	tinyxml2::XMLDocument doc;
	if(doc.LoadFile(map.string().c_str()) != tinyxml2::XML_SUCCESS) return;
	std::vector<tinyxml2::XMLElement*> pending{doc.RootElement()};
	while(!pending.empty())
	{
		auto* element=pending.back(); pending.pop_back();
		if(!element) continue;
		for(auto* child=element->FirstChildElement(); child; child=child->NextSiblingElement()) pending.push_back(child);
		const std::string node=Attr(element,"name");
		if(node.compare(0,7,"_start_")==0) mStarts.insert(node.substr(7));
		else if(mpWorld) continue;
		else if(node.compare(0,6,"_area_")==0)
		{
			const auto end=node.find('_',6); const bool typed=end!=std::string::npos;
			const std::string name=typed ? node.substr(end+1) : Lower(node.substr(6));
			auto& entity=mEntities[name]; entity.type=typed ? Lower(node.substr(6,end-6)) : "script";
		}
		else if(node.compare(0,5,"_ref_")==0)
		{
			// _ref_<resource>_<name>, or the older _ref_<name>_<resource>.
			tStringVec parts; tString separator="_";
			cString::GetStringVec(node,parts,&separator);
			if(parts.size()<3) continue;
			std::string file, name=parts.back();
			for(size_t i=1; i+1<parts.size(); ++i) file+=(i==1 ? "" : "_")+parts[i];
			if(FindFile(file+".ent").empty())
			{
				name=parts[1]; file.clear();
				for(size_t i=2; i<parts.size(); ++i) file+=(i==2 ? "" : "_")+parts[i];
				if(FindFile(file+".ent").empty()) continue;
			}
			auto& entity=mEntities[name]; entity.mesh=name; entity.type=EntityType(file+".ent");
		}
	}
	if(!mpWorld) return;
	for(const auto& pair : *mpWorld->GetAreaEntityMap())
	{
		auto& entity=mEntities[pair.first]; entity.type=pair.second->msType;
		entity.transform=pair.second->m_mtxTransform; entity.size=pair.second->mvSize;
	}
	auto meshes=mpWorld->GetDynamicMeshEntityIterator();
	while(meshes.HasNext())
	{
		auto* mesh=meshes.Next();
		if(!Ends(Lower(mesh->GetSourceFile()),".ent")) continue;
		auto& entity=mEntities[mesh->GetName()]; entity.mesh=mesh->GetName();
		entity.type=EntityType(mesh->GetSourceFile()); entity.transform=mesh->GetWorldMatrix();
	}
}

bool cMapScripts::Load(const std::string& map, cWorld* world, cCamera* camera)
{
	if(mbFailed || !mpScriptEngine) return false;
	mpWorld=world; mpCamera=camera; msMap=map; msNextMap.clear(); msNextStart.clear();
	mbPlayerActive=true; mbWideScreen=false; mfTransitionTime=0;
	msLookTarget.clear(); mbWaveGravity=false; mfWaveTime=0;
	mfFadeTarget=0; mfFadeSpeed=2;
	mEntities.clear(); mStarts.clear(); mLinks.clear(); mAreaSettings.clear(); mEntityCallbacks.clear(); mPickupCallbacks.clear(); mUseCallbacks.clear(); mCollideCallbacks.clear();
	mMessages.clear(); msMessagesCallback.clear(); msSubtitle.clear(); msTempString.clear();
	mDisabledBodies.clear();
	for(auto it=mTimers.begin();it!=mTimers.end();) if(!it->second.global) it=mTimers.erase(it); else ++it;
	mLocalVars=mSavedVars[map];
	mpModule=nullptr;
	const auto mapPath=FindFile(map);
	if(mapPath.empty()) { Error("Penumbra: script map '%s' not found\n",map.c_str()); ++mlErrors; return false; }
	ReadEntities(mapPath);
	const fs::path script=FindFile(fs::path(map).replace_extension(".hps").string());
	if(script.empty()) { Log("Penumbra: map '%s' has no script\n",map.c_str()); return true; }
	std::ifstream input(script,std::ios::binary); std::string source((std::istreambuf_iterator<char>(input)),{});
	if(const int fixed=FixEmptyIfs(source))
		Log("Penumbra script %s: %d empty if statements kept as HPL1 ran them\n",script.filename().string().c_str(),fixed);
	auto* module=mpScriptEngine->GetModule("PenumbraMap",asGM_ALWAYS_CREATE);
	if(module->AddScriptSection(script.filename().string().c_str(),source.data(),source.size())<0 || module->Build()<0)
	{
		Error("Penumbra: script '%s' does not compile; the map runs without it\n",script.filename().string().c_str());
		++mlErrors; mpScriptEngine->DiscardModule("PenumbraMap"); return true;
	}
	mpModule=module;
	Log("Penumbra script compiled: %s (AngelScript %s)\n",map.c_str(),asGetLibraryVersion());
	if(mVisited.insert(map).second) Invoke("OnStart",{},true);
	Invoke("OnLoad",{},true);
	return true;
}
void cMapScripts::Leave()
{
	Invoke("OnExit",{},true); mSavedVars[msMap]=mLocalVars;
	mpWorld=nullptr; mpCamera=nullptr;
}
bool cMapScripts::Invoke(std::string function, const std::vector<std::string>& args, bool optional)
{
	// A failed call is logged and counted; it never stops later calls.
	if(!mpModule || mbFailed) return !mbFailed;
	std::string declaration="void "+function+"(";
	for(size_t i=0;i<args.size();++i) declaration+=(i ? ", string" : "string");
	declaration+=")";
	auto* func=mpModule->GetFunctionByDecl(declaration.c_str());
	if(!func) { if(optional) return true; Error("Penumbra: script callback missing: %s\n",declaration.c_str()); ++mlErrors; return false; }
	auto* context=mpScriptEngine->CreateContext(); context->Prepare(func);
	for(size_t i=0;i<args.size();++i) context->SetArgObject(asUINT(i),const_cast<std::string*>(&args[i]));
	mlBudget=100000; context->SetLineCallback(asFUNCTION(Budget),this,asCALL_CDECL);
	const int result=context->Execute();
	const bool okay=result==asEXECUTION_FINISHED;
	if(!okay)
	{ Error("Penumbra: script '%s' failed (%d): %s\n",function.c_str(),result,context->GetExceptionString() ? context->GetExceptionString() : "execution budget exceeded"); ++mlErrors; }
	context->Release(); return okay;
}
bool cMapScripts::Contains(const cEntity& area, const cVector3f& point) const
{
	const auto local=cMath::MatrixMul(cMath::MatrixInverse(area.transform),point);
	return std::abs(local.x)<=area.size.x*0.5f && std::abs(local.y)<=area.size.y*0.5f && std::abs(local.z)<=area.size.z*0.5f;
}
void cMapScripts::Update(float dt)
{
	if(mbFailed) return;
	if(mMessages.empty() && !msMessagesCallback.empty()) { const auto cb=msMessagesCallback; msMessagesCallback.clear(); Invoke(cb); }
	mfTransitionTime-=dt;
	if(mfFadeAlpha<mfFadeTarget) mfFadeAlpha=std::min(mfFadeTarget,mfFadeAlpha+mfFadeSpeed*dt);
	else mfFadeAlpha=std::max(mfFadeTarget,mfFadeAlpha-mfFadeSpeed*dt);
	if(mfSubtitleTime>0 && (mfSubtitleTime-=dt)<=0) msSubtitle.clear();
	std::vector<std::pair<std::string,std::string>> callbacks;
	for(auto it=mTimers.begin();it!=mTimers.end();)
	{
		if(!it->second.paused) it->second.time-=dt;
		if(it->second.time<=0 && !it->second.paused) { callbacks.emplace_back(it->first,it->second.callback); it=mTimers.erase(it); } else ++it;
	}
	for(const auto& pair : callbacks) Invoke(pair.second,{pair.first});
	Invoke("OnUpdate",{},true);
	if(mpCamera)
	{
		if(!msLookTarget.empty())
		{
			auto entity=mEntities.find(msLookTarget);
			if(entity!=mEntities.end())
			{
				auto pos=entity->second.transform.GetTranslation(); if(auto* mesh=Mesh(msLookTarget)) pos=mesh->GetBoundingVolume()->GetWorldCenter();
				const auto delta=pos-mpCamera->GetPosition();
				const float yaw=std::atan2(-delta.x,-delta.z), pitch=std::atan2(delta.y,std::sqrt(delta.x*delta.x+delta.z*delta.z));
				const float yawDelta=std::remainder(yaw-mpCamera->GetYaw(),k2Pif), pitchDelta=pitch-mpCamera->GetPitch();
				const float limit=std::max(0.0f,mfLookMaxSpeed)*dt;
				mpCamera->AddYaw(std::clamp(yawDelta*mfLookSpeed*dt,-limit,limit));
				mpCamera->SetPitch(mpCamera->GetPitch()+std::clamp(pitchDelta*mfLookSpeed*dt,-limit,limit));
			}
		}
		// Snapshot callbacks: scripts may remove a callback while it is executing.
		std::vector<std::pair<std::string,std::vector<std::string>>> collisionCalls;
		for(auto& cb : mCollideCallbacks)
		{
			auto child=mEntities.find(cb.child); if(child==mEntities.end() || !child->second.active) continue;
			cVector3f pos=mpCamera->GetPosition()-cVector3f(0,0.8f,0);
			if(cb.parent!="Player") { auto* mesh=Mesh(cb.parent); if(!mesh) continue; pos=mesh->GetWorldPosition(); }
			const bool inside=Contains(child->second,pos);
			const auto event=Lower(cb.event);
			if((event=="enter" && inside && !cb.touching) || (event=="leave" && !inside && cb.touching) || (event=="during" && inside)) collisionCalls.push_back({cb.function,{cb.parent,cb.child}});
			cb.touching=inside;
		}
		for(const auto& callback : collisionCalls) Invoke(callback.first,callback.second);
	}
	if(mpWorld && mbWaveGravity)
	{
		mfWaveTime+=dt; const float angle=std::sin(mfWaveTime*k2Pif/mfWavePeriod)*cMath::ToRad(mfWaveAngle);
		const auto rotation=msWaveAxis=="z" ? cMath::MatrixRotateZ(angle) : cMath::MatrixRotateX(angle);
		mpWorld->GetPhysicsWorld()->SetGravity(cMath::MatrixMul(rotation,cVector3f(0,-mfGravity,0)));
	}
}

const std::string& cMapScripts::Message() const { static const std::string empty; return mMessages.empty() ? empty : mMessages.front(); }
void cMapScripts::DismissMessage()
{
	if(mMessages.empty()) return;
	mMessages.erase(mMessages.begin());
	if(mMessages.empty() && !msMessagesCallback.empty()) { const auto cb=msMessagesCallback; msMessagesCallback.clear(); Invoke(cb); }
}
void cMapScripts::Pickup(const std::string& name)
{
	auto found=mEntities.find(name);
	if(found==mEntities.end() || found->second.type!="Item" || !found->second.active) return;
	mInventory.insert(name); SetActive(name,false);
	const auto cb=mPickupCallbacks.find(name); if(cb!=mPickupCallbacks.end()) Invoke(cb->second,{name});
	Log("Penumbra picked up '%s'\n",name.c_str());
}
void cMapScripts::UseItem(const std::string& item, const std::string& entity)
{
	const auto cb=mUseCallbacks.find(item+"/"+entity);
	if(mInventory.count(item) && cb!=mUseCallbacks.end()) Invoke(cb->second,{item,entity});
}
void cMapScripts::Interact(const std::string& name)
{
	const auto entity=mEntities.find(name); if(entity==mEntities.end() || !entity->second.active) return;
	if(entity->second.type=="Item") { Pickup(name); return; }
	const auto callback=mEntityCallbacks.find("PlayerInteract/"+name);
	if(callback!=mEntityCallbacks.end()) { Invoke(callback->second,{name}); return; }
	const auto link=mLinks.find(name); if(link!=mLinks.end()) ChangeMap(link->second);
	else if(mpWorld && mpCamera && !entity->second.mesh.empty())
	{
		// Basic physical interaction for the viewer: nudge the prop or hinged
		// leaf. Full HPL1 dragging/player states remain later gameplay work.
		for(auto* body : Bodies(name))
			if(body->GetMass()>0) body->AddImpulse(mpCamera->GetForward()*std::min(8.0f,body->GetMass()*0.5f));
	}
}
void cMapScripts::Examine(const std::string& name)
{
	const auto entity=mEntities.find(name); if(entity==mEntities.end() || !entity->second.active) return;
	const auto callback=mEntityCallbacks.find("PlayerExamine/"+name);
	if(callback!=mEntityCallbacks.end()) { Invoke(callback->second,{name}); return; }
	if(!entity->second.description.empty())
	{
		mMessages.push_back(entity->second.description);
		if(entity->second.descriptionOnce) entity->second.description.clear();
	}
}
std::string cMapScripts::FindTarget(const cVector3f& origin, const cVector3f& direction, float distance) const
{
	std::string selected; float best=distance;
	for(const auto& pair : mEntities)
	{
		if(!pair.second.active) continue;
		cMatrixf transform=pair.second.transform; cVector3f size=pair.second.size;
		if(auto* mesh=Mesh(pair.first))
		{
			transform=cMatrixf::Identity; transform.SetTranslation(mesh->GetBoundingVolume()->GetWorldCenter());
			size=mesh->GetBoundingVolume()->GetSize();
		}
		if(size.x<=0 || size.y<=0 || size.z<=0) continue;
		const auto inverse=cMath::MatrixInverse(transform);
		const auto localOrigin=cMath::MatrixMul(inverse,origin);
		const auto localDirection=cMath::MatrixMul3x3(inverse,direction);
		float entryDistance=0,exitDistance=best;
		for(int axis=0;axis<3;++axis)
		{
			if(std::abs(localDirection.v[axis])<1e-6f) { if(std::abs(localOrigin.v[axis])>size.v[axis]*0.5f) exitDistance=-1; continue; }
			float a=(-size.v[axis]*0.5f-localOrigin.v[axis])/localDirection.v[axis], b=(size.v[axis]*0.5f-localOrigin.v[axis])/localDirection.v[axis];
			if(a>b) std::swap(a,b); entryDistance=std::max(entryDistance,a); exitDistance=std::min(exitDistance,b);
		}
		if(entryDistance<=exitDistance && entryDistance<best) { selected=pair.first; best=entryDistance; }
	}
	return selected;
}
cMeshEntity* cMapScripts::Mesh(const std::string& name) const
{
	if(!mpWorld) return nullptr; auto found=mEntities.find(name); if(found==mEntities.end() || found->second.mesh.empty()) return nullptr;
	auto meshes=mpWorld->GetDynamicMeshEntityIterator();
	while(meshes.HasNext()) { auto* mesh=meshes.Next(); if(mesh->GetName()==found->second.mesh) return mesh; }
	return nullptr;
}
std::string cMapScripts::SoundVariant(const std::string& name)
{
	// As HPL1's cSoundHandler::CreateChannel: a name not ending in 1-9 plays
	// one of its numbered variants (name1, name2...) at random, not the one
	// played last; without variants, the name itself.
	if(name.empty() || (name.back()>='1' && name.back()<='9') || !mpEngine) return name;
	auto* searcher=mpEngine->GetResources()->GetFileSearcher();
	auto exists=[&](const std::string& file) {
		for(const char* ext : {"ogg","wav"})
			if(!searcher->GetFilePath(cString::SetFileExt(file,ext)).empty()) return true;
		return false;
	};
	int count=0;
	while(exists(name+std::to_string(count+1))) ++count;
	if(count==0) return name;
	int& last=mLastSoundVariant[name];
	int pick=cMath::RandRectl(1,count);
	while(count>2 && pick==last) pick=cMath::RandRectl(1,count);
	last=pick;
	return name+std::to_string(pick);
}
std::vector<iPhysicsBody*> cMapScripts::Bodies(const std::string& name) const
{
	// The entity loader stores each body's entity in its user data.
	std::vector<iPhysicsBody*> result;
	auto* mesh=Mesh(name); if(!mesh) return result;
	auto bodies=mpWorld->GetPhysicsWorld()->GetBodyIterator();
	while(bodies.HasNext()) { auto* body=bodies.Next(); if(body->GetUserData()==mesh) result.push_back(body); }
	return result;
}
iLight* cMapScripts::Light(const std::string& name) const
{
	if(!mpWorld) return nullptr; if(auto* light=mpWorld->GetLight(name)) return light;
	auto lights=mpWorld->GetLightIterator(); while(lights.HasNext()) { auto* light=lights.Next(); if(Ends(light->GetName(),"_"+name)) return light; }
	return nullptr;
}
void cMapScripts::SetActive(const std::string& name, bool active)
{
	mEntities[name].active=active;
	if(auto* mesh=Mesh(name)) { mesh->SetActive(active); mesh->SetVisible(active); }
	// Re-enable only the bodies this disabled: skeleton colliders and ragdoll
	// bodies start inactive and must stay so.
	if(!active)
	{
		auto& disabled=mDisabledBodies[name];
		for(auto* body : Bodies(name)) if(body->IsActive()) { body->SetActive(false); disabled.push_back(body); }
	}
	else
	{
		auto disabled=mDisabledBodies.find(name);
		if(disabled==mDisabledBodies.end()) return;
		for(auto* body : disabled->second) body->SetActive(true);
		mDisabledBodies.erase(disabled);
	}
}
void cMapScripts::Travel(const std::string& map, const std::string& start)
{
	cLink link; link.map=map; link.start=start;
	ChangeMap(link);
}
void cMapScripts::ChangeMap(const cLink& link)
{
	if(!msNextMap.empty()) return;
	msNextMap=link.map; msNextStart=link.start; mfTransitionTime=std::max(0.0f,link.fadeOut);
	mfFadeTarget=1; mfFadeSpeed=link.fadeOut>0 ? 1/link.fadeOut : 0; if(link.fadeOut<=0) mfFadeAlpha=1;
	Log("Penumbra ChangeMap: %s -> %s, start '%s'\n",msMap.c_str(),msNextMap.c_str(),msNextStart.c_str());
}
void cMapScripts::Handle(std::initializer_list<const char*> names, tHandler handler, bool partial)
{
	for(const char* name : names)
	{
		mHandlers[name]=handler;
		if(partial) mPartial.insert(name);
	}
}

void cMapScripts::Generic(asIScriptGeneric* call)
{
	auto* self=static_cast<cMapScripts*>(call->GetEngine()->GetUserData());
	cScriptCall args(call);
	const auto handler=self->mHandlerById.find(call->GetFunction()->GetId());
	if(handler!=self->mHandlerById.end() && handler->second) { (*handler->second)(args); return; }
	if(self->mWarned.insert(args.Name()).second) Warning("Penumbra script stub: %s\n",args.Name().c_str());
	const int type=call->GetFunction()->GetReturnTypeId();
	if(type==self->mpScriptEngine->GetTypeIdByDecl("string")) args.Return(std::string());
	else if(type==asTYPEID_FLOAT) args.Return(0.0f);
	else if(type==asTYPEID_BOOL) args.Return(false);
	else if(type!=asTYPEID_VOID) args.Return(0);
}
}
