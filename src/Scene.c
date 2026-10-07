#include "std.h"
#include "Scene.h"

void RayScene_Create(RayScene* scene, RaySceneFunctions functions) { 
    scene->functions = functions;
    
    if(scene->functions.instantiateFunc != NULL)
        functions.instantiateFunc(scene);
}

void RayScene_Destroy(RayScene* scene) { 
    if(scene->functions.destroyFunc != NULL)
        scene->functions.destroyFunc(scene);
}