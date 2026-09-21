#include "source/Examples/E29_LevelSceneEditor/E29_App.h"
#include "source/Examples/E29_LevelSceneEditor/E29_AppInit.h"
#include "source/Examples/E29_LevelSceneEditor/E29_AppFrame.h"

//-----------------------------------------------------------------------------------
// E29 - Level + Scene editor. The editor itself is e29::app (E29_App.h).
//-----------------------------------------------------------------------------------
int E29_Example()
{
    e29::app App;
    if (const int Err = App.Init()) return Err;
    App.Run();
    App.Shutdown();
    return 0;
}
