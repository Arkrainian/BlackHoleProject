#include "TestFramework.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Physics/BlackHole.h"
#include "Physics/GeodesicSolver.h"

using namespace TestFramework;
using namespace Physics;

// Validates the numerically-integrated deflection angle against the
// weak-field approximation alpha ~= 2*r_s/b = 4GM/(bc^2), which is only
// expected to hold for b >> r_s (see e.g. MTW or Weinberg's derivation of
// the classic 1919-eclipse-style light-bending formula). This is the
// project's core "numerical vs analytical deflection" validation.
void RunDeflectionTests() {
    const double rs = BlackHole::SchwarzschildRadiusSim();

    // Impact parameters deep in the weak field: b/r_s = 25, 50, 100, 500.
    const double impactParametersInRs[] = {25.0, 50.0, 100.0, 500.0};

    for (double bOverRs : impactParametersInRs) {
        const double b = bOverRs * rs;

        // The "phi_total - pi" deflection extraction is only exact for a
        // ray that starts and ends at r = infinity. A FIXED starting radius
        // r0 would make the b/r0 truncation error grow with b (worse for
        // exactly the large-b cases we most want to be accurate); scaling
        // r0 with b keeps that truncation negligible (~5e-5 relative) and
        // uniform across every case, isolating the interesting physics
        // (the residual is then the genuine next-order GR correction, not
        // an artifact of finite r0).
        const double r0 = 2.0e4 * b;

        // The angular width (in phi) over which deflection actually
        // accumulates near closest approach shrinks like r_s/b, so a fixed
        // step size that's fine enough for small b badly under-resolves
        // large b. Scale the step to the impact parameter so every case
        // gets comparable angular resolution across the bending region.
        GeodesicConfig config;
        config.escapeRadius = 100.0 * r0;
        config.basePhiStep = std::min(0.002, 0.05 * rs / b);
        config.maxPhiStep = config.basePhiStep;
        config.maxSteps = 2000000;

        const PhotonTraceResult result = TracePhoton(glm::dvec3(-r0, b, 0.0), glm::dvec3(1.0, 0.0, 0.0), config);

        ExpectTrue(result.fate == PhotonFate::ESCAPED, "Weak-field ray (b/r_s = " + std::to_string(bOverRs) + ") escapes");

        const double numericalDeflection = std::abs(result.totalDeflectionAngle);
        const double analyticalDeflection = 2.0 * rs / b; // = 4GM/(bc^2) in sim units (GM=1)
        const double relativeError = std::abs(numericalDeflection - analyticalDeflection) / analyticalDeflection;

        std::printf("  b/r_s=%.0f: numerical=%.8f analytical=%.8f relative_error=%.4f%%\n",
                    bOverRs, numericalDeflection, analyticalDeflection, relativeError * 100.0);

        // alpha ~= 2rs/b is only the LEADING term of the full PPN expansion;
        // the next-order term is (15*pi/4)*(GM/(bc^2))^2, which relative to
        // the leading term is ~ (15*pi/32)*(rs/b) = ~1.47*(rs/b). At
        // b/r_s=25 that alone predicts a ~5.9% excess over the weak-field
        // formula -- i.e. the numerical result SHOULD disagree with the
        // pure first-order formula by a few percent at small b, and that
        // disagreement should shrink roughly like 1/(b/r_s) as b grows.
        // Tolerances below are the predicted next-order excess plus a
        // couple percent of margin for our own residual numerical error.
        const double predictedNextOrderExcess = 1.47 / bOverRs;
        const double tolerance = predictedNextOrderExcess + 0.03;
        ExpectTrue(relativeError < tolerance,
                   "Deflection at b/r_s = " + std::to_string(bOverRs) + " matches weak-field formula within tolerance");
    }
}
