#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include "models.h"

// =============================================================================
// System State Manager Interface
// =============================================================================

// Global system initialization and management functions
bool initializeSystem();
void updateSystem();
const SystemState& getSystemState();

#endif // STATE_MANAGER_H
