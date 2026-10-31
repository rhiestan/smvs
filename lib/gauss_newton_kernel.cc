/*
 * This software may be modified and distributed under the terms
 * of the BSD 3-Clause license. See the LICENSE.txt file for details.
 */

#include "gauss_newton_kernel.h"

#ifdef SMVS_USE_HIGHWAY

#include <cmath>
#include <vector>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "gauss_newton_kernel.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace smvs {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

/*
 * Every term of the gradient and the Hessian has the form
 *   gradient[c]    += a0[c] * d0 + a1[c] * d1
 *   hessian[c][c2] += a0[c] * b0[c2] + a1[c] * b1[c2]
 * with a (weighted) jacobian column a and the jacobian columns b. Lanes run
 * along c2. The 16 columns are a multiple of the vector size, which is
 * capped at 16. Only multiplications and additions are used (no fused
 * multiply-add), so that all instruction sets produce the same result.
 */
void
AccumulateImpl (std::size_t num_subs, double const* jac,
    double const* jac_grads, double const* grad_main, double r_factor,
    double* gradient, double* hessian)
{
    using D = hn::CappedTag<double, 16>;
    D const d;
    std::size_t const N = hn::Lanes(d);

    /* Structure of arrays: component, subview, column. */
    static thread_local std::vector<double> soa;
    soa.resize(num_subs * 32);
    double* const j0 = soa.data();
    double* const j1 = soa.data() + num_subs * 16;
    for (std::size_t i = 0; i < num_subs * 16; ++i)
    {
        j0[i] = jac[i * 2];
        j1[i] = jac[i * 2 + 1];
    }

    double grad[16] = {};
    double hess[256] = {};

    auto accumulate = [&](double const* a0, double const* a1,
        double const* b0, double const* b1, double d0, double d1)
    {
        for (int c = 0; c < 16; ++c)
            grad[c] += a0[c] * d0 + a1[c] * d1;
        for (int c = 0; c < 16; ++c)
        {
            auto const va0 = hn::Set(d, a0[c]);
            auto const va1 = hn::Set(d, a1[c]);
            for (std::size_t c2 = c & ~(N - 1); c2 < 16; c2 += N)
            {
                auto const sum = hn::Add(
                    hn::Mul(va0, hn::LoadU(d, b0 + c2)),
                    hn::Mul(va1, hn::LoadU(d, b1 + c2)));
                double* const h = hess + c * 16 + c2;
                hn::StoreU(hn::Add(hn::LoadU(d, h), sum), d, h);
            }
        }
    };

    double a0[16], a1[16], b0[16], b1[16];
    for (std::size_t j = 0; j < num_subs; ++j)
    {
        double const* const cj0 = j0 + j * 16;
        double const* const cj1 = j1 + j * 16;
        double const diff0 = jac_grads[j * 2] - grad_main[0];
        double const diff1 = jac_grads[j * 2 + 1] - grad_main[1];
        double const w0 = 1.0 / (r_factor + std::abs(diff0));
        double const w1 = 1.0 / (r_factor + std::abs(diff1));
        for (int c = 0; c < 16; ++c)
        {
            a0[c] = cj0[c] * w0;
            a1[c] = cj1[c] * w1;
        }
        accumulate(a0, a1, cj0, cj1, diff0, diff1);

        for (std::size_t k = j + 1; k < num_subs; ++k)
        {
            double const* const ck0 = j0 + k * 16;
            double const* const ck1 = j1 + k * 16;
            double const sub0 = jac_grads[j * 2] - jac_grads[k * 2];
            double const sub1 = jac_grads[j * 2 + 1] - jac_grads[k * 2 + 1];
            double const sw0 = 1.0 / (r_factor + std::abs(sub0));
            double const sw1 = 1.0 / (r_factor + std::abs(sub1));
            for (int c = 0; c < 16; ++c)
            {
                b0[c] = cj0[c] - ck0[c];
                b1[c] = cj1[c] - ck1[c];
                a0[c] = b0[c] * sw0;
                a1[c] = b1[c] * sw1;
            }
            accumulate(a0, a1, b0, b1, sub0, sub1);
        }
    }

    for (int c = 0; c < 16; ++c)
    {
        gradient[c] += grad[c];
        for (int c2 = c; c2 < 16; ++c2)
            hessian[c * 16 + c2] += hess[c * 16 + c2];
    }
}

void
RegularizeImpl (double const* div, double const* div_deriv,
    double regularizer_weight, double geom_weight, double r_factor,
    double* gradient, double* hessian)
{
    using D = hn::CappedTag<double, 16>;
    D const d;
    std::size_t const N = hn::Lanes(d);

    for (int v = 0; v < 6; ++v)
    {
        double const* const deriv = div_deriv + v * 16;
        double const weight = geom_weight / (r_factor + std::abs(div[v]));
        double const scale = regularizer_weight * weight;
        double a[16];
        for (int c = 0; c < 16; ++c)
        {
            a[c] = deriv[c] * scale;
            gradient[c] += a[c] * div[v];
        }
        for (int c = 0; c < 16; ++c)
        {
            auto const va = hn::Set(d, a[c]);
            for (std::size_t c2 = c & ~(N - 1); c2 < 16; c2 += N)
            {
                double* const h = hessian + c * 16 + c2;
                hn::StoreU(hn::Add(hn::LoadU(d, h),
                    hn::Mul(va, hn::LoadU(d, deriv + c2))), d, h);
            }
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace smvs
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
SMVS_NAMESPACE_BEGIN

HWY_EXPORT(AccumulateImpl);
HWY_EXPORT(RegularizeImpl);

void
gauss_newton_accumulate (std::size_t num_subs, double const* jac,
    double const* jac_grads, double const* grad_main, double r_factor,
    double* gradient, double* hessian)
{
    HWY_DYNAMIC_DISPATCH(AccumulateImpl)(num_subs, jac, jac_grads, grad_main,
        r_factor, gradient, hessian);
}

void
gauss_newton_regularize (double const* div, double const* div_deriv,
    double regularizer_weight, double geom_weight, double r_factor,
    double* gradient, double* hessian)
{
    HWY_DYNAMIC_DISPATCH(RegularizeImpl)(div, div_deriv, regularizer_weight,
        geom_weight, r_factor, gradient, hessian);
}

SMVS_NAMESPACE_END
#endif  // HWY_ONCE
#endif  // SMVS_USE_HIGHWAY
