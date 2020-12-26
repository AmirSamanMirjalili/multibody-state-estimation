/*+-------------------------------------------------------------------------+
  |            Multi Body State Estimation (mbse) C++ library               |
  |                                                                         |
  | Copyright (C) 2014-2020 University of Almeria                           |
  | Copyright (C) 2020 University of Salento                                |
  | See README for list of authors and papers                               |
  | Distributed under 3-clause BSD license                                  |
  |  See: <https://opensource.org/licenses/BSD-3-Clause>                    |
  +-------------------------------------------------------------------------+ */

#include <gtest/gtest.h>

#include <mbse/model-examples.h>
#include <mbse/dynamics/dynamic-simulators.h>
#include <mbse/CAssembledRigidModel.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/nonlinear/factorTesting.h>
#include <mbse/factors/FactorConstraintsVel.h>

using namespace std;
using namespace mbse;

TEST(Jacobians, FactorVelConstraints)
{
	// for use in EXPECT_CORRECT_FACTOR_JACOBIANS
	const auto name_ = "FactorVelConstraints";
#define EXPECT ASSERT_

	using gtsam::symbol_shorthand::A;
	using gtsam::symbol_shorthand::Q;
	using gtsam::symbol_shorthand::V;
	using namespace mbse;

	// Create the multibody object:
	CModelDefinition model;
	mbse::buildFourBarsMBS(model);

	auto aMBS = model.assembleRigidMBS();
	aMBS->setGravityVector(0, -9.81, 0);

	CDynamicSimulator_R_matrix_dense dynSimul(aMBS);
	// Must be called before solve_ddotq():
	dynSimul.prepare();

	const double t = 1.0;
	dynSimul.run(.0, t);
	std::cout << "Evaluating test for t=" << t << "\n";
	std::cout << "q  =" << aMBS->q_.transpose() << "\n";
	std::cout << "dq =" << aMBS->dotq_.transpose() << "\n";
	std::cout << "ddq =" << aMBS->ddotq_.transpose() << "\n";

	// Add factors:
	// Create factor noises:
	// const auto n = aMBS->q_.size();
	const auto m = aMBS->Phi_q_.getNumRows();

	auto noise = gtsam::noiseModel::Isotropic::Sigma(m, 0.1);

	// Create a dummy factor:
	const auto factor = FactorConstraintsVel(aMBS, noise, Q(1), V(1));

	// Convert plain Eigen vectors into state_t classes (used as Values
	// in GTSAM factor graphs):
	const state_t q = state_t(aMBS->q_);
	const state_t dotq = state_t(aMBS->dotq_);
	const state_t ddotq = state_t(aMBS->ddotq_);

	gtsam::Values values;
	values.insert(Q(1), q);
	values.insert(V(1), dotq);
	values.insert(A(1), ddotq);

	EXPECT_CORRECT_FACTOR_JACOBIANS(factor, values, 1e-9, 1e-3);
}