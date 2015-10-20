#ifndef CORE_MODEL_H
#define CORE_MODEL_H

#include "BaseInclude.h"
#include "CoreGeometry.h"
#include "CoreProgram.h"
#include "CoreVector3f.h"
#include "CoreMatrix4f.h"

class CoreModel {
public:
	int id;
  CoreGeometry* geometry(JSContext *cx);
  OVR::GlProgram* program(JSContext *cx);
  OVR::Matrix4f* matrix(JSContext *cx);
  void ComputeMatrix(JSContext *cx);

  mozilla::Maybe<JS::PersistentRootedValue> geometryVal;
  mozilla::Maybe<JS::PersistentRootedValue> programVal;
  mozilla::Maybe<JS::PersistentRootedValue> matrixVal;
  
	OVR::Vector3f* position;
	OVR::Vector3f* rotation;
	OVR::Vector3f* scale;
  OVR::Matrix4f* computedMatrix = NULL;
  mozilla::Maybe<JS::PersistentRootedValue> onFrame;
	mozilla::Maybe<JS::PersistentRootedValue> onHoverOver;
	mozilla::Maybe<JS::PersistentRootedValue> onHoverOut;
  mozilla::Maybe<JS::PersistentRootedValue> onGestureTouchDown;
  mozilla::Maybe<JS::PersistentRootedValue> onGestureTouchUp;
  mozilla::Maybe<JS::PersistentRootedValue> onGestureTouchCancel;
	bool isHovered;
  bool isTouching;
};

int GetNextModelId();

void SetupCoreModel(JSContext *cx, JS::RootedObject *global, JS::RootedObject *core);
JSObject* NewCoreModel(JSContext *cx, CoreModel* model);
CoreModel* GetCoreModel(JS::HandleObject obj);

#endif