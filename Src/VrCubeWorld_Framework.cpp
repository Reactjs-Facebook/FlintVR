#include "BaseInclude.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "SoundEffectContext.h"
#include <memory>
#include "CoreProgram.h"
#include "CoreVector3f.h"
#include "CoreVector4f.h"
#include "CoreMatrix4f.h"
#include "CoreGeometry.h"
#include "CoreModel.h"
#include "CoreScene.h"

#if 0
	#define GL( func )		func; EglCheckErrors();
#else
	#define GL( func )		func;
#endif

#define ERROR_DISPLAY_SECONDS 10

#define LOG_COMPONENT "VrCubeWorld"

/*
================================================================================

VrCubeWorld

================================================================================
*/

namespace OVR
{

static const int CPU_LEVEL			= 1;
static const int GPU_LEVEL			= 1;

OVR::String PREVIOUS_ERROR;
OVR::String LATEST_ERROR;

void reportError(JSContext *cx, const char *message, JSErrorReport *report) {
	OVR::String err = OVR::String::Format("%s:%u:%s\n",
			report->filename ? report->filename : "[no filename]",
      (unsigned int) report->lineno,
      message);
	if (err != PREVIOUS_ERROR) {
		PREVIOUS_ERROR = err;
		LATEST_ERROR = err;
		__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "%s", err.ToCStr());
	}
}

class VrCubeWorld : public VrAppInterface
{
public:
						VrCubeWorld(AAssetManager *assetManager);
						~VrCubeWorld();

	virtual void 		Configure( ovrSettings & settings );

	virtual void		OneTimeInit( const char * fromPackage, const char * launchIntentJSON, const char * launchIntentURI );
	virtual void		OneTimeShutdown();
	virtual bool		OnKeyEvent( const int keyCode, const int repeatCount, const KeyEventType eventType );
	virtual Matrix4f	Frame( const VrFrame& vrFrame );
	virtual Matrix4f	DrawEyeView( const int eye, const float fovDegreesX, const float fovDegreesY, ovrFrameParms & frameParms );

	ovrLocale &			GetLocale() { return *Locale; }

private:
	std::unique_ptr<ovrSoundEffectContext> SoundEffectContext;
	std::unique_ptr<OvrGuiSys::SoundEffectPlayer> SoundEffectPlayer;
	OvrGuiSys *			   GuiSys;
	ovrLocale *			   Locale;
	unsigned int		   Random;
	ovrMatrix4f			   CenterEyeViewMatrix;
	AAssetManager 		   *AssetManager;
	JSRuntime * 		   SpidermonkeyJSRuntime;
	JSContext *            SpidermonkeyJSContext;
	mozilla::Maybe<JS::PersistentRootedObject> SpidermonkeyGlobal;
	float				RandomFloat();
	CoreScene* coreScene;
	mozilla::Maybe<JS::CompileOptions> CompileOptions;
};

// Build global JS object
static JSClass globalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS
};

VrCubeWorld::VrCubeWorld(AAssetManager *assetManager) :
	GuiSys( OvrGuiSys::Create() ),
	Locale( NULL ),
	Random( 2 )
{
	CenterEyeViewMatrix = ovrMatrix4f_CreateIdentity();
	AssetManager = assetManager;
}

VrCubeWorld::~VrCubeWorld()
{
	OvrGuiSys::Destroy( GuiSys );
}


float VrCubeWorld::RandomFloat()
{
	Random = 1664525L * Random + 1013904223L;
	unsigned int rf = 0x3F800000 | ( Random & 0x007FFFFF );
	return (*(float *)&rf) - 1.0f;
}

void VrCubeWorld::OneTimeInit( const char * fromPackageName, const char * launchIntentJSON, const char * launchIntentURI )
{
	auto java = app->GetJava();
	SoundEffectContext.reset( new ovrSoundEffectContext( *java->Env, java->ActivityObject ) );
	SoundEffectContext->Initialize();
	SoundEffectPlayer.reset( new OvrGuiSys::ovrDummySoundEffectPlayer() );

	Locale = ovrLocale::Create( *app, "default" );

	String fontName;
	GetLocale().GetString( "@string/font_name", "efigs.fnt", fontName );
	GuiSys->Init( this->app, *SoundEffectPlayer, fontName.ToCStr(), &app->GetDebugLines() );

	app->SetShowFPS(true);

	// Load the script into memory
	const char* filename = "hello2.js";
	AAsset* asset = AAssetManager_open(AssetManager, filename, AASSET_MODE_BUFFER);
	if (NULL == asset) {
		__android_log_print(ANDROID_LOG_VERBOSE, LOG_COMPONENT, "ASSET NOT FOUND: %s", filename);
		return;
	}

	// Initialize JS engine
	JS_Init();

	// Set up the JS runtime and context
	SpidermonkeyJSRuntime = JS_NewRuntime(32L * 1024 * 1024); // Garbage collect at 32MB
	if (!SpidermonkeyJSRuntime) {
		return;
	}
	SpidermonkeyJSContext = JS_NewContext(SpidermonkeyJSRuntime, 8192);
	if (!SpidermonkeyJSContext) {
		return;
	}
	JSContext *cx = SpidermonkeyJSContext;

	JS_SetErrorReporter(SpidermonkeyJSRuntime, reportError);

	// Create the global JS object
	JS::RootedObject global(cx, JS::PersistentRootedObject(cx, JS_NewGlobalObject(cx, &globalClass, nullptr, JS::FireOnNewGlobalHook)));

	JS::RootedValue rval(cx);
	{
		JSAutoCompartment ac(cx, global);
		JS_InitStandardClasses(cx, global);

		// Compile and execute the script that should export vrmain
		CompileOptions.emplace(cx);
		bool ok = JS::Evaluate(cx, CompileOptions.ref(), (const char*)AAsset_getBuffer(asset), AAsset_getLength(asset), &rval);
		AAsset_close(asset);
		if (!ok) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not evaluate script");
			app->ShowInfoText(ERROR_DISPLAY_SECONDS, "Could not evaluate script");
		}

		// Build the environment to send to vrmain
		JS::RootedObject core(cx, JS_NewObject(cx, nullptr));
		JS::RootedValue coreValue(cx, JS::ObjectOrNullValue(core));
		SetupCoreProgram(cx, &global, &core);
		SetupCoreVector3f(cx, &global, &core);
		SetupCoreVector4f(cx, &global, &core);
		SetupCoreMatrix4f(cx, &global, &core);
		SetupCoreGeometry(cx, &global, &core);
		SetupCoreModel(cx, &global, &core);
		JS::RootedObject env(cx, JS_NewObject(cx, nullptr));
		coreScene = SetupCoreScene(cx, &global, &core, &env);
		if (!JS_SetProperty(cx, env, "core", coreValue)) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not add env.core\n");
			return;
		}
		JS::RootedValue envValue(cx, JS::ObjectOrNullValue(env));

		// Call vrmain from script
		if (ok) {
			ok = JS_CallFunctionName(cx, global, "vrmain", JS::HandleValueArray(envValue), &rval);
			if (!ok) {
				__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not call vrmain\n");
			}
		}

		SpidermonkeyGlobal.reset();
		SpidermonkeyGlobal.emplace(cx, global);
	}
}

void VrCubeWorld::OneTimeShutdown()
{
	JS_DestroyContext(SpidermonkeyJSContext);
	JS_DestroyRuntime(SpidermonkeyJSRuntime);
	JS_ShutDown();
}

void VrCubeWorld::Configure( ovrSettings & settings )
{
	settings.PerformanceParms.CpuLevel = CPU_LEVEL;
	settings.PerformanceParms.GpuLevel = GPU_LEVEL;
	settings.EyeBufferParms.multisamples = 2;
}

bool VrCubeWorld::OnKeyEvent( const int keyCode, const int repeatCount, const KeyEventType eventType )
{
	if ( GuiSys->OnKeyEvent( keyCode, repeatCount, eventType ) )
	{
		return true;
	}
	return false;
}

Matrix4f VrCubeWorld::Frame( const VrFrame& vrFrame )
{
	CenterEyeViewMatrix = vrapi_GetCenterEyeViewMatrix( &app->GetHeadModelParms(), &vrFrame.Tracking, NULL );

	// Show any errors
	if (!LATEST_ERROR.IsEmpty()) {
		app->ShowInfoText(ERROR_DISPLAY_SECONDS, "%s", LATEST_ERROR.ToCStr());
		LATEST_ERROR.Clear();
	}

	// Call the function returned from vrmain
	JSContext *cx = SpidermonkeyJSContext;

	JS::RootedObject global(cx, SpidermonkeyGlobal.ref());
	{
		JSAutoCompartment ac(cx, global);

		OVR::Vector3f* viewPos = new OVR::Vector3f( GetViewMatrixPosition( CenterEyeViewMatrix ) );
		OVR::Vector3f* viewFwd = new OVR::Vector3f( GetViewMatrixForward( CenterEyeViewMatrix ) );

		// Build the ev
		JS::RootedObject ev(cx, JS_NewObject(cx, nullptr));
		JS::RootedValue viewPosVal(cx, JS::ObjectOrNullValue(NewCoreVector3f(cx, viewPos)));
		if (!JS_SetProperty(cx, ev, "viewPos", viewPosVal)) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not set ev.viewPos\n");
			JS_ReportError(cx, "Could not set ev.viewPos");
			return CenterEyeViewMatrix;
		}
		JS::RootedValue viewFwdVal(cx, JS::ObjectOrNullValue(NewCoreVector3f(cx, viewFwd)));
		if (!JS_SetProperty(cx, ev, "viewFwd", viewFwdVal)) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not set ev.viewFwd\n");
			JS_ReportError(cx, "Could not set ev.viewFwd");
			return CenterEyeViewMatrix;
		}
		JS::RootedValue nowVal(cx, JS::NumberValue(vrapi_GetTimeInSeconds()));
		if (!JS_SetProperty(cx, ev, "now", nowVal)) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_COMPONENT, "Could not set ev.now\n");
			JS_ReportError(cx, "Could not set ev.now");
			return CenterEyeViewMatrix;
		}
		JS::RootedValue evValue(cx, JS::ObjectOrNullValue(ev));

		coreScene->ComputeMatrices(cx);
		coreScene->CallFrameCallbacks(cx, evValue);
		coreScene->CallGazeCallbacks(cx, viewPos, viewFwd, vrFrame, evValue);
	}

	// Update GUI systems last, but before rendering anything.
	GuiSys->Frame( vrFrame, CenterEyeViewMatrix );

	return CenterEyeViewMatrix;
}

Matrix4f VrCubeWorld::DrawEyeView( const int eye, const float fovDegreesX, const float fovDegreesY, ovrFrameParms& frameParms )
{
	const Matrix4f eyeViewMatrix = vrapi_GetEyeViewMatrix( &app->GetHeadModelParms(), &CenterEyeViewMatrix, eye );
	const Matrix4f eyeProjectionMatrix = ovrMatrix4f_CreateProjectionFov( fovDegreesX, fovDegreesY, 0.0f, 0.0f, 0.2f, 0.0f );
	const Matrix4f eyeViewProjection = eyeProjectionMatrix * eyeViewMatrix;

	// Call our scene's DrawEyeView
	JSContext *cx = SpidermonkeyJSContext;
	JS::RootedObject global(cx, SpidermonkeyGlobal.ref());
	{
		JSAutoCompartment ac(cx, global);
		coreScene->DrawEyeView(cx, eye, eyeViewMatrix, eyeProjectionMatrix, eyeViewProjection, frameParms);
	}

	GuiSys->RenderEyeView( CenterEyeViewMatrix, eyeViewProjection );

	return eyeViewProjection;
}

} // namespace OVR

extern "C"
{

long Java_com_oculus_vrcubeworld_MainActivity_nativeSetAppInterface( JNIEnv *jni, jclass clazz, jobject activity,
	jstring fromPackageName, jstring commandString, jstring uriString, jobject mgr )
{
	// This is called by the java UI thread.
	LOG( "nativeSetAppInterface" );
	AAssetManager *assetManager = AAssetManager_fromJava(jni, mgr);
	OVR::VrCubeWorld* cubeWorld = new OVR::VrCubeWorld(assetManager);
	return cubeWorld->SetActivity( jni, clazz, activity, fromPackageName, commandString, uriString );
}

} // extern "C"
