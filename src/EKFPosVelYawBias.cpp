#include "EKFPosVelYawBias.hpp"
#include <Eigen/LU> 
#include <math.h>
#include <fault_detection/FaultDetection.hpp>

using namespace pose_estimator;

/** The Filter State 
x_n, y_n, z_n in navigation frame
v_x_n, v_y_n, v_z_n in navigation frame 
bias_rot_z_b_n, the rotation beetwen body and nav 

/** The filter 
/** CHECK */ 
EKFPosYawBias::EKFPosYawBias() 
{
    // the position observation update 
    filter_position =  new ExtendedKalmanFilter::EKF<State::SIZE,INPUT_SIZE,MEASUREMENT_SIZE_POS>;
    filter_velocity =  new ExtendedKalmanFilter::EKF<State::SIZE,INPUT_SIZE,MEASUREMENT_SIZE_VEL>;
    filter_scan_match =  new ExtendedKalmanFilter::EKF<State::SIZE,INPUT_SIZE,MEASUREMENT_SIZE_SCAN_MATCH>;
    chi_square = new fault_detection::ChiSquared();
}

EKFPosYawBias::~EKFPosYawBias()
{
    delete filter_position; 
    delete filter_velocity; 
    delete filter_scan_match;
    delete chi_square;
}

void EKFPosYawBias::setPosRejectThreshold(int threshold)
{
    reject_pos_threshold = threshold; 
}

void EKFPosYawBias::setVelRejectThreshold(int threshold)
{
    reject_vel_threshold = threshold; 
}

void EKFPosYawBias::setIcpRejectThreshold(int threshold)
{
    reject_icp_threshold = threshold; 
}

/** update the filter */ 
void EKFPosYawBias::predict( const Eigen::Vector3d &acc_nav, double &dt )
{	
  
    //calculates the rotation from world to world frame corrected by the bias 
    Eigen::Quaterniond R_w2wb;
    R_w2wb = Eigen::AngleAxisd( x.yaw()(0,0), Eigen::Vector3d::UnitZ() ); 

    //sets the transition matrix 
    Eigen::Matrix<double, State::SIZE, 1> f;
    f.start<3>() = x.xi() + x.vi * dt; 
    f.segment<2>(3) = x.vi() + R_w2wb * acc_nav * dt; 
    f.end<1>() = x.yaw();

    //sets the Jacobian of the state transition matrix 
    Eigen::Matrix<double, State::SIZE, State::SIZE> J_F
	= jacobianF( dt );

	
    /** EKF **/ 
    //sets the current state to the filter 
    filter_position->x = x.vector(); 
    filter_position->P = P;
    
    //updates the Kalman Filter 
    filter_position->prediction( f, J_F, Q ); 

    //get the updated values 
    x.vector()=filter_position->x; 
    P=filter_position->P; 

}

void EKFPosYawBias::correctionPos(const Eigen::Matrix<double, MEASUREMENT_SIZE_POS, 1> &p, Eigen::Matrix<double, MEASUREMENT_SIZE_POS, MEASUREMENT_SIZE_POS> R_pos  )
{

    Eigen::Matrix<double, MEASUREMENT_SIZE_POS, State::SIZE> J_H;
    J_H.setIdentity();
    
std::cout<< " pos J_H \n"<<J_H << std::endl; 
    
    Eigen::Matrix<double, MEASUREMENT_SIZE_POS, 1> h;
    h = J_H * x.vector();

    /** EKF  */
    //sets the current state to the filter 
    filter_pos->x = x.vector(); 
    filter_pos->P = P; 
    
   //innovation steps
    filter_pos->innovation( p, h, J_H, R_pos ); 

    float threshold; 
    bool reject = true; 
    switch ( reject_pos_threshold ) 
    {
	case THRESHOLD_3D_99: 
	    threshold = chi_square->THRESHOLD_3D_99;
	    break; 
	case THRESHOLD_3D_95: 
	    threshold = chi_square->THRESHOLD_3D_95;
	    break;
	default: 
	    reject = false; 
	    break;
    }

    //test to reject data 
    if ( reject ) 
	reject_pos_observation=chi_square->rejectData3D( filter_position->y.start<3>(), filter_position->S, threshold );
  
  
    if(!reject_pos_observation)
    {
    
	//Kalman Gain
	filter_position->gain( J_H ); 
	
	//update teh state 
	filter_position->update(J_H ); 

	//get the corrected values 
	x.vector() = filter_position->x; 
	P = filter_position->P; 

    }
    else
    { 
     
	std::cout<< " Rejected Position data " << std::endl; 
    
    }

}

 
void EKFPosYawBias::correctionVel(const Eigen::Matrix<double, MEASUREMENT_SIZE_VEL, 1> &p, Eigen::Matrix<double, MEASUREMENT_SIZE_VEL, MEASUREMENT_SIZE_VEL> R_vel  )
{

    Eigen::Matrix<double, MEASUREMENT_SIZE_VEL, State::SIZE> J_H;
    J_H.setZero();
    J_H.block<3,3>(0,3) = Eigen::Matrix3d::Identity(); 
    
    Eigen::Matrix<double, MEASUREMENT_SIZE_VEL, 1> h;
    h = J_H * x.vector();

    /** TRANSLATE VELOCITY FROM MEASUREMENT TO NAV FRAME * / 
    
    /** EKF  */
    //sets the current state to the filter 
    filter_velocity->x = x.vector(); 
    filter_velocity->P = P; 
    
   //innovation steps
    filter_velocity->innovation( p, h, J_H, R_vel ); 

    float threshold; 
    bool reject = true; 
    switch ( reject_pos_threshold ) 
    {
	case THRESHOLD_3D_99: 
	    threshold = chi_square->THRESHOLD_3D_99;
	    break; 
	case THRESHOLD_3D_95: 
	    threshold = chi_square->THRESHOLD_3D_95;
	    break;
	default: 
	    reject = false; 
	    break;
    }

    //test to reject data 
    if ( reject ) 
	reject_pos_observation=chi_square->rejectData3D( filter_velocity->y.segment<2>(3), filter_velocity->S, threshold );
  
  
    if(!reject_pos_observation)
    {
    
	//Kalman Gain
	filter_velocity->gain( J_H ); 
	
	//update teh state 
	filter_velocity->update(J_H ); 

	//get the corrected values 
	x.vector() = filter_velocity->x; 
	P = filter_velocity->P; 

    }
    else
    { 
     
	std::cout<< " Rejected Velocity data " << std::endl; 
    
    }

}

void EKFPosYawBias::correctionScanMatch(const Eigen::Matrix<double, MEASUREMENT_SIZE_SCAN_MATCH, 1> &p, Eigen::Matrix<double, MEASUREMENT_SIZE_SCAN_MATCH, MEASUREMENT_SIZE_SCAN_MATCH> R_scan_match  )
{
    //jacobian of the observation function 
    Eigen::Matrix<double, MEASUREMENT_SIZE_SCAN_MATCH, State::SIZE> J_H 
	= jacobianScanMatchObservation (); 

    //observation function (the observation is linear so the h matrix can be defined as
    Eigen::Matrix<double, MEASUREMENT_SIZE_SCAN_MATCH, 1> h
	= J_H*x.vector(); 


    /** EKF  */
    //sets the current state to the filter 
    filter_scan_match->x = x.vector(); 
    filter_scan_match->P = P; 
    
   //innovation steps
    filter_scan_match->innovation( p, h, J_H, R_scan_match ); 

    float threshold; 
    bool reject = true; 
    switch ( reject_icp_threshold ) 
    {
	case THRESHOLD_3D_99: 
	    threshold = chi_square->THRESHOLD_3D_99;
	    break; 
	case THRESHOLD_3D_95: 
	    threshold = chi_square->THRESHOLD_3D_95;
	    break;
	default: 
	    reject = false; 
	    break;
    }

    //test to reject data 
    if ( reject ) 
      reject_scan_match_observation=chi_square->rejectData3D( filter_scan_match->y.start<3>(), filter_scan_match->S.block<3,3>(0,0), threshold );

    if(!reject_scan_match_observation)
    {
	
	//Kalman Gain
	filter_scan_match->gain( J_H ); 
	
	//update teh state 
	filter_scan_match->update( J_H ); 

	//get the corrected values 
	x.vector() = filter_scan_match->x; 
	P = filter_scan_match->P; 
    
    }
    else
    { 
     
	std::cout<< " Rejected SCAN Match data " << std::endl; 
    
    }

}

/** Calculates the Jacobian of the transition matrix */ 
Eigen::Matrix<double, State::SIZE, State::SIZE> EKFPosYawBias::jacobianF( const Eigen::Vector3d &acc_nav, double &dt )
{
    //derivate of the rotation do to yaw bias 
    Eigen::Matrix<double, 3,3> dR_z;
    dR_z << -sin( x.yaw()(0,0) ), -cos( x.yaw()(0,0) ), 0, cos( x.yaw()(0,0) ), -sin( x.yaw()(0,0) ),0 ,0 ,0 ,0; 

    //jacobian 
    Eigen::Matrix<double, State::SIZE, State::SIZE> J_F; 
    J_F.setIdentity(); 
    J_F.block<3,3>(0,3) = Eigen::Matrix3d::Identity() * dt; 
    J_F.block<3,1>(3,6)
	= dR_z * acc_nav * dt;

	
    std::cout << "Jacobian of f \n" << J_F << std::endl; 
    std::cout << "Jacobian of f \n" 
	<< "1 0 0 dt 0 0 0 \n" 
	<< "0 1 0 0 dt 0 0 \n"  
	<< "0 0 1 0 0 dt 0 \n"  
	<< "0 0 0 1 0 0 R*a*dt \n" 
	<< "0 0 0 0 1 0 R*a*dt \n"  
	<< "0 0 0 0 0 1 R*a*dt \n"  
	<< "0 0 0 0 0 0 1 \n" 
	<<std::endl; 
    
    return J_F;
}

/**jacobian observation model*/ 
Eigen::Matrix<double, EKFPosYawBias::MEASUREMENT_SIZE_SCAN_MATCH, State::SIZE> EKFPosYawBias::jacobianScanMatchObservation ()
{
    Eigen::Matrix<double, MEASUREMENT_SIZE_SCAN_MATCH, State::SIZE>  J_H;
    J_H.setIdentity();

    return J_H; 
}

/**jacobian of the GPS observation*/ 
Eigen::Matrix<double, EKFPosYawBias::MEASUREMENT_SIZE_GPS, State::SIZE> EKFPosYawBias::jacobianGpsObservation (Eigen::Transform3d C_w2gw_without_bias)
{
     //derivate of the rotation do to yaw bias 
    Eigen::Matrix<double, 3,3> dR_z;
    dR_z << -sin( x.yaw()(0,0) ), -cos( x.yaw()(0,0) ), 0,cos( x.yaw()(0,0) ),-sin( x.yaw()(0,0) ), 0, 0, 0, 0; 

    //derivate of the inverse rotation do to yaw bias 
    Eigen::Matrix<double, 3,3> dR_z_inv;
    dR_z_inv << -sin( -x.yaw()(0,0) ), -cos( -x.yaw()(0,0) ), 0, cos( -x.yaw()(0,0) ), -sin( -x.yaw()(0,0) ), 0, 0, 0, 0; 
    
    Eigen::Transform3d R_w2wb;
    R_w2wb = Eigen::AngleAxisd( x.yaw()(0,0), Eigen::Vector3d::UnitZ() ); 
    
    Eigen::Transform3d R_w2gw( R_w2wb * C_w2gw_without_bias * R_w2wb.inverse() );

    Eigen::Matrix<double, MEASUREMENT_SIZE_GPS, State::SIZE>  J_H;
    J_H.setZero();
    
    J_H.block<3,1>(0,0) = R_w2gw * Eigen::Vector3d::UnitX();
    J_H.block<3,1>(0,1) = R_w2gw * Eigen::Vector3d::UnitY();
    J_H.block<3,1>(0,2) = R_w2gw * Eigen::Vector3d::UnitZ();   
    J_H.block<3,1>(0,3) =  
	Eigen::Transform3d( dR_z * C_w2gw_without_bias * R_w2wb.inverse() ) * Eigen::Vector3d( x.vector().start<3>() )   
	+ Eigen::Transform3d( R_w2wb * C_w2gw_without_bias * dR_z_inv ) * Eigen::Vector3d( x.vector().start<3>() );

    return J_H; 
}

/** calculates the process noise in world frame*/ 
void EKFPosYawBias::processNoise(const Eigen::Matrix<double, State::SIZE, State::SIZE> &Q)
{ 
    this->Q = Q; 

    // Rotate from world to world with bias 
    //calculates the rotation from world to world frame corrected by the bias 
    Eigen::Quaterniond R_w2wb;
    R_w2wb=Eigen::AngleAxisd(x.yaw()(0,0), Eigen::Vector3d::UnitZ()); 

    Eigen::Matrix3d R_w2wb_ = Eigen::Matrix3d(R_w2wb);    

    this->Q.block<3,3>(0,0) = R_w2wb_*this->Q.block<3,3>(0,0) *R_w2wb_.transpose();
} 


/** configurarion hook */ 
void EKFPosYawBias::init(const Eigen::Matrix<double, State::SIZE, State::SIZE> &P, const Eigen::Matrix<double,State::SIZE,1> &x)
{
    reject_icp_threshold = THRESHOLD_2D_0; 
    reject_GPS_threshold = THRESHOLD_2D_0; 
    Q.setZero(); 
    this->P = P;
    this->x.vector() = x; 

}

