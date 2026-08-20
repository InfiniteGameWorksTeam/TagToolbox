// Copyright Infinite Game Works. All Rights Reserved.

#include "Modules/ModuleManager.h"

/** Runtime module: owns the settings/registry; no startup work required. */
class FTagToolboxModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FTagToolboxModule, TagToolbox)
