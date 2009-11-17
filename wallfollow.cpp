/// @file wallfollowing.cpp
/// @author Sebastian Rockel
///
/// @mainpage Robotic Project 2009
///
/// @par Copyright
/// Copyright (C) 2009 Sebastian Rockel.
/// This program can be distributed and modified under the condition mentioning
/// the @ref author.
///
/// @par Description
/// Wall following example for Pioneer 2DX robot.
/// This is part of the robotics students project at Uni Hamburg in 2009.
///
/// @par Sensor fusion
/// The program depends on sensor fusion of 16 sonar rangers and a 240 degree
/// urg laser ranger.
/// @image html PioneerShape.png "Overview about how laser and sonar sensors are fused"
#ifdef DEBUG  // {{{
#include <iostream>
#endif  // }}}
#include <cmath>
#include <libplayerc++/playerc++.h>

using namespace PlayerCc;

// {{{ DEBUG COMPILATION FLAGS
#define DEBUG_NO///< Has to be set if any debug output wanted !!!
#define DEBUG_STATE_NO
#define DEBUG_CRIT_NO
#define DEBUG_SONAR_NO
#define DEBUG_DIST_NO
#define DEBUG_POSITION_NO
#define DEBUG_LASER_NO
#define LASER///< Uses sonar + laser if defined
// }}}

// Parameters {{{
const double VEL       = 0.3;///< Normal_advance_speed in meters per sec.
const double TURN_RATE = 40; ///< Max wall following turnrate in deg per sec.
                             /// Low values: Smoother trajectory but more
                             /// restricted
const double STOP_ROT  = 30; ///< Stop rotation speed.
                             /// Low values increase manauverablility in narrow
                             /// edges, high values let the robot sometimes be
                             /// stuck.
const double WALLFOLLOWDIST = 0.5; ///< Preferred wall following distance.
const double STOP_WALLFOLLOWDIST = 0.2; ///< Stop distance.
const double WALLLOSTDIST  = 1.5; ///< Wall attractor.
const double SHAPE_DIST = 0.3; ///< Min Radius from sensor for robot shape.
// Laserranger
const int BEAMCOUNT = 2; ///< Number of laser beams taken for one average distance measurement
const double DEGPROBEAM   = 0.3515625; ///< 360./1024. in degree per laserbeam
const double LPMAX     = 5.0;  ///< max laser range in meters
const double COS45     = 0.83867056795; ///< Cos(33);
const double INV_COS45 = 1.19236329284; ///< 1/COS45
const double DIAGOFFSET  = 0.1;  ///< Laser to sonar diagonal offset in meters.
const double HORZOFFSET  = 0.15; ///< Laser to sonar horizontal offset in meters.
const double MOUNTOFFSET = 0.1;  ///< Sonar vertical offset at back for laptop mount.
const int LMIN  = 175;/**< LEFT min angle.       */ const int LMAX  = 240; ///< LEFT max angle.
const int LFMIN = 140;/**< LEFTFRONT min angle.  */ const int LFMAX = 175; ///< LEFTFRONT max angle.
const int FMIN  = 100;/**< FRONT min angle.      */ const int FMAX  = 140; ///< FRONT max angle.
const int RFMIN = 65; /**< RIGHTFRONT min angle. */ const int RFMAX = 100; ///< RIGHTFRONT max angle.
const int RMIN  = 0;  /**< RIGHT min angle.      */ const int RMAX  = 65;  ///< RIGHT max angle.
// }}} Parameters

/// This class represents a robot.
/// The robot object provides wall following behaviour.
class Robot {
private:
  PlayerClient    *robot;
#ifdef LASER
  LaserProxy      *lp;
#endif
  SonarProxy      *sp;
  Position2dProxy *pp;
  /// Current behaviour of the robot.
  enum StateType {  // {{{
    WALL_FOLLOWING,
    COLLISION_AVOIDANCE,
    WALL_SEARCHING
  };  // }}}
  /// Used for simple range area distinction.
  enum viewDirectType { // {{{
    LEFT,
    RIGHT,
    FRONT,
    BACK,
    LEFTFRONT,
    RIGHTFRONT,
    LEFTREAR,
    RIGHTREAR,
    ALL
  };  // }}}
  int       robotID;
  double    speed;
  double    turnrate, tmp_turnrate;
  StateType currentState;

  /// Returns the minimum distance of the given arc.
  /// Algorithm calculates the average of BEAMCOUNT beams
  /// to define a minimum value per degree.
  /// @param Range of angle (degrees)
  /// @return Minimum distance in range
  inline double getDistanceLas ( int minAngle, int maxAngle )
  {
    double minDist         = LPMAX; ///< Min distance in the arc.
    const double lMaxAngle = (lp->GetMaxAngle()*180)/M_PI*2; ///< Laser max angle in degree
#ifdef LASER
    if ( !(minAngle<0 || maxAngle<0 || minAngle>=maxAngle || minAngle>=lMaxAngle || maxAngle>lMaxAngle) ) {

      const int minBIndex = (int)(minAngle/DEGPROBEAM); ///< Beam index of min deg.
      const int maxBIndex = (int)(maxAngle/DEGPROBEAM); ///< Beam index of max deg.
      double sumDist     = 0.; ///< Sum of BEAMCOUNT beam's distance.
      double averageDist = LPMAX; ///< Average of BEAMCOUNT beam's distance.

      for (int beamIndex=minBIndex; beamIndex<maxBIndex; beamIndex++) {
        sumDist += lp->GetRange(beamIndex);
        if((beamIndex-minBIndex) % BEAMCOUNT == 1) { ///< On each BEAMCOUNT's beam..
          averageDist = sumDist/BEAMCOUNT; ///< Calculate the average distance.
          sumDist = 0.; ///< Reset sum of beam average distance
          // Calculate the minimum distance for the arc
          averageDist<minDist ? minDist=averageDist : minDist;
        }
#ifdef DEBUG_LASER // {{{
        usleep(1000); ///< Wait x micro seconds for an update.
        std::cout << "beamInd: " << beamIndex
          << "\tsumDist: " << sumDist
          << "\taveDist: " << averageDist
          << "\tminDist: " << minDist << std::endl;
#endif // }}}
      }

    }
#endif
  return minDist;
  }

  /// Returns the minimum distance of the given view direction.
  /// Robot shape shall be considered here by weighted SHAPE_DIST.
  /// Derived arcs, sonars and weights from graphic "PioneerShape.fig".
  /// NOTE: ALL might be slow due recursion, use it only for debugging!
  /// @param Robot view direction
  /// @return Minimum distance of requested view Direction
  inline double getDistance( viewDirectType viewDirection )
  {
    // Scan safety areas for walls
    switch (viewDirection) {
      case LEFT      : return min(getDistanceLas(LMIN,  LMAX) -HORZOFFSET-SHAPE_DIST, min(sp->GetScan(0), sp->GetScan(15))-SHAPE_DIST);
      case RIGHT     : return min(getDistanceLas(RMIN,  RMAX) -HORZOFFSET-SHAPE_DIST, min(sp->GetScan(7), sp->GetScan(8)) -SHAPE_DIST);
      case FRONT     : return min(getDistanceLas(FMIN,  FMAX)            -SHAPE_DIST, min(sp->GetScan(3), sp->GetScan(4)) -SHAPE_DIST);
      case RIGHTFRONT: return min(getDistanceLas(RFMIN, RFMAX)-DIAGOFFSET-SHAPE_DIST, min(sp->GetScan(5), sp->GetScan(6)) -SHAPE_DIST);
      case LEFTFRONT : return min(getDistanceLas(LFMIN, LFMAX)-DIAGOFFSET-SHAPE_DIST, min(sp->GetScan(1), sp->GetScan(2)) -SHAPE_DIST);
      case BACK      : return min(sp->GetScan(11), sp->GetScan(12))-MOUNTOFFSET-SHAPE_DIST; // Sorry, only sonar at rear
      case LEFTREAR  : return min(sp->GetScan(13), sp->GetScan(14))-MOUNTOFFSET-SHAPE_DIST; // Sorry, only sonar at rear
      case RIGHTREAR : return min(sp->GetScan(9) , sp->GetScan(10))-MOUNTOFFSET-SHAPE_DIST; // Sorry, only sonar at rear
      case ALL       : return min(getDistance(LEFT),
                           min(getDistance(RIGHT),
                             min(getDistance(FRONT),
                               min(getDistance(BACK),
                                 min(getDistance(RIGHTFRONT),
                                   min(getDistance(LEFTFRONT),
                                     min(getDistance(LEFTREAR), getDistance(RIGHTREAR) )))))));
      default: return 0.; // Should be recognized if happens
    }
  }

  /// Calculates the turnrate from range measurement and minimum wall follow
  /// distance.
  /// @param Current state of the robot.
  /// @returns Turnrate to follow wall.
  inline double wallfollow( StateType * currentState )
  {
    double turnrate  = 0;
    double DistLFov  = 0;
    double DistL     = 0;
    double DistLRear = 0;
    double DistFront = 0;

    // As long global goal is WF set it by default
    // Will potentially be overridden by higher prior behaviours
    *currentState = WALL_FOLLOWING;

    DistFront = getDistance(FRONT);
    DistLFov  = getDistance(LEFTFRONT);
    DistL     = getDistance(LEFT);
    DistLRear = getDistance(LEFTREAR);

    // do simple (left) wall following
    //do naiv calculus for turnrate; weight dist vector
    turnrate = atan( (COS45*DistLFov - WALLFOLLOWDIST ) * 4 );
#ifdef DEBUG_STATE  // {{{
    std::cout << "WALLFOLLOW" << std::endl;
#endif  // }}}

    // Normalize turnrate
    turnrate = limit(turnrate, -dtor(TURN_RATE), dtor(TURN_RATE));

    // Go straight if no wall is in distance (front, left and left front)
    if (DistLFov  >= WALLLOSTDIST  &&
        DistL     >= WALLLOSTDIST  &&
        DistLRear >= WALLLOSTDIST     )
    {
      turnrate = 0;
      *currentState = WALL_SEARCHING;
#ifdef DEBUG_STATE  // {{{
      std::cout << "WALL_SEARCHING" << std::endl;
#endif  // }}}
    }
    return turnrate;
  }

  inline void getDistanceFOV (double * right_min, double * left_min)
  {
    double distLeftFront  = getDistance(LEFTFRONT);
    double distFront      = getDistance(FRONT);
    double distRightFront = getDistance(RIGHTFRONT);

    *left_min  = (distFront + distLeftFront)  / 2;
    *right_min = (distFront + distRightFront) / 2;
  }

  // Biased by left wall following
  inline void collisionAvoid ( double * turnrate, StateType * currentState)
  {
    double left_min  = LPMAX;
    double right_min = LPMAX;

    // Scan FOV for Walls
    getDistanceFOV(&right_min, &left_min);

    if ((left_min  < STOP_WALLFOLLOWDIST) ||
        (right_min < STOP_WALLFOLLOWDIST)   )
    {
      *currentState = COLLISION_AVOIDANCE;
      // Turn right as long we want left wall following
      *turnrate = -dtor(STOP_ROT);
#ifdef DEBUG_STATE  // {{{
      std::cout << "COLLISION_AVOIDANCE" << std::endl;
#endif  // }}}
    }
  }

  /// @todo Code review
  inline double calcspeed ( void )
  {
    double tmpMinDistFront = min(getDistance(LEFTFRONT), min(getDistance(FRONT), getDistance(RIGHTFRONT)));
    double tmpMinDistBack  = min(getDistance(LEFTREAR), min(getDistance(BACK), getDistance(RIGHTREAR)));
    double speed = VEL;

    if (tmpMinDistFront < WALLFOLLOWDIST) {
      speed = VEL * (tmpMinDistFront/WALLFOLLOWDIST);

      // Do not turn back if there is a wall!
      if (tmpMinDistFront<0 && tmpMinDistBack<0)
        tmpMinDistBack<tmpMinDistFront ? speed=(VEL*tmpMinDistFront)/(tmpMinDistFront+tmpMinDistBack) : speed;
      //speed=(VEL*(tmpMinDistBack-tmpMinDistFront))/SHAPE_DIST;
      //tmpMinDistBack<tmpMinDistFront ? speed=(VEL*(tmpMinDistFront-tmpMinDistBack))/WALLFOLLOWDIST : speed;
    }
    return speed;
  }

  /// Checks if turning the robot is not causing collisions.
  /// Implements more or less a rotation policy which decides depending on
  /// obstacles at the 4 robot edge surounding spots
  /// To not interfere to heavy to overall behaviour turnrate is only inverted (or
  /// set to zero)
  /// @param Turnrate
  /// @todo Code review
  inline void checkrotate (double * turnrate)
  {
    if (*turnrate < 0) { // Right turn
      getDistance(LEFTREAR)<0 ? *turnrate=0 : *turnrate;
      getDistance(RIGHT)   <0 ? *turnrate=0 : *turnrate;
    } else { // Left turn
      getDistance(RIGHTREAR)<0 ? *turnrate=0 : *turnrate;
      getDistance(LEFT)     <0 ? *turnrate=0 : *turnrate;
    }
  }

public:
  Robot(std::string name, int address, int id) {
    robot = new PlayerClient(name, address);
    pp    = new Position2dProxy(robot, id);
#ifdef LASER
    lp    = new LaserProxy(robot, id);
#endif
    sp    = new SonarProxy(robot, id);
    robotID      = id;
    currentState = WALL_FOLLOWING;
    pp->SetMotorEnable(true);
  }

  inline void update ( void ) {
      robot->Read(); ///< This blocks until new data comes; 10Hz by default
  }
  inline void plan ( void ) {
#ifdef DEBUG_SONAR  // {{{
    std::cout << std::endl;
    for(int i=0; i< 16; i++)
      std::cout << "Sonar " << i << ": " << sp->GetScan(i) << std::endl;
#endif  // }}}
    // (Left) Wall following
    turnrate = wallfollow(&currentState);

    // Collision avoidance overrides wall follow turnrate if neccessary!
    collisionAvoid(&turnrate,
                   &currentState);

    // Set speed dependend on the wall distance
    speed = calcspeed();

    // Check if rotating is safe
    checkrotate(&tmp_turnrate);

    // Fusion of the vectors makes a smoother trajectory
    turnrate = (tmp_turnrate + turnrate) / 2;
#ifdef DEBUG_STATE  // {{{
    std::cout << "turnrate/speed/state:\t" << turnrate << "\t" << speed << "\t"
      << currentState << std::endl;
#endif  // }}}
#ifdef DEBUG_DIST // {{{
    std::cout << "Laser (l/lf/f/rf/r/rb/b/lb):\t" << getDistanceLas(LMIN, LMAX)-HORZOFFSET << "\t"
      << getDistanceLas(LFMIN, LFMAX)-DIAGOFFSET  << "\t"
      << getDistanceLas(FMIN,  FMAX)              << "\t"
      << getDistanceLas(RFMIN, RFMAX)-DIAGOFFSET  << "\t"
      << getDistanceLas(RMIN,  RMAX) -HORZOFFSET  << "\t"
      << "XXX"                                    << "\t"
      << "XXX"                                    << "\t"
      << "XXX"                                    << std::endl;
    std::cout << "Sonar (l/lf/f/rf/r/rb/b/lb):\t" << min(sp->GetScan(15),sp->GetScan(0)) << "\t"
      << min(sp->GetScan(1), sp->GetScan(2))              << "\t"
      << min(sp->GetScan(3), sp->GetScan(4))              << "\t"
      << min(sp->GetScan(5), sp->GetScan(6))              << "\t"
      << min(sp->GetScan(7), sp->GetScan(8))              << "\t"
      << min(sp->GetScan(9), sp->GetScan(10))-MOUNTOFFSET << "\t"
      << min(sp->GetScan(11),sp->GetScan(12))-MOUNTOFFSET << "\t"
      << min(sp->GetScan(13),sp->GetScan(14))-MOUNTOFFSET << std::endl;
    std::cout << "Shape (l/lf/f/rf/r/rb/b/lb):\t" << getDistance(LEFT) << "\t"
      << getDistance(LEFTFRONT)  << "\t"
      << getDistance(FRONT)      << "\t"
      << getDistance(RIGHTFRONT) << "\t"
      << getDistance(RIGHT)      << "\t"
      << getDistance(RIGHTREAR)  << "\t"
      << getDistance(BACK)       << "\t"
      << getDistance(LEFTREAR)   << std::endl;
#endif // }}}
#ifdef DEBUG_POSITION // {{{
    std::cout << pp->GetXPos() << "\t" << pp->GetYPos() << "\t" << rtod(pp->GetYaw()) << std::endl;
#endif  // }}}
  }
  // Command the motors
  inline void execute() { pp->SetSpeed(speed, turnrate); }
  void go() {
    this->update();
    this->plan();
    this->execute();
  }
}; // Class Robot

int main ( void ) {
  try {
    Robot r0("localhost", 6665, 0);
    std::cout.precision(2);
    while (true) {
      r0.go();
    }
  } catch (PlayerCc::PlayerError e) {
    std::cerr << e << std::endl; // let's output the error
    return -1;
  }
  return 1;
}
