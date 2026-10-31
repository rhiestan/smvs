/*
 * This software may be modified and distributed under the terms
 * of the BSD 3-Clause license. See the LICENSE.txt file for details.
 */

#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "gauss_newton_kernel.h"

#ifdef SMVS_USE_HIGHWAY

namespace
{
    /* Straightforward implementation of the weighted L1 error terms. */
    void
    reference (std::size_t num_subs, double const* jac, double const* grads,
        double const* grad_main, double r, double* gradient, double* hessian)
    {
        for (std::size_t j = 0; j < num_subs; ++j)
        {
            double diff[2], weight[2];
            for (int k = 0; k < 2; ++k)
            {
                diff[k] = grads[j * 2 + k] - grad_main[k];
                weight[k] = 1.0 / (r + std::abs(diff[k]));
            }
            for (int col = 0; col < 16; ++col)
            {
                double const* a = jac + (j * 16 + col) * 2;
                gradient[col] += diff[0] * weight[0] * a[0]
                    + diff[1] * weight[1] * a[1];
                for (int col2 = col; col2 < 16; ++col2)
                {
                    double const* b = jac + (j * 16 + col2) * 2;
                    hessian[col * 16 + col2] += a[0] * weight[0] * b[0]
                        + a[1] * weight[1] * b[1];
                }
            }
            for (std::size_t j2 = j + 1; j2 < num_subs; ++j2)
            {
                double sub[2], sw[2];
                for (int k = 0; k < 2; ++k)
                {
                    sub[k] = grads[j * 2 + k] - grads[j2 * 2 + k];
                    sw[k] = 1.0 / (r + std::abs(sub[k]));
                }
                for (int col = 0; col < 16; ++col)
                {
                    double jace[2];
                    for (int k = 0; k < 2; ++k)
                        jace[k] = (jac[(j * 16 + col) * 2 + k]
                            - jac[(j2 * 16 + col) * 2 + k]) * sw[k];
                    gradient[col] += jace[0] * sub[0] + jace[1] * sub[1];
                    for (int col2 = col; col2 < 16; ++col2)
                        hessian[col * 16 + col2] +=
                            jace[0] * (jac[(j * 16 + col2) * 2]
                                - jac[(j2 * 16 + col2) * 2])
                            + jace[1] * (jac[(j * 16 + col2) * 2 + 1]
                                - jac[(j2 * 16 + col2) * 2 + 1]);
                }
            }
        }
    }
}

TEST(GaussNewtonKernel, matches_reference)
{
    std::mt19937 rng(42);
    std::normal_distribution<double> dist(0.0, 1.0);
    for (std::size_t num_subs : {1, 2, 3, 6})
    {
        std::vector<double> jac(num_subs * 32), grads(num_subs * 2);
        double main[2] = {dist(rng), dist(rng)};
        for (double& v : jac) v = dist(rng);
        for (double& v : grads) v = dist(rng);

        std::vector<double> g1(16, 0.5), g2(16, 0.5);
        std::vector<double> h1(256, 0.25), h2(256, 0.25);
        reference(num_subs, jac.data(), grads.data(), main, 1e-4,
            g1.data(), h1.data());
        smvs::gauss_newton_accumulate(num_subs, jac.data(), grads.data(),
            main, 1e-4, g2.data(), h2.data());

        for (int c = 0; c < 16; ++c)
        {
            EXPECT_NEAR(g1[c], g2[c], 1e-9 * (1.0 + std::abs(g1[c])));
            for (int c2 = 0; c2 < 16; ++c2)
                EXPECT_NEAR(h1[c * 16 + c2], h2[c * 16 + c2],
                    1e-9 * (1.0 + std::abs(h1[c * 16 + c2])))
                    << "subs " << num_subs << " col " << c << "," << c2;
        }
    }
}

TEST(GaussNewtonKernel, regularize_matches_reference)
{
    std::mt19937 rng(7);
    std::normal_distribution<double> dist(0.0, 1.0);
    double div[6], deriv[96];
    for (double& v : div) v = dist(rng);
    for (double& v : deriv) v = dist(rng);
    double const bw = 0.37, gw = 0.8, r = 1e-4;

    std::vector<double> g1(16, 0.5), g2(16, 0.5);
    std::vector<double> h1(256, 0.25), h2(256, 0.25);
    for (int v = 0; v < 6; ++v)
    {
        double const weight = gw / (r + std::abs(div[v]));
        for (int c = 0; c < 16; ++c)
        {
            g1[c] += deriv[16 * v + c] * div[v] * bw * weight;
            for (int c2 = c; c2 < 16; ++c2)
                h1[c * 16 + c2] += deriv[v * 16 + c] * deriv[v * 16 + c2]
                    * bw * weight;
        }
    }
    smvs::gauss_newton_regularize(div, deriv, bw, gw, r, g2.data(), h2.data());

    for (int c = 0; c < 16; ++c)
    {
        EXPECT_NEAR(g1[c], g2[c], 1e-9 * (1.0 + std::abs(g1[c])));
        for (int c2 = c; c2 < 16; ++c2)
            EXPECT_NEAR(h1[c * 16 + c2], h2[c * 16 + c2],
                1e-9 * (1.0 + std::abs(h1[c * 16 + c2])));
    }
}

#endif
