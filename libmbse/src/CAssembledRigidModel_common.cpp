/*+-------------------------------------------------------------------------+
  |            Multi Body State Estimation (mbse) C++ library               |
  |                                                                         |
  | Copyright (C) 2014-2020 University of Almeria                           |
  | Copyright (C) 2020 University of Salento                                |
  | See README for list of authors and papers                               |
  | Distributed under 3-clause BSD license                                  |
  |  See: <https://opensource.org/licenses/BSD-3-Clause>                    |
  +-------------------------------------------------------------------------+ */

#include <mbse/CAssembledRigidModel.h>
#include <mbse/constraints/CConstraintRelativeAngle.h>
#include <mbse/constraints/CConstraintRelativeAngleAbsolute.h>
#include <mrpt/opengl.h>
#include <iostream>

using namespace mbse;
using namespace Eigen;
using namespace mrpt::math;
using namespace mrpt;
using namespace std;

const double DEFAULT_GRAVITY[3] = {0, -9.81, 0};

/** Constructor */
CAssembledRigidModel::CAssembledRigidModel(const TSymbolicAssembledModel& armi)
	: parent_(armi.model)
{
	for (int i = 0; i < 3; i++) gravity_[i] = DEFAULT_GRAVITY[i];

	const auto nEuclideanDOFs = armi.DOFs.size();
	const auto nRelativeDOFs = armi.rDOFs.size();

	const auto nDOFs = nEuclideanDOFs + nRelativeDOFs;

	ASSERTMSG_(
		nEuclideanDOFs > 0,
		"Trying to assemble model with 0 Natural Coordinate DOFs");

	q_.setZero(nDOFs);
	dotq_.setZero(nDOFs);
	ddotq_.setZero(nDOFs);
	Q_.setZero(nDOFs);

	// Keep a copy of DOF info:
	DOFs_ = armi.DOFs;
	rDOFs_ = armi.rDOFs;

	// Build reverse-lookup table & initialize initial values for "q":
	points2DOFs_.resize(armi.model.getPointCount());

	for (dof_index_t i = 0; i < nEuclideanDOFs; i++)
	{
		const size_t pt_idx = DOFs_.at(i).point_index;
		const Point2& pt = parent_.getPointInfo(pt_idx);
		switch (DOFs_.at(i).point_dof)
		{
			case PointDOF::X:
				q_[i] = pt.coords.x;
				points2DOFs_.at(pt_idx).dof_x = i;
				break;
			case PointDOF::Y:
				q_[i] = pt.coords.y;
				points2DOFs_.at(pt_idx).dof_y = i;
				break;
			// case 2: //z
			//	q_[i] = pt.coords.z;
			//	break;
			default:
				THROW_EXCEPTION("Unexpected value for DOFs_[i].point_dof");
		};
	}

	// Save the number of DOFs as the number of columsn in sparse Jacobians:
	defineSparseMatricesColumnCount(nDOFs);

	// Generate constraint equations & create structure of sparse Jacobians:
	// ----------------------------------------------------------------------
	const std::vector<CConstraintBase::Ptr>& parent_constraints =
		parent_.getConstraints();

	// 1/2: Constraints
	const size_t nConst = parent_constraints.size();
	constraints_.resize(nConst);
	for (size_t i = 0; i < nConst; i++)
	{
		constraints_[i] = parent_constraints[i]->clone();
	}

	// 2/2: Constraints from relative coordinates:
	relCoordinate2Index_.resize(rDOFs_.size());
	for (size_t i = 0; i < rDOFs_.size(); i++)
	{
		const auto& relConstr = rDOFs_[i];
		const dof_index_t idxInQ = nEuclideanDOFs + i;

		if (std::holds_alternative<RelativeAngleDOF>(relConstr))
		{
			const auto& c = std::get<RelativeAngleDOF>(relConstr);

			// Add constraint:
			auto co = std::make_shared<CConstraintRelativeAngle>(
				c.point_idx0, c.point_idx1, c.point_idx2, idxInQ);
			constraints_.push_back(co);

			// reverse look up table:
			relCoordinate2Index_[i] = idxInQ;
		}
		else if (std::holds_alternative<RelativeAngleAbsoluteDOF>(relConstr))
		{
			const auto& c = std::get<RelativeAngleAbsoluteDOF>(relConstr);

			// Add constraint:
			auto co = std::make_shared<CConstraintRelativeAngleAbsolute>(
				c.point_idx0, c.point_idx1, idxInQ);
			constraints_.push_back(co);

			// reverse look up table:
			relCoordinate2Index_[i] = idxInQ;
		}
		else
		{
			THROW_EXCEPTION("Unknown type of relative coordinate");
		}
	}

	// Final step: build structures
	for (auto& c : constraints_) c->buildSparseStructures(*this);
}

void CAssembledRigidModel::getGravityVector(
	double& gx, double& gy, double& gz) const
{
	gx = gravity_[0];
	gy = gravity_[1];
	gz = gravity_[2];
}

void CAssembledRigidModel::setGravityVector(
	const double gx, const double gy, const double gz)
{
	gravity_[0] = gx;
	gravity_[1] = gy;
	gravity_[2] = gz;
}

/** Call all constraint objects and command them to update their corresponding
 * parts in the sparse Jacobians */
void CAssembledRigidModel::update_numeric_Phi_and_Jacobians()
{
	// Update numeric values of the constraint Jacobians:
	for (size_t i = 0; i < constraints_.size(); i++)
		constraints_[i]->update(*this);
}

/** Returns a 3D visualization of the model */
void CAssembledRigidModel::getAs3DRepresentation(
	mrpt::opengl::CSetOfObjects::Ptr& outObj,
	const CBody::TRenderParams& rp) const
{
	if (!outObj)
		outObj = mrpt::opengl::CSetOfObjects::Create();
	else
		outObj->clear();

	// Render constraints:
	const std::vector<CConstraintBase::Ptr>& parent_constr =
		parent_.getConstraints();

	const size_t nConstr = parent_constr.size();
	for (size_t i = 0; i < nConstr; i++)
	{
		const CConstraintBase* constr_ptr = parent_constr[i].get();
		mrpt::opengl::CRenderizable::Ptr gl_obj;
		if (constr_ptr->get3DRepresentation(gl_obj))
		{
			// Insert in 3D scene:
			outObj->insert(gl_obj);
		}
	}

	// Render "ground" points:
	if (rp.show_grounds)
	{
		const size_t nPts = parent_.getPointCount();
		for (size_t i = 0; i < nPts; i++)
		{
			const Point2& pt = parent_.getPointInfo(i);
			if (!pt.fixed) continue;
			// This is a fixed point:
			outObj->insert(this->internal_render_ground_point(pt, rp));
		}
	}

	// Render bodies:
	const std::vector<CBody>& parent_bodies = parent_.getBodies();
	const size_t nBodies = parent_bodies.size();
	gl_objects_.resize(nBodies);

	for (size_t i = 0; i < nBodies; i++)
	{
		const CBody& b = parent_bodies[i];

		mrpt::opengl::CRenderizable::Ptr gl_obj = b.get3DRepresentation();

		// Insert in 3D scene:
		outObj->insert(gl_obj);

		// And save reference for quickly update the 3D pose in the future
		// during animations:
		gl_objects_[i] = gl_obj;
	}

	// Place each body in its current pose:
	this->update3DRepresentation(rp);
}

/** Animates a 3D representation of the MBS, previously built in
 * getAs3DRepresentation() \sa getAs3DRepresentation
 */
void CAssembledRigidModel::update3DRepresentation(
	const CBody::TRenderParams& rp) const
{
	const std::vector<CBody>& parent_bodies = parent_.getBodies();

	const size_t nBodies = parent_bodies.size();
	if (gl_objects_.size() != nBodies)
	{
		std::cerr << "[CAssembledRigidModel::update3DRepresentation] Warning: "
					 "Opengl model is not initialized.\n";
		return;
	}

	for (size_t i = 0; i < nBodies; i++)
	{
		mrpt::opengl::CRenderizable::Ptr& obj = gl_objects_[i];
		ASSERT_(obj);

		// Recover the 2D pose from 2 points:
		const CBody& b = parent_bodies[i];
		const size_t i0 = b.points[0];
		const size_t i1 = b.points[1];

		const Point2& pnt0 = parent_.getPointInfo(i0);
		const Point2& pnt1 = parent_.getPointInfo(i1);

		const Point2ToDOF dof0 = points2DOFs_[i0];
		const Point2ToDOF dof1 = points2DOFs_[i1];

		const double& p0x =
			(dof0.dof_x != INVALID_DOF) ? q_[dof0.dof_x] : pnt0.coords.x;
		const double& p0y =
			(dof0.dof_y != INVALID_DOF) ? q_[dof0.dof_y] : pnt0.coords.y;

		const double& p1x =
			(dof1.dof_x != INVALID_DOF) ? q_[dof1.dof_x] : pnt1.coords.x;
		const double& p1y =
			(dof1.dof_y != INVALID_DOF) ? q_[dof1.dof_y] : pnt1.coords.y;

		const double theta = atan2(p1y - p0y, p1x - p0x);

		obj->setPose(
			mrpt::poses::CPose3D(p0x, p0y, 0, theta, DEG2RAD(0), DEG2RAD(0)));

		// Update transparency:
		if (rp.render_style == CBody::reLine)
		{
			auto gl_line =
				mrpt::ptr_cast<mrpt::opengl::CSetOfObjects>::from(obj)
					->getByClass<mrpt::opengl::CSimpleLine>();
			if (gl_line) gl_line->setColorA_u8(rp.line_alpha);
		}
	}
}

size_t CAssembledRigidModel::addNewRowToConstraints()
{
	const size_t idx = Phi_.size();
	const size_t m = idx + 1;  // new size

	// Add rows:
	Phi_.resize(m);
	dotPhi_.resize(m);

	// Jacobians and related matrices:
	Phi_q_.setRowCount(m);
	dotPhi_q_.setRowCount(m);
	dPhiqdq_dq_.setRowCount(m);
	Phiqq_times_dq_.setRowCount(m);
	d_dotPhiq_ddq_times_dq_.setRowCount(m);  // ??

	return idx;
}

/** Only to be called between objects created from the same symbolic model, this
 * method replicates the state of "o" into "this". */
void CAssembledRigidModel::copyStateFrom(const CAssembledRigidModel& o)
{
#ifdef _DEBUG
	// Security checks, just in case...
	ASSERT_(this->q_.size() == o.q_.size());
	ASSERT_(this->dotq_.size() == o.dotq_.size());
	ASSERT_(this->DOFs_.size() == o.DOFs_.size());
	ASSERT_(this->Phi_.size() == o.Phi_.size());
	const double* ptr_q0 = &q_[0];
	const double* ptr_dotq0 = &dotq_[0];
#endif

	this->q_ = o.q_;
	this->dotq_ = o.dotq_;

#ifdef _DEBUG
	ASSERT_(
		ptr_q0 == &q_[0]);  // make sure the vectors didn't suffer mem
							// reallocation, since we save pointers to these!
	ASSERT_(
		ptr_dotq0 ==
		&dotq_[0]);  // make sure the vectors didn't suffer mem reallocation,
					 // since we save pointers to these!
#endif
}

/** Copies the opengl object from another instance */
void CAssembledRigidModel::copyOpenGLRepresentationFrom(
	const CAssembledRigidModel& o)
{
	this->gl_objects_ = o.gl_objects_;
}

/** Retrieves the current coordinates of a point, which may include either fixed
 * or variable components */
void CAssembledRigidModel::getPointCurrentCoords(
	const size_t pt_idx, mrpt::math::TPoint2D& pt) const
{
	const Point2& pt_info = parent_.getPointInfo(pt_idx);
	const Point2ToDOF& pt_dofs = points2DOFs_[pt_idx];

	pt.x =
		(pt_dofs.dof_x != INVALID_DOF) ? q_[pt_dofs.dof_x] : pt_info.coords.x;
	pt.y =
		(pt_dofs.dof_y != INVALID_DOF) ? q_[pt_dofs.dof_y] : pt_info.coords.y;
}

/** Retrieves the current velocity of a point, which may include either fixed or
 * variable components */
void CAssembledRigidModel::getPointCurrentVelocity(
	const size_t pt_idx, mrpt::math::TPoint2D& vel) const
{
	const Point2ToDOF& pt_dofs = points2DOFs_[pt_idx];

	vel.x = (pt_dofs.dof_x != INVALID_DOF) ? dotq_[pt_dofs.dof_x] : 0;
	vel.y = (pt_dofs.dof_y != INVALID_DOF) ? dotq_[pt_dofs.dof_y] : 0;
}

/** Computes the current coordinates of a point fixed to a given body, given its
 * relative coordinates wrt to system X:pt0->pt1, Y: orthogonal */
void CAssembledRigidModel::getPointOnBodyCurrentCoords(
	const size_t body_index, const mrpt::math::TPoint2D& relative_pt,
	mrpt::math::TPoint2D& out_pt) const
{
	ASSERTDEB_(body_index < parent_.getBodies().size());

	const CBody& b = parent_.getBodies()[body_index];

	mrpt::math::TPoint2D q[2];
	this->getPointCurrentCoords(b.points[0], q[0]);
	this->getPointCurrentCoords(b.points[1], q[1]);

	const double L = b.length();
	ASSERTDEB_(L > 0);
	const double Linv = 1.0 / L;

	mrpt::math::TPoint2D u, v;  // unit vectors in X,Y,Z local to the body

	u = (q[1] - q[0]) * Linv;
	v.x = -u.y;
	v.y = u.x;

	out_pt.x = q[0].x + u.x * relative_pt.x + v.x * relative_pt.y;
	out_pt.y = q[0].y + u.y * relative_pt.x + v.y * relative_pt.y;
}

/* Render a ground point */
mrpt::opengl::CSetOfObjects::Ptr
	CAssembledRigidModel::internal_render_ground_point(
		const Point2& pt, const CBody::TRenderParams& rp) const
{
	mrpt::opengl::CSetOfObjects::Ptr obj =
		mrpt::opengl::CSetOfObjects::Create();
	obj->setLocation(pt.coords.x, pt.coords.y, 0);

	const double support_LXZ = 0.03;
	const double support_LY = 0.05;

	auto gl_box = mrpt::opengl::CBox::Create(
		mrpt::math::TPoint3D(
			-0.5 * support_LXZ, -support_LY, -0.5 * support_LXZ),
		mrpt::math::TPoint3D(0.5 * support_LXZ, 0, 0.5 * support_LXZ), false);
	gl_box->setColor(0, 0, 0.7);
	obj->insert(gl_box);

	return obj;
}

/** Evaluate current energy of the system. */
void CAssembledRigidModel::evaluateEnergy(
	CAssembledRigidModel::TEnergyValues& e) const
{
	timelog().enter("evaluateEnergy");

	e = TEnergyValues();  // Reset to zero

	const std::vector<CBody>& bodies = parent_.getBodies();
	for (size_t i = 0; i < bodies.size(); i++)
	{
		const CBody& b = bodies[i];

		mrpt::math::TPoint2D dq[2];
		this->getPointCurrentVelocity(b.points[0], dq[0]);
		this->getPointCurrentVelocity(b.points[1], dq[1]);

		const Matrix<double, 2, 1> dq0 = Matrix<double, 2, 1>(dq[0].x, dq[0].y);
		const Matrix<double, 2, 1> dq1 = Matrix<double, 2, 1>(dq[1].x, dq[1].y);

		const Matrix2d& M00 = b.getM00();
		const Matrix2d& M01 = b.getM01();
		const Matrix2d& M11 = b.getM11();

		// Kinetic energy: 0.5 * [q0 q1] * [M00 M10;M10' M11] * [q0 q1]'
		e.E_kin +=
			0.5 * (dq0.transpose() * M00 * dq0 + dq1.transpose() * M11 * dq1)
					  .coeff(0, 0) +
			(dq0.transpose() * M01 * dq1).coeff(0, 0);

		// Potential energy:
		mrpt::math::TPoint2D
			global_cog;  // current COG position, in global coords:
		this->getPointOnBodyCurrentCoords(i, b.cog(), global_cog);

		e.E_pot -= b.mass() * (this->gravity_[0] * global_cog.x +
							   this->gravity_[1] * global_cog.y +
							   this->gravity_[2] * 0 /*global_cog.z*/);
	}

	e.E_total = e.E_kin + e.E_pot;

	timelog().leave("evaluateEnergy");
}

static char dof2letter(const PointDOF p)
{
	switch (p)
	{
		case PointDOF::X:
			return 'x';
		case PointDOF::Y:
			return 'y';
		case PointDOF::Z:
			return 'z';
		default:
			return '?';
	};
}

void CAssembledRigidModel::printCoordinates(std::ostream& o)
{
	MRPT_START

	ASSERT_EQUAL_(static_cast<size_t>(q_.size()), DOFs_.size() + rDOFs_.size());

	o << "[CAssembledRigidModel] |q|=" << q_.size() << ", " << DOFs_.size()
	  << " natural, " << rDOFs_.size() << " relative coordinates.\n";

	o << "Natural coordinates:\n";
	for (size_t i = 0; i < DOFs_.size(); i++)
	{
		o << " q[" << i << "]: " << dof2letter(DOFs_[i].point_dof)
		  << DOFs_[i].point_index << "\n";
	}
	if (!rDOFs_.empty())
	{
		o << "Relative coordinates:\n";
		for (size_t i = 0; i < rDOFs_.size(); i++)
		{
			o << " q[" << (i + DOFs_.size()) << "]: ";
			const auto& relConstr = rDOFs_[i];
			if (std::holds_alternative<RelativeAngleAbsoluteDOF>(relConstr))
			{
				const auto& c = std::get<RelativeAngleAbsoluteDOF>(relConstr);
				o << "relativeAngleWrtGround(" << c.point_idx0 << " - "
				  << c.point_idx1 << ")";
			}
			else
			{
				o << "???";
			}
			o << "\n";
		}
	}

	MRPT_END
}
