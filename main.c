#include "headers/std.h"
#include "headers/raylib.h" 
#include "headers/Game.h"
#include "headers/scenes/AllScenes.h"
 
int main() { 
	RayGame_Start("Friday Night Funkin' | C port");
	AllScenes_StartGame();
	TitleState_SetScene(); 

	RayGame_GameLoop();
	AllScenes_DestroyGame();
	RayGame_Destroy();

	return 0;
}