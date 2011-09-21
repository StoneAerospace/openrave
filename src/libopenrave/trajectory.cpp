// Copyright (C) 2006-2008 Carnegie Mellon University (rdiankov@cs.cmu.edu)
//
// This file is part of OpenRAVE.
// OpenRAVE is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
/** \file   Trajectory.cpp
    \brief  Definition of OpenRAVE::TrajectoryBase
 */
#include "libopenrave.h"

namespace OpenRAVE {

//  convenience inline functions for accessing linear blend params
inline dReal& SEG_OFFSET(TrajectoryBase::TSEGMENT& seg, int d)       {
    return seg.Get(0, d);
}
inline dReal  SEG_OFFSET(const TrajectoryBase::TSEGMENT& seg, int d) {
    return seg.Get(0, d);
}

inline dReal& SEG_SLOPE(TrajectoryBase::TSEGMENT& seg, int d)        {
    return seg.Get(1, d);
}
inline dReal  SEG_SLOPE(const TrajectoryBase::TSEGMENT& seg, int d)  {
    return seg.Get(1, d);
}

inline dReal& START_BLEND_TIME(TrajectoryBase::TSEGMENT& seg, int d) {
    return seg.Get(2, d);
}
inline dReal  START_BLEND_TIME(const TrajectoryBase::TSEGMENT& seg, int d)
{
    return seg.Get(2, d);
}

inline dReal& END_BLEND_TIME(TrajectoryBase::TSEGMENT& seg, int d)   {
    return seg.Get(3, d);
}
inline dReal  END_BLEND_TIME(const TrajectoryBase::TSEGMENT& seg, int d)
{
    return seg.Get(3, d);
}

inline dReal& START_A(TrajectoryBase::TSEGMENT& seg, int d)          {
    return seg.Get(4, d);
}
inline dReal  START_A(const TrajectoryBase::TSEGMENT& seg, int d)    {
    return seg.Get(4, d);
}

inline dReal& END_A(TrajectoryBase::TSEGMENT& seg, int d)            {
    return seg.Get(5, d);
}
inline dReal  END_A(const TrajectoryBase::TSEGMENT& seg, int d)      {
    return seg.Get(5, d);
}

inline int CountZeroBits(uint32_t v)
{
    unsigned int c;     // c will be the number of zero bits on the right,
    // so if v is 1101000 (base 2), then c will be 3
    // NOTE: if 0 == v, then c = 31.
    if (v & 0x1)  {
        // special case for odd v (assumed to happen half of the time)
        c = 0;
    }
    else {
        c = 1;
        if ((v & 0xffff) == 0)  {
            v >>= 16;
            c += 16;
        }
        if ((v & 0xff) == 0)  {
            v >>= 8;
            c += 8;
        }
        if ((v & 0xf) == 0)  {
            v >>= 4;
            c += 4;
        }
        if ((v & 0x3) == 0)  {
            v >>= 2;
            c += 2;
        }
        c -= v & 0x1;
    }
    return c;
}

TrajectoryBase::TrajectoryBase(EnvironmentBasePtr penv, int nDOF) : InterfaceBase(PT_Trajectory, penv)
{
    _nDOF = nDOF;
    _interpMethod = LINEAR;
    _maxAffineTranslationVel = Vector(1,1,1);
    _maxAffineRotationQuatVel = 1;
    __description = ":Interface Author: Rosen Diankov, James Kuffner\n\n";
}

void TrajectoryBase::SetAffineVelocity(const Vector& maxAffineTranslationVel, dReal maxAffineRotationQuatVel)
{
    _maxAffineTranslationVel = maxAffineTranslationVel;
    _maxAffineRotationQuatVel = maxAffineRotationQuatVel;
}

void TrajectoryBase::Reset(int nDOF)
{
    Clear();
    _nDOF = nDOF;
    _nQuaternionIndex = -1;
}

void TrajectoryBase::Clear()
{
    _vecpoints.resize(0);
    _lowerJointLimit.resize(0);
    _upperJointLimit.resize(0);
    _maxJointVel.resize(0);
    _maxJointAccel.resize(0);
    _vecsegments.resize(0);
}

void TrajectoryBase::Clone(InterfaceBaseConstPtr preference, int cloningoptions)
{
    InterfaceBase::Clone(preference,cloningoptions);
    TrajectoryBaseConstPtr r = RaveInterfaceConstCast<TrajectoryBase>(preference);
    _vecpoints = r->_vecpoints;
    _vecsegments = r->_vecsegments;
    _lowerJointLimit = r->_lowerJointLimit;
    _upperJointLimit = r->_upperJointLimit;
    _maxJointVel = r->_maxJointVel;
    _maxJointAccel = r->_maxJointAccel;
    _maxAffineTranslationVel = r->_maxAffineTranslationVel;
    _maxAffineRotationQuatVel = r->_maxAffineRotationQuatVel;
    _nQuaternionIndex = r->_nQuaternionIndex;
    _diffstatefn = r->_diffstatefn;
    _interpMethod = r->_interpMethod;
    _nDOF = r->_nDOF;
}

bool TrajectoryBase::CalcTrajTiming(RobotBaseConstPtr pRobot, InterpEnum interpolationMethod, bool bAutoCalcTiming, bool bActiveDOFs, dReal fMaxVelMult)
{
    if( _vecpoints.size() == 0 ) {
        return false;
    }
    if( !pRobot && bAutoCalcTiming ) {
        if( _nDOF > 0 ) {
            RAVELOG_WARN("need to specify a robot if calculating trajectory timings\n");
            return false;
        }
        // allow at least transformations to be interpolated
    }

    _nQuaternionIndex = -1;
    if( !!pRobot ) {
        if( bActiveDOFs ) {
            if( pRobot->GetActiveDOF() != GetDOF() ) {
                RAVELOG_WARN("trajectory has different degrees of freedom %d != %d\n", pRobot->GetActiveDOF(), GetDOF());
                return false;
            }

            pRobot->GetActiveDOFMaxVel(_maxJointVel);
            pRobot->GetActiveDOFMaxAccel(_maxJointAccel);
            pRobot->GetActiveDOFLimits(_lowerJointLimit, _upperJointLimit);
            _diffstatefn = boost::bind(&RobotBase::SubtractActiveDOFValues,pRobot,_1,_2);
            _nQuaternionIndex = pRobot->GetAffineDOFIndex(RobotBase::DOF_RotationQuat);
            BOOST_ASSERT(_nQuaternionIndex < 0 || _nQuaternionIndex+4 <= _nDOF );
        }
        else {
            std::vector<dReal> dummy;
            pRobot->GetDOFVelocityLimits(dummy,_maxJointVel);
            pRobot->GetDOFMaxAccel(_maxJointAccel);
            pRobot->GetDOFLimits(_lowerJointLimit, _upperJointLimit);
            _maxAffineTranslationVel = pRobot->GetAffineTranslationMaxVels();
            _maxAffineRotationQuatVel = pRobot->GetAffineRotationQuatMaxVels();
            _diffstatefn = boost::bind(&RobotBase::SubtractDOFValues,pRobot,_1,_2);
        }
    }
    else {
        _diffstatefn = subtractstates;
    }

    if( fMaxVelMult > 0 ) { // && fMaxVelMult <= 1 ) {
        FOREACH(it, _maxJointVel) {
            *it *= fMaxVelMult;
        }
        _maxAffineTranslationVel *= fMaxVelMult;
        _maxAffineRotationQuatVel *= fMaxVelMult;
    }
    else {
        RAVELOG_WARN("bad multipler set: %f, ignoring...\n", (float)fMaxVelMult);
    }

    // set the trajectory timing using the given interpolation method
    bool bSuccess = false;
    switch (interpolationMethod) {
    case LINEAR:
        // linear interpolation
        bSuccess = _SetLinear(bAutoCalcTiming, bActiveDOFs);
        break;

    //        case LINEAR_BLEND:
    //        // linear with quadratic blends
    //        return _SetLinearBlend(bAutoCalcTiming);
    //
    case CUBIC:
        bSuccess = _SetCubic(bAutoCalcTiming, bActiveDOFs);
        break;
    //    case QUINTIC:
    //        bSuccess = _SetQuintic(bAutoCalcTiming);
    //        break;
    default:
        RAVELOG_ERROR("TrajectoryBase:: ERROR - bad interpolation method: %d\n", interpolationMethod);
        break;
    }

    if( bSuccess ) {
        RAVELOG_VERBOSE("Total Trajectory Duration = %f\n", GetTotalDuration());
    }
    return bSuccess;
}

bool TrajectoryBase::SampleTrajectory(dReal time, TPOINT &sample) const
{
    if (_vecpoints.size() < 2) {
        if( _vecpoints.size() == 0 ) {
            RAVELOG_ERROR(str(boost::format("TrajectoryBase:: ERROR unable to sample.  numpoints = %d\n")%_vecpoints.size()));
            return false;
        }
        sample = _vecpoints.at(0);
        return true;
    }

    if (time <= 0.0) {
        sample = _vecpoints.at(0);
        return true;
    }
    else if (time >= GetTotalDuration()) {
        //if (time > GetTotalDuration()) RAVELOG(L"TrajectoryBase::WARNING- sample time > duration: %f\n", time);
        BOOST_ASSERT(_vecpoints.size()>0);
        sample = _vecpoints.back();
        return true;
    }

    sample.q.resize(_nDOF);

    // set up interpolation
    int index = _FindActiveInterval(time);
    const TPOINT& p0 = _vecpoints.at(index);
    const TPOINT& p1 = _vecpoints.at(index+1);
    const TSEGMENT& seg = _vecsegments.at(index);
    BOOST_ASSERT (p1.time != p0.time);
    BOOST_ASSERT (seg._fduration > 0.0);

    if((_nDOF == (int)p0.qtorque.size())&&(_nDOF == (int)p1.qtorque.size())) {
        sample.qtorque.resize(_nDOF);
        dReal fscale = p1.time == p0.time ? 0.0f : (time - p0.time)/(p1.time-p0.time);
        for (int d = 0; d < _nDOF; d++) {
            sample.qtorque[d] = p0.qtorque[d] * (1-fscale) + p1.qtorque[d]*fscale;
        }
    }

    switch (_interpMethod) {
    case LINEAR: return _SampleLinear(p0, p1, seg, time, sample);
    case LINEAR_BLEND: return _SampleLinearBlend(p0, p1, seg, time, sample);
    case CUBIC: return _SampleCubic(p0, p1, seg, time, sample);
    case QUINTIC: return _SampleQuintic(p0, p1, seg, time, sample);
    default:
        BOOST_ASSERT(0);
    }
    return false;
}

bool TrajectoryBase::IsValid() const
{
    return true;

    RAVELOG_VERBOSE("Checking validity of trajectory points...\n");
    bool bResult = true;

    // check joint limits, velocity and acceleration bounds
    vector<TPOINT>::const_iterator it;
    FORIT(it, _vecpoints) {
        for (int d = 0; d < _nDOF; d++) {
            if (it->q[d] < _lowerJointLimit[d]) {
                RAVELOG_WARN("Trajectory: WARNING! dof %d exceeds lower joint limit (%f)! q = %f\n", d, _lowerJointLimit[d], it->q[d]);
                bResult = false;
            }
            if (it->q[d] > _upperJointLimit[d]) {
                RAVELOG_WARN("Trajectory: WARNING! dof %d exceeds upper joint limit (%f)! q = %f\n", d, _upperJointLimit[d], it->q[d]);
                bResult = false;
            }
            if (fabs(it->qdot[d]) > _maxJointVel[d]) {
                RAVELOG_WARN("Trajectory: WARNING! dof %d exceeds max joint velocity (%f)! q = %f\n", d, _lowerJointLimit[d], it->qdot[d]);
                bResult = false;
            }
            //        if (fabs(tp.qaccel[d]) > _maxJointAccel[d]) {
            //            RAVELOG(L"Trajectory: WARNING! dof %d exceeds max joint acceleration (%f)! q = %f\n", d, _lowerJointLimit[d], tp.qaccel[d]);
            //            bResult = false;
            //        }
        }
    }

    return bResult;
}

bool TrajectoryBase::_SetLinear(bool bAutoCalcTiming, bool bActiveDOFs)
{
    _vecsegments.resize(_vecpoints.size());

    // if needed, preallocate all velocities, accelerations, and coefficients.
    FOREACH(itp, _vecpoints) {
        itp->qdot.resize(_nDOF);
        itp->linearvel = Vector();
        itp->angularvel = Vector();
        memset(&itp->qdot[0], 0, _nDOF*sizeof(dReal));
        //memset(&itp->qaccel[0], 0, _nDOF*sizeof(dReal));
    }

    _vecsegments.resize(_vecpoints.size());

    _vecpoints[0].time = 0.0;
    dReal timeInterval;

    for (int i = 1; i < (int)_vecpoints.size(); i++) {
        if (bAutoCalcTiming) {
            // compute minimum time interval that does not exceed the maximum joint velocities
            timeInterval = _MinimumTimeLinear(_vecpoints[i-1], _vecpoints[i], bActiveDOFs);
            if( timeInterval < g_fEpsilon ) {
                timeInterval= g_fEpsilon;
            }
            _vecpoints[i].time = _vecpoints[i-1].time + timeInterval;
        }
        else {
            // use time stamps from the points
            timeInterval = _vecpoints[i].time - _vecpoints[i-1].time;
            if( timeInterval < g_fEpsilon ) {
                timeInterval = g_fEpsilon;
            }
        }
    }

    // set up default linear interpolation segments
    vector<dReal> vd;
    for (size_t i = 1; i < _vecpoints.size(); i++) {
        _vecsegments[i-1].SetDimensions(1, _nDOF);
        _vecsegments[i-1]._fduration = _vecpoints[i].time - _vecpoints[i-1].time;

        // set all linear coefficients
        vd = _vecpoints[i].q;
        _diffstatefn(vd,_vecpoints[i-1].q);
        for (int d = 0; d < _nDOF; d++) {
            _vecsegments[i-1].Get(0, d) = _vecpoints[i-1].q[d];
            _vecsegments[i-1].Get(1, d) = vd[d] / _vecsegments[i-1]._fduration;
        }
        Transform tdelta = _vecpoints[i].trans * _vecpoints[i-1].trans.inverse();
        _vecsegments[i-1].linearvel = (_vecpoints[i].trans.trans - _vecpoints[i-1].trans.trans)*(1/_vecsegments[i-1]._fduration);
        _vecsegments[i-1].angularvel = axisAngleFromQuat(quatMultiply(_vecpoints[i].trans.rot,quatInverse(_vecpoints[i-1].trans.rot)))*(1/_vecsegments[i-1]._fduration);
    }

    // set the via point velocities if needed
    // init the start point velocity and accel
    dReal prevSlope, nextSlope;

    _vecpoints[0].linearvel = _vecsegments[0].linearvel;
    _vecpoints[0].angularvel = _vecsegments[0].angularvel;

    // set the via point velocities to zero for slope direction
    // reversals.  Otherwise, use the average of the slopes of
    // the preceding and subsequent trajectory segments.
    BOOST_ASSERT(_vecpoints.size()>0);
    for (size_t i = 1; i < _vecpoints.size()-1; i++) {
        _vecpoints[i].linearvel = _vecsegments[i].linearvel;
        _vecpoints[i].angularvel = _vecsegments[i].angularvel;

        for (int d = 0; d < _nDOF; d++) {
            prevSlope = _vecsegments[i-1].Get(1, d);
            nextSlope = _vecsegments[i].Get(1, d);

            // check for the same slope directions
            if (((prevSlope > 0)&&(nextSlope > 0)) || ((prevSlope < 0)&&(nextSlope < 0))) {
                // use the slope average velocities
                _vecpoints[i].qdot[d] = (dReal)0.5 * (prevSlope + nextSlope);
                //cerr << "i: " << i << "  " << prevSlope << "  " << nextSlope
                //     << " \t" << _vecpoints[i].qdot[d] << endl;
                //_vecpoints[i].qdot[d] = 0.0;
            }
            else {
                // otherwise use a zero velocity
                _vecpoints[i].qdot[d] = 0.0;
            }
        }
    }

    _interpMethod = LINEAR;
    return true;
}

//bool TrajectoryBase::_SetLinearBlend(bool bAutoCalcTiming)
//{
//    cerr << "Setting linear blend trajectory..." << endl;
//    _totalDuration = 0.0;
//    if (bAutoCalcTiming)
//        _vecpoints[0].time = 0.0;
//    float timeInterval;
//
//    for (int i = 1; i < _vecpoints.size(); i++)
//    {
//        if (bAutoCalcTiming) {
//            // compute minimum time interval that does not exceed the
//            // maximum joint velocities
//            timeInterval = _MinimumTimeLinearBlend(_vecpoints[i-1], _vecpoints[i]);
//            _vecpoints[i].time = _vecpoints[i-1].time + timeInterval;
//        }
//        else {
//            // use time stamps from the points
//            timeInterval = _vecpoints[i].time - _vecpoints[i-1].time;
//        }
//
//        // make sure the timing interval is valid
//        if (timeInterval <= 0.0) {
//            cerr << "Trajectory: ERROR - bad time interval: " << timeInterval
//                << "  Time stamp " << i << " : " << _vecpoints[i].time
//                << endl;
//            return false;
//        }
//    }
//
//    // update the total duration
//    _totalDuration = _vecpoints[_vecpoints.size()-1].time;
//
//    // set up trajectory segments
//    for (int i = 0; i < _vecpoints.size()-1; i++)
//    {
//        _vecsegments[i].degree = 5;
//        _vecsegments[i].duration = _vecpoints[i+1].time - _vecpoints[i].time;
//
//        // determine the position of the current segment
//        TSEGMENT::Type type;
//        TSEGMENT& prevSeg = _vecsegments[i];
//        if (i == 0) {
//            type = TSEGMENT::START;
//        }
//        else if (i == _vecpoints.size()-2){
//            type = TSEGMENT::END;
//            prevSeg = _vecsegments[i-1];
//        }
//        else {
//            type = TSEGMENT::MIDDLE;
//            prevSeg = _vecsegments[i-1];
//        }
//
//        // set linear blend coefficients for continuous velocities at via points
//        _CalculateLinearBlendCoefficients(type, _vecsegments[i], prevSeg,
//            _vecpoints[i], _vecpoints[i+1],
//            _maxJointAccel);
//    }
//
//    _interpMethod = LINEAR_BLEND;
//    cerr << "  Total Duration = " << _totalDuration << endl;
//
//    return true;
//}
//
//inline bool TrajectoryBase::_CalculateLinearBlendCoefficients(TSEGMENT::Type segType,
//                                                          TSEGMENT& seg, TSEGMENT& prev,
//                                                          TPOINT& p0, TPOINT& p1,
//                                                          const float blendAccel[])
//{
//    // extract duration and calculate blend times
//    float duration = p1.time - p0.time;
//    BOOST_ASSERT ( duration > 0.0 );
//    float posDiff, slopeDiff, accel, halfBlendTime;
//
//    // calculate parabolic and linear blend parameters for all DOFs
//    for (int d = 0; d < _numDOF; d++)
//    {
//        switch (segType) {
//    case TSEGMENT::START:
//        // slope of interior segment
//        posDiff = p1.q[d] - p0.q[d];
//        SEG_SLOPE(seg,d) = posDiff / duration;
//
//        if (posDiff < 0) accel = -blendAccel[d];
//        else             accel = blendAccel[d];
//        START_A(seg,d) = accel;
//        p0.qaccel[d]   = accel;
//
//        halfBlendTime           = 0.1;
//        START_BLEND_TIME(seg,d) = halfBlendTime;
//
//        SEG_OFFSET(seg,d) = p0.q[d] + SEG_SLOPE(seg,d) * START_BLEND_TIME(seg,d);
//        break;
//
//    case TSEGMENT::MIDDLE:
//        // slope of interior segment
//        SEG_SLOPE(seg,d) = (p1.q[d] - p0.q[d]) / duration;
//
//        slopeDiff = SEG_SLOPE(seg,d) - SEG_SLOPE(prev,d);
//        if (slopeDiff < 0) accel = -blendAccel[d];
//        else               accel = blendAccel[d];
//        START_A(seg,d) = accel;
//        END_A(prev,d)  = accel;
//        p0.qaccel[d]   = accel;
//
//        halfBlendTime           = (slopeDiff / accel) * 0.5;
//        START_BLEND_TIME(seg,d) = halfBlendTime;
//        END_BLEND_TIME(prev,d)  = prev.duration - halfBlendTime;
//
//        SEG_OFFSET(seg,d) = p0.q[d] + SEG_SLOPE(seg,d) * START_BLEND_TIME(seg,d);
//        break;
//
//    case TSEGMENT::END:
//        // slope of interior segment
//        SEG_SLOPE(seg,d) = (p1.q[d] - p0.q[d]) / duration;
//
//        slopeDiff = SEG_SLOPE(seg,d) - SEG_SLOPE(prev,d);
//        if (slopeDiff < 0) accel = -blendAccel[d];
//        else               accel = blendAccel[d];
//        START_A(seg,d) = accel;
//        END_A(prev,d)  = accel;
//        p0.qaccel[d]   = accel;
//
//        halfBlendTime           = (slopeDiff / accel) * 0.5;
//        START_BLEND_TIME(seg,d) = halfBlendTime;
//        END_BLEND_TIME(prev,d)  = prev.duration - halfBlendTime;
//
//        SEG_OFFSET(seg,d) = p0.q[d] + SEG_SLOPE(seg,d) * START_BLEND_TIME(seg,d);
//        break;
//
//    default:
//        cerr << "Trajectory: ERROR - unknown segment type: " << segType << endl;
//        return false;
//        }
//
//    }
//
//
//    return true;
//}
//

bool TrajectoryBase::_SetCubic(bool bAutoCalcTiming, bool bActiveDOFs)
{
    _vecsegments.resize(_vecpoints.size());
    // if needed, preallocate all velocities, accelerations, and coefficients.
    FOREACH(itp, _vecpoints) {
        itp->qdot.resize(_nDOF);
        itp->linearvel = Vector();
        itp->angularvel = Vector();
        memset(&itp->qdot[0], 0, _nDOF*sizeof(dReal));
        //memset(&itp->qaccel[0], 0, _nDOF*sizeof(dReal));
    }

    dReal timeInterval;
    for (size_t i = 1; i < _vecpoints.size(); i++) {
        if (bAutoCalcTiming) {
            // compute minimum time interval that does not exceed the
            // maximum joint velocities
            timeInterval = _MinimumTimeCubic(_vecpoints[i-1], _vecpoints[i], bActiveDOFs);
            if( timeInterval < 1e-4f ) {
                timeInterval= 1e-4f;
            }
            _vecpoints[i].time = _vecpoints[i-1].time + timeInterval;
        }
        else {
            // use time stamps from the points
            timeInterval = _vecpoints[i].time - _vecpoints[i-1].time;
            if( timeInterval < 1e-4f ) {
                timeInterval = 1e-4f;
            }
        }
    }

    // set up default linear interpolation segments
    vector<dReal> vd;
    for (size_t i = 1; i < _vecpoints.size(); i++) {
        _vecsegments[i-1].SetDimensions(3, _nDOF);
        _vecsegments[i-1]._fduration = _vecpoints[i].time - _vecpoints[i-1].time;

        // set all cubic coefficients
        //        vd = _vecpoints[i].q;
        //        _diffstatefn(vd,_vecpoints[i-1].q);
        //        for (int d = 0; d < _nDOF; d++) {
        //            _vecsegments[i-1].Get(0, d) = _vecpoints[i-1].q[d];
        //            _vecsegments[i-1].Get(1, d) = vd[d] / _vecsegments[i-1]._fduration;
        //        }
    }

    // recalculate via point velocities and accelerations
    _RecalculateViaPointDerivatives();

    // set all cubic coefficients for continuous velocities at via points
    for (size_t i = 1; i < _vecpoints.size(); i++) {
        _CalculateCubicCoefficients(_vecsegments[i-1], _vecpoints[i-1], _vecpoints[i]);
    }
    _interpMethod = CUBIC;
    return true;
}

void TrajectoryBase::_CalculateCubicCoefficients(TrajectoryBase::TSEGMENT& seg, const TPOINT& tp0, const TPOINT& tp1)
{
    dReal t = tp1.time - tp0.time; // extract duration
    BOOST_ASSERT ( t > 0.0 );
    dReal t_2 = t*t;
    dReal t_3 = t * t_2;

    // calculate smooth interpolating cubic for all DOFs
    vector<dReal> vd;
    vd = tp1.q;
    _diffstatefn(vd,tp0.q);
    for (int d = 0; d < _nDOF; d++) {
        seg.Get(0,d) = tp0.q[d];
        seg.Get(1,d) = tp0.qdot[d];
        seg.Get(2,d) = (3.0f/t_2)*vd[d] - (2.0f/t)*tp0.qdot[d] - (1.0f/t)*tp1.qdot[d];
        seg.Get(3,d) = (-2.0f/t_3)*vd[d] + (1.0f/t_2)*(tp1.qdot[d] + tp0.qdot[d]);
    }
}

////! smooth quintic spline interpolation with minimum jerk criteria
//bool TrajectoryBase::_SetQuintic(bool bAutoCalcTiming)
//{
//    cerr << "Setting minimum-jerk quintic trajectory..." << endl;
//    _totalDuration = 0.0;
//    if (bAutoCalcTiming)
//        _vecpoints[0].time = 0.0;
//    float timeInterval;
//
//    // the points
//    for (int i = 1; i < _vecpoints.size(); i++)
//    {
//        timeInterval = 0.0;
//        if (bAutoCalcTiming) {
//            // compute minimum time interval that does not exceed the
//            // maximum joint velocities
//            timeInterval = _MinimumTimeQuintic(_vecpoints[i-1], _vecpoints[i]);
//            _totalDuration += timeInterval;
//            _vecpoints[i].time = _totalDuration;
//        }
//        else {
//            // use time stamps from the points
//            timeInterval = _vecpoints[i].time - _vecpoints[i-1].time;
//            _totalDuration += timeInterval;
//        }
//
//        // make sure the timing interval is valid
//        if (timeInterval <= 0.0) {
//            cerr << "Trajectory: ERROR - bad time interval: " << timeInterval
//                << "  Time stamp " << i << " : " << _vecpoints[i].time
//                << endl;
//            return false;
//        }
//
//        // set the segments
//        _vecsegments[i-1].duration = timeInterval;
//        _vecsegments[i-1].degree = 5;
//    }
//
//    // recalculate via point velocities and accelerations
//    if (!_RecalculateViaPointDerivatives()) {
//        cerr << "Trajectory: ERROR calculating quintic accelerations" << endl;
//        return false;
//    }
//
//    // calculate all quintic equations
//    for (int i = 1; i < _vecpoints.size(); i++) {
//        // set all cubic coefficients for continuous velocities at via points
//        if (!_CalculateQuinticCoefficients(_vecsegments[i-1].a, _vecpoints[i-1],
//            _vecpoints[i]))
//            cerr << "Trajectory: ERROR calculating quintic coefficients" << endl;
//    }
//
//
//    _interpMethod = QUINTIC;
//    cerr << "  Total Duration = " << _totalDuration << endl;
//
//    return true;
//}

void TrajectoryBase::_RecalculateViaPointDerivatives()
{
    dReal prevSlope, nextSlope;
    dReal prevDur, nextDur;
    vector<dReal> vd0,vd1;

    // set the via point accelerations to max at direction reversals
    for (int i = 1; i < (int)_vecpoints.size()-1; i++) {
        prevDur = _vecsegments[i-1]._fduration;
        nextDur = _vecsegments[i]._fduration;

        vd0 = _vecpoints[i].q;
        _diffstatefn(vd0,_vecpoints[i-1].q);
        vd1 = _vecpoints[i+1].q;
        _diffstatefn(vd1,_vecpoints[i].q);

        for (int d = 0; d < _nDOF; d++) {
            prevSlope = vd0[d] / prevDur;
            nextSlope = vd1[d] / nextDur;

            // check the slope directions at via points
            if (prevSlope < 0) {
                if (nextSlope > 0) {
                    _vecpoints[i].qdot[d] = 0;
                    //_vecpoints[i].qaccel[d] = _maxJointAccel[d];
                }
                else {
                    _vecpoints[i].qdot[d] = 0.5f * (prevSlope + nextSlope);
                    //_vecpoints[i].qaccel[d] = 0.0;
                }
            }
            else {
                if (nextSlope > 0) {
                    _vecpoints[i].qdot[d] = 0.5f * (prevSlope + nextSlope);
                    //_vecpoints[i].qaccel[d] = 0.0;
                }
                else {
                    _vecpoints[i].qdot[d] = 0;
                    //_vecpoints[i].qaccel[d] = -_maxJointAccel[d];
                }
            }
        }
    }
}

//inline bool TrajectoryBase::_CalculateQuinticCoefficients(JointConfig a[],
//                                                      TPOINT& tp0,
//                                                      TPOINT& tp1)
//{
//    // extract duration
//    float t = tp1.time - tp0.time;
//    BOOST_ASSERT ( t > 0.0 );
//    float t_2 = SQR(t);
//    float t_3 = t * t_2;
//    float t_4 = t * t_3;
//    float t_5 = t * t_4;
//    float qdiff;
//
//    // calculate smooth interpolating quintic for all DOFs
//    for (int d = 0; d < _numDOF; d++)
//    {
//        a[0][d] = tp0.q[d];
//
//        a[1][d] = tp0.qdot[d];
//
//        a[2][d] = 0.5 * tp0.qaccel[d];
//
//        qdiff = tp1.q[d] - tp0.q[d];
//
//        //a[2][d] = 0.5 * tp0.qaccel[d];
//
//        a[3][d] = 20 * qdiff  -  ( 8 * tp1.qdot[d] + 12 * tp0.qdot[d] ) * t
//            - ( 3 * tp0.qaccel[d] - tp1.qaccel[d] ) * t_2;
//        a[3][d] /=  2 * t_3;
//
//
//        a[4][d] = -30 * qdiff  +  ( 14 * tp1.qdot[d] + 16 * tp0.qdot[d] ) * t
//            + ( 3 * tp0.qaccel[d] - 2 * tp1.qaccel[d] ) * t_2;
//        a[4][d] /=  2 * t_4;
//
//
//        a[5][d] = 12 * qdiff  -  ( 6 * tp1.qdot[d] + 6 * tp0.qdot[d] ) * t
//            - ( tp0.qaccel[d] - tp1.qaccel[d] ) * t_2;
//        a[5][d] /=  2 * t_5;
//    }
//
//    return true;
//}

dReal TrajectoryBase::_MinimumTimeLinear(const TPOINT& tp0, const TPOINT& tp1, bool bActiveDOFs)
{
    dReal minJointTime;
    dReal minPathTime = 0.0;

    vector<dReal> vd;
    vd = tp1.q;
    _diffstatefn(vd,tp0.q);

    // compute minimum time interval that does not exceed the maximum joint velocities
    int d = 0;
    while(d < _nDOF) {
        if(_maxJointVel[d] > 0.0) {
            if( _nQuaternionIndex == d ) {
                Vector q0(tp0.q[d],tp0.q[d+1],tp0.q[d+2],tp0.q[d+3]);
                Vector q1(tp1.q[d],tp1.q[d+1],tp1.q[d+2],tp1.q[d+3]);
                minJointTime = RaveAcos(min(dReal(1),RaveFabs(q0.dot(q1))))/_maxJointVel[d];
                d += 3; // d will be increased by 1 below
            }
            else {
                minJointTime = fabs(vd[d]) / _maxJointVel[d];
            }
            if (minPathTime < minJointTime) {
                minPathTime = minJointTime;
            }
        }
        ++d;
    }

    if( !bActiveDOFs ) {
        minPathTime = max(_MinimumTimeTransform(tp0.trans, tp1.trans),minPathTime);
    }
#ifndef _WIN32
    BOOST_ASSERT( !isnan(minPathTime));
#endif

    return minPathTime;
}

dReal TrajectoryBase::_MinimumTimeCubic(const TPOINT& tp0, const TPOINT& tp1, bool bActiveDOFs)
{
    dReal minJointTime, jointDiff;
    dReal velocityConstraint, accelConstraint;
    dReal minPathTime = 0.0;

    vector<dReal> vd;
    vd = tp1.q;
    _diffstatefn(vd,tp0.q);

    // compute minimum time interval that does not exceed the maximum joint velocities or accelerations
    int d = 0;
    while(d < _nDOF) {
        if((_maxJointVel[d] > 0.0)&&(_maxJointAccel[d] > 0)) {
            if( _nQuaternionIndex == d ) {
                Vector q0(tp0.q[d],tp0.q[d+1],tp0.q[d+2],tp0.q[d+3]);
                Vector q1(tp1.q[d],tp1.q[d+1],tp1.q[d+2],tp1.q[d+3]);
                // correct?
                jointDiff = RaveAcos(min(dReal(1),RaveFabs(q0.dot(q1))));
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
                d += 3; // d will be increased by 1 below
            }
            else {
                jointDiff = fabs(vd[d]);
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
            }

            if (minPathTime < minJointTime) {
                minPathTime = minJointTime;
            }
        }
        ++d;
    }

    if( !bActiveDOFs ) {
        minPathTime = max(_MinimumTimeTransform(tp0.trans, tp1.trans),minPathTime);
    }

#ifndef _WIN32
    BOOST_ASSERT( !isnan(minPathTime));
#endif

    return minPathTime;
}

dReal TrajectoryBase::_MinimumTimeCubicZero(const TPOINT& tp0, const TPOINT& tp1, bool bActiveDOFs)
{
    dReal minJointTime, jointDiff;
    dReal velocityConstraint, accelConstraint;
    dReal minPathTime = 0.0;

    vector<dReal> vd;
    vd = tp1.q;
    _diffstatefn(vd,tp0.q);

    // compute minimum time interval that does not exceed the maximum joint velocities or accelerations
    int d = 0;
    while(d < _nDOF) {
        if((_maxJointVel[d] > 0.0)&&(_maxJointAccel[d] > 0)) {
            if( _nQuaternionIndex == d ) {
                Vector q0(tp0.q[d],tp0.q[d+1],tp0.q[d+2],tp0.q[d+3]);
                Vector q1(tp1.q[d],tp1.q[d+1],tp1.q[d+2],tp1.q[d+3]);
                jointDiff = RaveAcos(min(dReal(1),RaveFabs(q0.dot(q1))));
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
                d += 3; // d will be increased by 1 below
            }
            else {
                jointDiff = fabs(vd[d]);
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
            }
            if (minPathTime < minJointTime) {
                minPathTime = minJointTime;
            }
        }
        ++d;
    }

    if( !bActiveDOFs ) {
        minPathTime = max(_MinimumTimeTransform(tp0.trans, tp1.trans),minPathTime);
    }
#ifndef _WIN32
    BOOST_ASSERT( !isnan(minPathTime));
#endif
    return minPathTime;
}

dReal TrajectoryBase::_MinimumTimeQuintic(const TPOINT& tp0, const TPOINT& tp1, bool bActiveDOFs)
{
    RAVELOG_ERROR("Trajectory: ERROR - inaccurate minimum time quintic calculation used.\n");

    dReal minJointTime, jointDiff;
    dReal velocityConstraint, accelConstraint;
    dReal minPathTime = 0.0;

    vector<dReal> vd;
    vd = tp1.q;
    _diffstatefn(vd,tp0.q);

    // compute minimum time interval that does not exceed the maximum joint velocities or accelerations
    int d = 0;
    while(d < _nDOF) {
        if((_maxJointVel[d] > 0.0)&&(_maxJointAccel[d] > 0.0)) {
            if( _nQuaternionIndex == d ) {
                Vector q0(tp0.q[d],tp0.q[d+1],tp0.q[d+2],tp0.q[d+3]);
                Vector q1(tp1.q[d],tp1.q[d+1],tp1.q[d+2],tp1.q[d+3]);
                jointDiff = RaveAcos(min(dReal(1),RaveFabs(q0.dot(q1))))/_maxJointVel[d];
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
                d += 3; // d will be increased by 1 below
            }
            else {
                jointDiff = fabs(vd[d]);
                velocityConstraint = (1.5f/_maxJointVel[d]) * jointDiff;
                accelConstraint = RaveSqrt((6.0f/_maxJointAccel[d]) * jointDiff);
                minJointTime = max( velocityConstraint, accelConstraint );
            }

            if (minPathTime < minJointTime) {
                minPathTime = minJointTime;
            }
        }
        ++d;
    }

    if( !bActiveDOFs ) {
        minPathTime = max(_MinimumTimeTransform(tp0.trans, tp1.trans),minPathTime);
    }
#ifndef _WIN32
    BOOST_ASSERT( !isnan(minPathTime));
#endif

    return minPathTime;
}

dReal TrajectoryBase::_MinimumTimeTransform(const Transform& t0, const Transform& t1)
{
    //calc time for translation
    //dReal x_time = fabs(t1.trans.x - t0.trans.x) / _maxAffineTranslationVel.x;
    dReal trans_time = RaveSqrt((t1.trans - t0.trans).lengthsqr3()) / _maxAffineTranslationVel.x;
    dReal rot_time = RaveAcos(min(dReal(1),RaveFabs(t0.rot.dot(t1.rot))))/_maxAffineRotationQuatVel;
    return max(trans_time,rot_time);
}

int TrajectoryBase::_FindActiveInterval(dReal time) const
{
    int index = 0;

    // for now, just do a simple linear search
    while (time > _vecpoints[index+1].time) {
        index++;
    }
    BOOST_ASSERT (index < (int)_vecpoints.size() - 1);
    return index;
}

bool TrajectoryBase::_SampleLinear(const TPOINT& p0, const TPOINT& p1, const TSEGMENT& seg, dReal time, TPOINT& sample) const
{
    BOOST_ASSERT (time >= p0.time && time <= p1.time);
    BOOST_ASSERT (seg._fduration > 0.0);
    BOOST_ASSERT (fabs((p1.time - p0.time) - seg._fduration) < 1e-4);
    dReal t = time - p0.time;
    dReal tduration = t/seg._fduration;
    sample.q.resize(_nDOF);
    sample.qdot.resize(_nDOF);
    //sample.qaccel.resize(_nDOF);

    // perform the interpolation
    int d = 0;
    while(d < _nDOF) {
        if( d == _nQuaternionIndex ) {
            Vector q0(p0.q[d],p0.q[d+1],p0.q[d+2],p0.q[d+3]);
            Vector q1(p1.q[d],p1.q[d+1],p1.q[d+2],p1.q[d+3]);
            Vector qres = quatSlerp(q0, q1,tduration);
            sample.q[d] = qres.x;
            sample.q[d+1] = qres.y;
            sample.q[d+2] = qres.z;
            sample.q[d+3] = qres.w;
            Vector qtemp = quatMultiply(q1,quatInverse(q0));
            Vector axis(qtemp.y,qtemp.z,qtemp.w);
            dReal sinangle = RaveSqrt(axis.lengthsqr3());
            if( sinangle > 0 ) {
                axis /= sinangle;
                Vector qlog(0,axis.x,axis.y,axis.z);
                qlog *= RaveAtan2(sinangle,qtemp.x)/seg._fduration;
                Vector qderiv = quatMultiply(qlog,qres);
                sample.qdot[d] = qderiv.x;
                sample.qdot[d+1] = qderiv.y;
                sample.qdot[d+2] = qderiv.z;
                sample.qdot[d+3] = qderiv.w;
            }
            else {
                sample.qdot[d+0] = 0;
                sample.qdot[d+1] = 0;
                sample.qdot[d+2] = 0;
                sample.qdot[d+3] = 0;
            }
            d += 3;
        }
        else {
            sample.q[d] = seg.Get(0, d) + t * seg.Get(1, d);
            sample.qdot[d] = seg.Get(1, d);
        }
        //sample.qaccel[d] = 0.0;
        ++d;
    }

    //NEED TO do t/(seg._fduration-step)
    sample.time = time;
    sample.trans.trans = p0.trans.trans + tduration*(p1.trans.trans - p0.trans.trans);
    sample.trans.rot = quatSlerp(p0.trans.rot, p1.trans.rot,tduration);
    sample.linearvel = p0.linearvel;
    sample.angularvel = p0.angularvel;
    return true;
}

bool TrajectoryBase::_SampleLinearBlend(const TPOINT& p0, const TPOINT& p1, const TSEGMENT& seg, dReal time, TPOINT& sample) const
{
    BOOST_ASSERT (time >= p0.time && time <= p1.time);
    BOOST_ASSERT (seg._fduration > 0.0);
    //BOOST_ASSERT (fabs((p1.time - p0.time) - seg.duration) < VERY_SMALL);
    dReal t = time - p0.time;

    for (int d = 0; d < _nDOF; d++) {
        if (t < START_BLEND_TIME(seg, d) ) {
            // sample in starting parabolic blend region
            sample.q[d]      = p0.q[d]; //+ 0.5 * START_A(seg,d) * SQR(t);
            sample.qdot[d]   = START_A(seg,d) * t;
            //sample.qaccel[d] = START_A(seg,d);
        }
        else if (t > END_BLEND_TIME(seg, d)) {
            // sample in ending parabolic blend region
            t = t - END_BLEND_TIME(seg, d);
            sample.q[d]      = SEG_OFFSET(seg,d) + t * SEG_SLOPE(seg,d);
            sample.qdot[d]   = SEG_SLOPE(seg,d);
            //sample.qaccel[d] = 0.0;
        }
        else {
            // perform linear interpolation in the middle section
            t = t - START_BLEND_TIME(seg, d);
            sample.q[d]      = p1.q[d]; //+ 0.5 * END_A(seg,d) * SQR(t);
            sample.qdot[d]   = END_A(seg,d) * t;
            //sample.qaccel[d] = END_A(seg,d);
        }
    }

    sample.time = time;
    sample.trans.trans = p0.trans.trans + (t/(seg._fduration))*(p1.trans.trans - p0.trans.trans);
    sample.trans.rot = quatSlerp(p0.trans.rot, p1.trans.rot,(t/(seg._fduration)));
    return true;
}

bool TrajectoryBase::_SampleCubic(const TPOINT& p0, const TPOINT& p1, const TSEGMENT& seg, dReal time, TPOINT& sample) const
{
    BOOST_ASSERT (time >= p0.time && time <= p1.time);
    BOOST_ASSERT (seg._fduration > 0.0);
    BOOST_ASSERT (fabs((p1.time - p0.time) - seg._fduration) < 1e-4);
    dReal t = time - p0.time;
    dReal t_2 = t*t;
    dReal t_3 = t * t_2;
    sample.q.resize(_nDOF);
    sample.qdot.resize(_nDOF);

    // perform the interpolation
    for (int d = 0; d < _nDOF; d++) {
        sample.q[d]      = seg.Get(0, d) + t * seg.Get(1, d) + t_2 * seg.Get(2, d) + t_3 * seg.Get(3, d);
        sample.qdot[d]   = seg.Get(1, d) + ((dReal)2.0 * t * seg.Get(2, d)) + ((dReal)3.0 * t_2 * seg.Get(3, d));
        //        sample.qaccel[d] = 2.0 * seg.Get(2, d) + (6.0 * t * seg.Get(3, d));
    }
    sample.time = time;
    sample.trans.trans = p0.trans.trans + (t/(seg._fduration))*(p1.trans.trans - p0.trans.trans);
    sample.trans.rot = quatSlerp(p0.trans.rot, p1.trans.rot,(t/(seg._fduration)));
    return true;
}

bool TrajectoryBase::_SampleQuintic(const TPOINT& p0, const TPOINT& p1, const TSEGMENT& seg, dReal time, TPOINT& sample) const
{
    BOOST_ASSERT (time >= p0.time && time <= p1.time);
    BOOST_ASSERT (seg._fduration > 0.0);
    BOOST_ASSERT (fabs((p1.time - p0.time) - seg._fduration) < 1e-4);
    dReal t   = time - p0.time;
    dReal t_2 = t*t;
    dReal t_3 = t * t_2;
    dReal t_4 = t * t_3;
    dReal t_5 = t * t_4;

    // perform the interpolation
    for (int d = 0; d < _nDOF; d++) {
        sample.q[d] = seg.Get(0, d) + t * seg.Get(1, d) + t_2 * seg.Get(2, d)  +  t_3 * seg.Get(3, d) + t_4 * seg.Get(4, d)  +  t_5 * seg.Get(5, d);
        sample.qdot[d] = seg.Get(1, d) + (2 * t * seg.Get(2, d)) + (3 * t_2 * seg.Get(3, d))  +  (4 * t_3 * seg.Get(4, d)) + (5 * t_4 * seg.Get(5, d));
        //        sample.qaccel[d] = (2 * seg.Get(2, d))  +  (6 * t * seg.Get(3, d)) + (12 * t_2 * seg.Get(4, d))  +  (20 * t_3 * seg.Get(5, d));
    }
    sample.time = time;
    sample.trans.trans = p0.trans.trans + (t/(seg._fduration))*(p1.trans.trans - p0.trans.trans);
    sample.trans.rot = quatSlerp(p0.trans.rot, p1.trans.rot,(t/(seg._fduration)));
    return true;
}

bool TrajectoryBase::Write(const std::string& filename, int options) const
{
    ofstream of(filename.c_str());
    if( !of ) {
        RAVELOG_WARN(str(boost::format("failed to write to file %s\n")%filename));
        return false;
    }
    of << std::setprecision(std::numeric_limits<dReal>::digits10+1);
    return Write(of, options);
}

bool TrajectoryBase::Write(std::ostream& f, int options) const
{
    if( !(options&TO_NoHeader) ) {
        if( !(options&TO_OneLine) ) {
            f << endl;
        }
        else {
            f << " ";
        }
        f << _vecpoints.size() << " " << _nDOF << " " << (options|(_interpMethod<<CountZeroBits(TO_InterpolationMask)));
        if( !(options&TO_OneLine) ) {
            f << endl;
        }
        else {
            f << " ";
        }
    }

    FOREACHC(it, _vecpoints) {
        if( options & TO_IncludeTimestamps ) {
            f << it->time << " ";
        }
        FOREACHC(itval, it->q) {
            f << *itval << " ";
        }
        if( options & TO_IncludeBaseTransformation ) {
            f << it->trans << " ";
        }
        if( options & TO_IncludeVelocities ) {
            BOOST_ASSERT((int)it->qdot.size()==GetDOF());
            FOREACHC(itvel, it->qdot) {
                f << *itvel << " ";
            }
            if( options & TO_IncludeBaseTransformation ) {
                f << it->linearvel.x << " " << it->linearvel.y << " " << it->linearvel.z << " " << it->angularvel.x << " " << it->angularvel.y << " " << it->angularvel.z << " ";
            }
        }

        if( options & TO_IncludeTorques ) {
            BOOST_ASSERT((int)it->qtorque.size()==GetDOF());
            FOREACHC(ittorque,it->qtorque) {
                f << *ittorque << " ";
            }
        }

        if( !(options&TO_OneLine) ) {
            f << endl;
        }
        else {
            f << " ";
        }
    }

    return !!f;
}

bool TrajectoryBase::Read(const std::string& filename, RobotBasePtr robot)
{
    ifstream fi(filename.c_str());
    if( !fi ) {
        RAVELOG_WARN(str(boost::format("failed to read file %s\n")%filename));
        return false;
    }

    return Read(fi, robot);
}

bool TrajectoryBase::Read(std::istream& f, RobotBasePtr robot)
{
    int size, dof, options;
    f >> size >> dof >> options;
    if( dof < 0 ) {
        return false;
    }

    Reset(dof);
    Transform tbody;
    if( !!robot ) {
        tbody = robot->GetTransform();
    }

    _vecpoints.resize(size);
    FOREACH(it, _vecpoints) {
        it->q.resize(dof);
        it->qdot.resize(dof);

        if( options & TO_IncludeTimestamps ) {
            f >> it->time;
        }
        FOREACH(itval, it->q) {
            f >> *itval;
        }
        if( options & TO_IncludeBaseTransformation ) {
            f >> it->trans;
        }
        else {
            it->trans = tbody;
        }
        if( options & TO_IncludeVelocities ) {
            FOREACH(itvel, it->qdot) {
                f >> *itvel;
            }
            if( options & TO_IncludeBaseTransformation ) {
                f >> it->linearvel.x >> it->linearvel.y >> it->linearvel.z >> it->angularvel.x >> it->angularvel.y >> it->angularvel.z;
            }
        }

        if( options & TO_IncludeVelocities ) {
            it->qtorque.resize(dof);
            FOREACH(ittorque, it->qtorque) {
                f >> *ittorque;
            }
        }
    }

    InterpEnum interp = (InterpEnum)((options&TO_InterpolationMask)>>CountZeroBits(TO_InterpolationMask));
    BOOST_ASSERT(interp<NUM_METHODS);
    if( !robot || dof == robot->GetDOF() ) {
        return CalcTrajTiming(robot, interp, false, false);
    }
    return true;
}

} // end namespace OpenRAVE