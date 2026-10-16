/* // * Friday Night Funkin´ c engine (c) 2026 nordjkpl */
#include "Scene.h"
// ~ Name Auto Explain What Have Here
// ^ Generic Create and Destroy Scenes part of Code

inline void RayScene_Create(RayScene* scene, RaySceneFunctions functions) { 
    scene->functions = functions;

    if(scene->functions.instantiateFunc != NULL)
        functions.instantiateFunc(scene);
}

inline void RayScene_Destroy(RayScene* scene) { 
    if(scene->functions.destroyFunc != NULL)
        scene->functions.destroyFunc(scene);
}