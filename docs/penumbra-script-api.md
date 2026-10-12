# Penumbra script API

GPL HPL1 reference: Antumbra `engine/game/ScriptFuncs.cpp` and `penumbra-overture/GameScripts.cpp`.

This checkout registers 173 functions (59 engine + 114 game), rather than the plan's expected 174.
`done` means implemented for the current map viewer; `partial` is an approximation or retains setup data for later gameplay; `stub` warns once and returns a neutral value.
Area setup functions retain their arguments. Collision callbacks currently use point-in-area overlap (Enter/Leave/During), rather than HPL1's full entity collision checks. Ambient color uses a legacy box light.
Map travel implements the destination, entrance and fade-out; link audio/loading text and the requested arrival fade remain pending (arrival currently fades in over 0.5 seconds). Game music uses a single channel without HPL1 priorities.
Enemy AI, full inventory UI, save games and advanced player effects remain later gameplay work.

| Declaration | Status |
|---|---|
| `void Print(string)` | done |
| `string FloatToString(float)` | done |
| `string IntToString(int)` | done |
| `float RandFloat(float, float)` | done |
| `int RandInt(int, int)` | done |
| `bool StringContains(string, string)` | done |
| `void ResetLogicTimer()` | stub |
| `void SetAmbientColor(float, float, float)` | partial |
| `void SetSkyboxActive(bool)` | done |
| `void SetSkyboxColor(float, float, float, float)` | done |
| `void SetSkybox(string)` | stub |
| `void CreateParticleSystemOnCamera(string, string)` | stub |
| `void SetFogActive(bool)` | done |
| `void SetFogCulling(bool)` | done |
| `void SetFogProperties(float, float, float, float, float)` | done |
| `void SetSectorProperties(string, float, float, float)` | stub |
| `void SetSectorPortalActive(string, int, bool)` | stub |
| `string Translate(string, string)` | done |
| `void PreloadSound(string)` | stub |
| `void SetMeshActive(string, bool)` | done |
| `void SetParticleSystemActive(string, bool)` | done |
| `void CreateParticleSystem(string, string, string, float, float, float)` | stub |
| `void KillParticleSystem(string)` | done |
| `void CreateBeam(string, string, string, string)` | stub |
| `void DestroyBeam(string)` | stub |
| `void FadeLight3D(string, float, float, float, float, float, float)` | done |
| `void AttachBillboardToLight3D(string, string, bool)` | stub |
| `void SetLight3DVisible(string, bool)` | done |
| `void SetLight3DOnlyAffectInSector(string, bool)` | stub |
| `void SetLight3DFlickerActive(string, bool)` | done |
| `void SetLight3DFlicker(string, float, float, float, float, float, float, float, string, string, float, float, string, string, bool, float, float)` | done |
| `void CreateSoundEntity(string, string, string)` | stub |
| `void PlaySoundEntity(string, bool)` | done |
| `void StopSoundEntity(string, bool)` | done |
| `void FadeInSoundEntity(string, float)` | done |
| `void FadeOutSoundEntity(string, float)` | done |
| `void PlayMusic(string, float, float, bool)` | done |
| `void StopMusic(float)` | done |
| `void PlayGuiSound(string, float)` | done |
| `void SetJointCallback(string, string, string)` | stub |
| `void BreakJoint(string)` | stub |
| `void SetJointControllerActive(string, string, bool)` | stub |
| `void ChangeJointController(string, string)` | stub |
| `void SetJointControllerPropertyFloat(string, string, string, float)` | stub |
| `float GetJointProperty(string, string)` | stub |
| `float GetBodyProperty(string, string)` | stub |
| `void SetBodyProperty(string, string, float)` | stub |
| `void AttachBodiesWithJoint(string, string, string)` | stub |
| `void SetJointProperty(string, string, float)` | stub |
| `void AddBodyForce(string, string, float, float, float)` | stub |
| `void AddBodyImpulse(string, string, float, float, float)` | stub |
| `void CreateLocalVar(string, int)` | done |
| `void SetLocalVar(string, int)` | done |
| `void AddLocalVar(string, int)` | done |
| `int GetLocalVar(string)` | done |
| `void CreateGlobalVar(string, int)` | done |
| `void SetGlobalVar(string, int)` | done |
| `void AddGlobalVar(string, int)` | done |
| `int GetGlobalVar(string)` | done |
| `void AddToTempString(string)` | done |
| `void AddToTempStringTrans(string, string)` | done |
| `void AddToTempStringAction(string)` | done |
| `void ResetGame()` | stub |
| `void StartCredits()` | stub |
| `void StartDemoEndText()` | stub |
| `void ClearSavedMaps()` | stub |
| `string GetActionKeyString(string)` | done |
| `void AddMessageTrans(string, string)` | done |
| `void AddMessage(string)` | done |
| `void AddMessageTempString()` | done |
| `void AddSubTitleTrans(string, string, float)` | done |
| `void AddSubTitle(string, float)` | done |
| `void AddSubTitleTempString(float)` | done |
| `void AddRadioMessage(string, string, string)` | stub |
| `void SetRadioOnEndCallback(string)` | stub |
| `void SetInventoryMessage(string)` | stub |
| `void SetInventoryMessageTrans(string, string)` | stub |
| `void SetMessagesOverCallback(string)` | done |
| `void ChangeMap(string, string, string, string, float, float, string, string)` | partial |
| `void SetMapGameName(string)` | stub |
| `void SetMapGameNameTrans(string, string)` | stub |
| `void AddNotebookTaskText(string, string)` | stub |
| `void AddNotebookTask(string, string, string)` | stub |
| `void RemoveNotebookTask(string)` | stub |
| `void AddNotebookNote(string, string, string, string)` | stub |
| `void StartNumericalPanel(string, int, int, int, int, float, string)` | stub |
| `void SetInventoryActive(bool)` | stub |
| `void FadeIn(float)` | done |
| `void FadeOut(float)` | done |
| `bool IsFading()` | done |
| `void SetWideScreenActive(bool)` | done |
| `void AutoSave()` | stub |
| `void StartFlash(float, float, float)` | stub |
| `void SetWaveGravityActive(bool)` | done |
| `void SetupWaveGravity(float, float, float, string)` | done |
| `void SetDepthOfFieldActive(bool, float)` | stub |
| `void SetupDepthOfField(float, float, float)` | stub |
| `void FocusOnEntity(string)` | stub |
| `void SetConstantFocusOnEntity(string)` | stub |
| `void PlayGameMusic(string, float, float, bool, int)` | partial |
| `void StopGameMusic(float, int)` | partial |
| `void StartScreenShake(float, float, float, float)` | stub |
| `void CreateLightFlashAtArea(string, float, float, float, float, float, float, float)` | stub |
| `void CreateSplashDamage(string, float, float, float, float, float, float, int)` | stub |
| `void CreateTimer(string, float, string, bool)` | done |
| `void DestroyTimer(string)` | done |
| `void SetTimerPaused(string, bool)` | done |
| `void SetTimerTime(string, float)` | done |
| `void AddTimerTime(string, float)` | done |
| `float GetTimerTime(string)` | done |
| `void GivePlayerDamage(float, string)` | stub |
| `void SetPlayerHealth(float)` | stub |
| `float GetPlayerHealth()` | stub |
| `void SetPlayerPose(string, bool)` | stub |
| `void SetPlayerActive(bool)` | done |
| `void StartPlayerLookAt(string, float, float)` | done |
| `void StopPlayerLookAt()` | done |
| `void StartPlayerFearFilter(float)` | stub |
| `void StopPlayerFearFilter()` | stub |
| `void SetFlashlightDisabled(bool)` | stub |
| `void AddPickupCallback(string, string)` | done |
| `void AddUseCallback(string, string, string)` | done |
| `void AddCombineCallback(string, string, string)` | stub |
| `void RemovePickupCallback(string)` | done |
| `void RemoveUseCallback(string)` | done |
| `void RemoveCombineCallback(string)` | stub |
| `bool HasItem(string)` | done |
| `void RemoveItem(string)` | done |
| `void GiveItem(string, string, int)` | done |
| `void ReplaceEntity(string, string, string, string)` | stub |
| `void SetGameEntityActive(string, bool)` | done |
| `bool GetGameEntityActive(string)` | done |
| `void CreateGameEntityVar(string, string, int)` | done |
| `void SetGameEntityVar(string, string, int)` | done |
| `void AddGameEntityVar(string, string, int)` | done |
| `int GetGameEntityVar(string, string)` | done |
| `void SetGameEntityMaxExamineDist(string, float)` | stub |
| `void SetGameEntityMaxInteractDist(string, float)` | stub |
| `void SetGameEntityDescriptionTrans(string, string, string)` | done |
| `void SetGameEntityDescriptionOnceTrans(string, string, string)` | done |
| `void SetGameEntityDescription(string, string)` | done |
| `void SetGameEntityGameNameTrans(string, string, string)` | stub |
| `void ChangeEntityAnimation(string, string, bool)` | stub |
| `void SetEntityHealth(string, float)` | stub |
| `void DamageEntity(string, float, int)` | stub |
| `void SetDoorState(string, string)` | stub |
| `void SetObjectInteractMode(string, string)` | stub |
| `void SetupLink(string, string, string, string, string, float, float)` | partial |
| `void SetupLinkLoadText(string, string, string, string, string, float, float, string, string)` | partial |
| `void SetAreaCustomIcon(string, string)` | partial |
| `void AddEnemyPatrolNode(string, string, float, string)` | stub |
| `void ClearEnemyPatrolNodes(string)` | stub |
| `void SetEnemyDeathCallback(string, string)` | stub |
| `void SetEnemyAttackCallback(string, string)` | stub |
| `float GetEnemyHealth(string)` | stub |
| `void SetEnemyUseTriggers(string, bool)` | stub |
| `void ShowEnemyPlayer(string)` | stub |
| `void SetDoorLocked(string, bool)` | stub |
| `void SetupStickArea(string, bool, bool, bool, bool, float, string, string, string, string, string, string)` | partial |
| `void AllowAttachment()` | stub |
| `void SetLampLit(string, bool, bool)` | stub |
| `void SetLampFlicker(string, bool)` | stub |
| `void SetLampLitChangeCallback(string, string)` | stub |
| `void SetupLadder(string, string, string, string)` | partial |
| `void SetupDamageArea(string, float, float, int, bool, bool)` | partial |
| `void SetupForceArea(string, float, float, float, float, bool, bool, bool, bool)` | partial |
| `void SetupLiquidArea(string, float, float, float, string, float, float, float, bool)` | partial |
| `void SetupSaveArea(string, string, string, string)` | partial |
| `void AddEntityCollideCallback(string, string, string, string)` | partial |
| `void RemoveEntityCollideCallback(string, string, string)` | done |
| `void AddEntityCallback(string, string, string)` | done |
| `void RemoveEntityCallback(string, string)` | done |
| `void CreateSoundEntityAt(string, string, string, string)` | stub |
