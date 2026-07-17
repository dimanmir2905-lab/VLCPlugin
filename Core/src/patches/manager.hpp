#pragma once
#include "utils/memory.hpp"

namespace Patches {

    void ApplyClientVersion(const char* version);
    void DisableOpenSAA();
    void RemoveVehicleLimit();
    void FixCNetStatFreeze();
    void ApplyCustomData();
    void DisableSampDialogs();

    // √лавна€ функци€, примен€юща€ все патчи разом
    void ApplyAll();

}