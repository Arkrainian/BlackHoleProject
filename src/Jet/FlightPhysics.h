#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Ported (and adapted for orbiting a black hole instead of flying over an
// Earth-like atmosphere) from the "jet system new" project's Physics class.
// Rotation is direct rate-control (input -> target angular velocity), not
// torque/moment of inertia -- see UpdateRotation.
//
// Forward speed is DIRECT "cruise control," not a thrust/drag force
// simulation -- a deliberate redesign, not the original design. The first
// version of this class modeled real thrust/drag forces (a Newton value
// integrated against velocity-squared drag until the two balanced at some
// equilibrium speed); that was replaced after two real, reported problems
// with it: (1) because equilibrium speed scales as sqrt(thrust) rather than
// linearly, a few seconds of Ctrl barely moved the actual cruise speed
// while the same few seconds of Shift felt dramatically more effective,
// reading as "I can't slow down and it speeds up" even though both keys
// changed the underlying value by the same amount; and (2) the user
// explicitly asked for direct, literal "set speed" control -- Shift/Ctrl
// should raise/lower a speed value with no ceiling of their own, not tune
// an indirect equilibrium. See `cruiseSpeedSimUnitsPerSec` below.
//
// Two other changes from the original "jet system new" project, both
// deliberate:
//   1. Gravity still pulls toward the black hole at the origin using the
//      EXACT general-relativistic Schwarzschild static-observer
//      acceleration, (GM/r^2)/sqrt(1-r_s/r) -- see UpdateForces -- instead
//      of a constant -9.81 or plain Newtonian 1/r^2. It is genuinely
//      integrated into `velocity` every frame, same as before the cruise-
//      control redesign -- but its effect on OVERALL SPEED is now
//      overwritten by the pilot's set speed every frame (see
//      `cruiseSpeedSimUnitsPerSec`'s own comment for exactly what survives
//      and what doesn't: the real, honest tradeoff this redesign makes).
//   2. Air density/altitude/ocean/ground mechanics are removed entirely --
//      there's no atmosphere or sea level in orbit around a black hole.
namespace Jet {

const float G_REFERENCE = 9.81f;           // m/s^2, used only to normalize G-force display, not as world gravity

// How fast Shift/Ctrl change `cruiseSpeedSimUnitsPerSec`, in (sim-units/s)
// per second -- i.e. this many sim-units/s of extra speed for every second
// either key is held. No ceiling of its own: holding Shift raises the set
// speed for as long as it's held, with no equilibrium or engine-rating
// limit to plateau against (explicitly requested: "shift or ctrl
// infinitely reduces/increases the speed"). The only thing that still
// bounds the ship's ACTUAL velocity is the honest relativistic cap in
// Update() below -- the set speed itself can be dialed arbitrarily high or
// low, it just won't all show up as real motion past that cap. Lowered
// from an original 400 alongside MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC
// below (see that constant's comment) -- reported directly: "the ship is
// very hard to control, make the speed limit much lower." At 400/s^2,
// even a brief Shift tap added hundreds of sim-units/s, overshooting the
// controllable range in well under a second; 150/s^2 gives a full ramp
// from the default cruise speed to the new, much lower cap in a few
// seconds instead of a fraction of one.
const float CRUISE_SPEED_RAMP_RATE = 150.0f;

// Hard ceiling on real speed, as a fraction of c -- chosen to sit comfortably
// above anything a real jet/spacecraft reaches but still read as "fast but
// not instantaneous" the way Interstellar's Endurance does near Gargantua,
// rather than letting an arbitrarily large set speed read as literally
// faster than light. See Update().
const float MAX_SPEED_FRACTION_OF_C = 0.2f;

// Floor on the speed cap, in sim-units/s, applied AFTER the real-c
// conversion above (see Update): max(relativisticCapSimUnits, this). Real
// bug this fixes, not a hypothetical -- the relativistic cap is a REAL
// speed (0.2c) converted into sim-units/s via metersPerSimUnit, and
// metersPerSimUnit grows with the black hole's mass (see Physics/
// Constants.h). At a stellar-mass hole metersPerSimUnit is kilometers, so
// the converted cap is huge and never binds in practice. At TON 618
// (~6.6e10 solar masses, this app's default) metersPerSimUnit is ~0.01
// light-years, so the SAME 0.2c converts to roughly 6e-7 sim-units/s --
// imperceptibly slow, meaning the jet was effectively frozen in place by
// its own speed cap, not by any gravity or input problem, immediately
// after spawning near any sufficiently massive hole. This floor keeps the
// pure relativistic cap intact (and binding) for small/moderate-mass
// holes, where it's already far above this floor and max() changes
// nothing, but guarantees the ship never becomes physically unplayable
// just because the hole is supermassive.
//
// Lowered from an original 10000 to 600, reported directly: "the ship is
// very hard to control, make the speed limit much lower." 10000 was
// sized to clear the OLD thrust/drag model's own natural top speed
// (~9000) so the cap would never artificially bind during ordinary
// throttle use -- but once speed became a direct "set speed" value with
// no equilibrium ceiling of its own (the cruise-control redesign, see
// the class comment above), there was no reason left to size the cap
// around a now-deleted force model's top speed; 600 is simply a speed
// this ship is actually controllable at, chosen directly rather than
// derived from an equilibrium calculation that no longer exists.
const float MIN_FLYABLE_SPEED_SIM_UNITS_PER_SEC = 600.0f;

class FlightPhysics {
public:
    // State
    glm::vec3 position;
    glm::vec3 velocity;
    glm::quat rotation;
    glm::vec3 angularVelocity;  // rad/s in local space

    // The pilot's directly-set forward speed, in sim-units/s -- Shift
    // raises it, Ctrl lowers it (InputHandler::ProcessKeyboard). No cap
    // of its own on the forward side: it can go arbitrarily high the
    // longer Shift is held. Floored at exactly 0 by InputHandler, NOT
    // here -- reverse flight is deliberately disabled (explicitly
    // requested: "make it so i can not reverse"), so Ctrl decelerates
    // down to a stop and holds there rather than continuing past zero.
    // Every frame, UpdateForces sets the ship's velocity component
    // ALONG its own nose direction to exactly this value -- not blended,
    // not fought by drag, immediate. The one thing this deliberately gives
    // up: gravity's REAL, divergent pull near the event horizon (see the
    // class-level comment's point 1) can no longer drag the ship's speed
    // up along a line it's already flying straight down/away from, since
    // that component gets overwritten by cruiseSpeedSimUnitsPerSec every
    // frame right after gravity is integrated into it -- "no amount of
    // thrust can hold station near the horizon" is no longer literally
    // true for a ship flying directly along the radial line, because
    // "thrust" isn't a force here anymore, it's a set speed. What gravity
    // still does, every frame, or real: bend the ship's heading away from
    // a straight line (its contribution PERPENDICULAR to the nose still
    // accumulates normally, unmodified, before the along-nose component is
    // overwritten) -- a close pass still visibly curves the flight path,
    // it just no longer forces your speed up along a nose-aligned dive.
    float cruiseSpeedSimUnitsPerSec;

    float currentGForce;
    float peakGForce;

    // User-adjustable copy of MAX_SPEED_FRACTION_OF_C (see above) -- a
    // setting, not flight state, so Reset() leaves it alone. Exposed so the
    // UI can offer a slider: the "right" cinematic speed trades off
    // differently against playability depending on the loaded black hole's
    // mass (a light-year-scale hole like TON 618 makes even 0.2c feel slow
    // to actually get anywhere; a stellar-mass hole barely notices the cap
    // at all).
    float maxSpeedFractionOfC = MAX_SPEED_FRACTION_OF_C;

    // Control inputs
    float pitchInput;
    float rollInput;
    float yawInput;
    bool afterburner;

    // Set to true for one frame when the jet crosses the event horizon
    // (see Update) -- App.cpp resets the flight on this instead of Physics
    // doing anything scene-specific itself.
    bool capturedByBlackHole;

    explicit FlightPhysics(const glm::vec3& spawnPosition);

    // metersPerSimUnit is the CURRENT black hole's GM/c^2 in meters (see
    // Physics/Constants.h's GravitationalRadiusMeters) -- it's what lets
    // the MAX_SPEED_FRACTION_OF_C cap above mean the same real speed
    // regardless of which black hole mass is loaded.
    void Update(float deltaTime, double metersPerSimUnit);
    void Reset(const glm::vec3& spawnPosition);

    // Getters
    float GetSpeed() const;
    glm::vec3 GetForward() const;
    glm::vec3 GetUp() const;
    glm::vec3 GetRight() const;

private:
    void UpdateForces(float deltaTime, float maxSpeedSimUnitsPerSec);
    void UpdateRotation(float deltaTime);
    void CalculateGForce(const glm::vec3& acceleration);
};

} // namespace Jet
