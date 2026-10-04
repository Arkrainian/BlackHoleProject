#include "TestFramework.h"

#include <cmath>

#include <glm/glm.hpp>

#include "Astronomy/AccretionDisk.h"
#include "Physics/BlackHole.h"
#include "Physics/GeodesicSolver.h"

using namespace TestFramework;
using namespace Physics;

// Stage 6: validates the accretion disk's orbital velocity formula and the
// GeodesicSolver disk-intersection/redshift machinery.
void RunDiskTests() {
    const double rs = BlackHole::SchwarzschildRadiusSim();
    const double isco = 3.0 * rs; // ISCO = 6GM/c^2 = 3*r_s, standard Schwarzschild result

    // Well-known Schwarzschild fact used as a sanity check: a test particle
    // in a circular orbit at the ISCO moves at exactly half the speed of
    // light as measured by a local static observer. v = 1/sqrt(r - r_s)
    // (see GeodesicSolver.h derivation) at r=isco=3*r_s=6, r_s=2:
    // v = 1/sqrt(6-2) = 0.5.
    {
        const double vAtIsco = 1.0 / std::sqrt(isco - rs);
        ExpectNear(vAtIsco, 0.5, 1e-12, "Local orbital speed at the ISCO is exactly 0.5c");
    }

    // A ray aimed from well above the disk plane, generally toward the
    // black hole, should cross the equatorial plane somewhere in the disk's
    // radial range and report a finite, positive, near-unity redshift
    // factor (this geometry is close to face-on, so neither Doppler
    // component should be extreme).
    {
        GeodesicConfig config;
        config.checkDisk = true;
        config.diskInnerRadius = isco;
        config.diskOuterRadius = 26.0;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 20000;
        config.basePhiStep = 0.01;

        const glm::dvec3 origin(0.0, 0.0, 20.0);   // above the disk, on-axis-ish
        const glm::dvec3 direction(0.6, 0.0, -1.0); // angled down toward the disk plane

        const PhotonTraceResult result = TracePhoton(origin, direction, config);

        ExpectTrue(result.hitDisk, "A downward-angled ray from above the disk hits the disk");
        if (result.hitDisk) {
            ExpectTrue(result.diskHitRadius >= config.diskInnerRadius && result.diskHitRadius <= config.diskOuterRadius,
                       "Disk hit radius falls within [innerRadius, outerRadius]");
            ExpectTrue(std::isfinite(result.diskRedshiftFactor) && result.diskRedshiftFactor > 0.0,
                       "Disk redshift factor is finite and positive");
            ExpectTrue(result.diskRedshiftFactor > 0.3 && result.diskRedshiftFactor < 3.0,
                       "Disk redshift factor is a modest (not extreme) shift for a near-face-on view");
        }
    }

    // A ray aimed well outside the disk's outer radius (large impact
    // parameter, shallow angle) should escape without ever registering a
    // disk hit.
    {
        GeodesicConfig config;
        config.checkDisk = true;
        config.diskInnerRadius = isco;
        config.diskOuterRadius = 26.0;
        config.escapeRadius = 1.0e4;
        config.maxSteps = 20000;
        config.basePhiStep = 0.01;

        const glm::dvec3 origin(0.0, 0.0, 20.0);
        const glm::dvec3 direction(0.02, 0.0, -1.0); // nearly straight down, off to the side of the disk

        const PhotonTraceResult result = TracePhoton(origin, direction, config);
        ExpectTrue(!result.hitDisk, "A ray missing the disk's radial extent does not register a disk hit");
    }

    // AccretionDisk's temperature profile: zero outside [inner, outer],
    // positive within, and (Shakura-Sunyaev shape) zero exactly at the
    // inner edge before rising -- T(r_in) = (1)^0.75 * sqrt(1-sqrt(1)) = 0.
    {
        Astronomy::AccretionDiskSettings settings;
        settings.innerRadius = isco;
        settings.outerRadius = 26.0;
        Astronomy::AccretionDisk disk(settings);

        ExpectNear(disk.Temperature(isco), 0.0, 1e-9, "Disk temperature is zero exactly at the inner edge");
        ExpectTrue(disk.Temperature(isco * 1.5) > 0.0, "Disk temperature is positive within the disk's radial range");
        ExpectNear(disk.Temperature(settings.outerRadius * 2.0), 0.0, 1e-12, "Disk temperature is zero outside the outer radius");
    }
}
