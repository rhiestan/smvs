/*
 * This software may be modified and distributed under the terms
 * of the BSD 3-Clause license. See the LICENSE.txt file for details.
 */

#ifndef SMVS_GAUSS_NEWTON_KERNEL_HEADER
#define SMVS_GAUSS_NEWTON_KERNEL_HEADER

#include <cstddef>

#include "defines.h"

SMVS_NAMESPACE_BEGIN

/**
 * Accumulates the gradient (16 entries) and the upper triangle of the
 * Hessian (16 x 16 entries) of the photometric error of one pixel.
 *
 * jac:       num_subs * 16 pairs of doubles (jacobian entries per subview)
 * jac_grads: num_subs pairs of doubles (gradient of the subview image)
 * grad_main: pair of doubles (gradient of the main image)
 *
 * The results are added to gradient and hessian. The implementation is
 * selected at runtime from the SIMD instruction sets of the CPU; the result
 * does not depend on the selection.
 */
void
gauss_newton_accumulate (std::size_t num_subs, double const* jac,
    double const* jac_grads, double const* grad_main, double r_factor,
    double* gradient, double* hessian);

/**
 * Adds the surface regularization terms to gradient and Hessian: for each of
 * the 6 values div[v] with derivative div_deriv[v * 16 .. v * 16 + 15],
 *   weight = geom_weight / (r_factor + |div[v]|)
 *   gradient += div_deriv * div * regularizer_weight * weight
 *   hessian  += div_deriv * div_deriv^T * regularizer_weight * weight
 */
void
gauss_newton_regularize (double const* div, double const* div_deriv,
    double regularizer_weight, double geom_weight, double r_factor,
    double* gradient, double* hessian);

SMVS_NAMESPACE_END

#endif /* SMVS_GAUSS_NEWTON_KERNEL_HEADER */
