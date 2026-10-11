#include "TestFramework.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "Physics/BinaryGeodesicSolver.h"
#include "Physics/BinarySystem.h"
#include "Physics/BlackHole.h"

using namespace TestFramework;
using namespace Physics;

namespace {

// Angle between a ray's initial direction and where it ends up heading
// (its escape direction) -- a general, scheme-agnostic way to measure
// deflection that doesn't require the solver to track an accumulated
// angle the way the Schwarzschild/Kerr solvers do (see HANDOFF.md Session
// 8 for two real bugs that angle-bookkeeping approach caused there).
double DeflectionAngle(const glm::dvec3& initialDirection, const PhotonTraceResult& result) {
    const double cosAngle =
        glm::clamp(glm::dot(glm::normalize(initialDirection), glm::normalize(result.escapeDirection)), -1.0, 1.0);
    return std::acos(cosAngle);
}

} // namespace

void RunBinaryTests() {
    // --- Orbital mechanics: barycenter + Kepler's third law ---------------
    {
        BinarySystem system;
        system.massRatio = 0.5;
        system.separation = 40.0;

        ExpectNear(system.Radius1() + system.Radius2(), system.separation, 1e-9,
                   "The two bodies' distances from the barycenter sum to the separation");

        // Barycenter condition: GM1*r1 = GM2*r2 (GM1=1 in these units).
        ExpectNear(system.Radius1(), system.massRatio * system.Radius2(), 1e-9,
                   "Barycenter condition GM1*r1 = GM2*r2 holds");

        // Kepler's third law for the two-body problem: Omega^2 = G(M1+M2)/a^3.
        const double expectedOmegaSq = (1.0 + system.massRatio) / std::pow(system.separation, 3.0);
        ExpectNear(system.AngularVelocity() * system.AngularVelocity(), expectedOmegaSq, 1e-12,
                   "Angular velocity matches Kepler's third law for the combined mass");

        const double expectedPeriod = 2.0 * glm::pi<double>() / system.AngularVelocity();
        ExpectNear(system.OrbitalPeriod(), expectedPeriod, 1e-9, "Orbital period matches 2*pi/Omega");

        // Half an orbit later, each body should be diametrically opposite
        // where it started (same radius, opposite side of the barycenter).
        const glm::dvec3 pos1Start = system.Position1();
        system.AdvancePhase(system.OrbitalPeriod() * 0.5); // AdvancePhase takes a TIME, not a phase increment
        ExpectNear(glm::length(system.Position1() - (-pos1Start)), 0.0, 1e-9,
                   "After half an orbital period, body 1 is diametrically opposite its start");
    }

    // --- Single-mass reduction: massRatio=0 collapses to a single mass at -
    // --- the barycenter, so TracePhotonBinary should reproduce the SAME ---
    // --- weak-field deflection formula already validated for the real -----
    // --- Schwarzschild solver (tests/test_deflection.cpp): alpha=2*rs/b, ---
    // --- rs=2 in these units -- NOT because the two solvers share code ----
    // --- (they don't, this one is a deliberately approximate doubled- -----
    // --- Newtonian scheme), but because that leading-order formula is the-
    // --- entire POINT of the doubling trick (see BinaryGeodesicSolver.h). -
    {
        BinarySystem system;
        system.massRatio = 0.0;
        system.separation = 40.0; // irrelevant when massRatio=0 (body 2 contributes no force either way)

        const double bOverRs[] = {50.0, 100.0, 200.0};
        for (double ratio : bOverRs) {
            const double b = ratio * 2.0; // rs=2 in these units
            const double r0 = 1.0e4 * b;

            // escapeRadius must scale with r0 (the launch distance) --
            // same lesson test_deflection.cpp already encodes (see its own
            // comment): a FIXED escapeRadius smaller than r0 makes the
            // very first step's escape check trigger immediately, before
            // the ray has traveled anywhere near the mass, silently
            // reporting ~0 deflection instead of a real trace.
            BinaryGeodesicConfig config;
            config.escapeRadius = 100.0 * r0;
            config.maxSteps = 2000000;
            config.baseStep = 0.02;

            const PhotonTraceResult result =
                TracePhotonBinary(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), system, config);

            ExpectTrue(result.fate == PhotonFate::ESCAPED,
                       "Single-mass-reduction ray (b/r_s=" + std::to_string(ratio) + ") escapes");
            if (result.fate != PhotonFate::ESCAPED) continue;

            const double numerical = DeflectionAngle(glm::dvec3(1.0, 0.0, 0.0), result);
            const double analytical = 4.0 / b; // 2*rs/b, rs=2
            const double relativeError = std::abs(numerical - analytical) / analytical;

            std::printf("  binary single-mass reduction b/r_s=%.0f: numerical=%.6f analytical=%.6f "
                        "relative_error=%.2f%%\n",
                        ratio, numerical, analytical, relativeError * 100.0);

            // Looser tolerance than test_deflection.cpp's exact-solver
            // check: this is an approximate scheme by design (fixed-speed
            // photon, superposed Newtonian-shaped force, adaptive but not
            // curvature-tuned stepping), so some residual beyond the
            // leading-order formula is expected even deep in the weak
            // field, on top of the SAME next-order PPN excess
            // test_deflection.cpp already accounts for.
            ExpectTrue(relativeError < 0.15,
                       "Binary solver's single-mass deflection is within 15% of the weak-field formula at b/r_s=" +
                           std::to_string(ratio));
        }
    }

    // --- Capture vs. escape classification ---------------------------------
    {
        BinarySystem system;
        system.massRatio = 1.0;
        system.separation = 40.0;
        system.phase = 0.0; // body1 at (+20,0,0), body2 at (-20,0,0)

        BinaryGeodesicConfig config;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 50000;

        const glm::dvec3 body1Pos = system.Position1();
        const glm::dvec3 body2Pos = system.Position2();

        // Aimed directly at body 1 from far away along the same line --
        // should be captured.
        {
            const glm::dvec3 origin = body1Pos + glm::dvec3(0.0, 0.0, 2.0e3);
            const PhotonTraceResult result = TracePhotonBinary(origin, glm::dvec3(0.0, 0.0, -1.0), system, config);
            ExpectTrue(result.fate == PhotonFate::CAPTURED, "A ray aimed directly at body 1 is captured");
        }
        // Aimed directly at body 2.
        {
            const glm::dvec3 origin = body2Pos + glm::dvec3(0.0, 0.0, 2.0e3);
            const PhotonTraceResult result = TracePhotonBinary(origin, glm::dvec3(0.0, 0.0, -1.0), system, config);
            ExpectTrue(result.fate == PhotonFate::CAPTURED, "A ray aimed directly at body 2 is captured");
        }
        // Aimed far from both -- should escape.
        {
            const glm::dvec3 origin(0.0, 0.0, 2.0e3);
            const PhotonTraceResult result =
                TracePhotonBinary(origin, glm::dvec3(0.3, 0.0, -1.0), system, config);
            ExpectTrue(result.fate == PhotonFate::ESCAPED, "A ray passing far from both bodies escapes");
        }
    }

    // --- Mirror symmetry sanity check ---------------------------------------
    // For equal masses placed symmetrically on the x-axis (phase=0), the
    // configuration is mirror-symmetric under y -> -y. A ray approaching
    // face-on (along -z) with its impact-parameter offset in +y vs. -y
    // should therefore deflect by the SAME magnitude -- this checks that
    // the superposition in Accel() actually respects the system's own
    // geometric symmetry, independent of any absolute reference formula.
    {
        BinarySystem system;
        system.massRatio = 1.0;
        system.separation = 40.0;
        system.phase = 0.0;

        BinaryGeodesicConfig config;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 50000;

        const double offset = 8.0;
        const glm::dvec3 dir(0.0, 0.0, -1.0);
        const PhotonTraceResult plusY =
            TracePhotonBinary(glm::dvec3(0.0, offset, 2.0e3), dir, system, config);
        const PhotonTraceResult minusY =
            TracePhotonBinary(glm::dvec3(0.0, -offset, 2.0e3), dir, system, config);

        ExpectTrue(plusY.fate == PhotonFate::ESCAPED && minusY.fate == PhotonFate::ESCAPED,
                   "Both mirror-symmetric rays escape");
        if (plusY.fate == PhotonFate::ESCAPED && minusY.fate == PhotonFate::ESCAPED) {
            const double deflectionPlus = DeflectionAngle(dir, plusY);
            const double deflectionMinus = DeflectionAngle(dir, minusY);
            std::printf("  mirror symmetry: deflection(+y)=%.6f deflection(-y)=%.6f\n", deflectionPlus,
                        deflectionMinus);
            ExpectNear(deflectionPlus, deflectionMinus, 1e-6,
                       "Mirror-symmetric rays deflect by the same magnitude");
        }
    }

    // --- Accretion disks: each body's own disk, correctly attributed -------
    {
        BinarySystem system;
        system.massRatio = 1.0;
        system.separation = 40.0;
        system.phase = 0.0; // body1 at (+20,0,0), body2 at (-20,0,0)

        BinaryGeodesicConfig config;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 50000;
        config.checkDisk = true;
        config.disk1InnerRadius = 6.0;
        config.disk1OuterRadius = 26.0;
        config.disk2InnerRadius = 6.0;
        config.disk2OuterRadius = 26.0;

        const glm::dvec3 body1Pos = system.Position1();
        const glm::dvec3 body2Pos = system.Position2();

        // Straight down (-z) from above, offset 10 units from each body
        // in its own disk plane -- within [6, 26] either way.
        const PhotonTraceResult hit1 =
            TracePhotonBinary(body1Pos + glm::dvec3(10.0, 0.0, 50.0), glm::dvec3(0.0, 0.0, -1.0), system, config);
        const PhotonTraceResult hit2 =
            TracePhotonBinary(body2Pos + glm::dvec3(10.0, 0.0, 50.0), glm::dvec3(0.0, 0.0, -1.0), system, config);

        ExpectTrue(hit1.hitDisk, "A ray above body 1's disk hits a disk");
        ExpectTrue(hit2.hitDisk, "A ray above body 2's disk hits a disk");
        if (hit1.hitDisk) {
            ExpectTrue(hit1.diskBodyIndex == 0, "The ray above body 1 is attributed to body 1's disk");
            ExpectTrue(hit1.diskHitRadius >= config.disk1InnerRadius && hit1.diskHitRadius <= config.disk1OuterRadius,
                       "Body 1's disk hit radius falls within [innerRadius, outerRadius]");
            ExpectTrue(std::isfinite(hit1.diskRedshiftFactor) && hit1.diskRedshiftFactor > 0.0,
                       "Body 1's disk redshift factor is finite and positive");
        }
        if (hit2.hitDisk) {
            ExpectTrue(hit2.diskBodyIndex == 1, "The ray above body 2 is attributed to body 2's disk");
            ExpectTrue(hit2.diskHitRadius >= config.disk2InnerRadius && hit2.diskHitRadius <= config.disk2OuterRadius,
                       "Body 2's disk hit radius falls within [innerRadius, outerRadius]");
            ExpectTrue(std::isfinite(hit2.diskRedshiftFactor) && hit2.diskRedshiftFactor > 0.0,
                       "Body 2's disk redshift factor is finite and positive");
        }

        // A ray passing well outside either disk's radial extent (far to
        // the side) should cross z=0 without registering a disk hit.
        const PhotonTraceResult miss =
            TracePhotonBinary(glm::dvec3(0.0, 100.0, 50.0), glm::dvec3(0.0, 0.0, -1.0), system, config);
        ExpectTrue(!miss.hitDisk, "A ray missing both disks' radial extent does not register a disk hit");
    }
}
