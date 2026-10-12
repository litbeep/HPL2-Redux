// Penumbra script host. GPLv3 or later; ABI and behavior reference: Antumbra/HPL1.
#ifndef PENUMBRA_MAP_SCRIPTS_H
#define PENUMBRA_MAP_SCRIPTS_H
#include "math/MathTypes.h"
#include <angelscript.h>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace hpl { class cEngine; class cWorld; class cCamera; class cMeshEntity; class iLight; class iPhysicsBody; }
namespace penumbra {

class cMapScripts
{
public:
	cMapScripts(const std::filesystem::path& root, hpl::cEngine* engine = nullptr, const std::string& language = "English.lang");
	~cMapScripts();
	// False only when the map is missing or the host is unusable. A script
	// that fails to compile or run is logged and counted in Errors(), and
	// play goes on without it, as in HPL1.
	bool Load(const std::string& map, hpl::cWorld* world = nullptr, hpl::cCamera* camera = nullptr);
	void Leave();
	void Update(float dt);
	bool Invoke(std::string function, const std::vector<std::string>& args = {}, bool optional = false);
	void Interact(const std::string& entity);
	void Examine(const std::string& entity);
	void Pickup(const std::string& entity);
	void UseItem(const std::string& item, const std::string& entity);
	void DismissMessage();
	std::string FindTarget(const hpl::cVector3f& origin, const hpl::cVector3f& direction, float distance) const;
	bool Failed() const { return mbFailed; } // The host itself could not be set up
	int Errors() const { return mlErrors; }  // Script errors so far, all maps
	bool PlayerActive() const { return mbPlayerActive && mMessages.empty(); }
	float FadeAlpha() const { return mfFadeAlpha; }
	bool WideScreen() const { return mbWideScreen; }
	const std::string& Message() const;
	const std::string& Subtitle() const { return msSubtitle; }
	const std::string& CurrentMap() const { return msMap; }
	const std::string& NextMap() const { return msNextMap; }
	const std::string& NextStart() const { return msNextStart; }
	bool TransitionReady() const { return !msNextMap.empty() && mfTransitionTime <= 0; }
	bool HasStart(const std::string& name) const { return mStarts.count(name)!=0; }
	const std::set<std::string>& Inventory() const { return mInventory; }
	static const char* Status(const std::string& name);
	static bool WriteApiStatus(const std::filesystem::path& path);

private:
	struct cTimer { float time; std::string callback; bool global, paused = false; };
	struct cEntity { std::string type, mesh, description; bool active = true, descriptionOnce = false; hpl::cMatrixf transform = hpl::cMatrixf::Identity; hpl::cVector3f size = 0; };
	struct cLink { std::string map, start, startSound, stopSound, category, entry; float fadeOut = 0, fadeIn = 0; };
	struct cCollide { std::string event, parent, child, function; bool touching = false; };
	void Dispatch(asIScriptGeneric* call);
	static void Generic(asIScriptGeneric* call);
	static void CompileMessage(const asSMessageInfo* message, void* runtime);
	static void Budget(asIScriptContext* context, void* runtime);
	std::filesystem::path FindFile(const std::string& name) const;
	void ReadEntities(const std::filesystem::path& map);
	void SetActive(const std::string& name, bool active);
	std::string EntityType(const std::string& entFile);
	hpl::cMeshEntity* Mesh(const std::string& name) const;
	std::vector<hpl::iPhysicsBody*> Bodies(const std::string& name) const;
	hpl::iLight* Light(const std::string& name) const;
	std::string Translate(const std::string& category, const std::string& entry) const;
	void ChangeMap(const cLink& link);
	bool Contains(const cEntity& area, const hpl::cVector3f& point) const;
	std::filesystem::path mRoot;
	hpl::cEngine* mpEngine;
	hpl::cWorld* mpWorld = nullptr;
	hpl::cCamera* mpCamera = nullptr;
	asIScriptEngine* mpScriptEngine = nullptr;
	asIScriptModule* mpModule = nullptr;
	bool mbFailed = false, mbPlayerActive = true, mbWideScreen = false;
	int mlBudget = 0, mlErrors = 0;
	std::map<std::string,std::filesystem::path> mFiles;
	std::map<std::string,std::string> mEntityTypes;
	std::map<std::string,std::vector<hpl::iPhysicsBody*>> mDisabledBodies;
	std::map<std::string,std::string> mTranslations;
	std::map<std::string,int> mLocalVars, mGlobalVars, mEntityVars;
	std::map<std::string,std::map<std::string,int>> mSavedVars;
	std::map<std::string,cEntity> mEntities;
	std::map<std::string,cLink> mLinks;
	std::map<std::string,std::vector<std::string>> mAreaSettings;
	std::map<std::string,cTimer> mTimers;
	std::map<std::string,std::string> mEntityCallbacks, mPickupCallbacks, mUseCallbacks;
	std::vector<cCollide> mCollideCallbacks;
	std::set<std::string> mInventory, mVisited, mWarned;
	std::set<std::string> mStarts;
	std::vector<std::string> mMessages;
	std::string msTempString, msMessagesCallback, msSubtitle, msMap, msNextMap, msNextStart;
	float mfSubtitleTime = 0, mfFadeAlpha = 0, mfFadeTarget = 0, mfFadeSpeed = 0, mfTransitionTime = 0;
	bool mbWaveGravity = false;
	float mfWaveTime = 0, mfWaveAngle = 0, mfWavePeriod = 1, mfGravity = 9.8f;
	std::string msWaveAxis = "x", msLookTarget;
	float mfLookSpeed = 1, mfLookMaxSpeed = 1;
};
}
#endif
