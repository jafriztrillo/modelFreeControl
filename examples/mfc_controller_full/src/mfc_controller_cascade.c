/**
 * ,---------,       ____  _ __
 * |  ,-^-,  |      / __ )(_) /_______________ _____  ___
 * | (  O  ) |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * | / ,--´  |    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *    +------`   /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2019 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h> 
#include <time.h> 
#include "usec_time.h"

#define DEBUG_MODULE "MFC_CONTROLLER_POS"
#include "debug.h"
// #include "controller.h"
// #include "controller_pid.h"
#include "math3d.h"
#include "mfc_controller_cascade.h"
#include "position_controller.h"
#include "attitude_controller.h"
#include "log.h"
#include "physicalConstants.h"
#include "platform_defaults.h"
#include "num.h"
#include "param.h"
#include "pid.h"

//Controller Gains
const static int CTRL_RATE = 100;
float ATTITUDE_UPDATE_DT = (float)(1.0f/ATTITUDE_RATE);
static float DT_POS = (float)(1.0f/CTRL_RATE);
float state_body_x, state_body_y, state_body_vx, state_body_vy;
static float pwmToThrustA = 0.091492681f;
static float pwmToThrustB = 0.067673604f;
static float rLimit = 30.0f * (float)M_PI / 180.0f;
static float pLimit = 30.0f * (float)M_PI / 180.0f;

static attitude_t attitudeDesired;
static attitude_t attitudeDesiredMFC;
static attitude_t rateDesired;
struct vec att_ddot_prev;
// static float actuatorThrust;
static float actuatorThrustMFC;
static float lpf;
static float theta_des;
static float phi_des;

//Logging Stuff
static float cmd_thrust;
static float cmd_roll;
static float cmd_pitch;
static float cmd_yaw;
static float r_roll;
static float r_pitch;
static float r_yaw;
static float accelz;
float vx, vy;
float x_uc;
float y_uc;

int16_t resetTick;
uint64_t start_time,end_time;

mfc_Variables_t resetStruct = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
};

mfc_Variables_t mfc_x = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 7.0f,
  .kd = 5.0f,
  .beta = 140.0f,
  .flag = 1,
};

mfc_Variables_t mfc_y = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 7.0f,
  .kd = 5.0f,
  .beta = 140.0f,
  .flag = 2.
};

mfc_Variables_t mfc_z = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 38.0f,
  .kd = 11.0f,
  .beta = 40.0f,
  .flag = 3,
};

mfc_Variables_t mfc_roll = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 7.0f,
  .kd = 5.0f,
  .beta = 140.0f,
  .flag = 1,
};

mfc_Variables_t mfc_pitch = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 7.0f,
  .kd = 5.0f,
  .beta = 140.0f,
  .flag = 2.
};

mfc_Variables_t mfc_yaw = {
  .F.x = 0.0f,
  .F.y = 0.0f,
  .F.z = -1e-6f,
  .P.m[0][0] = 1e-6f,
  .P.m[1][1] = 1e-6f,
  .P.m[2][2] = 1e-6f,
  .u_mfc = 0.0f,
  .prev_ydot = 0.0f,
  .prev_yddot = 0.0f,
  .prev_vel_error = 0.0f,
  .prev_pos_error = 0.0f,
  .u_c = 0.0f,
  .kp = 38.0f,
  .kd = 11.0f,
  .beta = 40.0f,
  .flag = 3,
};



// Some Default Functions left by Bitcraze
static float capAngle(float angle) {
  float result = angle;

  while (result > 180.0f) {
    result -= 360.0f;
  }

  while (result < -180.0f) {
    result += 360.0f;
  }

  return result;
}


// Resets
void MFCControllerReset(mfc_Variables_t* mfc){
  mfc->F.x = 0.0f;
  mfc->F.y = 0.0f;
  mfc->F.z = 0.0f;
  mfc->u_c = 0.0f;
  mfc->u_mfc = 0.0f;
}

void positionControllerResetAllParams(){
  MFCControllerReset(&mfc_x);
  MFCControllerReset(&mfc_y);
  MFCControllerReset(&mfc_z);
}

void appMain() {
  DEBUG_PRINT("Waiting for activation ...\n");

  while(1) {
    vTaskDelay(M2T(2000));
    // DEBUG_PRINT("Hello World!\n");
  }
}

void controllerOutOfTreeInit() {
  // Initialize your controller data here...
  // Call the PID controller instead in this example to make it possible to fly
  // controllerPidInit();
  attitudeControllerInit(ATTITUDE_UPDATE_DT);
  positionControllerInit();
}


bool controllerOutOfTreeTest() {
  bool pass = true;

  pass &= attitudeControllerTest();

  return pass;
}

void linearKF(mfc_Variables_t *mfc, float sens_val, float DT){
   // =============== Estimation of F ===============
      //Predeclared Constant Matrices
      float S[3] = {0.0f};
      struct mat33 Q = mdiag(0.1f,0.1f,0.1f);
      static struct vec H = {1.0f, 0.0f, 0.0f};   
      float Harr[3] = {1.0f, 0.0f, 0.0f};
      struct mat33 A = {{{1.0f, DT, DT*DT*0.5f},
                         {0.0f, 1.0f, DT},
                         {0.0f, 0.0f, 1.0f}}};
      struct mat33 I = meye();
      float HPHR = 0.00025f*0.00025f; //This is from Bitcraze themselves. The actual stdDev is a function but it varies from 0.0025:0.003 between 0m and 1m
      struct mat33 P_plus_prev = mfc->P;

      // start_time = usecTimestamp();
      //State Prediction
      S[0] = mfc->F.x + mfc->F.y*DT + 0.5f*mfc->F.z*DT*DT + 0.5f*mfc->beta*DT*DT*mfc->u_mfc;
      S[1] = mfc->F.y + mfc->F.z*DT + mfc->beta*DT*mfc->u_mfc;
      S[2] = mfc->F.z;

      //Covariance Prediction
      struct mat33 AT = mtranspose(A);
      struct mat33 PAT = mmul(P_plus_prev, AT);
      struct mat33 APAT = mmul(A,PAT);
      struct mat33 P_minus = madd(APAT,Q);

      struct vec PHT = mvmul(P_minus,H);
      float PHTarr[3] = {PHT.x, PHT.y, PHT.z};

      //Helper for Scalar Update
      for(int i = 0; i < 3; i++){
          HPHR += Harr[i]*PHTarr[i];
      }
      float try = 1.0f/HPHR;
      
      //Calculate Kalman Gain/Load into Float for Looping
      struct vec Kv = vscl(try, PHT);
      float K[3] = {Kv.x, Kv.y, Kv.z};
      float F_err = sens_val - S[0];
    
      //State Measurement Update
      for(int i = 0; i < 3; i++){
          S[i] = S[i] + K[i]*F_err;
      }

      //Covariance Measurement Update
      struct mat33 KH = mvecmult(Kv,H);
      struct mat33 IKH = msub(I,KH);
      struct mat33 P_plus = mmul(IKH,P_minus);

      //Enforce Covariance Boundaries
      for(int i = 0; i < 3; ++i){
        for(int j = i; j < 3;++j){
          float p = 0.5f*P_plus.m[i][j] + 0.5f*P_plus.m[j][i];
          if(isnan(p) || p > 100.0f){
            P_plus.m[i][j] = P_plus.m[j][i] = 100.0f;
          } else if (i==j && p < 1e-6f){
            P_plus.m[i][j] = P_plus.m[j][i] = 1e-6f;
          } else {
            P_plus.m[i][j] = P_plus.m[j][i] = p;
          }
        }
      }

      mfc->P = P_plus;
      mfc->F.x = S[0];
      mfc->F.y = S[1];
      mfc->F.z = S[2];
}

void controllerOutOfTree(control_t *control, const setpoint_t *setpoint, const sensorData_t *sensors, const state_t *state, const stabilizerStep_t stabilizerStep) {
  control->controlMode = controlModeLegacy;
  //Exactly the Attitude PID controller from Bitcraze
  if (RATE_DO_EXECUTE(ATTITUDE_RATE, stabilizerStep)) {
    // Rate-controled YAW is moving YAW angle setpoint
    if (setpoint->mode.yaw == modeVelocity) {
      attitudeDesired.yaw = capAngle(attitudeDesired.yaw + setpoint->attitudeRate.yaw * ATTITUDE_UPDATE_DT);

      float yawMaxDelta = attitudeControllerGetYawMaxDelta();
      if (yawMaxDelta != 0.0f)
      {
      float delta = capAngle(attitudeDesired.yaw-state->attitude.yaw);
      // keep the yaw setpoint within +/- yawMaxDelta from the current yaw
        if (delta > yawMaxDelta)
        {
          attitudeDesired.yaw = state->attitude.yaw + yawMaxDelta;
        }
        else if (delta < -yawMaxDelta)
        {
          attitudeDesired.yaw = state->attitude.yaw - yawMaxDelta;
        }
      }
    } else if (setpoint->mode.yaw == modeAbs) {
      attitudeDesired.yaw = setpoint->attitude.yaw;
    } else if (setpoint->mode.quat == modeAbs) {
      struct quat setpoint_quat = mkquat(setpoint->attitudeQuaternion.x, setpoint->attitudeQuaternion.y, setpoint->attitudeQuaternion.z, setpoint->attitudeQuaternion.w);
      struct vec rpy = quat2rpy(setpoint_quat);
      attitudeDesired.yaw = degrees(rpy.z);
    }

    attitudeDesired.yaw = capAngle(attitudeDesired.yaw);
  }

  /*
  Uncomment this block to bring back the PID Position Controller
  */
  // This will enforce the controller to only update at 100Hz
  // if (RATE_DO_EXECUTE(POSITION_RATE,stabilizerStep)){
  //   //XY
  //   positionController(&actuatorThrust, &attitudeDesired, setpoint, state);
  // }

  if(RATE_DO_EXECUTE(CTRL_RATE, stabilizerStep)) {
    if (setpoint->mode.z == modeAbs){
      // =============== Internal Controller ==================

      //Need to filter derivitive  terms
      mfc_x.u_c = mfc_x.kp*(state->position.x - setpoint->position.x) + (mfc_x.kd)*(state->velocity.x - setpoint->velocity.x);
      x_uc = mfc_x.u_c;
      mfc_y.u_c = mfc_y.kp*(state->position.y - setpoint->position.y) +  (mfc_y.kd)*(state->velocity.y - setpoint->velocity.y);
      y_uc = mfc_y.u_c;
      mfc_z.u_c = mfc_z.kp*(state->position.z - setpoint->position.z) + (mfc_z.kd)*(state->velocity.z - setpoint->velocity.z);


      // =============== Estimation of F ===============   
      //Prev Errors
      mfc_x.prev_vel_error = state->position.x - setpoint->position.x;
      mfc_y.prev_vel_error = state->position.y - setpoint->position.y;

      linearKF(&mfc_x, state->position.x, DT_POS);
      linearKF(&mfc_y, state->position.y, DT_POS);
      linearKF(&mfc_z, state->position.z, DT_POS);
      

      // ======== Final Controller Calculations ========
      // Compute Acceleration Reference for yd^(v)
      float yd_ddot_z = setpoint->acceleration.z; // Direct use from trajectory generation
      if(fabs(yd_ddot_z) > 2.5){
        yd_ddot_z = mfc_z.prev_yddot;
      }
      else{
        yd_ddot_z = lpf*yd_ddot_z + (1.0f-lpf)*mfc_z.prev_yddot;
      }
    
      // Control Effort to Thrust
      /*
      There was a design decision here to change the original MATLAB code with a double derivitive to use the acceleration setpoint as the property of differential
      flatness gives us the derivitives of our trajectory generation. A weird consequence is the setpoints don't seem to be too smooth which makes the double derivitive
      zero at some points but otherwise this works well. Need to investigate later
      */
      mfc_z.u_mfc = -(mfc_z.F.z - yd_ddot_z + mfc_z.u_c)  / mfc_z.beta;
      mfc_z.u_mfc = constrain((yd_ddot_z - mfc_z.u_c - mfc_z.F.z)  / mfc_z.beta, 0.0f, 3.0f);
      if(setpoint->position.z < 0.06f && state->position.z < 0.06f){actuatorThrustMFC = 0; return;}
      else{
        actuatorThrustMFC = (-pwmToThrustB + sqrtf(pwmToThrustB * pwmToThrustB + 4.0f * pwmToThrustA * mfc_z.u_mfc)) / (2.0f * pwmToThrustA);
        actuatorThrustMFC = constrain(actuatorThrustMFC,0.0f, 0.9f)*UINT16_MAX; //This seems to always saturate, how do we not hit these bounds?
      }

      // ======== XY Control and W2B Transform ========
      float cosyaw = cosf(state->attitude.yaw * (float)M_PI / 180.0f); // Working in radians
      float sinyaw = sinf(state->attitude.yaw * (float)M_PI / 180.0f);

      // Cap the setpoints
      float yd_ddot_x = setpoint->acceleration.x;
      float yd_ddot_y = setpoint->acceleration.y;

      // Compute Virtual Inputs
      mfc_x.u_mfc = (yd_ddot_x - mfc_x.u_c - mfc_x.F.z)  / mfc_x.beta;
      mfc_y.u_mfc = (yd_ddot_y - mfc_y.u_c - mfc_y.F.z)  / mfc_y.beta;
      
      // Transform World 2 Body
      float ux = (sinyaw * mfc_x.u_mfc - cosyaw * mfc_y.u_mfc) / mfc_z.u_mfc;
      float uy = (cosyaw * mfc_x.u_mfc + sinyaw * mfc_y.u_mfc) / mfc_z.u_mfc;

      
      // Assign Desired Roll/Pitch
      phi_des = asinf(ux);
      theta_des = asinf(uy/cosf(phi_des));
      attitudeDesiredMFC.roll = degrees(constrain(phi_des, -rLimit, rLimit));
      attitudeDesiredMFC.pitch  = -degrees(constrain(theta_des, -pLimit, pLimit));
      attitudeDesiredMFC.yaw = 0.0f;

      // ======== ATTITUDE CONTROLLER ========
      // Are these going to be too noisy?
      float stateAttitudeRateRoll = radians(sensors->gyro.x);
      float stateAttitudeRatePitch = -radians(sensors->gyro.y);
      float stateAttitudeRateYaw = radians(sensors->gyro.z);

      struct vec attErr = {state->attitude.roll - attitudeDesiredMFC.roll, state->attitude.pitch - attitudeDesiredMFC.pitch, state->attitude.yaw - setpoint->attitude.yaw};
      struct vec attErr_prev = {attErr.x, attErr.y, attErr.z};
      struct vec attRateErr = {stateAttitudeRateRoll - setpoint->attitudeRate.roll, stateAttitudeRatePitch - setpoint->attitudeRate.pitch, stateAttitudeRateYaw - setpoint->attitudeRate.yaw};
      mfc_roll.u_c = mfc_roll.kp*attErr.x +  mfc_roll.kd*attRateErr.x;
      mfc_pitch.u_c = mfc_pitch.kp*attErr.y +  mfc_pitch.kd*attRateErr.y;
      mfc_yaw.u_c = mfc_yaw.kp*attErr.z +  mfc_yaw.kd*attRateErr.z;

      // =============== Estimation of F ===============
      linearKF(&mfc_roll, state->attitude.roll, ATTITUDE_UPDATE_DT);
      linearKF(&mfc_pitch, state->attitude.pitch, ATTITUDE_UPDATE_DT);     
      linearKF(&mfc_yaw, state->attitude.yaw, ATTITUDE_UPDATE_DT);

      //Can I avoid derivitive kick from the setpoint by using the sensor value instead of the error?
      float yd_dot_r = (attErr.x - attErr_prev.x)/ATTITUDE_UPDATE_DT;
      float yd_dot_p = (attErr.y - attErr_prev.y)/ATTITUDE_UPDATE_DT;

      //Double D of Setpoint & Filtered
      float yd_ddot_r = (yd_dot_r - att_ddot_prev.x)/ATTITUDE_UPDATE_DT;
      float yd_ddot_p = (yd_dot_p - att_ddot_prev.y)/ATTITUDE_UPDATE_DT;
      att_ddot_prev.x = yd_ddot_r;
      att_ddot_prev.y =  yd_ddot_p;

      //Final Control Calculation
      mfc_roll.u_mfc = (yd_ddot_r - mfc_roll.u_c -  mfc_roll.F.z) / mfc_roll.beta;
      mfc_pitch.u_mfc = (yd_ddot_p - mfc_pitch.u_c -  mfc_pitch.F.z) / mfc_pitch.beta;
      mfc_yaw.u_mfc = (0.0f - mfc_yaw.u_c -  mfc_yaw.F.z) / mfc_yaw.beta;

    }
  }
  
  if (RATE_DO_EXECUTE(ATTITUDE_RATE, stabilizerStep)) {
    // Switch between manual and automatic position control
    if (setpoint->mode.z == modeDisable) {
      actuatorThrustMFC = setpoint->thrust;
    }
    if (setpoint->mode.x == modeDisable || setpoint->mode.y == modeDisable) {
      attitudeDesired.roll = setpoint->attitude.roll;
      attitudeDesired.pitch = setpoint->attitude.pitch;
    }

    attitudeControllerCorrectAttitudePID(state->attitude.roll, state->attitude.pitch, state->attitude.yaw,
                                attitudeDesiredMFC.roll, attitudeDesiredMFC.pitch, attitudeDesiredMFC.yaw,
                                &rateDesired.roll, &rateDesired.pitch, &rateDesired.yaw);

    // For roll and pitch, if velocity mode, overwrite rateDesired with the setpoint
    // value. Also reset the PID to avoid error buildup, which can lead to unstable
    // behavior if level mode is engaged later
    if (setpoint->mode.roll == modeVelocity) {
      rateDesired.roll = setpoint->attitudeRate.roll;
      attitudeControllerResetRollAttitudePID();
    }
    if (setpoint->mode.pitch == modeVelocity) {
      rateDesired.pitch = setpoint->attitudeRate.pitch;
      attitudeControllerResetPitchAttitudePID();
    }

    // TODO: Investigate possibility to subtract gyro drift.
    attitudeControllerCorrectRatePID(sensors->gyro.x, -sensors->gyro.y, sensors->gyro.z,
                             rateDesired.roll, rateDesired.pitch, rateDesired.yaw);

    attitudeControllerGetActuatorOutput(&control->roll,
                                        &control->pitch,
                                        &control->yaw);

    control->yaw = -control->yaw;

    cmd_thrust = control->thrust;
    cmd_roll = control->roll;
    cmd_pitch = control->pitch;
    cmd_yaw = control->yaw;
    r_roll = radians(sensors->gyro.x);
    r_pitch = -radians(sensors->gyro.y);
    r_yaw = radians(sensors->gyro.z);
    accelz = sensors->acc.z;
  }
  control->thrust = actuatorThrustMFC;
  // Some reset parameters 
  if (control->thrust == 0)
  {
    control->thrust = 0;
    control->roll = 0;
    control->pitch = 0;
    control->yaw = 0;

    cmd_thrust = control->thrust;
    cmd_roll = control->roll;
    cmd_pitch = control->pitch;
    cmd_yaw = control->yaw;

    attitudeControllerResetAllPID();
    positionControllerResetAllPID();
    positionControllerResetAllParams();

    // Reset the calculated YAW angle for rate control
    attitudeDesired.yaw = state->attitude.yaw;
  }

  // Call the PID controller instead in this example to make it possible to fly
  // controllerPid(control, setpoint, sensors, state, tick);
}


//Logging Parameters
LOG_GROUP_START(mfcLogs)
LOG_ADD(LOG_FLOAT, roll_d, &attitudeDesiredMFC.roll)
LOG_ADD(LOG_FLOAT, pitch_d, &attitudeDesiredMFC.pitch)
LOG_ADD(LOG_FLOAT, vx, &vx)
LOG_ADD(LOG_FLOAT, vy, &vy)
LOG_ADD(LOG_FLOAT, PID_PWM_Thrust, &actuatorThrustMFC)
LOG_ADD(LOG_FLOAT, cmd_thrust, &cmd_thrust)
LOG_ADD(LOG_FLOAT, cmd_roll, &cmd_roll)
LOG_ADD(LOG_FLOAT, cmd_pitch, &cmd_pitch)
LOG_ADD(LOG_FLOAT, cmd_yaw, &cmd_yaw)
LOG_ADD(LOG_FLOAT, roll, &attitudeDesired.roll)
LOG_ADD(LOG_FLOAT, pitch, &attitudeDesired.pitch)
LOG_GROUP_STOP(mfcLogs)

LOG_GROUP_START(mfcX)
LOG_ADD(LOG_FLOAT, d2, &mfc_x.prev_yddot)
LOG_ADD(LOG_FLOAT, F1, &mfc_x.F.x)
LOG_ADD(LOG_FLOAT, F2, &mfc_x.F.y)
LOG_ADD(LOG_FLOAT, F3, &mfc_x.F.z)
LOG_ADD(LOG_FLOAT, u_mfc, &mfc_x.u_mfc)
LOG_ADD(LOG_FLOAT, u_c, &mfc_x.u_c)
LOG_GROUP_STOP(mfcX)

LOG_GROUP_START(mfcY)
LOG_ADD(LOG_FLOAT, d2, &mfc_y.prev_yddot)
LOG_ADD(LOG_FLOAT, F1, &mfc_y.F.x)
LOG_ADD(LOG_FLOAT, F2, &mfc_y.F.y)
LOG_ADD(LOG_FLOAT, F3, &mfc_y.F.z)
LOG_ADD(LOG_FLOAT, u_mfc, &mfc_y.u_mfc)
LOG_ADD(LOG_FLOAT, u_c, &mfc_y.u_c)
LOG_GROUP_STOP(mfcY)

LOG_GROUP_START(mfcZ)
LOG_ADD(LOG_FLOAT, d2, &mfc_z.prev_yddot)
LOG_ADD(LOG_FLOAT, F1, &mfc_z.F.x)
LOG_ADD(LOG_FLOAT, F2, &mfc_z.F.y)
LOG_ADD(LOG_FLOAT, F3, &mfc_z.F.z)
LOG_ADD(LOG_FLOAT, u_mfc, &mfc_z.u_mfc)
LOG_ADD(LOG_FLOAT, PID_PWM_Thrust, &actuatorThrustMFC)
LOG_ADD(LOG_FLOAT, u_c, &mfc_z.u_c)
LOG_ADD(LOG_FLOAT, P00, &mfc_z.P.m[0][0])
LOG_ADD(LOG_FLOAT, P11, &mfc_z.P.m[1][1])
LOG_ADD(LOG_FLOAT, P22, &mfc_z.P.m[2][2])
LOG_GROUP_STOP(mfcZ)

PARAM_GROUP_START(mfcParams)
PARAM_ADD(PARAM_FLOAT, beta, &mfc_z.beta)
PARAM_ADD(PARAM_INT16, CTRL_RATE, &CTRL_RATE)
PARAM_ADD(PARAM_FLOAT, alpha, &lpf)
PARAM_GROUP_STOP(mfcParams)