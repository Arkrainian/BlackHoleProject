#include "TestFramework.h"

#include "Astronomy/TidalDisruptionFlare.h"

using namespace TestFramework;

// Validates Astronomy::TidalDisruptionFlare's decaying-accumulator model
// (spec 28.12/28.13's simplified stand-in for a real tidal-disruption-
// event fallback light curve -- see the class's own header comment for
// what it deliberately does NOT model).
void RunTidalFlareTests() {
    {
        Astronomy::TidalDisruptionFlare flare;
        ExpectTrue(!flare.IsActive(), "A fresh flare tracker starts inactive");
        ExpectNear(flare.EddingtonFractionBoost(), 0.0, 1e-12, "A fresh flare tracker has zero boost");
    }

    {
        Astronomy::TidalDisruptionFlare flare;
        flare.RegisterCapturedMass(5.972e24); // Earth mass
        ExpectTrue(flare.IsActive(), "Registering a substantial captured mass activates the flare");
        ExpectTrue(flare.EddingtonFractionBoost() > 0.0, "Registering captured mass produces a positive boost");
    }

    {
        // Decay: repeated Update() calls with no new captures should
        // monotonically reduce the boost toward zero.
        Astronomy::TidalDisruptionFlare flare;
        flare.RegisterCapturedMass(5.972e24);
        const double initialBoost = flare.EddingtonFractionBoost();
        double lastBoost = initialBoost;
        bool everIncreased = false;
        for (int step = 0; step < 300; ++step) { // 5 minutes at dt=1
            flare.Update(1.0);
            const double boost = flare.EddingtonFractionBoost();
            if (boost > lastBoost) everIncreased = true;
            lastBoost = boost;
        }
        ExpectTrue(!everIncreased, "The boost never increases on its own between captures (monotonic decay)");
        ExpectTrue(lastBoost < initialBoost, "The boost has decayed well below its initial value after 5 minutes");
        ExpectTrue(lastBoost >= 0.0, "The boost never goes negative");
    }

    {
        // Clamp: an enormous single capture event should not send the
        // boost past its documented ceiling.
        Astronomy::TidalDisruptionFlare flare;
        flare.RegisterCapturedMass(1.0e40); // absurdly large, far beyond any real planet
        ExpectTrue(flare.EddingtonFractionBoost() <= 0.6 + 1e-9,
                   "An enormous capture event is clamped to the documented boost ceiling");
    }

    {
        // Repeated registrations accumulate (don't just overwrite).
        Astronomy::TidalDisruptionFlare flare;
        flare.RegisterCapturedMass(1.0e23);
        const double afterOne = flare.EddingtonFractionBoost();
        flare.RegisterCapturedMass(1.0e23);
        const double afterTwo = flare.EddingtonFractionBoost();
        ExpectTrue(afterTwo > afterOne, "Registering captured mass twice produces a larger boost than once");
    }
}
