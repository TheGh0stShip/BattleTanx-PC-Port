#include "battletanx/runtime_math.hpp"

#include <array>

namespace battletanx {
namespace {

constexpr double pi=3.14159265358979323846;

// sin(x) for |x| <= pi/2 by its Taylor series to x^15 (error < 1e-9).
double polynomial_sine(double x) noexcept {
    const double x2=x*x;
    double term=x;
    double sum=x;
    for (int n=1; n<=7; ++n) {
        term=-term*x2/static_cast<double>((2*n)*(2*n+1));
        sum+=term;
    }
    return sum;
}

// atan(t) for 0 <= t <= 1 using range halving and a short series.
double polynomial_atan_unit(double t) noexcept {
    // atan(t) = 2*atan(t / (1 + sqrt(1 + t^2))); sqrt is correctly rounded.
    double s=1.0+t*t;
    double root=s;
    for (int i=0; i<40; ++i) root=0.5*(root+s/root);   // Newton, converges to sqrt(s)
    const double u=t/(1.0+root);                       // u <= tan(pi/8)
    const double u2=u*u;
    double term=u;
    double sum=u;
    for (int n=1; n<=12; ++n) {
        term=-term*u2;
        sum+=term/static_cast<double>(2*n+1);
    }
    return 2.0*sum;
}

} // namespace

AngleTable reference_angle_table() {
    std::array<float,AngleTable::value_count> values{};
    for (std::size_t index=0; index<values.size(); ++index) {
        const std::size_t quadrant=(index/64U)%4U;
        const std::size_t step=index%64U;
        const double phase=static_cast<double>(step)*(pi/128.0);
        double value=0.0;
        switch (quadrant) {
        case 0U: value=polynomial_sine(phase); break;
        case 1U: value=polynomial_sine(pi/2.0-phase); break;
        case 2U: value=-polynomial_sine(phase); break;
        default: value=-polynomial_sine(pi/2.0-phase); break;
        }
        values[index]=static_cast<float>(value);
    }
    return AngleTable(values);
}

std::uint16_t direction_angle(float dx,float dz) noexcept {
    const double x=dx;
    const double z=dz;
    const double ax=x<0.0 ? -x : x;
    const double az=z<0.0 ? -z : z;
    if (ax==0.0 && az==0.0) return 0U;
    // Angle from +z toward +x, in radians in [0, pi/2] for the first quadrant.
    double angle=ax<=az ? polynomial_atan_unit(ax/az) : pi/2.0-polynomial_atan_unit(az/ax);
    if (z<0.0) angle=pi-angle;
    if (x<0.0) angle=2.0*pi-angle;
    const double units=angle*(65536.0/(2.0*pi));
    const auto rounded=static_cast<std::uint32_t>(units+0.5);
    return static_cast<std::uint16_t>(rounded&0xFFFFU);
}

} // namespace battletanx
