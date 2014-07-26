#define _GNU_SOURCE
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>


//
// NOTES ON IMAGE ORIENTATION (CAM1 / IMG0)
//
// 
// RAflip = +1
// +DEC = -x
// +RA = -y 
//
// RAflip = -1
// +DEC = +x
// +RA = +y
//


#define SBUFFERSIZE 5000

#define TRACKINGRATELOG_SIZE 10000

static int NOSTOP = 0;

float PIXELSIZE_CAM[4] = {11.37, 10.45, 11.37, 10.45};
// PIXELSIZE_CAM1 11.37 // arcsec per pix - TO BE CONFIRMED
// PIXELSIZE_CAM2 10.45 // arcsec per pix - TO BE CONFIRMED
// PIXELSIZE_CAM3 11.37 // arcsec per pix - TO BE CONFIRMED
// PIXELSIZE_CAM4 10.45 // arcsec per pix - TO BE CONFIRMED

// Cameras can be controlled either through USB or shutter input
// USB inputs active on high
// shutter inputs at active on low
// shutter input needs to be inactive (1) to connect USB


char DIOCHAN_CAM_USB[4] = {'D', 'B', 'C', 'A'};
char DIOCHAN_CAM_TTL[4] = {'I', 'K', 'J', 'L'};
char CAMNAME[4][SBUFFERSIZE] = {"500D", "550D", "600D", "550D"};
int CAMTTLON[4] = {0, 0, 0, 0};
char CAMPORT[4][SBUFFERSIZE];
// as seen from back
// 'A'   550D / T2i / 32GB card / Rokinon / top right
// 'B'   550D / T2i / 16GB card / Canon / bottom left
// 'C'   600D / T3i / 64GB card / Canon / bottom right
// 'D'   500D / T1i / 8GB card / Rokinon / top left

// G : 24 V
// H : 7.4 V

// 'L' // Camera A
// 'K' // Camera B
// 'J' // Camera C
// 'I' // Camera D

// 'E' USB light


#define DIOCHAN_WEBCAM 'F'





#define VERSION "2014-04-30"


int C_ERRNO = 0; // keeps track of C error core
int fdcnt = 0; // keeps track of number of file descriptors


#define SKYCAM_STATUSFILE "/skycam/status.txt" // current status 
// this file is useful to check current status, and to recover after exit
// it is read upon startup
// file contains values for:
// OBSERVINGSTATUS
// MOUNTSTATUS

#define SKYCAM_FORCEOBSERVINGSTATUSFILE "/skycam/forcestatus.txt"
// this file is used to force the system into a mode



#define DATA_DIRECTORY "/skycamdata"
#define DATA_CR2_DIRECTORY2 "CR2"
#define DATA_FITS_DIRECTORY2 "FITS"
#define DATA_JPEG_DIRECTORY2 "JPEG"
#define DATA_HEADER_DIRECTORY2 "CR2info"
#define DATA_WEBCAM_DIRECTORY2 "webcam"

#define DATA_LOG_DIRECTORY "/skycamdata"





static long NBdark = 0; // number of darks taken so far for this night
static long NBdarkMAX = 1; // maximum number of darks to be acquired per night
static long NBflatfield = 0;
static long NBflatfieldMAX = 30; // maximum number of flat field frames to be acquired per night
static float etimeflat = 10.0;
static int isoflat = 2;
static double CAMERABIAS = 1024.0;

// Note: to mount the cloud sensor file:
// sudo mount -t cifs //192.168.1.112/ClarityII winmt
#define CLOUDSENSOR_FILE "/skycam/winmt/ClarityData.txt" 
#define CLOUDSENSOR_FILE1 "/skycam/ClarityData.txt" 


#define BUFS 1024

// MLO coordinates, rad
#define SITE_LONG -2.71531537633  // -155.576 deg
#define SITE_LAT 0.340969461923



// WEATHER SETTINGS
static double DTEMPERATURE_LIMIT = 0.0;    // in C, positive = clear, negative = cloudy
static double CLOUDSENSOR_DTEMPERATURE_LIMIT = 30.0; // in C, positive = clear, negative = cloudy
// LIMITS ARE DEFINED IN /skycam/config/ directory
static double HUMIDITY_LIMIT = 90.0;       // in percent
static double SUNELEV_LIMIT_DEG = -15.0;    // in deg (note: should be neg)
static double SUNELEV_LIMIT_DEG_SAFE = -8.0; // in deg
static long WeatherOK_cnt = 0; // number of consecutive sky clear measurements
static long WeatherOK_cnt_lim = 3; // required number of consecutive sky clear measurements 

static int CLOUDSENSOR_OK = 0;
static char CLOUDSENSOR_DATE[SBUFFERSIZE];
static char CLOUDSENSOR_TIME[SBUFFERSIZE];
static char CLOUDSENSOR_T;
static char CLOUDSENSOR_V;
static float CLOUDSENSOR_SKYT = -999.0;
static float CLOUDSENSOR_AMBT = -999.0;
static float CLOUDSENSOR_SENT = -999.0;
static float CLOUDSENSOR_WIND = -999.0;
static float CLOUDSENSOR_HUM = -999.0;
static float CLOUDSENSOR_DEWPT = -999.0;
static int CLOUDSENSOR_HEA = -1;
static int CLOUDSENSOR_RAIN = -1;
static int CLOUDSENSOR_WET = -1;
static long CLOUDSENSOR_SINCE = -1;
static int CLOUDSENSOR_CLOUD_COND = -1;
static int CLOUDSENSOR_WIND_COND = -1;
static int CLOUDSENSOR_RAIN_COND = -1;
static int CLOUDSENSOR_DAYLIGHT_COND = -1;
static int CLOUDSENSOR_ROOF = -1;

static int MLOWEATHER_OK = 0;
static int MLOWEATHER_YEAR;
static int MLOWEATHER_MONTH;
static int MLOWEATHER_DAY;
static int MLOWEATHER_HOUR;
static int MLOWEATHER_MIN;
static int MLOWEATHER_SEC;
static char MLOWEATHER_DATE[SBUFFERSIZE];
static char MLOWEATHER_TIME[SBUFFERSIZE];
static float MLOWEATHER_WSPD10M;
static float MLOWEATHER_WDIR10M;
static float MLOWEATHER_HUM;
static float MLOWEATHER_HUMprevious;
static float MLOWEATHER_TEMP2M;
static float MLOWEATHER_TEMP10M;
static float MLOWEATHER_ATM;
static float MLOWEATHER_PREC;
static int rval;


static float IMLEVEL_WEBCAM[5];

// TEMPERATURE MEASUREMENT VARIABLES
static double TEMPTABLE_start = -20.0;
static long TEMPTABLE_size = 6000;
static double TEMPTABLE_step = 0.01;
static double temptable[10000];

static int TEMPTABLEOK = 0;
static double TEMPERATURE1;
static double TEMPERATURE2;
static double TEMPERATURE = 15.0;
static double HUMIDITY;

// AC power ahead of UPS
static double ACPOWERSTATUS = 0.0;
static double ACPOWERLIMIT = 4.5;





// STATUS PARAMETERS

static int WEATHER_DARK_STATUS = 0;
// 0: DAYLIGHT, CAMERA SHOULD BE POINTING DOWN
// 1: TWIGHLIGHT, GOOD FOR FLATS
// 2: DARK

static int WEATHER_CLEAR_STATUS = 0;
// 0: WEATHER NOT CLEAR, CAMERA SHOULD BE POINTING DOWN
// 1: WEATHER IS CLEAR, GOOD FOR OBSERVATIONS


static int OBSERVINGSTATUS = 0;
// -1: FAILURE: HOME AND ABORT
// 0: PARK POSITION, DO NOT OBSERVE
// 1: PARK POSITION, TAKE DARKS
// 2: TAKE FLATS
// 3: OBSERVE

static int MOUNTSTATUS = -1;
// -1: UNKNOWN
// 0: OFF
// 1: ON, position unknown or lost
// 2: INITIALIZED, NOT MOVING 
// 3: INITIALIZED, PARKED AT HOME 
// 4: INITIALIZED, SLEWING
// 5: INITIALIZED, TRACKING
// 6: INITIALIZED, FLATFIELD POSITION
// RULES:
// power on mount : ANY  -> 1
// home mount : (>0) -> 3
// slewing : (>1) -> 4   -> 2 when slew complete
// 





// MOTORS AND MOUNT ALIGNMENT PARAMETERS

static long MOUNTSLEWSPEED = 200000; // [steps per sec]
static long MOUNTHOLDCURRENT = 5; // [percent]
static long MOUNTSLEWCURRENT = 80; // [percent]
static int MOUNTRADIR = 0;
static int MOUNTDECDIR = 0;
static long NBstepMountRot = 36070000;  // number of steps for a full rotation
// Note: 120s necessary for full rotation
static double mRAmin = 0.00;    // mininum value for mRA 
static double mRAmax = 0.40;    // maximum value for mRA (default)
static double mRAmax1 = 0.40;   // maximum value for mRA (mDEC adjusted)
static int MOVEDECFIRST = 0; // if = 1, move in DEC, then RA
static int MOVEDECWAIT = 1;
static double mDECmin = 0.00;      // minimum value for mDEC
static double mDECmax = 0.75;   // maximum value for mDEC
static double MOUNT_PARK_RA = 0.0;
static double MOUNT_PARK_DEC = 0.0;

static double POINTING_ACCURACY = 0.0001; // [rad]  20 arcsec
// mDEC axis

long TRACK_MRA_vtrack1;
long TRACK_MRA_vtrack2;
double TRACK_MRA_alpha; // fraction of time that should be spent on vtrack1
double TRACK_MRA_t1; // total time spent so far on vtrack1 [sec]
double TRACK_MRA_t2; // total time spent so far on vtrack2 [sec]
int TRACK_MRA_status;
time_t TRACK_MRA_time_old;

long TRACK_MDEC_vtrack1;
long TRACK_MDEC_vtrack2;
double TRACK_MDEC_alpha; // fraction of time that should be spent on vtrack1
double TRACK_MDEC_t1; // total time spent so far on vtrack1 [sec]
double TRACK_MDEC_t2; // total time spent so far on vtrack2 [sec]
int TRACK_MDEC_status;
time_t TRACK_MDEC_time_old;




// NOTE: 1.0 = 360 deg 
static double mRA_MERIDIAN = 0.25938; //0.240; // distance between RA at meridian and home
static double mDEC_NPOLE   = 0.33389;    //0.32; // distance between DEC=+90 and home 

//      COORD_mDEC =(COORD_DEC/M_PI*180.0-90.0)/360.0 + mDEC_NPOLE;
//      COORD_mRA = (TIME_LST-COORD_RA/M_PI*12.0)/24.0 + 0.25 + mRA_MERIDIAN;
//
// ->   COORD_RA  = TIME_LST - ((COORD_mRA-mRA_MERIDIAN)-0.25)*24.0 [hr]  -> COORD_RA  = .... + mRA_MERIDIAN
// ->   COORD_DEC = (COORD_mDEC - mDEC_NPOLE)*360.0+90.0  [deg]           -> COORD_DEC = .... - mDEC_NPOLE

// true pointing: 
// 6:05 +19 [h]
// 1.5926 +0.3316 [rad]
// 91.25 +19 [deg]

// estimated pointing:
// 84.2733 +24.0000 [deg]
// RAflip = -1

// RA: need to INCREASE COORD_RA by 6.9767 deg = 0.1218 rad = 0.01938 -> increase mRA_MERIDIAN
// DEC: need to DECREASE COORD_DEC by 5 deg = 0.087 rad     = 0.01389 -> increase mDEC_NPOLE

static unsigned int TIME_WAIT_TRACKING_ENGAGE = 2;  // waiting period between start of tracking command and beginning of exposure

static long MOUNT_NBMOVE; // number of moves since last homing
static int GETPOSMOUNT = 1;
time_t T_TRACKING; // time since tracking started
int RAflip; // -1 or 1

// mRA mDEC -> RA DEC
//
// RAflip = -1 if mDEC < mDEC_NPOLE, (mDEC-mDEC_NPOLE<0)
// RAflip = +1 if mDEC > mDEC_NPOLE, (mDEC-mDEC_NPOLE>0)
//
// RA  [hr]  = 24.0*(-(mRA-mRA_MERIDIAN)-0.25*RAflip)+LST
// DEC [deg] = 90.0-360.0*fabs(mDEC-mDEC_NPOLE)
// 
//
// if RAflip = +1
// mDEC = (90.0-DEC[deg])/360.0+mDEC_NPOLE
// 
// if RAflip = -1
// mDEC = -(90.0-DEC[deg])/360.0+mDEC_NPOLE
// 
//
// mRA = (LST[hr]-RA[hr])/24.0-0.25*RAflip+mRA_MERIDIAN
// 



// CURRENT POSITION OF MOUNT
double pos_mountra = 0.0; // current position (0-1), mount RA
double pos_mountdec = 0.0; // current position (0-1), mount DEC
double LAST_GETPOSMOUNTRA = 0.0; // last read from get posmount
double LAST_GETPOSMOUNTDEC = 0.0; // last read from get posmount
double pos_RA = 0.0; // sky RA radian
double pos_DEC = 0.0; // sky DEC radian
double pos_ALT = 0.0; // sky alt radian
double pos_AZ = 0.0; // sky az radian
int initpos = 0; // goes to 1 after first pointing
double TRACKrate_RA = 0.0;  // steps per second
double TRACKrate_DEC = 0.0; // steps per second
static double TIME_UTC_TRACK_STOP; // from 0.0 to 24.0, time at which tracking will stop

float TRACKINGRATELOG_mRA[TRACKINGRATELOG_SIZE];
float TRACKINGRATELOG_mDEC[TRACKINGRATELOG_SIZE];
int TRACKINGRATELOG_RAflip[TRACKINGRATELOG_SIZE];
float TRACKINGRATELOG_TrateRA[TRACKINGRATELOG_SIZE]; // arcsec per sec
float TRACKINGRATELOG_TrateDEC[TRACKINGRATELOG_SIZE];
long TRACKINGRATELOG_NBpt;

long CAM_TO_LOAD[4] = {0, 0, 0, 0};


// OBSERVING PLAN
static double observation_minelevation = 20.0; // in degree, can be overridden by ./config/_minelev.txt
static double observation_minelevation_random = 40.0; // in degree
static double DISTMOON_LIMIT_DEG = 20.0; // can be overridden by ./config/_moondist_limit.txt
static double MAXmotion = 180.0; // in degree
static double MINmotion = -1.0; // in degree (if disabled, set to negative value)
static double ETIME_MIN_SEC = 10.0; // minimum exposure time, can be overridden by ./config/_etimemin.txt
static double ETIME_MAX_SEC = 600.0; // maximum exposure time, can be overridden by ./config/_etimemax.txt

int PLAN_MODE = 1;
// 1: random
//    each pointing is random position on the sky





// COMMUNICATION
int mountUSBportNB = -1;
int dioUSBportNB = -1;
int aioUSBportNB = -1;
int mountfd = -2; /* File descriptor for the port controlling the mount */
int diofd = -2; /* File descriptor for the port controlling the DIO board */
int aiofd = -2; /* File descriptor for the port controlling the AIO board */
// COMMUNICATION SETTINGS 
double tcommdelay = 0.02; // time in second between consecutive port read
double readtimeout = 0.2; // read timeout (sec)

long readbufsize = 1024;
char READBUF[1024]; // read buffer



// DIO VARIABLES
static int dio_inA = -1;
static int dio_inB = -1;

// MOUNT LIMIT SWITCHES
static int lswra0 = -1;
static int lswra1 = -1;
static int lswra2 = -1;
static int lswra3 = -1;
static int lswdec0 = -1;
static int lswdec1 = -1;
static int lswdec2 = -1;
static int lswdec3 = -1;



// ASTRONOMICAL VARIABLES
static double TIME_LST; // from 0.0 to 24.0

static int TIME_UTC_YR = -1;
static int TIME_UTC_MON = -1;
static int TIME_UTC_DAY = -1;
static double TTIME_UTC = 0.0; // from 0.0 to 24.0


static double MOON_RA; // radian
static double MOON_DEC; // radian
static double MOON_ALT; // radian
static double MOON_AZ; // radian
static double MOON_MAGN;

static double SUN_RA; // radian
static double SUN_DEC; // radian
static double SUN_ALT; // radian
static double SUN_AZ; // radian

static double COORD_ALT; // radian
static double COORD_AZ; // radian

static double COORD_RA; // radian
static double COORD_DEC; // radian

static double COORD_mRA; // mount RA axis
static double COORD_mDEC; // mount DEC axis

static double DISTMOON; // distance to Moon
static double DISTMOVE; // distance to previous position
static long cntcoordOK = 0; // number of attempts to find suitable pointing





// IMAGES, CAMERA
static int CAMMODE[4] = {0,0,0,0}; // 0=inactive, 1=USB, 2=TTL
static long IMGindex[4] = {0,0,0,0};


// image file header
char IMGHEADER_TARGETDESCRIPTION[SBUFFERSIZE];
static char   IMGHEADER_IMTYPE[SBUFFERSIZE]; // image type (object, dark or flat)
static double IMGHEADER_UT_START; // hr
static double IMGHEADER_RA; // deg
static double IMGHEADER_DEC; // deg
static double IMGHEADER_ALT; // deg
static double IMGHEADER_AZ; // deg
static double IMGHEADER_MOUNTRA; // 0-1
static double IMGHEADER_MOUNTDEC; // 0-1
static double IMGHEADER_MOUNT_TRACKrate_RA; // steps / sec
static double IMGHEADER_MOUNT_TRACKrate_DEC; // steps / sec
static int    IMGHEADER_MOUNT_RAflip; // -1 or 1
static double IMGHEADER_TEMPERATURE1; // C
static double IMGHEADER_TEMPERATURE2; // C
static double IMGHEADER_HUMIDITY; // %
static double IMGHEADER_SUN_ALT; // deg
static double IMGHEADER_SUN_AZ; // deg
static double IMGHEADER_MOON_ALT; // deg
static double IMGHEADER_MOON_AZ; // deg
static double IMGHEADER_MOON_MAGN;
static int    IMGHEADER_WEATHERDARKSTATUS;
static int    IMGHEADER_WEATHERCLEARSTATUS;
static int    IMGHEADER_OBSERVINGSTATUS;

static double IMGHEADER_SHUTTER; // sec
static int    IMGHEADER_ISO;

static double IMGHEADER_PERCENTILE01;
static double IMGHEADER_PERCENTILE05;
static double IMGHEADER_PERCENTILE50;
static double IMGHEADER_PERCENTILE95;
static double IMGHEADER_PERCENTILE99;

static double IMGHEADER_RED_PERCENTILE01;
static double IMGHEADER_RED_PERCENTILE05;
static double IMGHEADER_RED_PERCENTILE10;
static double IMGHEADER_RED_PERCENTILE20;
static double IMGHEADER_RED_PERCENTILE50;
static double IMGHEADER_RED_PERCENTILE80;
static double IMGHEADER_RED_PERCENTILE90;
static double IMGHEADER_RED_PERCENTILE95;
static double IMGHEADER_RED_PERCENTILE99;
static double IMGHEADER_RED_PERCENTILE995;
static double IMGHEADER_RED_PERCENTILE998;
static double IMGHEADER_RED_PERCENTILE999;

static double IMGHEADER_GREEN1_PERCENTILE01;
static double IMGHEADER_GREEN1_PERCENTILE05;
static double IMGHEADER_GREEN1_PERCENTILE10;
static double IMGHEADER_GREEN1_PERCENTILE20;
static double IMGHEADER_GREEN1_PERCENTILE50;
static double IMGHEADER_GREEN1_PERCENTILE80;
static double IMGHEADER_GREEN1_PERCENTILE90;
static double IMGHEADER_GREEN1_PERCENTILE95;
static double IMGHEADER_GREEN1_PERCENTILE99;
static double IMGHEADER_GREEN1_PERCENTILE995;
static double IMGHEADER_GREEN1_PERCENTILE998;
static double IMGHEADER_GREEN1_PERCENTILE999;

static double IMGHEADER_GREEN2_PERCENTILE01;
static double IMGHEADER_GREEN2_PERCENTILE05;
static double IMGHEADER_GREEN2_PERCENTILE10;
static double IMGHEADER_GREEN2_PERCENTILE20;
static double IMGHEADER_GREEN2_PERCENTILE50;
static double IMGHEADER_GREEN2_PERCENTILE80;
static double IMGHEADER_GREEN2_PERCENTILE90;
static double IMGHEADER_GREEN2_PERCENTILE95;
static double IMGHEADER_GREEN2_PERCENTILE99;
static double IMGHEADER_GREEN2_PERCENTILE995;
static double IMGHEADER_GREEN2_PERCENTILE998;
static double IMGHEADER_GREEN2_PERCENTILE999;

static double IMGHEADER_BLUE_PERCENTILE01;
static double IMGHEADER_BLUE_PERCENTILE05;
static double IMGHEADER_BLUE_PERCENTILE10;
static double IMGHEADER_BLUE_PERCENTILE20;
static double IMGHEADER_BLUE_PERCENTILE50;
static double IMGHEADER_BLUE_PERCENTILE80;
static double IMGHEADER_BLUE_PERCENTILE90;
static double IMGHEADER_BLUE_PERCENTILE95;
static double IMGHEADER_BLUE_PERCENTILE99;
static double IMGHEADER_BLUE_PERCENTILE995;
static double IMGHEADER_BLUE_PERCENTILE998;
static double IMGHEADER_BLUE_PERCENTILE999;



/*
static double IMGHEADER_NOISE_RED_PERCENTILE01;
static double IMGHEADER_NOISE_RED_PERCENTILE05;
static double IMGHEADER_NOISE_RED_PERCENTILE10;
static double IMGHEADER_NOISE_RED_PERCENTILE20;
static double IMGHEADER_NOISE_RED_PERCENTILE50;
static double IMGHEADER_NOISE_RED_PERCENTILE80;
static double IMGHEADER_NOISE_RED_PERCENTILE90;
static double IMGHEADER_NOISE_RED_PERCENTILE95;
static double IMGHEADER_NOISE_RED_PERCENTILE99;
static double IMGHEADER_NOISE_RED_PERCENTILE995;
static double IMGHEADER_NOISE_RED_PERCENTILE998;
static double IMGHEADER_NOISE_RED_PERCENTILE999;

static double IMGHEADER_NOISE_GREEN1_PERCENTILE01;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE05;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE10;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE20;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE50;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE80;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE90;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE95;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE99;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE995;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE998;
static double IMGHEADER_NOISE_GREEN1_PERCENTILE999;

static double IMGHEADER_NOISE_GREEN2_PERCENTILE01;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE05;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE10;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE20;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE50;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE80;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE90;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE95;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE99;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE995;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE998;
static double IMGHEADER_NOISE_GREEN2_PERCENTILE999;

static double IMGHEADER_NOISE_BLUE_PERCENTILE01;
static double IMGHEADER_NOISE_BLUE_PERCENTILE05;
static double IMGHEADER_NOISE_BLUE_PERCENTILE10;
static double IMGHEADER_NOISE_BLUE_PERCENTILE20;
static double IMGHEADER_NOISE_BLUE_PERCENTILE50;
static double IMGHEADER_NOISE_BLUE_PERCENTILE80;
static double IMGHEADER_NOISE_BLUE_PERCENTILE90;
static double IMGHEADER_NOISE_BLUE_PERCENTILE95;
static double IMGHEADER_NOISE_BLUE_PERCENTILE99;
static double IMGHEADER_NOISE_BLUE_PERCENTILE995;
static double IMGHEADER_NOISE_BLUE_PERCENTILE998;
static double IMGHEADER_NOISE_BLUE_PERCENTILE999;


static double IMGHEADER_NOISE_RED;
static double IMGHEADER_NOISE_GREEN1;
static double IMGHEADER_NOISE_GREEN2;
static double IMGHEADER_NOISE_BLUE;
*/




/*
static double IMGHEADER_ASTROM_FITQUALITY;
static double IMGHEADER_ASTROM_RA;
static double IMGHEADER_ASTROM_DEC;
static double IMGHEADER_ASTROM_PA;
static long IMGHEADER_ASTROM_NBstarImage;
static double IMGHEADER_ASTROM_PIXSCALE;
static double IMGHEADER_ASTROM_DISTX_Y;
static double IMGHEADER_ASTROM_DISTY_Y;
static double IMGHEADER_ASTROM_DISTX_XX;
static double IMGHEADER_ASTROM_DISTX_XY;
static double IMGHEADER_ASTROM_DISTX_YY;
static double IMGHEADER_ASTROM_DISTY_XX;
static double IMGHEADER_ASTROM_DISTY_XY;
static double IMGHEADER_ASTROM_DISTY_YY;
static double IMGHEADER_ASTROM_DISTX_XXX;
static double IMGHEADER_ASTROM_DISTX_XXY;
static double IMGHEADER_ASTROM_DISTX_XYY;
static double IMGHEADER_ASTROM_DISTX_YYY;
static double IMGHEADER_ASTROM_DISTY_XXX;
static double IMGHEADER_ASTROM_DISTY_XXY;
static double IMGHEADER_ASTROM_DISTY_XYY;
static double IMGHEADER_ASTROM_DISTY_YYY;
static double IMGHEADER_ASTROM_DISTX_XXXX;
static double IMGHEADER_ASTROM_DISTX_XXXY;
static double IMGHEADER_ASTROM_DISTX_XXYY;
static double IMGHEADER_ASTROM_DISTX_XYYY;
static double IMGHEADER_ASTROM_DISTX_YYYY;
static double IMGHEADER_ASTROM_DISTY_XXXX;
static double IMGHEADER_ASTROM_DISTY_XXXY;
static double IMGHEADER_ASTROM_DISTY_XXYY;
static double IMGHEADER_ASTROM_DISTY_XYYY;
static double IMGHEADER_ASTROM_DISTY_YYYY;
static long IMGHEADER_ASTROM_XSIZE;
static long IMGHEADER_ASTROM_YSIZE;
*/



float IMAGE_percB50 = -1.0;
float IMAGE_perc90 = -1.0;


static int CAMERA_MODE_ISO = 1;
// 1 : 100
// 2 : 200
// 3 : 400
// 4 : 800
// 5 : 1600









// GUIDING TABLE
// this table gets filled up when consecutive images are acquired over the same field

#define GTsize 1000   // max # of entries in table
long GTtablenb = 0; // table number 
char GTtablename[SBUFFERSIZE];
long GTindex = 0; // current index in table

long GT_imnb[GTsize]; // image number (same as filename)

double GT_posmountRA[GTsize];
double GT_posmountDEC[GTsize];

double GT_tstart[GTsize]; // sec since beginning of sequence
double GT_etime[GTsize]; // sec

double GT_dRA[GTsize]; // RA offset compared to reference, sky coordinate and direction
double GT_dDEC[GTsize]; // DEC offset compated to reference, sky coordinate and direction

double GT_RArate[GTsize]; // tracking rate during exposure
double GT_DECrate[GTsize];

double GT_RAoffset[GTsize]; // offset applied before the exposure
double GT_DECoffset[GTsize];

double GT_RArate_meas[GTsize]; // measured tracking rate during exposure
double GT_DECrate_meas[GTsize];


double GTlim = 0.01;
double GTrlim = 0.01;

struct timespec tnowts;







static char line[SBUFFERSIZE];



// --------------------------- STANDARD FUNCTIONS ---------------------------

int read_parameter(char *filename, char *keyword, char *content);
void qs(double *array, long left, long right);
void quick_sort(double *array, long count);
int SKYCAM_checkstop();
double ran1();
int SKYCAM_log();

int SKYCAM_write_STATUS();
int SKYCAM_read_STATUS();


// --------------------------- ASTRONOMICAL ---------------------------

double compute_UTCnow();
double compute_LST(double site_long, int year, int month, double day);
double compute_LSTnow();
int compute_coordinates_from_RA_DEC(double RA, double DEC);
int compute_coordinates_from_ALT_AZ(double ALT, double AZ);
double get_Moon_pos();
double get_Sun_pos();

// --------------------------- COMMUNICATION ---------------------------

int open_ttyUSBport(int portnb);
int read_ttyUSBport(int fd);

int openmountfd();
int closemountfd();
int read_ttyUSBport_mount(int fd);

int charhex_to_int(char c);
char int_to_charhex(int value);
int scanttyUSBports();


// --------------------------- LOW LEVEL COMMUNICATION & TEST ---------------------------

int DIO_power_Cams(int value);
int DIO_power_webcam(int value);
int DIO_power_Mount(int value);
int dio_limitswitch_status(char limitswitchchar);
int dio_setdigital_out(char limitswitchchar, int val);
int aio_analogchannel_init(char channelchar, int mode, int dec);
float aio_analogchannel_value(char channelchar);

int SKYCAM_command_readDIO();



// --------------------------- MOUNT ---------------------------

int mount_command(char *command, int log);

int compute_mount_radec_from_radec();
int goto_posmount_radec(double mrapos, double mdecpos);

double track_mra(double mrarate);
double track_update_mra();
double track_mdec(double mdecsrate);
double track_update_mdec();
double track_mra_mdec(double mrarate, double mdecrate);
int track_mra_mdec_time(double mrarate, double mdecrate, double mratime, double mdectime);
//int set_posmount_radec(double mrapos, double mdecpos);


// THIS FUNCTION OK
int get_posmountradec();
int get_mountswitches();
int test_posmount(double mrapos, double mdecpos, int quiet);

int SKYCAM_command_homeRA();
int SKYCAM_command_homeDEC();
double SKYCAM_command_movpos(double v1, double v2);

double SKYCAM_command_tracksidN();
int SKYCAM_command_MOUNTSTOP();
int SKYCAM_command_home();
int SKYCAM_command_mountinit();
int SKYCAM_command_park();
int SKYCAM_command_mvposFLATFIELDpos();
int SKYCAM_ABORT();
int SKYCAM_SLEEP();



// --------------------------- TEMPERATURE, HUMIDITY, LIGHT LEVEL ---------------------------

int make_temperature_V_table();
float VtoTemp(float v);

int SKYCAM_command_gettemp12(long avcnt);
int SKYCAM_command_gethumidity(long avcnt);
int SKYCAM_command_getwebcamlum(int camNb);



// ---------------------------- CAMERA --------------------------------------------

int SKYCAM_command_setCAM_mode(int cam, int mode);
int SKYCAM_command_cam_listFILES(int cam);
int SKYCAM_command_cam_loadFILES(int cam);
int SKYCAM_command_archiveCAM(int cam);
int SKYCAM_command_cam_rmFILES(int cam);
int SKYCAM_command_camSetISO(int cam, int ISOmode);

// ---------------------------- High level SKYCAM commands ---------------------------

int SKYCAM_testcoord();
int SKYCAM_command_observingstatus();
int SKYCAM_command_init();
int SKYCAM_print_imgheader_basic(char *fname);
int SKYCAM_write_imgheader();
int SKYCAM_read_imgheader(int timeutc_yr, int timeutc_mon, int timeutc_day, int camera, long index, int IMGHEADERupdate);
int SKYCAM_command_polaralign(double rotrange_deg, int isomode, double etime);
int SKYCAM_command_ACQUIREimage(double etime, int isomode);

long SKYCAM_loadTrackingLog(char *fname);
long SKYCAM_computeTrackingRate_sidN( double mRA, double mDEC, int flip);

int main(int argc, char **argv);


int SKYCAM_exit();






// --------------------------- STANDARD FUNCTIONS ---------------------------

int printERROR(const char *file, const char *func, int line, char *errmessage)
{
  char buff[SBUFFERSIZE] = "";

  fprintf(stderr,"%c[%d;%dm ERROR [ FILE: %s   FUNCTION: %s   LINE: %d ]  %c[%d;m\n", (char) 27, 1, 31, file, func, line, (char) 27, 0);
  if(C_ERRNO != 0)
    {
      if( strerror_r( errno, buff, SBUFFERSIZE ) == 0 ) {
	fprintf(stderr,"C Error: %s\n", buff );
      }
      else
	fprintf(stderr,"Unknown C Error\n");
    }
  else
    fprintf(stderr,"No C error (errno = 0)\n");
   
  fprintf(stderr,"%c[%d;%dm %s  %c[%d;m\n", (char) 27, 1, 31, errmessage, (char) 27, 0);

  C_ERRNO = 0;
  
  return(0);
}



int read_parameter(char *filename, char *keyword, char *content)
{
  FILE *fp;
  char keyw[SBUFFERSIZE];
  char cont[SBUFFERSIZE];
  int read;
  long n;


  read = 0;
  if((fp=fopen(filename,"r"))==NULL)
    {
      printf("Cannot open file \"%s\"\n",filename);
      exit(0);
    }
  
  strcpy(content,"---");
  while(fgets(line,SBUFFERSIZE,fp)!=NULL)
    {
      //  sscanf(line,"%s %s",keyw,cont);
      strncpy(keyw,line,strlen(keyword));
     
      if(strncmp(keyw,keyword,strlen(keyword))==0)
	{
	  for(n=0;n<(long) strlen(keyword);n++)
	    line[n]=' ';
	  n = sscanf(line,"%s",cont);
	  if(n!=1)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"sscanf() error");
	      printf("filename = %s\n",filename);
	      printf("keyword = %s\n",keyword);
	      printf("n = %ld\n",n);
	      snprintf(cont,1,"-");
	    }
	  strcpy(content, cont);
	  read = 1;
	}
      //   printf("[%s] KEYWORD : \"%s\"   CONTENT : \"%s\"\n",keyword,keyw,cont);
    }
  if(read==0)
    {
      printf("parameter \"%s\" does not exist in file \"%s\"\n",keyword,filename);
      snprintf(cont,1,"-");
      strcpy(content, cont);
      //      exit(0);
    }

  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  
  return(read);
}


void qs(double *array, long left, long right)
{
  register long i,j;
  double x,y;
  
  i = left; j = right;
  x = array[(left+right)/2];
  
  do {
    while(array[i]<x && i<right) i++;
    while(x<array[j] && j>left) j--;
    
    if(i<=j) {
      y = array[i];
      array[i] = array[j];
      array[j] = y;
      i++; j--;
    }
  } while(i<=j);
  
  if(left<j) qs(array,left,j);
  if(i<right) qs(array,i,right);
}

void quick_sort(double *array, long count)
{
  qs(array,0,count-1);
}






// if file STOP or STOP1 exists, quit
int SKYCAM_checkstop()
{
  FILE *fp = NULL;
  time_t ltime;
  struct tm *utctimenow;
  double hour;
  int hr,min,sec;
  char fname[SBUFFERSIZE];
  int n;

  fp = fopen("/skycam/STOP","r");
  if(fp != NULL)
    {
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      system("rm /skycam/STOP");

      SKYCAM_command_MOUNTSTOP();      
      time( &ltime );
      utctimenow = gmtime( &ltime );
      TIME_UTC_YR = 1900+(int) (utctimenow->tm_year);
      TIME_UTC_MON = 1+(int) (utctimenow->tm_mon);
      TIME_UTC_DAY = (int) (utctimenow->tm_mday);
      hour = ((1.0*utctimenow->tm_sec/60.0+1.0*utctimenow->tm_min)/60.0)+1.0*utctimenow->tm_hour;
      TTIME_UTC = hour;

      hr = (int) TTIME_UTC;
      min = (int) (60.0*(TTIME_UTC-hr));
      sec = (int) (3600.0*(TTIME_UTC-1.0*hr-1.0/60.0*min));

      n = snprintf(fname,SBUFFERSIZE,"%s/%04d-%02d-%02d-skycam.log",DATA_LOG_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      if((fp=fopen(fname,"a"))==NULL)
	{
	  printf("Cannot open file \"%s\"\n",fname);
	  exit(0);
	}
      fprintf(fp,"%04d-%02d-%02d-%02d:%02d:%02d  ------- STOP -------\n",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,hr,min,sec);
      printf("%04d-%02d-%02d-%02d:%02d:%02d   --------- STOP -------\n",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,hr,min,sec);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
      SKYCAM_exit();
    }

  return(0);
}


double ran1()
{
  double value;

  value = 1.0/RAND_MAX*rand();

  return(value);
}


int SKYCAM_log()
{
  FILE *fp = NULL;
  char fname[SBUFFERSIZE];
  int hr;
  int min;
  int sec;
  char command[SBUFFERSIZE];
  int n;

  compute_UTCnow();
  hr = (int) TTIME_UTC;
  min = (int) (60.0*(TTIME_UTC-hr));
  sec = (int) (3600.0*(TTIME_UTC-1.0*hr-1.0/60.0*min));

  n = snprintf(command, SBUFFERSIZE, "mkdir -p %s", DATA_LOG_DIRECTORY);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  printf("EXECUTING SYSTEM COMMAND: %s\n", command);
  system(command);
      
  n = snprintf(fname,SBUFFERSIZE,"%s/%04d-%02d-%02d-skycam.log",DATA_LOG_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }

  if((fp = fopen(fname,"a"))==NULL)
    {
      printf("ERROR: cannot write to file \"%s\"\n",fname);
      //      exit(0);
    }
  else
    {
      fprintf(fp,"%04d-%02d-%02d-%02d:%02d:%02d   %s",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,hr,min,sec,line);
      printf("%04d-%02d-%02d-%02d:%02d:%02d   %s",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,hr,min,sec,line);
      fflush(stdout);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
    }
  fp = NULL;

  return 0;
}


int SKYCAM_write_STATUS()
{
  FILE *fp = NULL;
  int n;
  
  if((fp = fopen(SKYCAM_STATUSFILE,"w"))==NULL)
    {      
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: CANNOT CREATE FILE \"%s\"\n",SKYCAM_STATUSFILE);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
      exit(0);
    }
  else
    {
      fprintf(fp,"%04d-%02d-%02d\n",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY);
      fprintf(fp,"OBSERVINGSTATUS  %d\n",OBSERVINGSTATUS);
      fprintf(fp,"MOUNTSTATUS      %d\n",MOUNTSTATUS);
      
      
      fprintf(fp,"CAMMODE[0]         %d\n", CAMMODE[0]);
      fprintf(fp,"CAMMODE[1]         %d\n", CAMMODE[1]);
      fprintf(fp,"CAMMODE[2]         %d\n", CAMMODE[2]);
      fprintf(fp,"CAMMODE[3]         %d\n", CAMMODE[3]);

      fprintf(fp,"CAMPORT[0]         %s\n", CAMPORT[0]);
      fprintf(fp,"CAMPORT[1]         %s\n", CAMPORT[1]);
      fprintf(fp,"CAMPORT[2]         %s\n", CAMPORT[2]);
      fprintf(fp,"CAMPORT[3]         %s\n", CAMPORT[3]);

      fprintf(fp,"IMGINDEX[0]        %ld\n", IMGindex[0]);
      fprintf(fp,"IMGINDEX[1]        %ld\n", IMGindex[1]);
      fprintf(fp,"IMGINDEX[2]        %ld\n", IMGindex[2]);
      fprintf(fp,"IMGINDEX[3]        %ld\n", IMGindex[3]);

      fprintf(fp,"CAM_TO_LOAD[0]     %ld\n", CAM_TO_LOAD[0]);
      fprintf(fp,"CAM_TO_LOAD[1]     %ld\n", CAM_TO_LOAD[1]);
      fprintf(fp,"CAM_TO_LOAD[2]     %ld\n", CAM_TO_LOAD[2]);
      fprintf(fp,"CAM_TO_LOAD[3]     %ld\n", CAM_TO_LOAD[3]);

      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
    }
  fp = NULL;

  return(0);
}


int SKYCAM_read_STATUS()
{
  FILE *fp = NULL;
  int n;
  int TIME_UTC_YR_status;
  int TIME_UTC_MON_status;
  int TIME_UTC_DAY_status;
  int i;

  long IMGindex_tmp[4] = {-1, -1, -1, -1};

  if((fp = fopen(SKYCAM_STATUSFILE,"r"))==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"WARNING: FILE \"%s\" DOES NOT EXIST\n",SKYCAM_STATUSFILE);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }
  else
    {
      fscanf(fp,"%04d-%02d-%02d\n",&TIME_UTC_YR_status,&TIME_UTC_MON_status,&TIME_UTC_DAY_status);
      fscanf(fp,"OBSERVINGSTATUS  %d\n",&OBSERVINGSTATUS);
      fscanf(fp,"MOUNTSTATUS      %d\n",&MOUNTSTATUS);

      fscanf(fp,"CAMMODE[0]         %d\n", &CAMMODE[0]);
      fscanf(fp,"CAMMODE[1]         %d\n", &CAMMODE[1]);
      fscanf(fp,"CAMMODE[2]         %d\n", &CAMMODE[2]);
      fscanf(fp,"CAMMODE[3]         %d\n", &CAMMODE[3]);

      fscanf(fp,"CAMPORT[0]         %s\n", CAMPORT[0]);
      fscanf(fp,"CAMPORT[1]         %s\n", CAMPORT[1]);
      fscanf(fp,"CAMPORT[2]         %s\n", CAMPORT[2]);
      fscanf(fp,"CAMPORT[3]         %s\n", CAMPORT[3]);

      fscanf(fp,"IMGINDEX[0]        %ld\n",&IMGindex_tmp[0]);
      fscanf(fp,"IMGINDEX[1]        %ld\n",&IMGindex_tmp[1]);
      fscanf(fp,"IMGINDEX[2]        %ld\n",&IMGindex_tmp[2]);
      fscanf(fp,"IMGINDEX[3]        %ld\n",&IMGindex_tmp[3]);

      fscanf(fp,"CAM_TO_LOAD[0]     %ld\n",&CAM_TO_LOAD[0]);
      fscanf(fp,"CAM_TO_LOAD[1]     %ld\n",&CAM_TO_LOAD[1]);
      fscanf(fp,"CAM_TO_LOAD[2]     %ld\n",&CAM_TO_LOAD[2]);
      fscanf(fp,"CAM_TO_LOAD[3]     %ld\n",&CAM_TO_LOAD[3]);

      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      n = snprintf(line,SBUFFERSIZE,"MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }

  if((TIME_UTC_YR==TIME_UTC_YR_status)&&(TIME_UTC_MON==TIME_UTC_MON_status)&&(TIME_UTC_DAY==TIME_UTC_DAY_status))
    {
      printf("ADOPTING IMAGE INDEXES FROM STATUS FILE\n");
      for(i=0;i<4;i++)
	IMGindex[i] = IMGindex_tmp[i];
    }
  
  return(0);
}










// --------------------------- ASTRONOMICAL FUNCTIONS ---------------------------

double compute_UTCnow()
{
  time_t ltime;
  struct tm *utctimenow;
  double hr;

  if(NOSTOP==0)
    SKYCAM_checkstop();

  time( &ltime );
  utctimenow = gmtime( &ltime );

  TIME_UTC_YR = 1900+(int) (utctimenow->tm_year);
  TIME_UTC_MON = 1+(int) (utctimenow->tm_mon);
  TIME_UTC_DAY = (int) (utctimenow->tm_mday);

  hr = ((1.0*utctimenow->tm_sec/60.0+1.0*utctimenow->tm_min)/60.0)+1.0*utctimenow->tm_hour;
  TTIME_UTC = hr;

  while(TTIME_UTC>24.0)
    TTIME_UTC -= 24.0;
  while(TTIME_UTC<0.0)
    TTIME_UTC += 24.0;

  return(TTIME_UTC);
}


// compute local sidereal time from UT time
double compute_LST(double site_long, int year, int month, double day)
{
  struct tm tm2000;
  time_t t2000;
  struct tm tmv;
  time_t tv;
  int mday, hr, min,sec;
  double dt;
  double v;
  double GMST;
  //int GMST_hr,GMST_min;
  //  double GMST_sec;
  double LST;
  //  int LST_hr,LST_min;
  //  double LST_sec;

  tm2000.tm_year = 2000-1900;
  tm2000.tm_mon = 0;
  tm2000.tm_mday = 1;
  tm2000.tm_hour = 12;
  tm2000.tm_min = 0;
  tm2000.tm_sec = 0;

  t2000 = mktime(&tm2000);
  
  mday = (int) day;
  v  = 1.0*day - mday;
  hr = (int) (24.0*v);
  v = 24.0*v - hr;
  v = v*60.0;
  min = (int) v;
  v = v-min;
  v = v*60.0;
  sec = (int) v;
  
  //  printf("%s\n",asctime(&tm2000));

  tmv.tm_year = year-1900;
  tmv.tm_mon = month-1;
  tmv.tm_mday = mday;
  tmv.tm_hour = hr;
  tmv.tm_min = min;
  tmv.tm_sec = sec;

  tv = mktime(&tmv);
  dt = difftime(tv,t2000);

  dt = dt / (24.0*3600.0);

  GMST = 18.697374558 + 24.06570982441908 * dt;
  while(GMST>24.0)
    GMST -= 24.0;
  // GMST_hr = (int) GMST;
  // GMST_min = (int) (60.0*(GMST-GMST_hr));
  //  GMST_sec = 60.0*(60.0*(GMST-GMST_hr)-GMST_min);

  /* local sideral time (in hours) - site_long is in radian */
  LST = GMST + site_long*24.0/2.0/M_PI;
  while (LST<0.0)
    LST += 24.0;
  while (LST>24.0)
    LST -= 24.0;

  //  LST_hr = (int) LST;
  //  LST_min = (int) (60.0*(LST-LST_hr));
  //  LST_sec = 60.0*(60.0*(LST-LST_hr)-LST_min);

  TIME_LST = LST;

  return (LST);
}

double compute_LSTnow()
{
  struct tm *utctimenow;
  time_t ltime;
  double LST = 0.0;
  struct tm tm2000;
  time_t t2000;
  time_t tv;
  double dt;
  double GMST;
  //  int GMST_hr,GMST_min;
  // double GMST_sec;
  // int LST_hr,LST_min;
  //double LST_sec;

  compute_UTCnow();

  tm2000.tm_year = 2000-1900;
  tm2000.tm_mon = 0;
  tm2000.tm_mday = 1;
  tm2000.tm_hour = 12;
  tm2000.tm_min = 0;
  tm2000.tm_sec = 0;
  t2000 = mktime(&tm2000);

  /* Obtain current coordinated universal time: */
  time( &ltime );
  utctimenow = gmtime( &ltime );
  tv = mktime( utctimenow );
 

  dt = difftime(tv,t2000);
  
  dt = dt / (24.0*3600.0);
  
  GMST = 18.697374558 + 24.06570982441908 * dt;
  while(GMST>24.0)
    GMST -= 24.0;
  //GMST_hr = (int) GMST;
  //GMST_min = (int) (60.0*(GMST-GMST_hr));
  //GMST_sec = 60.0*(60.0*(GMST-GMST_hr)-GMST_min);
  //  printf("GMST = %.6f hr\n",GMST);
  //  printf("GMST = %02d:%02d:%05.2f\n",GMST_hr,GMST_min,GMST_sec);
  

  /* local sideral time (in hours) - site_long is in radian */
  LST = GMST + SITE_LONG*24.0/2.0/M_PI;
  while (LST<0.0)
    LST += 24.0;
  while (LST>24.0)
    LST -= 24.0;

  // LST_hr = (int) LST;
  //LST_min = (int) (60.0*(LST-LST_hr));
  //LST_sec = 60.0*(60.0*(LST-LST_hr)-LST_min);
  //  printf("LST = %02d:%02d:%05.2f\n",LST_hr,LST_min,LST_sec);

  TIME_LST = LST;

  return(LST);
}


// RA and DEC should be in radian
// RA, DEC -> ALT, AZ
int compute_coordinates_from_RA_DEC(double RA, double DEC)
{
  double H;
  double Sh;
  double tmpptr;
  
  COORD_RA = RA;
  COORD_DEC = DEC;
  
  /* local hour angle (in radians) */
  H = TIME_LST/24.0*2.0*M_PI - COORD_RA;
  
  Sh = sin(SITE_LAT)*sin(DEC)+cos(SITE_LAT)*cos(DEC)*cos(H);
  COORD_ALT = asin(Sh);

  COORD_AZ = 2.0*M_PI - (atan2((cos(H)*sin(SITE_LAT)-tan(DEC)*cos(SITE_LAT)),sin(H))+M_PI/2.0);
  COORD_AZ = modf(COORD_AZ/(2.0*M_PI)+4,&tmpptr)*2.0*M_PI;  

  return(0);
}


// ALT and AZ should be in radian
// ALT, AZ -> RA, DEC
int compute_coordinates_from_ALT_AZ(double ALT, double AZ)
{
  double sinDEC, cosDEC, cosHA;

  COORD_ALT = ALT;
  COORD_AZ = AZ;

  sinDEC = cos(AZ)*cos(ALT)*cos(SITE_LAT) + sin(ALT)*sin(SITE_LAT);
  COORD_DEC = asin(sinDEC);

  cosDEC = cos(COORD_DEC);
  cosHA = (sin(ALT)-sinDEC*sin(SITE_LAT))/(cosDEC*cos(SITE_LAT));

  
  if(AZ>M_PI)
    COORD_RA = TIME_LST/24.0*2.0*M_PI - acos(cosHA);
  else
    COORD_RA = TIME_LST/24.0*2.0*M_PI + acos(cosHA);

  while(COORD_RA>2.0*M_PI)
    COORD_RA -= 2.0*M_PI;
  while(COORD_RA<0.0)
    COORD_RA += 2.0*M_PI;

  return(0);
}




double get_Moon_pos()
{
  FILE *fp = NULL;
  int n,i;
  size_t lsize = 500;
  double frac;
  char utcstring0[SBUFFERSIZE];
  char utcstring1[SBUFFERSIZE];
  char str1[SBUFFERSIZE];
  char *fline;
  double ra0 = 0.0;
  double ra1 = 0.0;
  double dec0 = 0.0;
  double dec1 = 0.0;
  double mag0,mag1;
  long cntl;
  ssize_t nss;

//   printf("Getting Moon position ...\n");
//   fflush(stdout);

  compute_UTCnow();
  
  frac = TTIME_UTC - (int) (TTIME_UTC);
  n = snprintf(utcstring0,SBUFFERSIZE," %04d-%02d-%02d %02d:00:00.000",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,(int) (TTIME_UTC));
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  n = snprintf(utcstring1,SBUFFERSIZE," %04d-%02d-%02d %02d:00:00.000",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,1+(int) (TTIME_UTC));
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  if((fp = fopen("/skycam/ephemeris_data/MoonPosition.txt","r"))==NULL)
    {
      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
      exit(0);
    }
 
  if(fp==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot find Moon position file\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  cntl = 0;
  
  fline = (char *) malloc(lsize+1);

  while ((nss = getline(&fline, &lsize, fp)) != -1) 
    {
      if(fline!=NULL)
	{
	  if(strncmp(fline,utcstring0,strlen(utcstring0))==0)
	    {
// 	            printf("%s\n", fline);
	      for(i=0;i<9;i++)
		str1[i] = fline[i+29];
	      str1[i]='\0';
	       printf("str1 = \"%s\"\n",str1);
	      ra0 = atof(str1)/180.0*M_PI;
	      
	      for(i=0;i<9;i++)
		str1[i] = fline[i+39];
	      str1[i]='\0';
// 	      printf("str1 = \"%s\"\n",str1);
	      dec0 = atof(str1)/180.0*M_PI;
	      
	      for(i=0;i<6;i++)
		str1[i] = fline[i+67];
	      str1[i]='\0';
// 	      printf("str1 = \"%s\"\n",str1);
	      mag0 = atof(str1);
	    }
	  
	  if(strncmp(fline,utcstring1,strlen(utcstring1))==0)
	    {
// 	      printf("%s\n", fline);
	      for(i=0;i<9;i++)
		str1[i] = fline[i+29];
	      str1[i]='\0';
	      ra1 = atof(str1)/180.0*M_PI;
// 	      printf("str1 = \"%s\"\n",str1);
	      
	      for(i=0;i<9;i++)
		str1[i] = fline[i+39];
	      str1[i]='\0';
// 	      printf("str1 = \"%s\"\n",str1);
	      dec1 = atof(str1)/180.0*M_PI;	
	      
	      for(i=0;i<6;i++)
		str1[i] = fline[i+67];
	      str1[i]='\0';
// 	      printf("str1 = \"%s\"\n",str1);
	      mag1 = atof(str1);
	    }
	}
      cntl ++;
    }
//     printf("MOONPOS: REACHED LINE %ld\n",cntl);
  if(fp==NULL)
    {
      printf("FILE POINTER IS NULL... THIS IS STRANGE\n");
      fflush(stdout);
    }
  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  fp = NULL;

 
  MOON_RA = (1.0-frac)*ra0 + frac*ra1;
  MOON_DEC = (1.0-frac)*dec0 + frac*dec1;
  MOON_MAGN = (1.0-frac)*mag0 + frac*mag1;

  compute_LSTnow();


  compute_coordinates_from_RA_DEC(MOON_RA,MOON_DEC);


  MOON_ALT = COORD_ALT;
  MOON_AZ = COORD_AZ;

  printf("Getting Moon position -> DONE\n");
  fflush(stdout);
  free(fline);

  return(0);
}



double get_Sun_pos()
{
  FILE *fp;
  int n,i;
  size_t lsize;
  double frac;
  char utcstring0[SBUFFERSIZE];
  char str1[SBUFFERSIZE];
  double ra0,ra1,dec0,dec1;
  char *fline = NULL;
  ssize_t nss;

  compute_UTCnow();
  frac = TTIME_UTC - (int) (TTIME_UTC);

  n = snprintf(utcstring0,SBUFFERSIZE," %04d-%02d-%02d %02d:00:00.000",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,(int) (TTIME_UTC));
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");

  fp = fopen("/skycam/ephemeris_data/SunPosition.txt","r");
  if(fp==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot find Sun position file\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  while ((nss = getline(&fline, &lsize, fp)) != -1) 
    {
      if(strncmp(fline,utcstring0,strlen(utcstring0))==0)
      {
	for(i=0;i<9;i++)
	  str1[i] = fline[i+29];
	str1[i]='\0';
	ra0 = atof(str1)/180.0*M_PI;
	for(i=0;i<9;i++)
	  str1[i] = fline[i+39];
	str1[i]='\0';
	dec0 = atof(str1)/180.0*M_PI;
	
	n = getline(&fline,&lsize,fp);

	for(i=0;i<9;i++)
	  str1[i] = fline[i+29];
	str1[i]='\0';
	ra1 = atof(str1)/180.0*M_PI;

	for(i=0;i<9;i++)
	  str1[i] = fline[i+39];
	str1[i]='\0';
	dec1 = atof(str1)/180.0*M_PI;	
      }
  }
  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  fp = NULL;
  
  SUN_RA = (1.0-frac)*ra0 + frac*ra1;
  SUN_DEC = (1.0-frac)*dec0 + frac*dec1;

  //  printf("SUN:\n");
  //  printf("  RA = %f rad\n",SUN_RA);
  //  printf(" DEC = %f rad\n",SUN_DEC);

  compute_LSTnow();
  compute_coordinates_from_RA_DEC(SUN_RA,SUN_DEC);
  SUN_ALT = COORD_ALT;
  SUN_AZ = COORD_AZ;

  //  n = snprintf(line1,SBUFFERSIZE,"SUN ELEVATION = %f deg\n",SUN_ALT/M_PI*180.0);
  //  SKYCAM_log();

  return(0);
}


























// --------------------------- COMMUNICATION FUNCTIONS ---------------------------


int open_ttyUSBport(int portnb){
  char pname[SBUFFERSIZE]="";
  int fd = -1;
  int n;

  n = snprintf(pname, SBUFFERSIZE, "/dev/ttyUSB%d", portnb);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");

  fd = open(pname, O_RDWR | O_NOCTTY | O_NDELAY);

  if (fd < 0)
    {
      /*
      * Could not open the port.
      */
      printf("Unable to open %s\n",pname);
      n = snprintf(line,SBUFFERSIZE,"ERROR: Unable to open %s\n",pname);
      SKYCAM_log();
      sleep(1);
      //      SKYCAM_exit();
    }
  else
    {
      fdcnt++;
      fcntl(fd, F_SETFL, FNDELAY); // to be checked
      n = snprintf(line,SBUFFERSIZE,"%s is open on file descriptor %d\n",pname,fd);
      SKYCAM_log();
    }
    
  return (fd);
}




int read_ttyUSBport(int fd)
{
  int stopread = 0; // goes to 1 when read complete
  int i,n,n1;
  char buf[BUFS+1];
  long cnt;
  double t; // time since read start
  int rvalue = 0;

  t = 0.0;
  cnt = 0;
  n1 = 0;
  while(stopread==0)
    {
      usleep((useconds_t) (tcommdelay*1000000));
      n = read(fd,buf,BUFS);
      if(n>0)
        buf[n] = '\0';
      else
        buf[0] = '\0';
      //   printf("%ld %d %s %d\n",cnt,n,buf,errno);
      for(i=0;i<n;i++)
        {
	  if(i>BUFS)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: index i above buf size, func %s, line %d",__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	    }
	  if(n1>readbufsize-1)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: index n1 above readbufsize, func %s, line %d",__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	    }
	  

	  READBUF[n1] = buf[i];
          n1++;

        }
      if((((READBUF[n1-1]=='#')||(READBUF[n1-1]=='\r'))&&(n==-1))||(t>readtimeout))
        {
          stopread = 1;
          if(t>readtimeout)
            {
	      n = snprintf(line,SBUFFERSIZE,"read timeout at t = %f sec\n",t);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	      READBUF[n1] = '\0';
	      //	      n = snprintf(line,SBUFFERSIZE,"read: \"%s\" (%f sec)\n",READBUF,t);
	      SKYCAM_log();
	      rvalue = 1;
	    }
	}
      cnt ++;
      t += tcommdelay;
    }
  READBUF[n1] = '\0';
  //  printf("read: \"%s\" (%f sec)\n",READBUF,t);

  return(rvalue);
}



int openmountfd()
{
  if(!(mountfd<0))
    {
      printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
      SKYCAM_exit();
    }
  
  mountfd = open_ttyUSBport(mountUSBportNB);

  return(mountfd);
}

int closemountfd()
{
  if(close(mountfd)!=0)
    {
      printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
      SKYCAM_exit();
    }
  mountfd = -1;
  fdcnt--;

  return(0);
}


int read_ttyUSBport_mount(int fd)
{
  int stopread = 0; // goes to 1 when read complete
  int i,n,n1;
  char buf[BUFS+1];
  char buf1[BUFS+1];
  long cnt;
  double t; // time since read start
  int rvalue = 0;
  int initOK = 0;
  //  char statchar;


  t = 0.0;
  cnt = 0;
  n1 = 0;
  while(stopread==0)
    {
      usleep((useconds_t) (tcommdelay*1000000));
      n = read(fd,buf,BUFS);
      if(n>0)
        buf[n] = '\0';
      else
        buf[0] = '\0';
      //   printf("%ld %d %s %d\n",cnt,n,buf,errno);
      for(i=0;i<n;i++)
        {
	  if(i>BUFS)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: index i above buf size, func %s, line %d",__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	    }
	  if(n1>readbufsize-1)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: index n1 above readbufsize, func %s, line %d",__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	    }
	  
	  buf1[n1] = buf[i];
	  n1++;	 
        }
      if((((buf1[n1-1]=='\n')||(buf1[n1-1]=='\r'))&&(n==-1))||(t>readtimeout))
        {
          stopread = 1;
          if(t>readtimeout)
	    buf1[n1] = '\0';
	}
      cnt ++;
      t += tcommdelay;
    }
  buf1[n1] = '\0';

  i = 0;
  n = 0;
  while(i<n1)
    {
      if((initOK==0)&&(i<n1-1))
	if((buf1[i]=='/')&&(buf1[i+1]=='0'))
	  {
	    initOK = 1;
	    //	    if(i+2<n1)
	    //statchar = buf1[i+2];
	    i += 3;
	  }
      if((initOK==1)&&(i<n1))
	{
	  READBUF[n] = buf1[i];
	  n ++;
	}
      i++;
    }
  READBUF[n-3] = '\0';

  //  printf("read: \"%s\" (%f sec)\n",READBUF,t);

  /*  printf("stat char = ");
  for(i = 7; i>=0; i--) {
    if((1<<i)&statchar) printf("1");
    else printf("0");
  }
  printf("\n");
  */

  return(rvalue);
}



int charhex_to_int(char c)
{
  int value;
  
  if((c>='0') && (c<='9'))
    value = (int) (c-'0');
  else
    value = (int) (c-'A'+10);  
  
  return(value);
}

char int_to_charhex(int value)
{
  char c;
  int n;

  if((value<0)||(value>15))
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: value %d out of range\n",value);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  if(value<=9)
    c = '0'+value;
  else
    c = 'A'+(value-10);
  
  return(c);
}




int scanttyUSBports()
{
  int i,n;
  int fd;
  int portnb_mount = -1;
  int portnb_dio = -1; // digital IO
  int portnb_aio = -1; // analog IO
  char command[SBUFFERSIZE];
  int returnval = -1;
  FILE *fp = NULL;

  n = snprintf(line,SBUFFERSIZE,"Scanning ports for mount & DIO\n");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();


  i = 0;
  while((i<5)&&((portnb_dio == -1)||(portnb_aio == -1)||(portnb_mount == -1)))
    {
      fd = open_ttyUSBport(i);
      if(!(fd<0))
        {
	  n = snprintf(line,SBUFFERSIZE,"Setting port ttyUSB%i parameters\n",i);
	  SKYCAM_log();
	  
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  n = snprintf(command,SBUFFERSIZE,"stty -F /dev/ttyUSB%d 5:0:8bd:0:3:1c:7f:15:1:0:1:0:11:13:1a:0:12:f:17:16:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0:0",i);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  printf("EXECUTING SYSTEM COMMAND: %s\n", command);
	  system(command);

	  if(portnb_mount == -1) // test for mount
	    {
	      n = snprintf(command,SBUFFERSIZE,"/1&\r");
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      n = write(fd,command,strlen(command));
	      read_ttyUSBport_mount(fd);
	      printf("Response = \"%s\"\n",READBUF);

	      // axis 1 : EZStepper AllMotion V3.85 8-15-06 
	      // axis 2 : EZStepper AllMotion V7.05f 03-07-11

	      if((READBUF[0]=='E')&&(READBUF[1]=='Z'))
		{
		  n = snprintf(line,SBUFFERSIZE,"Mount found on /dev/ttyUSB%d\n",i);
		  if(n >= SBUFFERSIZE) 
		    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  SKYCAM_log();
		  portnb_mount = i;
		}	      
	    }

	
	  if((portnb_dio == -1)&&(portnb_mount != i)) // test for DIO
	    {
	      n = snprintf(command,SBUFFERSIZE,"A00\r");
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      n = write(fd,command,strlen(command));
	      read_ttyUSBport(fd);
	      printf("-----Response = %s\n",READBUF);
	      if((READBUF[0]=='A')&&(READBUF[1]=='?'))
		{
		  n = snprintf(line,SBUFFERSIZE,"DIO found on /dev/ttyUSB%d\n",i);
		  if(n >= SBUFFERSIZE) 
		    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  SKYCAM_log();
		  portnb_dio = i;
		}       
	    }
 
        
	  
	  if((portnb_aio == -1)&&(portnb_dio != i)&&(portnb_mount != i)) // test for AIO
	    {
	      n = snprintf(command,SBUFFERSIZE,"B00\r");
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      n = write(fd,command,strlen(command));
	      read_ttyUSBport(fd);
	      printf("-----Response = %s\n",READBUF);
	      if((READBUF[0]=='B')&&(READBUF[1]=='?'))
		{
		  n = snprintf(line,SBUFFERSIZE,"AIO found on /dev/ttyUSB%d\n",i);
		  if(n >= SBUFFERSIZE) 
		    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  SKYCAM_log();
		  portnb_aio = i;
		}       
	    }
          if(close(fd)!=0)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",fd,__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      SKYCAM_log();
	      SKYCAM_exit();
	    }
        
      

	}     
      i++;
    }
  


  if((portnb_aio==-1)||(portnb_dio==-1)||(portnb_mount==-1))
    returnval = -1;
  else
    returnval = 0;
  
  mountUSBportNB = portnb_mount;
  dioUSBportNB = portnb_dio;
  aioUSBportNB = portnb_aio;
  
  if((fp = fopen("/skycam/USBports.txt","w"))==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not open file /skycam/USBports.txt\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
SKYCAM_log();
      SKYCAM_exit();
    }
  

  fprintf(fp,"mountUSBportNB  %d\n",mountUSBportNB);
  fprintf(fp,"dioUSBportNB  %d\n",dioUSBportNB);
  fprintf(fp,"aioUSBportNB  %d\n",aioUSBportNB);
  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  fp = NULL;
  
 
  return(returnval);
}








// --------------------------- LOW LEVEL COMMUNICATION & TEST ---------------------------

// turn on power to mount and camera
int DIO_power_Cams(int value)
{
  char command[SBUFFERSIZE];
  int n;
  
  n = snprintf(line,SBUFFERSIZE,"Power = %d\n",value);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();
  if(value==0)
    {
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"ALH\r");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      write(diofd,command,strlen(command));
      read_ttyUSBport(diofd);
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
    }
  if(value==1)
    {
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"AHH\r");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      write(diofd,command,strlen(command));
      read_ttyUSBport(diofd);
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
    }

  return(0);
}


int DIO_power_webcam(int value)
{
  int n;

  n = snprintf(line,SBUFFERSIZE,"Power webcam = %d\n",value);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();
  dio_setdigital_out(DIOCHAN_WEBCAM, value);

  return(0);
}



int DIO_power_Mount(int value)
{
  char command[SBUFFERSIZE];
  int n;
  
  n = snprintf(line,SBUFFERSIZE,"Power Mount = %d\n",value);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  if(value==0)
    {
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"ALG\r");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      write(diofd,command,strlen(command));
      read_ttyUSBport(diofd);
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      MOUNTSTATUS = 0;
      SKYCAM_write_STATUS();
    }

  if(value==1)
    {
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"AHG\r");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      write(diofd,command,strlen(command));
      read_ttyUSBport(diofd);
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      if(MOUNTSTATUS<1)
	{
	  MOUNTSTATUS = 1;
	  SKYCAM_write_STATUS();
	}
    }
  sleep(2);
  scanttyUSBports();

  
  openmountfd();
  n = snprintf(command,SBUFFERSIZE,"/1F%dh%ldm%ldR",MOUNTRADIR,MOUNTHOLDCURRENT,MOUNTSLEWCURRENT);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);
  sleep(1);
  n = snprintf(command,SBUFFERSIZE,"/2F%dh%ldm%ldR",MOUNTDECDIR,MOUNTHOLDCURRENT,MOUNTSLEWCURRENT);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);
  closemountfd();
  

  return(0);
}


int dio_limitswitch_status(char limitswitchchar)
{
  int value = -1;
  char command[SBUFFERSIZE];
  int rvalue;
  long comcnt;
  int n;

  n = snprintf(command,SBUFFERSIZE,"AR%c\r",limitswitchchar);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  write(diofd,command,strlen(command));
  rvalue = read_ttyUSBport(diofd);
  comcnt = 0;
  while(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"dio_limitswitch_status: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      comcnt++;
      fdcnt--;
      if(comcnt>200)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: number of consecutive communication failures = 200 in dio_limitswitch_status\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"AR%c\r",limitswitchchar);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      //printf("COMMAND: %s",command);
      write(diofd,command,strlen(command));
      rvalue = read_ttyUSBport(diofd);
    }

  //  printf("read = %s\n",READBUF);
  if(READBUF[2]=='H')
    value = 1;
  if(READBUF[2]=='L')
    value = 0;
    
  return(value);
}



int dio_setdigital_out(char limitswitchchar, int val)
{
  char command[SBUFFERSIZE];
  int rvalue;
  long comcnt;
  int n;

  if(val==1)
    {
      n = snprintf(command,SBUFFERSIZE,"AH%c\r",limitswitchchar);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
    }
  else
    {
      n = snprintf(command,SBUFFERSIZE,"AL%c\r",limitswitchchar);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
    }

  if(!(diofd<0))
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
     if(n >= SBUFFERSIZE) 
       printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters"); 
      SKYCAM_log();
      SKYCAM_exit();
    }
  diofd = open_ttyUSBport(dioUSBportNB);
  
  printf("\n");


  //printf("COMMAND: %s",command);
  write(diofd,command,strlen(command));
  rvalue = read_ttyUSBport(diofd);
  comcnt = 0;
  while(rvalue == 1)
    {
      printf("dio_limitswitch_status: retry\n");
      if(close(diofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      comcnt++;
      if(comcnt>200)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: number of consecutive communication failures = 200 in dio_limitswitch_status\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      write(diofd,command,strlen(command));
      rvalue =   read_ttyUSBport(diofd);
    }

  if(close(diofd)!=0)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  diofd = -1;
  fdcnt--;


  //  printf("read = %s\n",READBUF);
    
  return(val);
}



int aio_analogchannel_init(char channelchar, int mode, int dec)
{
  char command[SBUFFERSIZE];
  int rvalue;
  long comcnt;
  int n;

  n = snprintf(command,SBUFFERSIZE,"BM%c%d\r",channelchar,mode);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters"); 
  write(aiofd,command,strlen(command));
  rvalue = read_ttyUSBport(aiofd);
  comcnt = 0;
  while(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"aio_analogchannel_init: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      if(close(aiofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      comcnt++;
      if(comcnt>200)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: number of consecutive communication failures = 200 in aio_analogchannel_init\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
      write(diofd,command,strlen(command));
      rvalue = read_ttyUSBport(diofd);
    }

  n = snprintf(command,SBUFFERSIZE,"BD%c%d\r",channelchar,dec);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  write(aiofd,command,strlen(command));
  rvalue = read_ttyUSBport(aiofd);  
  comcnt = 0;
  while(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"aio_analogchannel_init: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      if(close(aiofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      aiofd = -1;
      fdcnt--;
      comcnt++;
      if(comcnt>200)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: number of consecutive communication failures = 200 in aio_analogchannel_init\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      if(!(diofd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      aiofd = open_ttyUSBport(aioUSBportNB);
      write(aiofd,command,strlen(command));
      rvalue = read_ttyUSBport(aiofd);
    }


  return(0);
}



float aio_analogchannel_value(char channelchar)
{
  float value = 0.0;
  char command[SBUFFERSIZE];
  int rvalue;
  long comcnt;
  int n;

  n = snprintf(command,SBUFFERSIZE,"BR%c\r",channelchar);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  //printf("COMMAND: %s",command);
  write(aiofd,command,strlen(command));

  rvalue = read_ttyUSBport(aiofd);
  //  printf("rvalue = %d\n",rvalue);
  comcnt = 0;
  while(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"aio_analogchannel_value: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      if(close(aiofd)!=0)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      aiofd = -1;
      fdcnt--;
      comcnt++;
      if(comcnt>200)
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: number of consecutive communication failures = 200 in aio_analogchannel_value\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      if(!(aiofd<0))
	{
	  printf(line,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      aiofd = open_ttyUSBport(aioUSBportNB);
      n = snprintf(command,SBUFFERSIZE,"BR%c\r",channelchar);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      //printf("COMMAND: %s",command);
      write(aiofd,command,strlen(command));
      rvalue = read_ttyUSBport(aiofd);
      //  printf("rvalue = %d\n",rvalue);
    }


  //  printf("read = %s\n",READBUF);
  READBUF[0] = ' ';
  value = atof(READBUF);
  //  printf("-> value = %f\n",value);
  //if(READBUF[2]=='H')
  //  value = 1;
  //if(READBUF[2]=='L')
  //  value = 0;
    
  return(0.001*value);
}



int SKYCAM_command_readDIO()
{
  long cnt = 0;
  useconds_t twait = 1000;
  int n;
   
   if(!(diofd<0))
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  diofd = open_ttyUSBport(dioUSBportNB);
  
  printf("\n");
  while(cnt<100)
    {
      usleep(twait);
      dio_inA = dio_limitswitch_status('A');
      usleep(twait);
      dio_inB = dio_limitswitch_status('B');
      printf(" (%06ld)  A=%d B=%d\r",cnt,dio_inA,dio_inB);      
      fflush(stdout);
      cnt ++;
    }
  usleep(twait);

  if(close(diofd)!=0)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  diofd = -1;
  fdcnt--;

  return(0);
}












// --------------------------- MOUNT ---------------------------



int mount_command(char *command, int log)
{
  useconds_t twait = 50000; // limit is 30000, put 50000 (50ms) to be safe
  char command1[SBUFFERSIZE];
  int n;
  int close = 0;

  if(mountfd < 0)
    {
      close = 1;
      openmountfd();
    }
  n = snprintf(command1,SBUFFERSIZE,"%s\r",command);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  write(mountfd,command1,strlen(command1));

  if(log==1)
    {
      n = snprintf(line,SBUFFERSIZE,"MOUNT COMMAND = \"%s\"\n",command);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
    }
  usleep(twait);

  if(close==1)
    closemountfd();

  return(0);
}





int compute_mount_radec_from_radec()
{
  int n;

  // mRA mDEC -> RA DEC
  //
  // RAflip = -1 if mDEC < mDEC_NPOLE, (mDEC-mDEC_NPOLE<0)
  // RAflip = +1 if mDEC > mDEC_NPOLE, (mDEC-mDEC_NPOLE>0)
  //
  // RA  [hr]  = 24.0*(-(mRA-mRA_MERIDIAN)-0.25*RAflip)+LST
  // DEC [deg] = 90.0-360.0*fabs(mDEC-mDEC_NPOLE)
  // 
  //
  // if RAflip = +1
  // mDEC = (90.0-DEC[deg])/360.0+mDEC_NPOLE
  // 
  // if RAflip = -1
  // mDEC = -(90.0-DEC[deg])/360.0+mDEC_NPOLE
  // 
  //
  // mRA = (LST[hr]-RA[hr])/24.0-0.25*RAflip+mRA_MERIDIAN
  // 

  // RAflip = +1
  COORD_mDEC = (90.0-COORD_DEC/M_PI*180.0)/360.0 + mDEC_NPOLE;
  COORD_mRA = (TIME_LST-COORD_RA/M_PI*12.0)/24.0 - 0.25 + mRA_MERIDIAN;
  
  if(COORD_mDEC<0.0)
    COORD_mDEC += 1.0;
  if(COORD_mDEC>1.0)
    COORD_mDEC -= 1.0;
  
  if(COORD_mRA<0.0)
    COORD_mRA += 1.0;
  if(COORD_mRA>1.0)
    COORD_mRA -= 1.0;
  
 
  if(test_posmount(COORD_mRA,COORD_mDEC,1)==0)  
    {
      // RAflip = -1
      COORD_mDEC = -(90.0-COORD_DEC/M_PI*180.0)/360.0 + mDEC_NPOLE;
      COORD_mRA = (TIME_LST-COORD_RA/M_PI*12.0)/24.0 + 0.25 + mRA_MERIDIAN;

      if(COORD_mDEC<0.0)
	COORD_mDEC += 1.0;
      if(COORD_mDEC>1.0)
	COORD_mDEC -= 1.0;
      
      if(COORD_mRA<0.0)
	COORD_mRA += 1.0;
      if(COORD_mRA>1.0)
	COORD_mRA -= 1.0;
 
      if(test_posmount(COORD_mRA,COORD_mDEC,0)==0)  
      	{
	  n = snprintf(line,SBUFFERSIZE,"Cannot access this position\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	}
    }

  return(0);
}





int goto_posmount_radec(double mrapos, double mdecpos)
{
  char command[SBUFFERSIZE];
  long stepmra,stepmdec;
  int n;
  //  int i;
  //  long twait = 50000; // limit is 30000, put 50000 (50ms) to be safe

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot move mount if not turned on or initialized\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  if((mrapos<0)||(mrapos>1.0)||(mdecpos<0)||(mdecpos>1.0))
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: mrapos, mdecpos = %g, %g out of range\n",mrapos,mdecpos);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  else
    {
      n = snprintf(line,SBUFFERSIZE,"Moving to %g %g\n",mrapos,mdecpos);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
    }

  stepmra = (long) (mrapos*NBstepMountRot);  
  stepmdec = (long) (mdecpos*NBstepMountRot);
  
 
  if(MOVEDECFIRST==1)
    {
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldm%ldA%ldR",MOUNTDECDIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmdec);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      usleep((useconds_t) (MOVEDECWAIT*1000000));

      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldm%ldA%ldR",MOUNTRADIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmra);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);    
    }
  else
    {
      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldm%ldA%ld",MOUNTRADIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmra);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      //  usleep(twait);
      
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldm%ldA%ld",MOUNTDECDIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmdec);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);

      n = snprintf(command,SBUFFERSIZE,"/AR");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      //  usleep(twait);
    }


  MOUNTSTATUS = 4;
  SKYCAM_write_STATUS();

  return(0);
}




int set_posmount_radec(double mrapos, double mdecpos)
{
  char command[SBUFFERSIZE];
  long stepmra,stepmdec;
  int n;
  //  int i;
  //  long twait = 50000; // limit is 30000, put 50000 (50ms) to be safe

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot set mount if not turned on or initialized\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  if((mrapos<0)||(mrapos>1.0)||(mdecpos<0)||(mdecpos>1.0))
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: mrapos, mdecpos = %g, %g out of range\n",mrapos,mdecpos);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  else
    {
      n = snprintf(line,SBUFFERSIZE,"Moving to %g %g\n",mrapos,mdecpos);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
    }

  stepmra = (long) (mrapos*NBstepMountRot);  
  stepmdec = (long) (mdecpos*NBstepMountRot);
  
 
  if(MOVEDECFIRST==1)
    {
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldm%ldz%ldR",MOUNTDECDIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmdec);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      usleep((useconds_t) (MOVEDECWAIT*1000000));

      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldm%ldz%ldR",MOUNTRADIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmra);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);    
    }
  else
    {
      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldm%ldz%ld",MOUNTRADIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmra);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      //  usleep(twait);
      
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldm%ldz%ld",MOUNTDECDIR,MOUNTSLEWSPEED,MOUNTSLEWCURRENT,stepmdec);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);

      n = snprintf(command,SBUFFERSIZE,"/AR");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      mount_command(command,1);
      //  usleep(twait);
    }


  MOUNTSTATUS = 4;
  SKYCAM_write_STATUS();

  return(0);
}












// how to implement tracking rate in between two integer values of steps per sec ?
//
// dither between two velocity values
// 
// 


/*
 * mrarate is in steps per sec
 *
 * returns max tracking time
 */
double track_mra(double mrarate)
{
  long vtrack;    
  char command[SBUFFERSIZE];
  int n;
  long nsteps;
  double Ttrack = 0.0; // max tracking time

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ? not initialized ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  
  TRACK_MRA_vtrack1 = (long) (fabs(mrarate));
  TRACK_MRA_vtrack2 = TRACK_MRA_vtrack1 + 1;
  TRACK_MRA_alpha = 1.0-(fabs(mrarate)-TRACK_MRA_vtrack1); // fraction of time that should be spent on vtrack1
  TRACK_MRA_t1 = 0.0; // total time spent so far on vtrack1 [sec]
  TRACK_MRA_t2 = 0.0; // total time spent so far on vtrack2 [sec]
  TRACK_MRA_status = 0; // 0 for vtrack1, 1 for vtrack2
  TRACK_MRA_time_old = time(NULL);

  vtrack = (long) (fabs(mrarate)+0.5); // steps per sec
  // (1.0*NBstepMountRot/360.0/3600.0*fabs(mrarate));

  if(vtrack==TRACK_MRA_vtrack1)
    TRACK_MRA_status = 0;
  else
    TRACK_MRA_status = 1;


  mRAmax1 = mRAmax;
  if((pos_mountdec>0.0)&&(pos_mountdec<0.40))
    mRAmax1 = 0.52;  
  if((pos_mountdec>0.17)&&(pos_mountdec<0.29))
    mRAmax1 = 0.55;

  if(mrarate>0)
    {
      nsteps = (long) ((mRAmax1 - pos_mountra)*NBstepMountRot);
      Ttrack = fabs(1.0*nsteps/vtrack); // max tracking time in sec
      printf("Tacking for maximum %ld steps = %lf sec = %lf hr\n",nsteps,Ttrack,Ttrack/3600.0);
      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldP%ldR",MOUNTRADIR,vtrack,nsteps);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      IMGHEADER_MOUNT_TRACKrate_RA = mrarate;
    }
  else
    {
      nsteps = (long) ((pos_mountra - mRAmin)*NBstepMountRot);
      Ttrack = fabs(1.0*nsteps/vtrack); // max tracking time in sec
      printf("Tacking for maximum %ld steps = %lf sec = %lf hr\n",nsteps,Ttrack,Ttrack/3600.0);
      n = snprintf(command,SBUFFERSIZE,"/1F%dV%ldD%ldR",MOUNTRADIR,vtrack,nsteps);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      IMGHEADER_MOUNT_TRACKrate_RA = mrarate;
    }

  if(nsteps>0)
    {
      mount_command(command,1);
      MOUNTSTATUS = 5;
      SKYCAM_write_STATUS();
    }
  else
    {
      printf("Tracking would interfere with mount limits -> no tracking\n");
      IMGHEADER_MOUNT_TRACKrate_RA = 0.0;
    }

  MOUNTSTATUS = 5;
  SKYCAM_write_STATUS();

  return(Ttrack);
}




double track_update_mra()
{
  time_t tnow;
  double dt;
  double ttot,ttot1;
  long n;
  char command[SBUFFERSIZE];
  double deadband = 2.0;

  tnow = time(NULL);
  dt = difftime(tnow,TRACK_MRA_time_old); // time in sec since last update

  // update t1 and t2
  if(TRACK_MRA_status == 0)
    {
      TRACK_MRA_t1 += dt;
    }
  else
    {
      TRACK_MRA_t2 += dt;
    }
  TRACK_MRA_time_old = tnow;

  // test if status needs to be changed
  ttot = TRACK_MRA_t1 + TRACK_MRA_t2; // total time
  ttot1 = ttot*TRACK_MRA_alpha; // cumulative time that should have been spent on speed 1

  //  n = snprintf(line,SBUFFERSIZE,"[%g %g %g %g]\n",TRACK_MRA_t1,TRACK_MRA_t2,ttot,ttot1);
  // SKYCAM_log();

  if(TRACK_MRA_t1>ttot1+deadband) // too much time on speed 1
    if(TRACK_MRA_status == 0) // currently on speed 1
      {
	n = snprintf(command,SBUFFERSIZE,"/1V%ldR",TRACK_MRA_vtrack2);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }
	mount_command(command,0);
	TRACK_MRA_status = 1;
	//n = snprintf(line,SBUFFERSIZE,"(ADJUSTING RA SPEED TO = %ld [%g])",TRACK_MRA_vtrack2,TRACK_MRA_alpha);
	//	SKYCAM_log();
      }

  if(TRACK_MRA_t1<ttot1-deadband) // too little time on speed 1
    if(TRACK_MRA_status == 1) // currently on speed 2
      {
	n = snprintf(command,SBUFFERSIZE,"/1V%ldR",TRACK_MRA_vtrack1);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }

	mount_command(command,0);
	TRACK_MRA_status = 0;
	//n = snprintf(line,SBUFFERSIZE,"(ADJUSTING RA SPEED TO = %ld [%g])",TRACK_MRA_vtrack1,TRACK_MRA_alpha);
	//	SKYCAM_log();
      }

  return(0);
}





/*
 * mdecrate is in steps per sec
 *
 * returns max tracking time
 *
 */
double track_mdec(double mdecrate)
{
  long vtrack;    
  char command[SBUFFERSIZE];
  int n;
  long nsteps;
  double Ttrack = 0.0;

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ? not initialized ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }


  TRACK_MDEC_vtrack1 = (long) (fabs(mdecrate));
  TRACK_MDEC_vtrack2 = TRACK_MDEC_vtrack1 + 1;
  TRACK_MDEC_alpha = 1.0-(fabs(mdecrate)-TRACK_MDEC_vtrack1); // fraction of time that should be spent on vtrack1
  TRACK_MDEC_t1 = 0.0; // total time spent so far on vtrack1 [sec]
  TRACK_MDEC_t2 = 0.0; // total time spent so far on vtrack2 [sec]
  TRACK_MDEC_status = 0; // 0 for vtrack1, 1 for vtrack2
  TRACK_MDEC_time_old = time(NULL);

  vtrack = (long) (fabs(mdecrate)+0.5); // steps per sec
  // (1.0*NBstepMountRot/360.0/3600.0*fabs(mrarate));

  if(vtrack==TRACK_MDEC_vtrack1)
    TRACK_MDEC_status = 0;
  else
    TRACK_MDEC_status = 1;

  vtrack = (long) (fabs(mdecrate)+0.5); //(1.0*NBstepMountRot/360.0/3600.0*fabs(mdecrate));

  if(mdecrate>0.0)
    {
      nsteps = (long) ((mDECmax - pos_mountdec)*NBstepMountRot);      
      Ttrack = fabs(1.0*nsteps/vtrack);
      printf("Tacking for maximum %ld steps = %lf sec = %lf hr\n",nsteps,Ttrack,Ttrack/3600.0);
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldP%ldR",MOUNTDECDIR,vtrack,nsteps);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      IMGHEADER_MOUNT_TRACKrate_DEC = mdecrate;
    }
  else
    {
      nsteps = (long) ((pos_mountdec - mDECmin)*NBstepMountRot);
      Ttrack = fabs(1.0*nsteps/vtrack);
      printf("Tacking for maximum %ld steps = %lf sec = %lf hr\n",nsteps,Ttrack,Ttrack/3600.0);
      n = snprintf(command,SBUFFERSIZE,"/2F%dV%ldD%ldR",MOUNTDECDIR,vtrack,nsteps);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      IMGHEADER_MOUNT_TRACKrate_DEC = mdecrate;
    }


  if(nsteps>0)
    {
      mount_command(command,1);
      MOUNTSTATUS = 5;
      SKYCAM_write_STATUS();
    }
  else
    {
      printf("Tracking would interfere with mount limits -> no tracking\n");
      IMGHEADER_MOUNT_TRACKrate_DEC = 0.0;
    }

  return(Ttrack);
}





double track_update_mdec()
{
  time_t tnow;
  double dt;
  double ttot,ttot1;
  long n;
  char command[SBUFFERSIZE];
  double deadband = 2.0;

  tnow = time(NULL);
  dt = difftime(tnow,TRACK_MDEC_time_old); // time in sec since last update

  // update t1 and t2
  if(TRACK_MDEC_status == 0)
    {
      TRACK_MDEC_t1 += dt;
    }
  else
    {
      TRACK_MDEC_t2 += dt;
    }
  TRACK_MDEC_time_old = tnow;

  // test if status needs to be changed
  ttot = TRACK_MDEC_t1 + TRACK_MDEC_t2; // total time
  ttot1 = ttot*TRACK_MDEC_alpha; // cumulative time that should have been spent on speed 1
  
  if(TRACK_MDEC_t1>ttot1+deadband) // too much time on speed 1
    if(TRACK_MDEC_status == 0) // currently on speed 1
      {
	n = snprintf(command,SBUFFERSIZE,"/2V%ldR",TRACK_MDEC_vtrack2);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }
	mount_command(command,0);
	TRACK_MDEC_status = 1;
	//	n = snprintf(line,SBUFFERSIZE,"(ADJUSTING DEC SPEED TO = %ld [%g])",TRACK_MDEC_vtrack2,TRACK_MDEC_alpha);
	//SKYCAM_log();
      }

  if(TRACK_MDEC_t1<ttot1-deadband) // too little time on speed 1
    if(TRACK_MDEC_status == 1) // currently on speed 2
      {
	n = snprintf(command,SBUFFERSIZE,"/2V%ldR",TRACK_MDEC_vtrack1);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }
	mount_command(command,0);
	TRACK_MDEC_status = 0;
	//n = snprintf(line,SBUFFERSIZE,"(ADJUSTING DEC SPEED TO = %ld [%g])",TRACK_MDEC_vtrack1,TRACK_MDEC_alpha);
	//SKYCAM_log();
      }

  return(0);
}







/*
 * mrarate and mdecrate are in steps per sec
 *
 * return max tracking time
 *
 */
double track_mra_mdec(double mrarate, double mdecrate)
{
  double Ttrack = 0.0;
  double tmp1;

  // IMGHEADER_MOUNT_TRACKrate_RA = 1.0*((long) ((1.0*NBstepMountRot/360.0/3600.0*15.0)));
 

  Ttrack = track_mra(mrarate);
  tmp1 = track_mdec(mdecrate);
  if (tmp1<Ttrack)
    Ttrack = tmp1;
  
  return(Ttrack);
}











int get_posmountradec()
{
  char command[SBUFFERSIZE]="";
  int n;
  long stepmdec,stepmra;
  int rvalue;
  int close = 0;

  if(MOUNTSTATUS<1)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }


  n = snprintf(command,SBUFFERSIZE,"/1?A\r");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  if(mountfd < 0)
    {
      close = 1;
      openmountfd();
    }
  n = write(mountfd,command,strlen(command));
 if(n >= SBUFFERSIZE) 
   printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters"); 
  rvalue = read_ttyUSBport_mount(mountfd);
  if(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"get_posmountradec: error\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();      
      exit(0);
    }
  stepmra = atol(READBUF);
  printf("STEP Mount RA = %ld\n",stepmra);

  n = snprintf(command,SBUFFERSIZE,"/2?A\r");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  n = write(mountfd,command,strlen(command));
  rvalue = read_ttyUSBport_mount(mountfd);
  if(rvalue == 1)
    {
      n = snprintf(line,SBUFFERSIZE,"get_posmountradec: error\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();      
      exit(0);
    }
  stepmdec = atol(READBUF);
  printf("STEP Mount DEC = %ld\n",stepmdec);

  if(close==1)
    closemountfd();

  pos_mountra = 1.0*stepmra/NBstepMountRot;
  pos_mountdec = 1.0*stepmdec/NBstepMountRot;
  
  LAST_GETPOSMOUNTRA = pos_mountra;
  LAST_GETPOSMOUNTDEC = pos_mountdec;

  return(0);
}


int get_mountswitches()
{
  int n,i;
  int rvalue;
  // long comcnt;
  int val[8];

  if(MOUNTSTATUS<1)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }

  mount_command("/1?4",0);
  rvalue = read_ttyUSBport_mount(mountfd);
  // comcnt = 0;
  if(rvalue == 1)
    {
      // TODO error handling
      n = snprintf(line,SBUFFERSIZE,"get_posmountradec: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();      
      exit(0);
    }
  printf("MOUNT LIMIT SWITCHES RA  = \"%s\" ",READBUF);
  printf("bin = \n");
  for(i = 7; i>=0; i--) {
    val[i] = (int) (((1<<i)&READBUF[1]));
    printf("%d -> %d\n",i,val[i]);
  }
  printf("\n");
  
  lswra0 = val[0];
  lswra1 = val[1];
  lswra2 = val[2];
  lswra3 = val[3];


  mount_command("/2?4",0);
  rvalue = read_ttyUSBport_mount(mountfd);
  // comcnt = 0;
  if(rvalue == 1)
    {
      // TODO error handling
      n = snprintf(line,SBUFFERSIZE,"get_posmountradec: retry\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      exit(0);
    }  
  //  stepmra = atol(READBUF);
  printf("MOUNT LIMIT SWITCHES DEC = %s\n",READBUF);

  printf("bin = \n");
  for(i = 7; i>=0; i--) {
    val[i] = (int) (((1<<i)&READBUF[1]));
    printf("%d -> %d\n",i,val[i]);
  }
  printf("\n");
  
  lswdec0 = val[0];
  lswdec1 = val[1];
  lswdec2 = val[2];
  lswdec3 = val[3];

  
  return(0);
}








int test_posmount(double mrapos, double mdecpos, int quiet)
{
  int posOK;
  int n;

  mRAmax1 = mRAmax;

  if((mrapos>mRAmax)&&(pos_mountdec>0.4))
    {
      MOVEDECFIRST = 1;
      MOVEDECWAIT = 1.0*(pos_mountdec-0.4)*NBstepMountRot/MOUNTSLEWSPEED;
      if(MOVEDECWAIT>40.0)
	MOVEDECWAIT = 40.0;
      if(MOVEDECWAIT<1.0)
	MOVEDECWAIT = 1.0;
    }
  else
    MOVEDECFIRST = 0;


  if((mdecpos>0.0)&&(mdecpos<0.40))
    mRAmax1 = 0.52;
  if((mdecpos>0.17)&&(mdecpos<0.29))
    mRAmax1 = 0.55;
    

  if((mrapos>mRAmax1)||(mrapos<mRAmin)||(mdecpos<mDECmin)||(mdecpos>mDECmax))
    {
      if(quiet==0)
	{
	  n = snprintf(line,SBUFFERSIZE,"ERROR: requested move outside range\n");
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"Valid RA range  : %f - %f\n",mRAmin,mRAmax1);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"Valid DEC range : %f - %f\n",mDECmin,mDECmax);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"requested position: %f %f\n",mrapos,mdecpos);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	}
      posOK = 0;
    }
  else
    posOK = 1;
  
  return(posOK);
}








int SKYCAM_command_homeRA(double maxmove)
{
  char command[SBUFFERSIZE];
  int n;
  
  if(MOUNTSTATUS<1)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  n = snprintf(command,SBUFFERSIZE,"/1F%dm%ldV%ldf1R",MOUNTRADIR,MOUNTSLEWCURRENT,MOUNTSLEWSPEED);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);

  n = snprintf(command,SBUFFERSIZE,"/1Z%ldR",(long) (maxmove*NBstepMountRot));
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);

  return(0);
}



int SKYCAM_command_homeDEC(double maxmove)
{
  char command[SBUFFERSIZE];
  int n;

  if(MOUNTSTATUS<1)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not turned on ?\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }

  n = snprintf(command,SBUFFERSIZE,"/2F%dm%ldV%ldf1R",MOUNTDECDIR,MOUNTSLEWCURRENT,MOUNTSLEWSPEED);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);

  n = snprintf(command,SBUFFERSIZE,"/2Z%ldR",(long) (maxmove*NBstepMountRot));
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  mount_command(command,1);

  return(0);
}



double SKYCAM_command_movpos(double v1, double v2)
{ 
  long movposcnt = 0;
  double ra_move, dec_move;
  double dist;
  double praold,pdecold;
  double dra, ddec;
  double movsp = 1.0;
  double movsp_lim = 1.0e-5;
  int n;

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not initialized\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }

  if(test_posmount(v1,v2,0)==0)
    {
      n = snprintf(line,SBUFFERSIZE,"REQUESTED POSITION NOT VALID\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  SKYCAM_command_MOUNTSTOP();

  get_posmountradec();
  ra_move = v1-pos_mountra;
  ra_move = fabs(ra_move);
  
  dec_move = v2-pos_mountdec;
  dec_move = fabs(dec_move);
  
  n = snprintf(line,SBUFFERSIZE,"MOVE = %f %f\n",ra_move,dec_move);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();
  
  MOUNTSTATUS = 4;
  goto_posmount_radec(v1,v2);
  
  dist = 1.0;
  
  usleep(1000000);
  if(1) // WAIT
    {
      praold = 10.0;
      pdecold = 10.0;
      printf("\n");
      while((dist>POINTING_ACCURACY)&&(movsp>movsp_lim))
	{
	  usleep(100000);
	  get_posmountradec();
	  
	  n = snprintf(line,SBUFFERSIZE,"(moving) %f %f\n", pos_mountra, pos_mountdec);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  
	  dra = pos_mountra-praold;
	  ddec = pos_mountdec-pdecold;
	  praold = pos_mountra;
	  pdecold = pos_mountdec;
	  
	  ra_move = v1-pos_mountra;
	  ra_move = fabs(ra_move);
	  
	  dec_move = v2-pos_mountdec;
	  dec_move = fabs(dec_move);
	  
	  dist = sqrt(ra_move*ra_move+dec_move*dec_move);
	  
	  movsp = 0.5*movsp + 0.5*sqrt(dra*dra+ddec*ddec);
	  n = snprintf(line,SBUFFERSIZE,"\r[%ld] dist = %f  (%g)   ",movposcnt,dist,movsp);
	  if(n >= SBUFFERSIZE) 
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  SKYCAM_log();
	  
	  fflush(stdout);
	  movposcnt++;
	}
      printf("\n");
    }
  usleep(100000);

  MOUNTSTATUS = 2;
  SKYCAM_write_STATUS();

  if(dist>0.05)
    {
      MOUNTSTATUS = -1;
      SKYCAM_write_STATUS();
      n = snprintf(line,SBUFFERSIZE,"ERROR: Mount lost (mount error = %f)\n",dist);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      sleep(1);
      SKYCAM_command_home();
      SKYCAM_exit();
    }
  MOUNT_NBMOVE ++;  
  
  return(dist);
}



// returns tracking time
double SKYCAM_command_tracksidN()
{
  long n;
  double Ttrack;
  

  if(pos_mountdec<mDEC_NPOLE)
    RAflip = -1;
  else
    RAflip = 1;

  SKYCAM_computeTrackingRate_sidN( pos_mountra, pos_mountdec, RAflip);
  
  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR (%s): mount not initialized\n",__func__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      n = snprintf(line,SBUFFERSIZE,"Current MOUNTSTATUS = %d\n",MOUNTSTATUS);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }

  T_TRACKING = time(NULL);
  Ttrack = track_mra_mdec(TRACKrate_RA, TRACKrate_DEC);

  n = snprintf(line,SBUFFERSIZE,"Start tracking Sidereal rate (N)\n");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  MOUNTSTATUS = 5;
  SKYCAM_write_STATUS();

  compute_UTCnow();
  TIME_UTC_TRACK_STOP = TTIME_UTC + Ttrack/3600.0; // time at which tracking will stop

  return(Ttrack);
}



int SKYCAM_command_MOUNTSTOP()
{

  mount_command("/ATR",1);
  if(MOUNTSTATUS>1)
    MOUNTSTATUS = 2;
  SKYCAM_write_STATUS();

  return(0);
}



int SKYCAM_command_home() // MOUNTSTATUS -> 3
{
  int n;
  double maxmove = 0.01;

  if(MOUNTSTATUS<1)
    {
      n = snprintf(line,SBUFFERSIZE,"WARNING: CANNOT HOME MOUNT: MOUNT NOT ON\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();

      if(MOUNTSTATUS < 1)
	{
	  // power up mount
	  // MOUNTSTATUS -> 1
	  //	  DIO_power_Mount(0);
	  // sleep(2);
	  //	  DIO_power_Mount(1);
	  sleep(10);
	  MOUNTSTATUS = 1;
	  SKYCAM_write_STATUS();
	}
    }

  SKYCAM_command_homeRA(maxmove);
  SKYCAM_command_homeDEC(maxmove);

  MOUNTSTATUS = 3;
  SKYCAM_write_STATUS();
  
  return(0);
}



int SKYCAM_command_mountinit() // MOUNTSTATUS -> 3
{

  if(MOUNTSTATUS<2)
    SKYCAM_command_home(); // home mount

  return(0);
}






int SKYCAM_command_park() // MOUNTSTATUS -> 3
{
  int n;

  if(MOUNTSTATUS<2)
    {
      n = snprintf(line,SBUFFERSIZE,"WARNING: CANNOT PARK MOUNT: MOUNT NOT INITIALIZED\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_command_mountinit();
    }

  SKYCAM_command_MOUNTSTOP();

  //  mRA_home = 1.0-mRA_MERIDIAN;
  //  mDEC_home =  mDEC_NPOLE-mDEC_NPOLE;


  if(SKYCAM_command_movpos(MOUNT_PARK_RA,MOUNT_PARK_DEC)>0.0001)
    SKYCAM_command_movpos(MOUNT_PARK_RA,MOUNT_PARK_DEC);
  n = snprintf(line,SBUFFERSIZE,"MOUNT PARKED\n");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  MOUNTSTATUS = 3; // mount is parked
  SKYCAM_write_STATUS();

  return(0);
}



int SKYCAM_command_mvposFLATFIELDpos() // MOUNTSTATUS -> 6
{
  double mRA_flat, mDEC_flat;
  int n;
  
  if(MOUNTSTATUS<3)
    {
      n = snprintf(line,SBUFFERSIZE,"WARNING: CANNOT PUT MOUNT TO FLAT FIELD POSITION: MOUNT NOT INITIALIZED\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_command_mountinit();
    }

  SKYCAM_command_MOUNTSTOP();

  mRA_flat = 0.05;
  mDEC_flat = mDEC_NPOLE + 0.2;


  if(SKYCAM_command_movpos(mRA_flat,mDEC_flat)>0.0001)
    SKYCAM_command_movpos(mRA_flat,mDEC_flat);
  n = snprintf(line,SBUFFERSIZE,"MOUNT AT FLAT FIELD POSITION\n");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  MOUNTSTATUS = 6; // mount is at flat field position
  SKYCAM_write_STATUS();

  return(0);
}



int SKYCAM_SLEEP(double sleeptime)
{
  int n;

  if((MOUNTSTATUS==4)||(MOUNTSTATUS==2))
    {
      n = snprintf(line,SBUFFERSIZE,"ENTERING SLEEP MODE...\n");
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_command_MOUNTSTOP();
      SKYCAM_command_park();
      DIO_power_Cams(0);
      //      DIO_power_Mount(0);

      MOUNTSTATUS = 0;
      SKYCAM_write_STATUS();
    }

  sleep(sleeptime);
  n = snprintf(line,SBUFFERSIZE,"ASLEEP\n");
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  return(0);
}





int SKYCAM_ABORT()
{
  SKYCAM_command_MOUNTSTOP();
  SKYCAM_command_park();
  DIO_power_Cams(0);
  //  DIO_power_Mount(0);
  MOUNTSTATUS = 0;
  SKYCAM_write_STATUS();

  system("touch /skycam/STOPmonitor");

  SKYCAM_exit();

  return(0);
}















// --------------------------- TEMPERATURE & HUMIDITY ---------------------------

int make_temperature_V_table()
{
  double v;
  long i;
  double temp; // unit = C
  // result from fit from -20C to +40C
  double a = 6984.44;
  double b = -0.0645563;
  double a0 = 2438.28;
  double a1 = -43.534;
  double a3 = 0.00365908;
  
  double r1 = 20000.0/3.0;
  double r2 = 20000.0/3.0;
  double r3 = 20000.0/3.0;
  double r4 = 20000.0/3.0;
  double rx; // thermistor resistance value
  double E = 5.0; // exitation voltage
  double E1;

  //  printf("Building temperature / voltage table ...");
  fflush(stdout);
  //  temptable = (double*) malloc(sizeof(double)*TEMPTABLE_size);
  
  for(i=0;i<TEMPTABLE_size;i++)
    {
      temp = TEMPTABLE_start + TEMPTABLE_step*i;

      rx = a*exp(b*temp) + a0 + a1*temp + a3*temp*temp*temp;      
      E1 = E * (1.0/(1.0/(r1+r2)+1.0/(r3+rx)) / (1.0/(1.0/(r1+r2)+1.0/(r3+rx))+r4));
      v = -E1 * (rx/(r3+rx) - r2/(r1+r2));
      temptable[i] = v;
      //  printf("%ld %lf %lf %lf %lf\n",i,temp,rx,E1,v);      
      
    }

  TEMPTABLEOK = 1;
  //  printf("temperature / voltage table built\n");
  fflush(stdout);
  //  SKYCAM_exit();
  return(0);
}


float VtoTemp(float v)
{
  long i;
  float x0,x1,ifrac;
  float value;
  
  if(TEMPTABLEOK==0)
    make_temperature_V_table();

  i = 0;
  while((temptable[i]<v)&&(i<TEMPTABLE_size))
    i++;
  
  x0 = temptable[i-1];
  x1 = temptable[i];
  ifrac = (v-x0)/(x1-x0);
  
  value = (1.0*(i-1)+ifrac)*TEMPTABLE_step+TEMPTABLE_start;
  
  return(value);
}

int SKYCAM_command_gettemp12(long avcnt)
{
  double v1,v2;
  double v1tot = 0.0;
  double v2tot = 0.0;
  long cnt = 0;
  int n;


  if(!(aiofd<0))
    {
      n = snprintf(line, SBUFFERSIZE, "FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n", aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = open_ttyUSBport(aioUSBportNB);
  v1tot = 0.0;
  v2tot = 0.0;
  aio_analogchannel_init('A',3,2);
  aio_analogchannel_init('B',3,2);
  cnt = 0;
  while(cnt<avcnt)
    {
      v1 = VtoTemp(aio_analogchannel_value('A'));
      v2 = VtoTemp(aio_analogchannel_value('B'));
      
      v1tot += v1;
      v2tot += v2;
      cnt ++;
      printf("\r%ld %.6f %.6f  %.6f  [ %.6f  %.6f  %.6f ]",cnt,v1,v2,v1-v2,v1tot/cnt,v2tot/cnt,(v1tot-v2tot)/cnt);
      fflush(stdout);
      usleep(10000);	
    }
  
  if(close(aiofd)!=0)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = -1;
  fdcnt--;
  printf("\n");
  TEMPERATURE1 = v1tot/cnt;
  TEMPERATURE2 = v2tot/cnt;      

  TEMPERATURE = (TEMPERATURE1+TEMPERATURE2)/2.0;
  n = snprintf(line,SBUFFERSIZE,"TEMPERATURE %.6lf %.6lf %.6lf\n",TEMPERATURE1,TEMPERATURE2,TEMPERATURE1-TEMPERATURE2);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();

  return(0);
}

  
int SKYCAM_command_gethumidity(long avcnt)
{
  double v1;
  double v1tot;
  long cnt;
  int n;

  if(!(aiofd<0))
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = open_ttyUSBport(aioUSBportNB);
  v1tot = 0.0;
  aio_analogchannel_init('D',1,0);
  cnt = 0;
  while(cnt<avcnt)
    {
      v1 = aio_analogchannel_value('D');      
      v1 = (v1-0.848198)/0.031572342;
      v1 = v1/(1.0546-0.00216*TEMPERATURE);
      v1tot += v1;
      cnt ++;
      printf("\r%ld %.6f   ave = %.6f   ",cnt,v1,v1tot/cnt);
      fflush(stdout);
      usleep(10000);	
    }
  if(close(aiofd)!=0)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = -1;
  fdcnt--;
  printf("\n");
  
  HUMIDITY = v1tot/cnt;
   
  n = snprintf(line,SBUFFERSIZE,"HUMIDITY =  %.6lf %%\n",HUMIDITY);
  if(n >= SBUFFERSIZE) 
    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
  SKYCAM_log();
  
  return(0);
}


int SKYCAM_start_webcamloop(int camNbmax)
{
  char fname[SBUFFERSIZE];
  char fnamerun[SBUFFERSIZE];
  char fnamestop[SBUFFERSIZE];
  FILE *fp;
  float cam_brightness = 50.0; // [%]
  float cam_gain = 50.0; // [%]
  long NBexp = 255; // roughly 1 image per 50sec
  char command[SBUFFERSIZE];
  int hr,min,sec;
  int webcamloopOK = 1;
  int n;
  float flux;
  float tsleep = 1.0;
  int camNb;
  char webcamoptions[200];

  sprintf(webcamoptions,"-r 1600x1200 --jpeg 100");
  // sprintf(webcamoptions,"-p YUYV -r 1600x1200 --jpeg 100");

  n = snprintf(fnamerun,SBUFFERSIZE,"_webcamloop%d_run",camNbmax);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }      
 
  if((fp = fopen(fnamerun,"w"))==NULL)
    {
      C_ERRNO = errno;
      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
      exit(0);
    }
  
  n = snprintf(fnamestop,SBUFFERSIZE,"_webcamloop_stop");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }      
  
  
  fprintf(fp,"%d\n",(int) getpid());
  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }


  while(webcamloopOK==1)
    {     
      for(camNb=1;camNb<camNbmax+1;camNb++)
	{
	  compute_UTCnow();
	  hr = (int) TTIME_UTC;
	  min = (int) (60.0*(TTIME_UTC-hr));
	  sec = (int) (3600.0*(TTIME_UTC-1.0*hr-1.0/60.0*min));
	  
	  n = snprintf(command, SBUFFERSIZE, "mkdir -p %s/%04d-%02d-%02d/%s", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_WEBCAM_DIRECTORY2);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }      
	  printf("EXECUTING SYSTEM COMMAND: %s\n", command);
	  system(command);
	  
	  
	  n = snprintf(command,SBUFFERSIZE,"fswebcam -d /dev/video%d %s --set gain=%ld%% --set brightness=%ld%% -F %ld tmp/im%d.jpg", camNb, webcamoptions, (long) cam_gain, (long) cam_brightness, NBexp, camNb);      
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }      
	  
	  printf("EXECUTING: %s\n",command);
	  system(command);
	  
	  n = snprintf(command, SBUFFERSIZE, "cp tmp/im%d.jpg /var/www/im%d.jpg", camNb, camNb);      
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }      
	  
	  printf("EXECUTING: %s\n",command);
	  system(command);

	  
	  // MEASURE FLUX IN IMAGE
	  n = snprintf(command,SBUFFERSIZE,"convert ./tmp/im%d.jpg -colorspace gray -format \"%%[fx:100*mean]\" info: > ./tmp/webcam%d_lightlevel.tmp.txt",camNb,camNb);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }    
	  printf("EXECUTING: %s\n",command);
	  system(command);
	  
	  
	  n = snprintf(fname,SBUFFERSIZE,"./tmp/webcam%d_lightlevel.tmp.txt",camNb);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }
	  if((fp = fopen(fname,"r"))==NULL)
	    {
	      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	      exit(0);
	    }
	  fscanf(fp,"%f",&flux);
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  printf("------------ Flux = %f\n",flux);
	  if((fp = fopen(fname,"w"))==NULL)
	    {
	      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	      exit(0);
	    }
 
	  fprintf(fp,"%.6f %.8lf",hr+(1.0*min+1.0*sec/60.0)/60.0,flux/pow((cam_brightness/50.0)*(cam_gain/50.0),2.0)/1000.0);
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  snprintf(command,SBUFFERSIZE,"mv ./tmp/webcam%d_lightlevel.tmp.txt ./tmp/webcam%d_lightlevel.txt",camNb,camNb);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }

	  system(command);
	  
	  n = snprintf(fname,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/webcamlog_%d.txt",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_WEBCAM_DIRECTORY2,camNb);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }

	  if((fp = fopen(fname,"a"))==NULL)
	    {
	      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	      exit(0);
	    }

	  
	  fprintf(fp,"%.6f %02d:%02d:%02d %f %f %f\n",hr+(1.0*min+1.0*sec/60.0)/60.0,hr,min,sec,cam_gain,cam_brightness,flux);
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  if(flux > 31.0)
	    {
	      cam_gain -= 1.0;
	      cam_brightness -= 1.0;
	    }
	  if(flux < 29.0)
	    {
	      cam_gain += 1.0;
	      cam_brightness += 1.0;
	    }
	  if(cam_gain>99.0)
	    cam_gain = 99.0;
	  if(cam_gain<1.0)
	    cam_gain = 1.0;
	  
	  if(cam_brightness>99.0)
	    cam_brightness = 99.0;
	  if(cam_brightness<1.0)
	    cam_brightness = 1.0;
	  
	  
	  // MOVE TO FINAL LOCATION
	  n = snprintf(fname,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/%02d:%02d:%02d-cam%d.jpg",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_WEBCAM_DIRECTORY2,hr,min,sec,camNb);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }
	  
	  n = snprintf(command,SBUFFERSIZE,"mv /skycam/tmp/im%d.jpg %s",camNb,fname);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }

	  printf("EXECUTING: %s\n",command);
	  fflush(stdout);
	  system(command);
	  
	  
	  sleep(tsleep);
	  
	  
	  fp = fopen(fnamestop,"r");
	  if(fp != NULL)
	    {
	      webcamloopOK = 0;
	      if(fclose(fp)!=0)
		{
		  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		  exit(0);
		}
	    }
	  
	}
    }


  
  n = snprintf(command,SBUFFERSIZE,"rm %s",fnamerun);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }    
  printf("EXECUTING %s\n",command);
  system(command);
  
  n = snprintf(command,SBUFFERSIZE,"rm %s",fnamestop);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }      
  printf("EXECUTING %s\n",command);
  system(command);
  
  return(0);
}

int SKYCAM_command_getwebcamlum(int camNb)
{
  FILE *fp;
  float tmeasurement, tnow;
  long dt;
  int hr,min,sec;
  char fname[SBUFFERSIZE];
  int n;
  float flux;
  int OK = 1;

  compute_UTCnow();
  hr = (int) TTIME_UTC;
  min = (int) (60.0*(TTIME_UTC-hr));
  sec = (int) (3600.0*(TTIME_UTC-1.0*hr-1.0/60.0*min));
 
  n = snprintf(fname,SBUFFERSIZE,"./tmp/webcam%d_lightlevel.txt",camNb);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  
  if((fp = fopen(fname,"r"))==NULL)
    {
      printf("File %s not found\n",fname);
      OK = 0;      
    }
  else
    {
      printf("File %s found\n",fname);
      fscanf(fp,"%f %f",&tmeasurement,&flux);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      printf("------------ Flux = %f\n",flux);
      IMLEVEL_WEBCAM[camNb] = flux;
      tnow = hr+(1.0*min+1.0*sec/60.0)/60.0;
      
      dt = (long) ((tnow-tmeasurement)*3600.0+0.1);
      
      n = snprintf(line,SBUFFERSIZE,"IMLEVEL_WEBCAM %d =  %.6lf (%ld sec old)\n",camNb,IMLEVEL_WEBCAM[camNb],dt);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      printf("%s",line);
      //  SKYCAM_log();
    }

  return(OK);
}


int SKYCAM_command_getACPowerStatus(long avcnt)
{
  double v1;
  double v1tot;
  long cnt;
  int n;

  if(!(aiofd<0))
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = open_ttyUSBport(aioUSBportNB);
  v1tot = 0.0;
  aio_analogchannel_init('C',1,0);
  cnt = 0;
  while(cnt<avcnt)
    {
      v1 = aio_analogchannel_value('C');      
      v1tot += v1;
      cnt ++;
      printf("\r%ld %.6f   ave = %.6f   ",cnt,v1,v1tot/cnt);
      fflush(stdout);
      usleep(10000);	
    }
  if(close(aiofd)!=0)
    {
      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",aiofd,__func__,__LINE__);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      SKYCAM_log();
      SKYCAM_exit();
    }
  aiofd = -1;
  fdcnt--;
  
  printf("\n");
  
  ACPOWERSTATUS = v1tot/cnt;

  n = snprintf(line,SBUFFERSIZE,"ACPOWERSTATUS =  %.6lf\n",ACPOWERSTATUS);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  
  

  SKYCAM_log();
  
  return(0);
}











// ---------------------------- Camera commands -------------------------------------


int SKYCAM_command_setCAM_mode(int cam, int mode)
{
  FILE *fp;
  char command[SBUFFERSIZE];
  int n;
  char str1[SBUFFERSIZE];
  char str2[SBUFFERSIZE];
  char str3[SBUFFERSIZE];
  char str4[SBUFFERSIZE];
  char fname[SBUFFERSIZE];
  int try = 0;
  int OK = 0;
  int maxtry = 5;

  switch (mode) {

  case 0 : // no acquisition    
    dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 1);
    dio_setdigital_out(DIOCHAN_CAM_USB[cam], 0);
    CAMMODE[cam] = 0;
    SKYCAM_write_STATUS();
    break;

  case 1 : // USB mode
    try = 0;
    OK = 0;
    while((OK==0)&&(try<maxtry))
      {
	dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 1);
	sleep(1);
	dio_setdigital_out(DIOCHAN_CAM_USB[cam], 1);
	sleep(5);     
    
	sprintf(command, "gvfs-mount -u ~/.gvfs/gphoto2* > /dev/null");
	printf("EXECUTING SYSTEM COMMAND: %s\n", command);
	system(command);
	
	sleep(1);
	n = snprintf(command,SBUFFERSIZE,"gphoto2 --auto-detect | grep \"usb:0\" | grep \"%s\" > cam%dport.txt", CAMNAME[cam], cam);
	printf("CAMERA COMMAND: %s\n", command);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }
	printf("EXECUTING SYSTEM COMMAND: %s\n", command);
	system(command);
	
	sprintf(fname, "cam%dport.txt", cam);
	if((fp = fopen(fname,"r"))==NULL)
	  {
	    printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	    exit(0);
	  }
	
	if(fscanf(fp,"%s %s %s %s\n", str1, str2, str3, str4)==4)
	  {
	    n = snprintf(CAMPORT[cam], SBUFFERSIZE, "%s", str4);
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__, "Attempted to write string buffer with too many characters");
		exit(0);
	      }
	    printf("CAM%d (%s) port = %s\n", cam, CAMNAME[cam], CAMPORT[cam]);
	    OK = 1;	    
	  }
	else
	  {
	    printf("ERROR: cannot open CAM%d in USB mode ... trying again (attempt %d/%d)\n", cam, try, maxtry);
	    try++;
	    dio_setdigital_out(DIOCHAN_CAM_USB[cam], 0);
	    sleep(1);     
	  }
	if(fclose(fp)!=0)
	  {
	    printERROR( __FILE__, __func__, __LINE__, "fclose() error");
	    exit(0);
	  }
      }
    if(OK == 0)
      {
	printf("ERROR: cannot open CAM%d in USB mode ... \n", cam);
	exit(0);
      }
    CAMMODE[cam] = 1;
    SKYCAM_write_STATUS();
    break;

  case 2 : // TTL mode
    dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 1);
    sleep(1);
    dio_setdigital_out(DIOCHAN_CAM_USB[cam], 0);
    CAMMODE[cam] = 2;
    SKYCAM_write_STATUS();
    break;
  default : 
    printf("ERROR: camera mode not valid\n");
    exit(0); 
    break;
  }

  printf("Camera mode changed\n");
  fflush(stdout);

    
  return(0);
}





int SKYCAM_command_cam_listFILES(int cam)
{
  int oldCAMmode = 0;
  int n;
  char command[SBUFFERSIZE];

  // record previous mode setting
  oldCAMmode = CAMMODE[cam];

  if(oldCAMmode != 1) // not USB
    SKYCAM_command_setCAM_mode(cam, 1); // put camera in USB mode to be able to download frames
      
  n = snprintf(command,SBUFFERSIZE,"gphoto2 --port=%s -L", CAMPORT[cam]);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  printf("EXECUTING SYSTEM COMMAND: %s\n", command);
  system(command);
  

  sleep(1);
  
  if(oldCAMmode != 1) // was originally not in USB mode
    SKYCAM_command_setCAM_mode(cam, oldCAMmode); // put camera in original mode


  return(0);
}


int SKYCAM_command_cam_loadFILES(int cam)
{
  int oldCAMmode = 0;
  int n;
  char command[SBUFFERSIZE];
  char cmd1[SBUFFERSIZE];


  // record previous mode setting

  oldCAMmode = CAMMODE[cam];
  
  
  if(oldCAMmode != 1) // not USB
    SKYCAM_command_setCAM_mode(cam, 1); // put camera in USB mode to be able to download frames


  n = snprintf(command, SBUFFERSIZE, "cd tmpCR2cam%d/; gphoto2 --port=%s -P --force-overwrite; cd ..", cam, CAMPORT[cam]);
  printf("CAMERA COMMAND: %s\n", command);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
      }
  system(command);
  sleep(1);
  n = snprintf(command, SBUFFERSIZE, "gphoto2 --port=%s -R -D", CAMPORT[cam]);
  printf("CAMERA COMMAND: %s\n", command);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__, __func__, __LINE__, "Attempted to write string buffer with too many characters");
      exit(0);
    }
  
  n = snprintf(cmd1, SBUFFERSIZE, "touch /skycam/_CAMLOAD"); 
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__, __func__, __LINE__, "Attempted to write string buffer with too many characters");
      exit(0);
    }
  system(cmd1);

  
  system(command);

  n = snprintf(cmd1, SBUFFERSIZE,"touch /skycam/_SKYCAM_RUNNING"); 
  system(cmd1);

  n = snprintf(cmd1, SBUFFERSIZE, "rm /skycam/_CAMLOAD"); 
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__, __func__, __LINE__, "Attempted to write string buffer with too many characters");
      exit(0);
    }
  system(cmd1);



  sleep(1);
  
  if(oldCAMmode != 1) // was originally not in USB mode
    SKYCAM_command_setCAM_mode(cam, oldCAMmode); // put camera in original mode

  
  return(0);
}


int SKYCAM_command_archiveCAM(int cam)
{
  char command[SBUFFERSIZE];
  int n;

  n = snprintf(command,SBUFFERSIZE,"/skycam/soft/archiveCAM %d %04d-%02d-%02d",cam,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }

  system(command);

  return(0);
}



int SKYCAM_command_cam_rmFILES(int cam)
{
  int oldCAMmode = 0;
  int n;
  char command[SBUFFERSIZE];


  // record previous mode setting
  oldCAMmode = CAMMODE[cam];
  
  if(oldCAMmode != 1) // not USB
    SKYCAM_command_setCAM_mode(cam, 1); // put camera in USB mode to be able to download frames
  
  n = snprintf(command,SBUFFERSIZE,"gphoto2 --port=%s -R -D", CAMPORT[cam]);
  printf("CAMERA COMMAND: %s\n", command);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  system(command);
 

  if(oldCAMmode != 1) // was originally not in USB mode
    SKYCAM_command_setCAM_mode(cam, oldCAMmode); // put camera in original mode

  return(0);
}




int SKYCAM_command_camSetISO(int cam, int ISOmode)
{
  int oldCAMmode = 0;
  int n;
  char command[SBUFFERSIZE];


  // record previous mode setting
  oldCAMmode = CAMMODE[cam];
  
  if(oldCAMmode != 1) // not USB
    SKYCAM_command_setCAM_mode(cam, 1); // put camera in USB mode to be able to download frames
  
  
  n = snprintf(command,SBUFFERSIZE,"gphoto2 --port=%s --set-config /main/imgsettings/iso=%d", CAMPORT[cam], ISOmode);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }
  printf("CAMERA COMMAND: %s\n", command);
  system(command);
  
  sleep(1);
  
  if(oldCAMmode != 1) // was originally not in USB mode
    SKYCAM_command_setCAM_mode(cam,oldCAMmode); // put camera in original mode
  
  return(0);
}









// ---------------------------- High level SKYCAM commands ---------------------------



int SKYCAM_testcoord()
{
  FILE *fp;
  float tmpf;
  int coordOK = 0;
  int cntlim = 1000;
  int n;
  // MLO tower: 
  double COORD_ALT_1 = 15.0/180.0*M_PI;
  double COORD_AZ_1 = 289.0/180.0*M_PI;

  double dist;
  double distlim = 30.0/180.0*M_PI;


  if((fp=fopen("/skycam/config/_minelev.txt","r"))!=NULL)
    {
      fscanf(fp,"%f",&tmpf);
      observation_minelevation = tmpf;
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
    }

  if((fp=fopen("/skycam/config/_MLOtowerdistlim.txt","r"))!=NULL)
    {
      fscanf(fp,"%f",&tmpf);
      distlim = tmpf/180.0*M_PI;
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
    }


  if(COORD_ALT/M_PI*180.0>observation_minelevation)
    coordOK = 1;
  else
    {
      n = snprintf(line,SBUFFERSIZE,"PROPOSED ELEVATION TOO LOW (%+7.2f deg / %+7.2f deg)\n",COORD_ALT/M_PI*180.0,observation_minelevation);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }

  DISTMOON = 2.0*asin(sqrt( pow(sin((COORD_DEC-MOON_DEC)/2.0),2.0) + cos(COORD_DEC)*cos(MOON_DEC)*pow(sin((COORD_RA-MOON_RA)/2.0),2.0) ));

  DISTMOVE = 2.0*asin(sqrt( pow(sin((COORD_DEC-pos_DEC)/2.0),2.0) + cos(COORD_DEC)*cos(pos_DEC)*pow(sin((COORD_RA-pos_RA)/2.0),2.0) ));




  // sky viewing angle from site
  // MLO site
  
  dist = 2.0*asin(sqrt( pow(sin((COORD_ALT-COORD_ALT_1)/2.0),2.0) + cos(COORD_ALT)*cos(COORD_ALT_1)*pow(sin((COORD_AZ-COORD_AZ_1)/2.0),2.0) ));
  if(dist<distlim)
    coordOK = 0;

  // relax maximum move constraint at 50% of the count
  if((initpos==1)&&(DISTMOVE>MAXmotion/180.0*M_PI)&&(cntcoordOK<0.5*cntlim))
    {
      coordOK = 0;
      n = snprintf(line,SBUFFERSIZE,"PROPOSED MOVE TOO LARGE (%7.2f deg)\n",DISTMOVE/M_PI*180.0);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }


  if((initpos==1)&&(DISTMOVE<MINmotion/180.0*M_PI))
    {
      coordOK = 0;
      n = snprintf(line,SBUFFERSIZE,"PROPOSED MOVE TOO SMALL (%7.2f deg)\n",DISTMOVE/M_PI*180.0);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }



  if((fp=fopen("/skycam/config/_moondist_limit.txt","r"))==NULL)
    {
      //      n = snprintf(line,SBUFFERSIZE,"Cannot read file /skycam/config/_moondist_limit.txt. Using previous setting\n");
      // SKYCAM_log();
    }
  else
    {
      fscanf(fp,"%f",&tmpf);
      DISTMOON_LIMIT_DEG = tmpf;
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
    }


  if((MOON_MAGN<-5.0)&&(DISTMOON<DISTMOON_LIMIT_DEG/180.0*M_PI))
    {
      coordOK = 0;
      n = snprintf(line,SBUFFERSIZE,"PROPOSED POSITION TOO CLOSE TO MOON (%7.2f deg / %7.2f deg)\n",DISTMOON/M_PI*180.0,DISTMOON_LIMIT_DEG);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
    }       
  

  // if RAflip = +1
  // mDEC = (90.0-DEC[deg])/360.0+mDEC_NPOLE
  // 
  // if RAflip = -1
  // mDEC = -(90.0-DEC[deg])/360.0+mDEC_NPOLE
  // 
  //
  // mRA = (LST[hr]-RA[hr])/24.0-0.25*RAflip
  // 
  
	

  if(cntcoordOK>cntlim)
    {
      n = snprintf(line,SBUFFERSIZE,"FAILED TO FIND SUITABLE POINTING\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  if(coordOK == 1)
    cntcoordOK = 0;
  else
    cntcoordOK ++;

  return(coordOK);			  
}


// loops if file is recent, returns 0 if file is old
int SKYCAM_monitor_skycam()
{
  long tsleep = 10;
  time_t t_now;
  double tdiff;
  struct stat buf;
  struct stat buf1;
  char filename[SBUFFERSIZE];
  char filename1[SBUFFERSIZE];
  int OK = 1;
  FILE *fpstop;
  int n;

  n = snprintf(filename1,SBUFFERSIZE,"/skycam/_CAMLOAD"); 
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }


  while(OK==1)
    {
      if((fpstop = fopen("/skycam/STOPmonitor","r"))!=NULL)
	{
	  system("rm /skycam/STOPmonitor");
	  printf("STOPPING MONITOR\n");
	  fflush(stdout);
	  exit(0);
	}
      
      sleep(tsleep);
      t_now = time(NULL);      
      n = snprintf(filename,SBUFFERSIZE,"/skycam/_SKYCAM_RUNNING"); 
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      
      if (!stat(filename1, &buf1)) // do not kill if files are downloading
	{
	  OK = 1;
	} 
      else	
	{
	  if (!stat(filename, &buf)) 
	    {
	      tdiff = difftime(t_now,buf.st_atime);
	      printf("FILE IS %e SEC OLD\n",tdiff);
	      fflush(stdout);
	      if(tdiff>1000.0) 
		OK = 0;
	      else
		OK = 1;
	    }
	  else
	    OK = 0;      
	}
    }
  printf("SKYCAM STOPPED RUNNING\n");
  fflush(stdout);

  return(0);
}


int SKYCAM_command_observingstatus()
{
  FILE *fp;
  int cam;
  float tmpf;
  int n;
  char tmpstr[SBUFFERSIZE];
  float tmpf1;
  struct tm tm_cloudsensor;
  struct tm tm_mloweather;
  char timestring[SBUFFERSIZE];
  char command[SBUFFERSIZE];
  time_t t_cloudsensor;
  time_t t_mloweather;
  time_t t_now;
  double tdiff;
  long ts;

  OBSERVINGSTATUS = 0; // park position, do not observe
  
  system("touch /skycam/_SKYCAM_RUNNING");

  n = snprintf(command,SBUFFERSIZE,"cp %s %s&",CLOUDSENSOR_FILE,CLOUDSENSOR_FILE1);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }

  system(command);
  system("sleep 0.2");

  if((fp=fopen(CLOUDSENSOR_FILE1,"r"))==NULL)
    {
      CLOUDSENSOR_OK = 0;
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot read cloud sensor file\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      SKYCAM_log();
    }
  else
    {
      CLOUDSENSOR_OK = 1;
      fscanf(fp,"%s %s %c %c %f %f %f %f %f %f %d %d %d %ld %s %d %d %d %d %d\n",CLOUDSENSOR_DATE,CLOUDSENSOR_TIME,&CLOUDSENSOR_T,&CLOUDSENSOR_V,&CLOUDSENSOR_SKYT,&CLOUDSENSOR_AMBT,&CLOUDSENSOR_SENT,&CLOUDSENSOR_WIND,&CLOUDSENSOR_HUM,&CLOUDSENSOR_DEWPT,&CLOUDSENSOR_HEA,&CLOUDSENSOR_RAIN,&CLOUDSENSOR_WET,&CLOUDSENSOR_SINCE,tmpstr,&CLOUDSENSOR_CLOUD_COND,&CLOUDSENSOR_WIND_COND,&CLOUDSENSOR_RAIN_COND,&CLOUDSENSOR_DAYLIGHT_COND,&CLOUDSENSOR_ROOF);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
            
      if(CLOUDSENSOR_T=='F')
	{
	  tmpf1 = CLOUDSENSOR_SKYT;
	  CLOUDSENSOR_SKYT = (tmpf1-32)*5.0/9.0;
	  tmpf1 = CLOUDSENSOR_AMBT;
	  CLOUDSENSOR_AMBT = (tmpf1-32)*5.0/9.0;
	  tmpf1 = CLOUDSENSOR_SENT;
	  CLOUDSENSOR_SENT = (tmpf1-32)*5.0/9.0;            
	}
      if(CLOUDSENSOR_V=='M')
	CLOUDSENSOR_WIND *= 1.609344;
      
    
      // is data sufficiently recent ?
      n = snprintf(timestring,SBUFFERSIZE,"%s %s",CLOUDSENSOR_DATE,CLOUDSENSOR_TIME);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      strptime (timestring, "%F %X",&tm_cloudsensor);
      t_now = time(NULL);
      t_cloudsensor = mktime(&tm_cloudsensor);
      
      tdiff = difftime(t_now,t_cloudsensor);
      
      n = snprintf(line,SBUFFERSIZE,"CLOUD SENSOR MEASUREMENT IS %f sec OLD\n",tdiff);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}

      SKYCAM_log();

      if((tdiff>120.0)||(tdiff<0.0)||(CLOUDSENSOR_HUM<0.0)||(CLOUDSENSOR_HUM>150.0))
	{
	  CLOUDSENSOR_OK = 0;
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR FILE %s NOT OK: ignoring cloud sensor\n",CLOUDSENSOR_FILE);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
      else
	{
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: DATE      = %s\n",CLOUDSENSOR_DATE);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: TIME      = %s\n",CLOUDSENSOR_TIME);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: SINCE     = %ld sec\n",CLOUDSENSOR_SINCE);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Ambient T = %.2f C\n",CLOUDSENSOR_AMBT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Sky T     = %.2f C\n",CLOUDSENSOR_SKYT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Wind sp   = %.2f km/h\n",CLOUDSENSOR_WIND);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Humidity  = %.0f %%\n",CLOUDSENSOR_HUM);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();	  
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Rain      = %d (0=dry, 1=rain in last mn, 2=rain now)\n",CLOUDSENSOR_RAIN);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Wet       = %d (0=dry, 1=wet in last mn, 2=wet now)\n",CLOUDSENSOR_WET);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR: Daylight  = %d \n",CLOUDSENSOR_DAYLIGHT_COND);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
    }


  MLOWEATHER_OK = 0;
  // system("/skycam/soft/getMLOweatherFile");
  if((fp = fopen("/skycam/log/MLOweather.txt2","r"))==NULL)
    {
      MLOWEATHER_OK = 0;
    }
  else
    {
      rval = fscanf(fp,"%d %d %d %d %d %d %f %f %f %f %f %f %f\n", &MLOWEATHER_YEAR, &MLOWEATHER_MONTH, &MLOWEATHER_DAY, &MLOWEATHER_HOUR, &MLOWEATHER_MIN, &MLOWEATHER_SEC, &MLOWEATHER_WSPD10M, &MLOWEATHER_WDIR10M, &MLOWEATHER_HUM, &MLOWEATHER_TEMP2M, &MLOWEATHER_TEMP10M, &MLOWEATHER_ATM, &MLOWEATHER_PREC);
      if(rval != 13)
	MLOWEATHER_OK = 0;
      else
	{
	  // is data sufficiently recent ?
	  n = snprintf(timestring,SBUFFERSIZE,"%d-%02d-%02dT%02d:%02d:%02d.000Z",MLOWEATHER_YEAR,MLOWEATHER_MONTH,MLOWEATHER_DAY,MLOWEATHER_HOUR,MLOWEATHER_MIN,MLOWEATHER_SEC);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  strptime (timestring, "%FT%X%z",&tm_mloweather);
	  
	  ts =  mktime(&tm_mloweather) - timezone;
	  localtime_r( &ts  , &tm_mloweather);
	  //tm_mloweather.tm_year = MLOWEATHER_YEAR;
	  //tm_mloweather.tm_mon = MLOWEATHER_MONTH;
	  //tm_mloweather.tm_mday = MLOWEATHER_DAY;
	  //tm_mloweather.tm_hour = MLOWEATHER_HOUR;
	  //tm_mloweather.tm_min = MLOWEATHER_MIN;
	  //tm_mloweather.tm_sec = MLOWEATHER_SEC;
	  
	  t_now = time(NULL);
	  t_mloweather = mktime(&tm_mloweather);
	  
	  tdiff = difftime(t_now,t_mloweather);
	  
	  n = snprintf(line,SBUFFERSIZE,"MLOWEATHER SENSOR MEASUREMENT IS %f sec OLD\n",tdiff);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  
	  SKYCAM_log();
	  
	  if(tdiff>300.0)
	    {
	      CLOUDSENSOR_OK = 0;
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER FILE TOO OLD (%f sec): ignoring cloud sensor\n",tdiff);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      
	      SKYCAM_log();
	      MLOWEATHER_OK = 0;
	    }
	  else
	    {
	      MLOWEATHER_OK = 1;
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: DATE       = %d-%02d-%02d\n",MLOWEATHER_YEAR,MLOWEATHER_MONTH,MLOWEATHER_DAY);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: TIME       = %02d:%02d:%02d\n",MLOWEATHER_HOUR,MLOWEATHER_MIN,MLOWEATHER_SEC);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: WINDSPD10M = %.2f m/s\n",MLOWEATHER_WSPD10M);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: WINDDIR10M = %.2f deg\n",MLOWEATHER_WDIR10M);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: HUMIDITY   = %.2f %%\n",MLOWEATHER_HUM);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: TEMP2M     = %.2f C\n",MLOWEATHER_TEMP2M);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: TEMP10M    = %.2f C\n",MLOWEATHER_TEMP10M);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: ATM        = %.2f mB\n",MLOWEATHER_ATM);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"MLOWEATHER: PREC       = %.2f\n",MLOWEATHER_PREC);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();

	      if((MLOWEATHER_HUM<-5.0)||(MLOWEATHER_HUM>110.0))
		MLOWEATHER_OK = 0;
	      if(fabs(MLOWEATHER_HUM-MLOWEATHER_HUMprevious)>30.0)
		MLOWEATHER_OK = 0;
	      MLOWEATHER_HUMprevious = MLOWEATHER_HUM;
	    }
	}      
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
    }
  


  // WEATHER_DARK_STATUS

  SKYCAM_command_getwebcamlum(1);
  SKYCAM_command_getwebcamlum(2);
  SKYCAM_command_getwebcamlum(3);
  SKYCAM_command_getwebcamlum(4);
  get_Sun_pos();
  n = snprintf(line,SBUFFERSIZE,"SUN ELEVATION = %f deg\n",SUN_ALT/M_PI*180.0);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  // if sun high, reset NBdark, IMGindex to zero
  if(SUN_ALT>0)
    {
      NBdark = 0;
      NBflatfield = 0;
      for(cam=0; cam<4; cam++)
	IMGindex[cam] = 0;

      SKYCAM_write_STATUS();
    }

  WEATHER_DARK_STATUS = 0; // by default: too bright
  if((IMLEVEL_WEBCAM[1]<0.1)&&(IMLEVEL_WEBCAM[2]<0.1)&&(IMLEVEL_WEBCAM[3]<0.1)&&(IMLEVEL_WEBCAM[4]<0.1))
    {
      if(SUN_ALT<SUNELEV_LIMIT_DEG_SAFE/180.0*M_PI)
	WEATHER_DARK_STATUS = 1;

      if((fp=fopen("/skycam/config/_sunelev_limit.txt","r"))==NULL)
	{
	  n = snprintf(line,SBUFFERSIZE,"Cannot read file /skycam/config/_sunelev_limit.txt. Using previous setting\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
      else
	{
	  fscanf(fp,"%f",&tmpf);
	  SUNELEV_LIMIT_DEG = tmpf;
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  fp = NULL;
	}

      if(SUN_ALT<SUNELEV_LIMIT_DEG/180.0*M_PI)
	WEATHER_DARK_STATUS = 2;
      else
	{
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: Sun is too high (%f deg): not ready for observing\n",SUN_ALT/M_PI*180.0);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  

	  SKYCAM_log();
	}


      if((CLOUDSENSOR_DAYLIGHT_COND!=1)&&(CLOUDSENSOR_OK==1))
	{
	  WEATHER_DARK_STATUS = 0;
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: DAYLIGHT = %d: not ready for observing\n",CLOUDSENSOR_DAYLIGHT_COND);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  
	  SKYCAM_log();
	}
    }
        
  n = snprintf(line,SBUFFERSIZE,"WEATHER_DARK_STATUS = %d\n",WEATHER_DARK_STATUS);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  

  SKYCAM_log();
  
  







  // WEATHER_CLEAR_STATUS
  WEATHER_CLEAR_STATUS = 0;
  if((fp=fopen("/skycam/config/_humidity_limit.txt","r"))==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"Cannot read file /skycam/config/_humidity_limit.txt. Using previous setting\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  

      SKYCAM_log();
    }
  else
    {
      fscanf(fp,"%f",&tmpf);
      HUMIDITY_LIMIT = tmpf;
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
    }

  if((fp=fopen("/skycam/config/_dtemp_limit.txt","r"))==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"Cannot read file /skycam/config/_dtemp_limit.txt. Using previous setting\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      
      SKYCAM_log();
    }
  else
    {
      fscanf(fp,"%f",&tmpf);
      DTEMPERATURE_LIMIT = tmpf;
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
    }

  SKYCAM_command_gettemp12(5);
  SKYCAM_command_gethumidity(5);





  // IS SKY CLEAR ??

  WEATHER_CLEAR_STATUS = 0;

  if((CLOUDSENSOR_SKYT < -200.0)||(CLOUDSENSOR_AMBT<-50.0))
    CLOUDSENSOR_OK = 0;
  
  if(CLOUDSENSOR_OK == 1) // if VYSOS cloud sensor OK, use it 
    {
      if(CLOUDSENSOR_AMBT-CLOUDSENSOR_SKYT<CLOUDSENSOR_DTEMPERATURE_LIMIT)
	{
	  WEATHER_CLEAR_STATUS = 0;
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR_DT = %f C < %f C\n",CLOUDSENSOR_AMBT-CLOUDSENSOR_SKYT,CLOUDSENSOR_DTEMPERATURE_LIMIT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: SKY NOT CLEAR: not ready for observing\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
      else
	{
	  WEATHER_CLEAR_STATUS = 1;
	  n = snprintf(line,SBUFFERSIZE,"CLOUDSENSOR_DT = %f C > %f C\n",CLOUDSENSOR_AMBT-CLOUDSENSOR_SKYT,CLOUDSENSOR_DTEMPERATURE_LIMIT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: SKY CLEAR\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
    }
  else // use crude sensor
    {
      if(TEMPERATURE1-TEMPERATURE2<DTEMPERATURE_LIMIT)
	{
	  WEATHER_CLEAR_STATUS = 0;
	  n = snprintf(line,SBUFFERSIZE,"DT = %f C < %f C\n",TEMPERATURE1-TEMPERATURE2,DTEMPERATURE_LIMIT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: SKY NOT CLEAR: not ready for observing\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
		}	  
	  SKYCAM_log();
	}
      else
	{
	  WEATHER_CLEAR_STATUS = 1;
	  n = snprintf(line,SBUFFERSIZE,"DT = %f C > %f C\n",TEMPERATURE1-TEMPERATURE2,DTEMPERATURE_LIMIT);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: SKY CLEAR\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	}
    }




  // IS HUMIDITY LOW ENOUGH ?

  if(HUMIDITY>HUMIDITY_LIMIT)
    {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: HUMIDITY TOO HIGH [%5.2f %5.2f]: not ready for observing\n",HUMIDITY,HUMIDITY_LIMIT);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }
  
  if((CLOUDSENSOR_OK == 1)&&(CLOUDSENSOR_HUM>HUMIDITY_LIMIT))
     {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: CLOUD SENSOR HUMIDITY TOO HIGH [%5.2f %5.2f]: not ready for observing\n",HUMIDITY,HUMIDITY_LIMIT);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }

  if((MLOWEATHER_OK == 1)&&(MLOWEATHER_HUM>HUMIDITY_LIMIT))
     {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: MLO SENSOR HUMIDITY TOO HIGH [%5.2f %5.2f]: not ready for observing\n",MLOWEATHER_HUM,HUMIDITY_LIMIT);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }


  if((HUMIDITY<-5.0)&&(CLOUDSENSOR_OK==0))
    {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: HUMIDITY SENSOR OFF [%5.2f]: not ready for observing\n",HUMIDITY);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }

  if((CLOUDSENSOR_RAIN>0)&&(CLOUDSENSOR_OK==1))
    {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: RAINY WEATHER: not ready for observing\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }

  if((CLOUDSENSOR_WET>0)&&(CLOUDSENSOR_OK==1))
    {
      WEATHER_CLEAR_STATUS = 0;
      n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: WET WEATHER: not ready for observing\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
    }




  if(WEATHER_CLEAR_STATUS==1)
    WeatherOK_cnt ++;
  else
    WeatherOK_cnt = 0;
  

  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: NUMBER OF CONSECUTIVE CLEAR SKY MEASUREMENTS = %ld (%ld)\n",WeatherOK_cnt,WeatherOK_cnt_lim);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  if(WeatherOK_cnt < WeatherOK_cnt_lim)
    WEATHER_CLEAR_STATUS = 0;



  OBSERVINGSTATUS = 0;

  if(WEATHER_DARK_STATUS == 0) // DO NOT OBSERVE (daylight)
    OBSERVINGSTATUS = 0;
  

  if(WEATHER_DARK_STATUS == 1) // twighlight
    {
      if(WEATHER_CLEAR_STATUS == 0) 
	OBSERVINGSTATUS = 0;
      if(WEATHER_CLEAR_STATUS == 1)
	OBSERVINGSTATUS = 2; // take flats
    }
    
  if(WEATHER_DARK_STATUS == 2) // dark
    {
      OBSERVINGSTATUS = 0;
      if(WEATHER_CLEAR_STATUS == 0)
	{
	  if((SUN_ALT<-15.0/180.0*M_PI)&&(MOON_ALT<-10.0/180.0*M_PI))
	    OBSERVINGSTATUS = 1; // take darks
	  else
	    OBSERVINGSTATUS = 0;
	}
      if(WEATHER_CLEAR_STATUS == 1)
	OBSERVINGSTATUS = 3; // observe
    }

  n = snprintf(line,SBUFFERSIZE,"OBSERVING STATUS: %d\n",OBSERVINGSTATUS);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log(); 

  if(OBSERVINGSTATUS!=0)
    {
      if((fp=fopen(SKYCAM_FORCEOBSERVINGSTATUSFILE,"r"))!=NULL)
	{
	  fscanf(fp,"%d",&OBSERVINGSTATUS);
	  
	  n = snprintf(line, SBUFFERSIZE, "FORCED OBSERVING STATUS TO %d\n",OBSERVINGSTATUS);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  

	  SKYCAM_log(); 
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  fp = NULL;
	}
    }


  if((OBSERVINGSTATUS<0)||(OBSERVINGSTATUS>3))
    {
      printf("ERROR: invalid Observing status");
      SKYCAM_exit();
    }

  printf("[ OBSERVING STATUS = %d ]\n",OBSERVINGSTATUS);
  fflush(stdout);

  return(OBSERVINGSTATUS);
}





int SKYCAM_command_init()
{
  //  double azerr, alterr;
  int n;
  int OK;
  
  n = snprintf(line,SBUFFERSIZE,"SKYCAM_command_init\n");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  n = snprintf(line,SBUFFERSIZE,"SKYCAM_command_init: starting\n");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();
  
  // scan USB ports up to 2 times
  OK = scanttyUSBports();
  if(dioUSBportNB == -1)
    {
      sleep(1);
      scanttyUSBports();
      if(dioUSBportNB == -1)
	{
	  n = snprintf(line,SBUFFERSIZE,"ERROR: cannot detect DIO board on USB\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  SKYCAM_exit();
	}
    }
  if(OK == -1)
    {
      DIO_power_Cams(1);
      sleep(10);
      
      if(scanttyUSBports()==-1)
	{
	  sleep(1);
	  if(scanttyUSBports()==-1)
	    {
	      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot detect DIO board + mount on USB\n");
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      SKYCAM_exit();
	    }
	}
    }

  if(MOUNTSTATUS<3)
    SKYCAM_command_mountinit();
  

  /*
  if(!(mountfd<0))
    {
      printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
      SKYCAM_exit();
    }
  mountfd = open_ttyUSBport(mountUSBportNB);
  
  set_posmount_radec(1.0-mRA_MERIDIAN,mDEC_NPOLE-mDEC_NPOLE);
  
  if(close(mountfd)!=0)
    {
      printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
      SKYCAM_exit();
    }
  mountfd = -1;
  */
  


  // CAMERA INIT
  //  system("/skycam/soft/initCamera");
 
  n = snprintf(line,SBUFFERSIZE,"SKYCAM_command_init: completed\n");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  return(0);
}



int SKYCAM_print_imgheader_basic(char *fname)
{
  FILE *fp;

  // BASIC INFO
  if((fp = fopen(fname,"w"))==NULL)
    {
      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
      exit(0);
    }	  

  fprintf(fp,"IMTYPE:               %s\n",IMGHEADER_IMTYPE);
  fprintf(fp,"TARGETDESCRIPTION:    %s\n",IMGHEADER_TARGETDESCRIPTION);
  fprintf(fp,"UT_START:             %.4f hr\n", IMGHEADER_UT_START);
  fprintf(fp,"SHUTTER:              %f sec\n",IMGHEADER_SHUTTER);
  fprintf(fp,"ISO:                  %d\n",IMGHEADER_ISO);
  fprintf(fp,"RA:                   %.4f deg\n",IMGHEADER_RA);
  fprintf(fp,"DEC:                  %.4f deg\n",IMGHEADER_DEC);
  fprintf(fp,"ALT:                  %.4f deg\n",IMGHEADER_ALT);
  fprintf(fp,"AZ:                   %.4f deg\n",IMGHEADER_AZ);

  fprintf(fp,"\n");

  fprintf(fp,"MOUNTDEC:             %.8f\n",    IMGHEADER_MOUNTDEC);
  fprintf(fp,"MOUNTRA:              %.8f\n",    IMGHEADER_MOUNTRA);
  fprintf(fp,"MOUNT_TRACKrateDEC:   %.5f steps/sec\n",    IMGHEADER_MOUNT_TRACKrate_DEC);
  fprintf(fp,"MOUNT_TRACKrateRA:    %.5f steps/sec\n",    IMGHEADER_MOUNT_TRACKrate_RA);  
  fprintf(fp,"MOUNT_RAflip:         %.d\n",     IMGHEADER_MOUNT_RAflip);

  fprintf(fp,"\n");

  fprintf(fp,"TEMPERATURE1:         %.4f C\n",  IMGHEADER_TEMPERATURE1);
  fprintf(fp,"TEMPERATURE2:         %.4f C\n",  IMGHEADER_TEMPERATURE2);
  fprintf(fp,"HUMIDITY:             %.4f %%\n", IMGHEADER_HUMIDITY);
  fprintf(fp,"CLOUDSENSOR_OK:       %d\n",CLOUDSENSOR_OK);
  fprintf(fp,"CLOUDSENSOR_DATE:     %s\n",CLOUDSENSOR_DATE);
  fprintf(fp,"CLOUDSENSOR_TIME:     %s\n",CLOUDSENSOR_TIME);
  fprintf(fp,"CLOUDSENSOR_SINCE:    %ld sec\n",CLOUDSENSOR_SINCE);
  fprintf(fp,"CLOUDSENSOR_AmbientT: %.2f C\n",CLOUDSENSOR_AMBT);
  fprintf(fp,"CLOUDSENSOR_SkyT:     %.2f C\n",CLOUDSENSOR_SKYT);
  fprintf(fp,"CLOUDSENSOR_WINDSP:   %.2f km/h\n",CLOUDSENSOR_WIND);
  fprintf(fp,"CLOUDSENSOR_HUM:      %.0f %%\n",CLOUDSENSOR_HUM);
  fprintf(fp,"CLOUDSENSOR_RAIN:     %d (0=dry, 1=rain in last mn, 2=rain now)\n",CLOUDSENSOR_RAIN);
  fprintf(fp,"CLOUDSENSOR_WET:      %d (0=dry, 1=wet in last mn, 2=wet now)\n",CLOUDSENSOR_WET);

  fprintf(fp,"\n");

  fprintf(fp,"MLOWEATHER_OK:        %d\n",MLOWEATHER_OK);
  fprintf(fp,"MLOWEATHER_DATE:      %d-%02d-%02d\n",MLOWEATHER_YEAR,MLOWEATHER_MONTH,MLOWEATHER_DAY);
  fprintf(fp,"MLOWEATHER_TIME:      %02d:%02d:%02d UT\n",MLOWEATHER_HOUR,MLOWEATHER_MIN,MLOWEATHER_SEC);
  fprintf(fp,"MLOWEATHER_WSPD10M:   %.2f m/s\n",MLOWEATHER_WSPD10M);
  fprintf(fp,"MLOWEATHER_WDIR10M:   %.2f deg\n",MLOWEATHER_WDIR10M);
  fprintf(fp,"MLOWEATHER_HUM:       %.2f %%\n",MLOWEATHER_HUM);
  fprintf(fp,"MLOWEATHER_TEMP2M:    %.2f C\n",MLOWEATHER_TEMP2M);
  fprintf(fp,"MLOWEATHER_TEMP10M:   %.2f C\n",MLOWEATHER_TEMP10M);
  fprintf(fp,"MLOWEATHER_ATM:       %.2f mB\n",MLOWEATHER_ATM);
  fprintf(fp,"MLOWEATHER_PREC:      %.2f\n",MLOWEATHER_PREC);

  fprintf(fp,"\n");

  fprintf(fp,"MOONALT:              %.4f deg\n",IMGHEADER_MOON_ALT);
  fprintf(fp,"MOONAZ:               %.4f deg\n",IMGHEADER_MOON_AZ);
  fprintf(fp,"MOONMAGN:             %.4f\n",    IMGHEADER_MOON_MAGN);
  fprintf(fp,"SUNALT:               %.4f deg\n",IMGHEADER_SUN_ALT);
  fprintf(fp,"SUNAZ:                %.4f deg\n",IMGHEADER_SUN_AZ);
  fprintf(fp,"WEATHERDARKSTATUS:    %d\n",IMGHEADER_WEATHERDARKSTATUS);
  fprintf(fp,"WEATHERCLEARSTATUS:   %d\n",IMGHEADER_WEATHERCLEARSTATUS);
  fprintf(fp,"OBSERVINGSTATUS:      %d\n",IMGHEADER_OBSERVINGSTATUS);

  fprintf(fp,"\n");
  fprintf(fp,"\n");

  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  fp = NULL;
  

  return(0);
}


int SKYCAM_write_imgheader_analysis(char *fnameimg, char *fname, char *jpegfnameimg)
{
  long n;
  char commandline[SBUFFERSIZE];

  n = snprintf(commandline, SBUFFERSIZE, "dcraw -i -v %s > ./header1.info.tmp; ./soft/imanalyzeCR2 %s; mv ./tmpjpeg.jpeg %s;cat ./header0.info.tmp ./imstat.info.txt > %s; rm ./header0.info.tmp", fnameimg, fnameimg, jpegfnameimg, fname);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  
  system(commandline);
    
  return(0);
}



int SKYCAM_write_imgheader()
{
  FILE *fp;
  int cam;
  char fname[4][SBUFFERSIZE];
  char fnameimg[4][SBUFFERSIZE];
  char jpegfnameimg[4][SBUFFERSIZE];

  //  char fname2[SBUFFERSIZE];
  //  char fname2img[SBUFFERSIZE];
  //  char jpegfname2img[SBUFFERSIZE];

  char commandline[SBUFFERSIZE];
  long cnt = 0;
  //char line[SBUFFERSIZE];
  char keyw[200];
  char cont[200];
  int read;
  int n;

  // while _IMG_header_write is not here, wait for 1 SEC
  while((fp=fopen("/skycam/_IMG_header_write_OK","r"))==NULL)
    {
      cnt ++;
      usleep(100000); // wait 0.1s
      if(cnt>10)
	system("touch /skycam/_IMG_header_write_OK");
    }
  if(fclose(fp)!=0)
    {
      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
      exit(0);
    }
  fp = NULL;  
  system("rm /skycam/_IMG_header_write_OK");

  for(cam=0; cam<4; cam++)
    {
      n = snprintf(fname[cam], SBUFFERSIZE, "%s/%04d-%02d-%02d/%s/IMG%d_%04ld.info", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_HEADER_DIRECTORY2, cam, IMGindex[cam]);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      
      n = snprintf(fnameimg[cam], SBUFFERSIZE, "%s/%04d-%02d-%02d/%s/IMG%d_%04ld.CR2", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_CR2_DIRECTORY2, cam, IMGindex[cam]);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
  
      n = snprintf(jpegfnameimg[cam], SBUFFERSIZE, "%s/%04d-%02d-%02d/%s/IMG%d_%04ld.CR2.jpeg", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_JPEG_DIRECTORY2, cam, IMGindex[cam]);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
    }      
  
  
  // COMMON INFO
  SKYCAM_print_imgheader_basic("/skycam/header0.info.tmp");



  // COPY TO HEADER FILE(S)
  for(cam=0; cam<4; cam++)
    { 
      switch (CAMMODE[cam]) {
      case 1 : // USB
	n = snprintf(commandline, SBUFFERSIZE, "dcraw -i -v %s > /skycam/header1.info.tmp; /skycam/soft/imanalyzeCR2 %s; mv /skycam/tmpjpeg.jpeg %s;cat /skycam/header0.info.tmp /skycam/header1.info.tmp /skycam/imstat.info.txt > %s; rm /skycam/header1.info.tmp; touch /skycam/_IMG_header_write_OK &", fnameimg[cam], fnameimg[cam], jpegfnameimg[cam], fname[cam]);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }	  
    
	n = snprintf(line, SBUFFERSIZE,"executing: %s\n", commandline);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }	  
	
	SKYCAM_log();
	system(commandline);
	// read percentile50 value from imstat.info.txt
	read = 0;
	if((fp=fopen("/skycam/imstat.info.txt","r"))==NULL)
	  {
	    n = snprintf(line,SBUFFERSIZE,"ERROR: cannot read /skycam/imstat.info.txt");
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    
	    SKYCAM_log();
	  }
	else
	  {
	    while(fgets(line, 1000, fp)!=NULL)
	      {
		sscanf(line, "%s %s", keyw, cont);
		if(strcmp(keyw, "BLUE_percentile50")==0)
		  {
		    IMAGE_percB50 = atof(cont);
		    read = 1;
		  }
	      }
	    if(read==0)
	      IMAGE_percB50 = -1.0;            
	    if(fclose(fp)!=0)
	      {
		printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		exit(0);
	      }
	    fp = NULL;
	  }
	// read percentile90 value from imstat.info.txt
	read = 0;
	if((fp=fopen("/skycam/imstat.info.txt","r"))==NULL)
	  {
	    n = snprintf(line,SBUFFERSIZE,"ERROR: cannot read /skycam/imstat.info.txt");
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    
	    SKYCAM_log();
	  }
	else
	  {
	    while(fgets(line,1000,fp)!=NULL)
	      {
		sscanf(line,"%s %s",keyw,cont);
		if(strcmp(keyw,"percentile90")==0)
		  {
		    IMAGE_perc90 = atof(cont);
		    read = 1;
		  }
	      }
	    if(read==0)
	      IMAGE_perc90 = -1.0;            
	    if(fclose(fp)!=0)
	      {
		printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		exit(0);
	      }
	    fp = NULL;
	  }
	system("rm /skycam/imstat.info.txt");
	break;

      case 2 : // TTL
	n = snprintf(commandline, SBUFFERSIZE, "cat /skycam/header0.info.tmp > %s; touch /skycam/_IMG_header_write_OK &", fname[cam]);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }	  
	
	n = snprintf(line,SBUFFERSIZE,"executing: %s\n",commandline);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }	  
	
	SKYCAM_log();
	system(commandline);
	break;
      }
    }

 
  
  system("rm /skycam/header0.info.tmp");
  
  return(0);
}



//
// set IMGHEADERupdate to 1 if new updated header should be generated
//
int SKYCAM_read_imgheader(int timeutc_yr, int timeutc_mon, int timeutc_day, int camera, long index, int IMGHEADERupdate)
{
  //  FILE *fp;

  char fnameinfo[SBUFFERSIZE];
  char fnameimstat[SBUFFERSIZE];
  char fnameimastrom[SBUFFERSIZE];
  char fnameimg[SBUFFERSIZE];
  char jpegfnameimg[SBUFFERSIZE];

  //  char commandline[SBUFFERSIZE];
  //  long cnt = 0;
  //char line[SBUFFERSIZE];
  //  char keyw[SBUFFERSIZE];
  char content[SBUFFERSIZE];
  char content1[SBUFFERSIZE];
  int read, read1;
  int n;
  
  //  double RAest, DECest;

  //  struct stat file_st;
  //  int info_exists = 0;
  //  int raw_exists = 0;
  //  int jpeg_exists = 0;



  TIME_UTC_YR = timeutc_yr;
  TIME_UTC_MON = timeutc_mon;
  TIME_UTC_DAY = timeutc_day;
      

  n = snprintf(fnameinfo,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/IMG%d_%04ld.info",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2,camera,index);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  

  n = snprintf(fnameimstat,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/IMG%d_%04ld.imstat",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2,camera,index);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  


  n = snprintf(fnameimastrom,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/IMG%d_%04ld.astrom",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2,camera,index);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  
  n = snprintf(fnameimg,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/IMG%d_%04ld.CR2",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_CR2_DIRECTORY2,camera,index);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  

  n = snprintf(jpegfnameimg,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s/IMG%d_%04ld.CR2.jpeg",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_JPEG_DIRECTORY2,camera,index);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  
  
  // TEST IF FILES EXIST
  //  if(stat(fnameinfo,&file_st) == 0)
  //  info_exists = 1;
  
  //  if(stat(fnameimg,&file_st) == 0)
  // raw_exists = 1;
  
  //if(stat(jpegfnameimg,&file_st) == 0)
  //  jpeg_exists = 1;
  
  

  // READ PARAMETERS


  // BASIC PARAMETERS 

  read = read_parameter(fnameinfo,"IMTYPE:",content);
  n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"%s",content);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  printf("------- IMTYPE:               %s\n",IMGHEADER_IMTYPE);

  read = read_parameter(fnameinfo,"TARGETDESCRIPTION:",content);
  n = snprintf(IMGHEADER_TARGETDESCRIPTION,SBUFFERSIZE,"%s",content);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  printf("------- TARGETDESCRIPTION:    %s\n",IMGHEADER_TARGETDESCRIPTION);

  read = read_parameter(fnameinfo,"UT_START:",content);
  IMGHEADER_UT_START = atof(content);
  printf("------- UT_START:             %.4f hr\n",IMGHEADER_UT_START);


  compute_LST(SITE_LONG, TIME_UTC_YR, TIME_UTC_MON, 1.0*TIME_UTC_DAY+1.0*IMGHEADER_UT_START/24.0);

  read = read_parameter(fnameinfo,"RA:",content);
  IMGHEADER_RA = atof(content);
  printf("------- RA:                   %.4f deg\n",IMGHEADER_RA);

  read = read_parameter(fnameinfo,"DEC:",content);
  IMGHEADER_DEC = atof(content);
  printf("------- DEC:                  %.4f deg\n",IMGHEADER_DEC);

  compute_coordinates_from_RA_DEC(IMGHEADER_RA/180.0*M_PI, IMGHEADER_DEC/180.0*M_PI);
    
  read = read_parameter(fnameinfo,"ALT:",content);
  IMGHEADER_ALT = atof(content);
  printf("------- ALT:                  %.4f deg\n",IMGHEADER_ALT);

  read = read_parameter(fnameinfo,"AZ:",content);
  IMGHEADER_AZ = atof(content);
  printf("------- AZ:                   %.4f deg\n",IMGHEADER_AZ);

  printf("ALT : %.4f deg\n",COORD_ALT/M_PI*180.0);
  printf("AZ  : %.4f deg\n",COORD_AZ/M_PI*180.0);

  
  /*  if((fabs(COORD_ALT/M_PI*180.0-IMGHEADER_ALT)>0.1)||(fabs(COORD_AZ/M_PI*180.0-IMGHEADER_AZ)>0.1))
    {
      printf("******************** WRONG ALT AZ ***************\n");
      fp=fopen("errlogaltaz.txt","a");
      fprintf(fp,"%d %d %d %f %.4f %.4f\n",TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMGHEADER_UT_START, COORD_ALT/M_PI*180.0-IMGHEADER_ALT, COORD_AZ/M_PI*180.0-IMGHEADER_AZ);
      fclose(fp);
      if(strncmp(IMGHEADER_IMTYPE,"OBJECT",strlen("OBJECT"))==0)
	exit(0);    
	}*/



  read = read_parameter(fnameinfo,"MOUNTRA:",content);
  IMGHEADER_MOUNTRA = atof(content);
  printf("------- MOUNTRA:              %.4f\n",IMGHEADER_MOUNTRA);

  read = read_parameter(fnameinfo,"MOUNTDEC:",content);
  IMGHEADER_MOUNTDEC = atof(content);
  printf("------- MOUNTDEC:             %.4f\n",IMGHEADER_MOUNTDEC);


  if(IMGHEADER_MOUNTDEC<mDEC_NPOLE)
    RAflip = -1;
  else
    RAflip = 1;

  //  RAest = 360.0* ((-(IMGHEADER_MOUNTRA - mRA_MERIDIAN) - 0.25*RAflip) + (TIME_LST/24.0));
  // DECest  = 90.0-360.0*fabs(IMGHEADER_MOUNTDEC - mDEC_NPOLE);
  
  /*
  if((fabs(RAest-IMGHEADER_RA)>0.1)||(fabs(DECest-IMGHEADER_DEC)>0.1))
    {
      printf("******************** WRONG RA DEC ***************\n");
      fp=fopen("errlogradec.txt","a");
      fprintf(fp,"%d %d %d %f %.4f %.4f\n",TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMGHEADER_UT_START, RAest-IMGHEADER_RA, DECest-IMGHEADER_DEC);
      fclose(fp);

      IMGHEADER_RA = RAest;
      printf("------- RA:                   %.4f deg\n",IMGHEADER_RA);
      IMGHEADER_DEC = DECest;
      printf("------- DEC:                   %.4f deg\n",IMGHEADER_DEC);
      compute_coordinates_from_RA_DEC(IMGHEADER_RA/180.0*M_PI, IMGHEADER_DEC/180.0*M_PI);
      IMGHEADER_ALT = COORD_ALT/M_PI*180.0;
      IMGHEADER_AZ = COORD_AZ/M_PI*180.0;
      if(strncmp(IMGHEADER_IMTYPE,"OBJECT",strlen("OBJECT"))==0)
	exit(0);
    }
  */

  read = read_parameter(fnameinfo,"MOUNT_TRACKrateRA:",content);
  read1 = read_parameter(fnameinfo,"MOUNT_TRACKrateDEC:",content1);
  if((read==0)||(read1==0))
    {
      if(strncmp(IMGHEADER_IMTYPE,"OBJECT",strlen("OBJECT"))==0)
	{
	  IMGHEADER_MOUNT_TRACKrate_RA = 417.0; // steps per sec	  
	  IMGHEADER_MOUNT_TRACKrate_DEC = 0.0; // steps per sec
	}
      else
	{
	  IMGHEADER_MOUNT_TRACKrate_RA = 0.0;	  
	  IMGHEADER_MOUNT_TRACKrate_DEC = 0.0;	  
	}
    }
  else
    {
      IMGHEADER_MOUNT_TRACKrate_RA = atof(content);
      IMGHEADER_MOUNT_TRACKrate_DEC = atof(content1);
      if(fabs(IMGHEADER_MOUNT_TRACKrate_RA-15.0)<0.01)
	{
	  IMGHEADER_MOUNT_TRACKrate_RA = 417.0;
	  IMGHEADER_MOUNT_TRACKrate_DEC = 0.0;
	}
    }

  printf("------- MOUNT_TRACKrateRA:    %.4f steps/sec\n",IMGHEADER_MOUNT_TRACKrate_RA);
  printf("------- MOUNT_TRACKrateDEC:   %.4f steps/sec\n",IMGHEADER_MOUNT_TRACKrate_DEC);
    
  read = read_parameter(fnameinfo,"MOUNT_RAflip:",content);
  if(read==0)
    IMGHEADER_MOUNT_RAflip = RAflip;
  else
    IMGHEADER_MOUNT_RAflip = atoi(content);
  printf("------- MOUNT_RAflip:         %d\n",IMGHEADER_MOUNT_RAflip);


  // CORRECT FOR RA DEC ERROR
  /*  if(TIME_UTC_YR==2011)
    {
      if((TIME_UTC_MON<10)&&(TIME_UTC_DAY<16)) // before sept 16
	if((TIME_UTC_MON>7)) // Aug or later
	  {
	    IMGHEADER_RA += 86.4;
	    compute_coordinates_from_RA_DEC(IMGHEADER_RA/180.0*M_PI, IMGHEADER_DEC/180.0*M_PI);
	    IMGHEADER_ALT = COORD_ALT/M_PI*180.0;
	    IMGHEADER_AZ = COORD_AZ/M_PI*180.0;		    
	  }
    }
  */

  read = read_parameter(fnameinfo,"TEMPERATURE1:",content);
  IMGHEADER_TEMPERATURE1 = atof(content);
  printf("------- TEMPERATURE1:         %.4f C\n",IMGHEADER_TEMPERATURE1);

  read = read_parameter(fnameinfo,"TEMPERATURE2:",content);
  IMGHEADER_TEMPERATURE2 = atof(content);
  printf("------- TEMPERATURE2:         %.4f C\n",IMGHEADER_TEMPERATURE2);

  read = read_parameter(fnameinfo,"HUMIDITY:",content);
  IMGHEADER_HUMIDITY = atof(content);
  printf("------- HUMIDITY:             %.4f %%\n",IMGHEADER_HUMIDITY);

  read = read_parameter(fnameinfo,"CLOUDSENSOR_OK:",content);
  if(read==1)
    {
      CLOUDSENSOR_OK = atoi(content);
      printf("------- CLOUDSENSOR_OK:       %d\n",CLOUDSENSOR_OK);
    }
  else
    {
      read = read_parameter(fnameinfo,"CLOUDSENSOR_OK",content);
      CLOUDSENSOR_OK = atoi(content);
      printf("------- CLOUDSENSOR_OK:       %d\n",CLOUDSENSOR_OK);
    }


  read = read_parameter(fnameinfo,"CLOUDSENSOR_DATE:",content);
  if(read==1)
    {
      n = snprintf(CLOUDSENSOR_DATE,SBUFFERSIZE,"%s",content);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("------- CLOUDSENSOR_DATE:     %s\n",CLOUDSENSOR_DATE);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_TIME:",content);
  if(read==1)
    {
      n = snprintf(CLOUDSENSOR_TIME,SBUFFERSIZE,"%s",content);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("------- CLOUDSENSOR_TIME:     %s\n",CLOUDSENSOR_TIME);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_SINCE:",content);
  if(read==1)
    {
      CLOUDSENSOR_SINCE = atol(content);
      printf("------- CLOUDSENSOR_SINCE:    %ld sec\n",CLOUDSENSOR_SINCE);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_AmbientT:",content);
  if(read==1)
    {
      CLOUDSENSOR_AMBT = atof(content);
      printf("------- CLOUDSENSOR_AmbientT: %.2f C\n",CLOUDSENSOR_AMBT);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_SkyT:",content);
  if(read==1)
    {
      CLOUDSENSOR_SKYT = atof(content);
      printf("------- CLOUDSENSOR_SkyT:     %.2f C\n",CLOUDSENSOR_SKYT);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_WINDSP:",content);
  if(read==1)
    {
      CLOUDSENSOR_WIND = atof(content);
      printf("------- CLOUDSENSOR_WINDSP:   %.2f km/h\n",CLOUDSENSOR_WIND);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_HUM:",content);
  if(read==1)
    {
      CLOUDSENSOR_HUM = atof(content);
      printf("------- CLOUDSENSOR_HUM:      %.0f %%\n",CLOUDSENSOR_HUM);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_RAIN:",content);
  if(read==1)
    {
      CLOUDSENSOR_RAIN = atoi(content);
      printf("------- CLOUDSENSOR_RAIN:     %d (0=dry, 1=rain in last mn, 2=rain now)\n",CLOUDSENSOR_RAIN);
    }

  read = read_parameter(fnameinfo,"CLOUDSENSOR_WET:",content);
  if(read==1)
    {
      CLOUDSENSOR_WET = atoi(content);
      printf("------- CLOUDSENSOR_WET:      %d (0=dry, 1=wet in last mn, 2=wet now)\n",CLOUDSENSOR_WET);
    }
  

  read = read_parameter(fnameinfo,"MLOWEATHER_OK:",content);
  MLOWEATHER_OK = atoi(content);
  printf("------- MLOWEATHER_OK:        %d\n",MLOWEATHER_OK);

  read = read_parameter(fnameinfo,"MLOWEATHER_DATE:",content);
  n = snprintf(MLOWEATHER_DATE,SBUFFERSIZE,"%s",content);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  printf("------- MLOWEATHER_DATE:      %s\n",MLOWEATHER_DATE);

  read = read_parameter(fnameinfo,"MLOWEATHER_TIME:",content);
  n = snprintf(MLOWEATHER_TIME,SBUFFERSIZE,"%s",content);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  printf("------- MLOWEATHER_TIME:      %s\n",MLOWEATHER_TIME);

  read = read_parameter(fnameinfo,"MLOWEATHER_WSPD10M:",content);
  MLOWEATHER_WSPD10M = atof(content);
  printf("------- MLOWEATHER_WSPD10M:   %.2f m/s\n",MLOWEATHER_WSPD10M);

  read = read_parameter(fnameinfo,"MLOWEATHER_WDIR10M:",content);
  MLOWEATHER_WDIR10M = atof(content);
  printf("------- MLOWEATHER_WDIR10M:   %.2f deg\n",MLOWEATHER_WDIR10M);

  read = read_parameter(fnameinfo,"MLOWEATHER_HUM:",content);
  MLOWEATHER_HUM = atof(content);
  printf("------- MLOWEATHER_HUM:       %.2f %%\n",MLOWEATHER_HUM);

  read = read_parameter(fnameinfo,"MLOWEATHER_TEMP2M:",content);
  MLOWEATHER_TEMP2M = atof(content);
  printf("------- MLOWEATHER_TEMP2M:    %.2f C\n",MLOWEATHER_TEMP2M);

  read = read_parameter(fnameinfo,"MLOWEATHER_TEMP10M:",content);
  MLOWEATHER_TEMP10M = atof(content);
  printf("------- MLOWEATHER_TEMP10M:   %.2f C\n",MLOWEATHER_TEMP10M);

  read = read_parameter(fnameinfo,"MLOWEATHER_ATM:",content);
  MLOWEATHER_ATM = atof(content);
  printf("------- MLOWEATHER_ATM:       %.2f mB\n",MLOWEATHER_ATM);

  read = read_parameter(fnameinfo,"MLOWEATHER_PREC:",content);
  MLOWEATHER_PREC = atof(content);
  printf("------- MLOWEATHER_PREC:      %.2f\n",MLOWEATHER_PREC);


  read = read_parameter(fnameinfo,"MOONALT:",content);
  IMGHEADER_MOON_ALT = atof(content);
  printf("------- MOONALT:              %.2f deg\n",IMGHEADER_MOON_ALT);

  read = read_parameter(fnameinfo,"MOONAZ:",content);
  IMGHEADER_MOON_AZ = atof(content);
  printf("------- MOONAZ:               %.2f deg\n",IMGHEADER_MOON_ALT);

  read = read_parameter(fnameinfo,"MOONMAGN:",content);
  IMGHEADER_MOON_MAGN = atof(content);
  printf("------- MOONMAGN:             %.2f\n",IMGHEADER_MOON_MAGN);

  read = read_parameter(fnameinfo,"SUNALT:",content);
  IMGHEADER_SUN_ALT = atof(content);
  printf("------- SUNALT:               %.2f deg\n",IMGHEADER_SUN_ALT);

  read = read_parameter(fnameinfo,"MOONAZ:",content);
  IMGHEADER_SUN_AZ = atof(content);
  printf("------- SUNAZ:                %.2f deg\n",IMGHEADER_SUN_ALT);


  read = read_parameter(fnameinfo,"WEATHERDARKSTATUS:",content);
  IMGHEADER_WEATHERDARKSTATUS = atoi(content);
  printf("------- WEATHERDARKSTATUS:    %d\n",IMGHEADER_WEATHERDARKSTATUS);

  read = read_parameter(fnameinfo,"WEATHERCLEARSTATUS:",content);
  IMGHEADER_WEATHERCLEARSTATUS = atoi(content);
  printf("------- WEATHERCLEARSTATUS:    %d\n",IMGHEADER_WEATHERCLEARSTATUS);

  read = read_parameter(fnameinfo,"OBSERVINGSTATUS:",content);
  IMGHEADER_OBSERVINGSTATUS = atoi(content);
  printf("------- OBSERVINGSTATUS:       %d\n",IMGHEADER_OBSERVINGSTATUS);
  



  // IMAGE STATISTICS
  /*
  if(stat(fnameimstat, &file_st) != 0)
    {
      printf("File %s does not exit - CREATING\n",fnameimstat);
      n = snprintf(commandline, SBUFFERSIZE,"./PROCESS/bin/skycam_mkimstat %s/%04d-%02d-%02d/%s/IMG%d_%04ld\n",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_CR2_DIRECTORY2,camera,index);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      system(commandline);
      n = snprintf(commandline,SBUFFERSIZE,"mv %s/%04d-%02d-%02d/%s/IMG%d_%04ld.imstat %s/%04d-%02d-%02d/%s/\n",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_CR2_DIRECTORY2,camera,index,DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      system(commandline);
    }
  */

   read = read_parameter(fnameinfo,"Shutter:",content);
   if(read == 1)
     IMGHEADER_SHUTTER = atof(content);
   else
     {
       read = read_parameter(fnameinfo,"SHUTTER:",content);
       IMGHEADER_SHUTTER = atof(content);	 
     }
   printf("------- SHUTTER:              %f sec\n",IMGHEADER_SHUTTER);
  
   read = read_parameter(fnameinfo,"ISO speed:",content);
   if(read == 1)
     IMGHEADER_ISO = atoi(content);
   else
     {
       read = read_parameter(fnameinfo,"ISO:",content);
       IMGHEADER_ISO = atoi(content);
     }
   printf("------- ISO:                 %d\n",IMGHEADER_ISO);
  


   read_parameter(fnameinfo,"percentile01 ",content);
   IMGHEADER_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"percentile05 ",content);
   IMGHEADER_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"percentile50 ",content);
   IMGHEADER_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"percentile95 ",content);
   IMGHEADER_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"percentile99 ",content);
   IMGHEADER_PERCENTILE99 = atof(content);
   
   
   read_parameter(fnameinfo,"RED_percentile01 ",content);
   IMGHEADER_RED_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"RED_percentile05 ",content);
   IMGHEADER_RED_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"RED_percentile10 ",content);
   IMGHEADER_RED_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"RED_percentile20 ",content);
   IMGHEADER_RED_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"RED_percentile50 ",content);
   IMGHEADER_RED_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"RED_percentile80 ",content);
   IMGHEADER_RED_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"RED_percentile90 ",content);
   IMGHEADER_RED_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"RED_percentile95 ",content);
   IMGHEADER_RED_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"RED_percentile99 ",content);
   IMGHEADER_RED_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"RED_percentile995 ",content);
   IMGHEADER_RED_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"RED_percentile998 ",content);
   IMGHEADER_RED_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"RED_percentile999 ",content);
   IMGHEADER_RED_PERCENTILE999 = atof(content);
   
   
   read_parameter(fnameinfo,"GREEN1_percentile01 ",content);
   IMGHEADER_GREEN1_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile05 ",content);
   IMGHEADER_GREEN1_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile10 ",content);
   IMGHEADER_GREEN1_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile20 ",content);
   IMGHEADER_GREEN1_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile50 ",content);
   IMGHEADER_GREEN1_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile80 ",content);
   IMGHEADER_GREEN1_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile90 ",content);
   IMGHEADER_GREEN1_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile95 ",content);
   IMGHEADER_GREEN1_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile99 ",content);
   IMGHEADER_GREEN1_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile995 ",content);
   IMGHEADER_GREEN1_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile998 ",content);
   IMGHEADER_GREEN1_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"GREEN1_percentile999 ",content);
   IMGHEADER_GREEN1_PERCENTILE999 = atof(content);
   
    
   read_parameter(fnameinfo,"GREEN2_percentile01 ",content);
   IMGHEADER_GREEN2_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile05 ",content);
   IMGHEADER_GREEN2_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile10 ",content);
   IMGHEADER_GREEN2_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile20 ",content);
   IMGHEADER_GREEN2_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile50 ",content);
   IMGHEADER_GREEN2_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile80 ",content);
   IMGHEADER_GREEN2_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile90 ",content);
   IMGHEADER_GREEN2_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile95 ",content);
   IMGHEADER_GREEN2_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile99 ",content);
   IMGHEADER_GREEN2_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile995 ",content);
   IMGHEADER_GREEN2_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile998 ",content);
   IMGHEADER_GREEN2_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"GREEN2_percentile999 ",content);
   IMGHEADER_GREEN2_PERCENTILE999 = atof(content);
   
   read_parameter(fnameinfo,"BLUE_percentile01 ",content);
   IMGHEADER_BLUE_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile05 ",content);
   IMGHEADER_BLUE_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile10 ",content);
   IMGHEADER_BLUE_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile20 ",content);
   IMGHEADER_BLUE_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile50 ",content);
   IMGHEADER_BLUE_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile80 ",content);
   IMGHEADER_BLUE_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile90 ",content);
   IMGHEADER_BLUE_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile95 ",content);
   IMGHEADER_BLUE_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile99 ",content);
   IMGHEADER_BLUE_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile995 ",content);
   IMGHEADER_BLUE_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile998 ",content);
   IMGHEADER_BLUE_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"BLUE_percentile999 ",content);
   IMGHEADER_BLUE_PERCENTILE999 = atof(content);
   

   /* 
   read_parameter(fnameinfo,"NOISE_RED_percentile01 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile05 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile10 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile20 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile50 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile80 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile90 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile95 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile99 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile995 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile998 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"NOISE_RED_percentile999 ",content);
   IMGHEADER_NOISE_RED_PERCENTILE999 = atof(content);
   
   
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile01 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile05 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile10 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile20 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile50 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile80 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile90 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile95 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile99 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile995 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile998 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN1_percentile999 ",content);
   IMGHEADER_NOISE_GREEN1_PERCENTILE999 = atof(content);
   
    
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile01 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile05 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile10 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile20 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile50 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile80 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile90 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile95 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile99 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile995 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile998 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"NOISE_GREEN2_percentile999 ",content);
   IMGHEADER_NOISE_GREEN2_PERCENTILE999 = atof(content);
   
   read_parameter(fnameinfo,"NOISE_BLUE_percentile01 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE01 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile05 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE05 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile10 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE10 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile20 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE20 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile50 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE50 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile80 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE80 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile90 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE90 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile95 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE95 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile99 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE99 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile995 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE995 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile998 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE998 = atof(content);
   read_parameter(fnameinfo,"NOISE_BLUE_percentile999 ",content);
   IMGHEADER_NOISE_BLUE_PERCENTILE999 = atof(content);
   

   IMGHEADER_NOISE_RED = (IMGHEADER_NOISE_RED_PERCENTILE10/0.17798575846455308436 + IMGHEADER_NOISE_RED_PERCENTILE20/0.35856389265878862993 + IMGHEADER_NOISE_RED_PERCENTILE50/0.95419833789129959722)/3.0;

   IMGHEADER_NOISE_GREEN1 = (IMGHEADER_NOISE_GREEN1_PERCENTILE10/0.17798575846455308436 + IMGHEADER_NOISE_GREEN1_PERCENTILE20/0.35856389265878862993 + IMGHEADER_NOISE_GREEN1_PERCENTILE50/0.95419833789129959722)/3.0;
   IMGHEADER_NOISE_GREEN2 = (IMGHEADER_NOISE_GREEN2_PERCENTILE10/0.17798575846455308436 + IMGHEADER_NOISE_GREEN2_PERCENTILE20/0.35856389265878862993 + IMGHEADER_NOISE_GREEN2_PERCENTILE50/0.95419833789129959722)/3.0;

   IMGHEADER_NOISE_BLUE = (IMGHEADER_NOISE_BLUE_PERCENTILE10/0.17798575846455308436 + IMGHEADER_NOISE_BLUE_PERCENTILE20/0.35856389265878862993 + IMGHEADER_NOISE_BLUE_PERCENTILE50/0.95419833789129959722)/3.0;
   */

   
   // ASTROMETRY
   
   /*
   if(strncmp(IMGHEADER_IMTYPE,"OBJECT",strlen("OBJECT"))==0)
     {
       if(stat(fnameimastrom,&file_st) != 0)
	 {
	   printf("File %s does not exit - CREATING\n",fnameimastrom);
	   n = snprintf(commandline,SBUFFERSIZE,"./PROCESS/bin/astromSolve %04d-%02d-%02d IMG%d_%04ld\n",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,camera,index);
	   if(n >= SBUFFERSIZE) 
	     {
	       printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	       exit(0);
	     }	  
	   system(commandline);
	 }
       
       read_parameter(fnameimastrom,"FitQuality ",content);
       IMGHEADER_ASTROM_FITQUALITY = atof(content);

       read_parameter(fnameimastrom,"RA_deg ",content);
       IMGHEADER_ASTROM_RA = atof(content);

       read_parameter(fnameimastrom,"DEC_deg ",content);
       IMGHEADER_ASTROM_DEC = atof(content);
      
       read_parameter(fnameimastrom,"PA_deg ",content);
       IMGHEADER_ASTROM_PA = atof(content);

       read_parameter(fnameimastrom,"PixScale_arcsec ",content);
       IMGHEADER_ASTROM_PIXSCALE = atof(content);

       read_parameter(fnameimastrom,"DISTX_Y ",content);
       IMGHEADER_ASTROM_DISTX_Y = atof(content);

       read_parameter(fnameimastrom,"DISTY_Y ",content);
       IMGHEADER_ASTROM_DISTY_Y = atof(content);

       read_parameter(fnameimastrom,"DISTX_XX ",content);
       IMGHEADER_ASTROM_DISTX_XX = atof(content);

       read_parameter(fnameimastrom,"DISTX_XY ",content);
       IMGHEADER_ASTROM_DISTX_XY = atof(content);

       read_parameter(fnameimastrom,"DISTX_YY ",content);
       IMGHEADER_ASTROM_DISTX_YY = atof(content);

       read_parameter(fnameimastrom,"DISTY_XX ",content);
       IMGHEADER_ASTROM_DISTY_XX = atof(content);

       read_parameter(fnameimastrom,"DISTY_XY ",content);
       IMGHEADER_ASTROM_DISTY_XY = atof(content);

       read_parameter(fnameimastrom,"DISTY_YY ",content);
       IMGHEADER_ASTROM_DISTY_YY = atof(content);

       read_parameter(fnameimastrom,"DISTX_XXX ",content);
       IMGHEADER_ASTROM_DISTX_XXX = atof(content);

       read_parameter(fnameimastrom,"DISTX_XXY ",content);
       IMGHEADER_ASTROM_DISTX_XXY = atof(content);

       read_parameter(fnameimastrom,"DISTX_XYY ",content);
       IMGHEADER_ASTROM_DISTX_XYY = atof(content);

       read_parameter(fnameimastrom,"DISTX_YYY ",content);
       IMGHEADER_ASTROM_DISTX_YYY = atof(content);
     }
   else
     {
       IMGHEADER_ASTROM_FITQUALITY = -1.0;
       IMGHEADER_ASTROM_RA = 0.0;
       IMGHEADER_ASTROM_DEC = 0.0;
     }
   */

  return(0);
}















int SKYCAM_command_polaralign(double rotrange_deg, int isomode, double etime)
{
  //  double RA; // rad
  //  double DEC; // rad
  //  double mountra, mountdec;
  char imgname[SBUFFERSIZE];
  char commandline[SBUFFERSIZE];
  int imgdone;
  FILE *fp;
  long cnt;
  int n;

  // step 1: point to North Pole
  openmountfd();
  SKYCAM_command_MOUNTSTOP();
  
  //  track_mra_mdec(0.0,0.0);  
  /*RA = TIME_LST/12.0*M_PI;
  DEC = M_PI/2.0;
  mountra  = (-RA/M_PI*12.0+TIME_LST)/24.0;
  mountra -= rotrange_deg/2.0/360.0;
  mountdec = (-DEC/M_PI*180.0+SITE_LAT/M_PI*180.0)/360.0;
  SKYCAM_command_movpos(mountra,mountdec);
  */

  //  SKYCAM_command_movpos(0.0,mDEC_NPOLE);

  

  // step 2: take exposure while rotating
  track_mra_mdec(rotrange_deg/360.0/etime*NBstepMountRot,0.0);
   

  n = snprintf(imgname,SBUFFERSIZE,"/skycam/polaralign_mount.CR2");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  n = snprintf(commandline,SBUFFERSIZE,"rm %s",imgname);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  system(commandline);
  n = snprintf(commandline, SBUFFERSIZE, "/skycam/soft/takeimageUSB %.1f %d %s &", etime, isomode, imgname);  
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__, "Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  printf("CAMERA command: %s\n", commandline);
  system(commandline);
  
  n = snprintf(line,SBUFFERSIZE, "Waiting for image completion  ");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();
  imgdone = 0;
  cnt = 0;
  while(imgdone == 0)
    {
      fflush(stdout);
      fp = fopen(imgname,"r");
      if(fp != NULL)
	{
	  imgdone = 1;
	  if(fclose(fp)!=0)
	    {
	      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	      exit(0);
	    }
	  fp = NULL;
	}
      usleep(200000);
      printf(".");
      fflush(stdout);
      if(cnt > (long) (1.2*(etime+20.0)/0.1))
	{
	  n = snprintf(line,SBUFFERSIZE,"ERROR: IMAGE NOT AVAILABLE AFTER TIMEOUT\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  SKYCAM_ABORT();
	}
      cnt++;
    }
  n = snprintf(line,SBUFFERSIZE,"IMAGE WRITTEN ON DISK\n");
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();
  //  track_mra_mdec(0.0,0.0);

  /*  RA = TIME_LST/12.0*M_PI;
  DEC = M_PI/2.0;
  mountra  = (-RA/M_PI*12.0+TIME_LST)/24.0;
  mountdec = (-DEC/M_PI*180.0+SITE_LAT/M_PI*180.0)/360.0;
  SKYCAM_command_movpos(mountra,mountdec);
  */

  //  SKYCAM_command_movpos(0.0,mDEC_NPOLE);

  SKYCAM_command_MOUNTSTOP();
  closemountfd();

  return(0);
}



// NOTE: ISO MODE ONLY WORKS IN USB MODE
int SKYCAM_command_ACQUIREimage(double etime, int isomode)
{
  int cam;
  char command[SBUFFERSIZE];
  char commandline[SBUFFERSIZE];
  //char line[SBUFFERSIZE];
  FILE *fp;
  char imgname[4][SBUFFERSIZE];
  //  char img2name[SBUFFERSIZE];
  char imgnametmp[4][SBUFFERSIZE];
  //  char img2nametmp[SBUFFERSIZE];
  long cnt;
  int imgdone[4]; //, img1done, img2done;
  int img_done;
  time_t t_now;
  double dt;
  int n;
  time_t tstart[4];
  //  time_t t2start; 
  int USBdone = 1;
  double USB_time_offset = 20.0; // additional time for USB image
  double dt1; // = USB_time_offset if one of the cameras is in USB mode
  int etimeIndex;
  double dx, dy;


  system("touch /skycam/_SKYCAM_RUNNING");

  // create CR2 data directory if it does not exist
  n = snprintf(command, SBUFFERSIZE, "mkdir -p %s/%04d-%02d-%02d/%s", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_CR2_DIRECTORY2);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	   
  system(command);
  printf(".");
  fflush(stdout);

  // create JPEG directory if it does not exist
  n = snprintf(command, SBUFFERSIZE, "mkdir -p %s/%04d-%02d-%02d/%s", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_JPEG_DIRECTORY2);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  system(command);
  printf(".");
  fflush(stdout);

  // create image header directory if it does not exist
  n = snprintf(command, SBUFFERSIZE, "mkdir -p %s/%04d-%02d-%02d/%s", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON,TIME_UTC_DAY, DATA_HEADER_DIRECTORY2);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  system(command);
  printf(".");
  fflush(stdout);
 
  

  for(cam=0; cam<4; cam++)
    {
      printf("Setting up filenames for cam %d ...", cam);
      fflush(stdout);
      n = snprintf(imgname[cam], SBUFFERSIZE, "%s/%04d-%02d-%02d/%s/IMG%d_%04ld.CR2", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_CR2_DIRECTORY2, cam, IMGindex[cam]);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	        

      n = snprintf(imgnametmp[cam] ,SBUFFERSIZE, "%s/%04d-%02d-%02d/%s/IMG%d_%04ld.tmp", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, DATA_CR2_DIRECTORY2, cam, IMGindex[cam]);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("\n");
      fflush(stdout);
    }
  


  // fill in data for guide table

  clock_gettime(CLOCK_REALTIME, &tnowts);
  GT_tstart[GTindex] = 1.0*((long) tnowts.tv_sec) + 1.0e-9*tnowts.tv_nsec;;




  for(cam=0; cam<4; cam++)
    {
      switch (CAMMODE[cam]) {
      case 1 : // USB
	GT_imnb[GTindex] = IMGindex[cam];
	if(etime<8.0)
	  {
	    etimeIndex = (long) (0.5-log(etime*0.025)/0.23);
	    if(etimeIndex>52)
	      etimeIndex = 52;
	    if(etimeIndex<7)
	      etimeIndex=7;
	    n = snprintf(commandline, SBUFFERSIZE, "/skycam/soft/takeimageUSB_ShortExp %d %d %s &", etimeIndex, isomode, imgname[cam]);
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    system(commandline);
	    tstart[cam] = time(NULL); // set tstart
	    n = snprintf(line, SBUFFERSIZE, "STARTING USB ACQUISITION for %s (etime = %f - INDEX = %d) [%f-%f]\n", imgname[cam], etime,etimeIndex, ETIME_MIN_SEC, ETIME_MAX_SEC);
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    SKYCAM_log();
	  }
	else
	  {
	    n = snprintf(commandline, SBUFFERSIZE, "/skycam/soft/takeimageUSB %.1f %d %s &", etime, isomode, imgname[cam]);
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    printf("SYSTEM COMMAND: %s\n", commandline);
	    system(commandline);
	    tstart[cam] = time(NULL); // set t1start
	    n = snprintf(line,SBUFFERSIZE,"STARTING USB ACQUISITION for %s (etime = %.1f - BULB) [%f-%f]\n", imgname[cam], etime, ETIME_MIN_SEC, ETIME_MAX_SEC);
	    if(n >= SBUFFERSIZE) 
	      {
		printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		exit(0);
	      }	  
	    SKYCAM_log();	
	  }	  
	break;
      case 2 : // TTL
	CAM_TO_LOAD[cam] ++;
	dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 0);
	CAMTTLON[cam] = 1;
	tstart[cam] = time(NULL); // set tstart
	n = snprintf(line, SBUFFERSIZE, "STARTING TTL ACQUISITION for %s (etime = %.1f)\n", imgname[cam], etime);
	if(n >= SBUFFERSIZE) 
	  {
	    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	    exit(0);
	  }	  
	SKYCAM_log();		  
	break;
      }
    }


  

  
  // fill up header info, check conditions...
  compute_LSTnow();
  IMGHEADER_UT_START = TTIME_UTC;
  IMGHEADER_SHUTTER = etime;
  IMGHEADER_ISO = isomode;

  if(MOUNTSTATUS>0)
    {
      if(!(mountfd<0))
	{
	  n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();
	  SKYCAM_exit();
	}
      
      
      if(GETPOSMOUNT == 1)
	{
	  mountfd = open_ttyUSBport(mountUSBportNB);
	  get_posmountradec();
	  if(close(mountfd)!=0)
	    {
	      n = snprintf(line,SBUFFERSIZE,"FATAL ERROR: could not close file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      SKYCAM_exit();
	    }
	  mountfd = -1;
	  fdcnt--;
	}
      else
	{      
	  t_now = time(NULL);
	  dt =  difftime(t_now,T_TRACKING);
	  pos_mountra = LAST_GETPOSMOUNTRA + dt/3600.0/24.0;
	  if(pos_mountra>1.0)
	    pos_mountra -= 1.0;
	  pos_mountdec = LAST_GETPOSMOUNTDEC;
	}
      
      GT_posmountRA[GTindex] = pos_mountra;
      GT_posmountDEC[GTindex] = pos_mountdec;



      if(pos_mountdec<mDEC_NPOLE)
	RAflip = -1;
      else
	RAflip = 1;
      compute_LSTnow();

      pos_RA = 2.0*M_PI*(24.0*(-(pos_mountra-mRA_MERIDIAN)-0.25*RAflip)+TIME_LST)/24.0;
      pos_DEC = 2.0*M_PI*(90.0-360.0*fabs(pos_mountdec-mDEC_NPOLE))/360.0;

      while(pos_RA<0.0)
	pos_RA += 2.0*M_PI;
      while(pos_RA>2.0*M_PI)
	pos_RA -= 2.0*M_PI;

      compute_coordinates_from_RA_DEC(pos_RA, pos_DEC);
  
      IMGHEADER_MOUNTDEC = pos_mountdec;
      IMGHEADER_MOUNTRA = pos_mountra;
      IMGHEADER_RA = COORD_RA/M_PI*180.0;
      IMGHEADER_DEC = COORD_DEC/M_PI*180.0;
      IMGHEADER_ALT = COORD_ALT/M_PI*180.0;
      IMGHEADER_AZ = COORD_AZ/M_PI*180.0;
      IMGHEADER_MOUNT_RAflip = RAflip;
    }
  else
    {
      IMGHEADER_MOUNTDEC = -1.0;
      IMGHEADER_MOUNTRA = -1.0;
      IMGHEADER_RA = -1.0;
      IMGHEADER_DEC = -1.0;
      IMGHEADER_ALT = -1.0;
      IMGHEADER_AZ = -1.0;
    }

  // check conditions
  SKYCAM_command_observingstatus();
  get_Moon_pos();
  
  IMGHEADER_HUMIDITY = HUMIDITY;
  IMGHEADER_TEMPERATURE1 = TEMPERATURE1;
  IMGHEADER_TEMPERATURE2 = TEMPERATURE2;
  IMGHEADER_MOON_ALT = MOON_ALT/M_PI*180.0;
  IMGHEADER_MOON_AZ = MOON_AZ/M_PI*180.0;
  IMGHEADER_MOON_MAGN = MOON_MAGN;
  IMGHEADER_SUN_ALT = SUN_ALT/M_PI*180.0;
  IMGHEADER_SUN_AZ = SUN_AZ/M_PI*180.0;
  
  IMGHEADER_WEATHERDARKSTATUS = WEATHER_DARK_STATUS;
  IMGHEADER_WEATHERCLEARSTATUS = WEATHER_CLEAR_STATUS;
  IMGHEADER_OBSERVINGSTATUS = OBSERVINGSTATUS;
  
  // wait for image completion
  for(cam=0; cam<4; cam++)
    {
      printf("CAM %d : mode = %d\n", cam, CAMMODE[cam]);
      if (CAMMODE[cam] == 1)
      	printf("Waiting for image name \"%s\"\n", imgname[cam]);
    }
  n = snprintf(line,SBUFFERSIZE,"Waiting for image(s) completion ( %f sec ) ...\n",etime);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  dt1 = 0.0;
  for(cam=0; cam<4; cam++)
    if(CAMMODE[cam]==1)
      dt1 = USB_time_offset;


  img_done = 0;
  for(cam=0; cam<4; cam++)
    imgdone[cam] = 0;

  USBdone = 0;
  cnt = 0;


  while(img_done == 0) // WAIT FOR IMAGE COMPLETION - MOST TIME SPENT IN THIS LOOP
    {
      fflush(stdout);
     
      for(cam=0; cam<4; cam++)
	{
	  if(imgdone[cam] == 0)
	    {
	      switch (CAMMODE[cam]) {
	      case 1 : // USB
		//	printf("Testing for \"%s\"\n", imgname[cam]);
		fp = fopen(imgname[cam], "r");
		if(fp != NULL)
		  {
		    imgdone[cam] = 1;
		    USBdone = 1;
		    if(fclose(fp)!=0)
		      {
			printERROR( __FILE__, __func__, __LINE__,"fclose() error");
			exit(0);
		      }
		    fp = NULL;
		    if(GTindex==0)
		      {
			n = snprintf(command, SBUFFERSIZE, "cp %s imREFG.CR2", imgname[cam]);
			if(n >= SBUFFERSIZE) 
			  {
			    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
			    exit(0);
			  }	  
			printf("COPYING IMAGE TO GUIDE REFERENCE - EXECUTING SYSTEM COMMAND: %s\n", command);
			system(command);
		      }
		    else
		      {
			printf("COMPUTING GUIDING OFFSET\n");

			n = snprintf(commandline,SBUFFERSIZE,"./soft/measureoffset_fast_cam0 imREFG.CR2 %s", imgname[cam]);
			if(n >= SBUFFERSIZE) 
			  {
			    printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
			    exit(0);
			  }	  
			printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
			system(commandline);
			
			if((fp = fopen("xcent.txt","r"))==NULL)
			  {
			    printERROR(__FILE__,__func__,__LINE__,"fopen() error");
			    exit(0);
			  }
			fscanf(fp,"%lf", &dx);
			fclose(fp);
			
			if((fp = fopen("ycent.txt","r"))==NULL)
			  {
			    printERROR(__FILE__,__func__,__LINE__,"fopen() error");
			    exit(0);
			  }
			fscanf(fp,"%lf", &dy);
			fclose(fp);
			
			if(RAflip==-1)
			  {
			    GT_dRA[GTindex] = dy*PIXELSIZE_CAM[cam]; // arcsec
			    GT_dDEC[GTindex] = dx*PIXELSIZE_CAM[cam];
			  }
			else
			  {
			    GT_dRA[GTindex] = -dy*PIXELSIZE_CAM[cam];
			    GT_dDEC[GTindex] = -dx*PIXELSIZE_CAM[cam];
			  }
						
		      }
		    printf("WRITING IMG HEADER\n");
		    fflush(stdout);
		    SKYCAM_write_imgheader();
		  }
		break;
	      case 2 : // TTL
		t_now = time(NULL);
		dt = difftime(t_now, tstart[cam]);
		if((dt>etime+dt1)||(USBdone==1))
		  {
		    dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 1);
		    CAMTTLON[cam] = 0;
		    n = snprintf(command, SBUFFERSIZE, "touch %s", imgnametmp[cam]);
		    if(n >= SBUFFERSIZE) 
		      {
			printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
			exit(0);
		      }	  
		    printf("TTL EXPOSURE ENDED - EXECUTING SYSTEM COMMAND: %s\n", command);
		    system(command);
		    imgdone[cam] = 1;
		  }
		break;
	      default :
		imgdone[cam] = 1;
		break;
	      }
	    }
	}
      


      img_done = 1;
      for(cam=0; cam<4; cam++)
	img_done *= imgdone[cam];
      
      usleep(100000); // 0.1 sec
      printf("-");
      fflush(stdout);
      track_update_mra();
      usleep(100000); // 0.1 sec
      track_update_mdec();
      usleep(100000); // 0.1 sec
    

      if(cnt > (long) (1.2*(etime+120.0)/0.3))
	{
	  n = snprintf(line, SBUFFERSIZE, "ERROR: IMAGE(S) NOT AVAILABLE AFTER TIMEOUT\n");
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  SKYCAM_log();

	  SKYCAM_command_MOUNTSTOP();
	    
	  system("gphoto2 --auto-detect");
	  SKYCAM_command_park();	 
	  sleep(10.0);
	  SKYCAM_exit();
	}
      cnt++;
    }



  n = snprintf(line, SBUFFERSIZE, "IMAGE(s) %s %s %s %s ACQUIRED\n", imgname[0], imgname[1], imgname[2], imgname[3]);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();


  //  if((CAMMODE[0]!=0)||(CAMMODE[1]!=0)||(CAMMODE[2]!=0)||(CAMMODE[3]!=0))
  //  SKYCAM_write_imgheader();

  for(cam=0; cam<4; cam++)
    if(CAMMODE[cam] != 0)
      IMGindex[cam]++;
   
  return(0);
}




long SKYCAM_loadTrackingLog(char *fname)
{
  long k = 0;
  long kmax;
  FILE *fp;
  int UTyr, UTmon, UTday;
  long index1, index2, index3;
  float mRA, mDEC;
  int RAflip1;
  float TrateRA1, TrateDEC1;
  float tstart, tetime, dRA, dDEC, gtrarate, gtdecrate, gtraoffset, gtdecoffset;

  // uses circular buffer to keep most recent entries
  
  kmax = 0;
  if((fp=fopen(fname,"r"))==NULL)
    {
      printf("Cannot open file \"%s\"",fname);
    }
  else
    {
      while(fgets(line,SBUFFERSIZE,fp)!=NULL)
	{
	  
	  if(sscanf(line,"%04d-%02d-%02d %4ld %5ld %5ld %2d %f %f %f %f %f %f %f %f %f %f %f %f\n", &UTyr, &UTmon, &UTday, &index1, &index2, &index3, &RAflip1, &mRA, &mDEC, &tstart, &tetime, &dRA, &dDEC, &gtrarate, &gtdecrate, &gtraoffset, &gtdecoffset, &TrateRA1, &TrateDEC1)==19)		  
	    {
	      TRACKINGRATELOG_mRA[k] = mRA;
	      TRACKINGRATELOG_mDEC[k] = mDEC;
	      TRACKINGRATELOG_RAflip[k] = RAflip1;
	      TRACKINGRATELOG_TrateRA[k] = TrateRA1;
	      TRACKINGRATELOG_TrateDEC[k] = TrateDEC1;
	      k++;
	      //	      printf(".");
	      if(k>kmax)
		kmax = k;
	      if(k==TRACKINGRATELOG_NBpt)
		k = 0;
	    }
	}
      
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
    }
  printf("\n");
  printf("Loaded tracking log file, %ld entries\n",kmax);

  TRACKINGRATELOG_NBpt = kmax;
  
  return(k);
}



//
//
// In motor units 
//
long SKYCAM_computeTrackingRate_sidN( double mRA, double mDEC, int flip)
{
  double TrateRA = 0.0; // steps per sec
  double TrateDEC = 0.0; // steps per sec
  long k;
  double *array1 = NULL;
  double *array2 = NULL;
  long k1;
  long NBk1;
  double tmp1, tmp2, dist;
  double coeff;
  //  double tcoeff = 0.0;
  double dlim = 0.05;
  double lim1min, lim1max, lim2min, lim2max;
  double Tra, Tdec;
  double coeffTra, coeffTdec;
  long n, n1;

  array1 = (double*) malloc(sizeof(double)*TRACKINGRATELOG_NBpt);
  array2 = (double*) malloc(sizeof(double)*TRACKINGRATELOG_NBpt);
  
  //  tcoeff = 0.0;
  k1 = 0;
  for(k=0;k<TRACKINGRATELOG_NBpt;k++)
    {
      tmp1 = mRA - TRACKINGRATELOG_mRA[k];
      tmp2 = mDEC - TRACKINGRATELOG_mDEC[k];
      dist = sqrt(tmp1*tmp1+tmp2*tmp2)/dlim;
      coeff = exp(-dist*dist);
      //    printf("%f %f %d/%d -> %f\n",tmp1,tmp2,TRACKINGRATELOG_RAflip[k],flip,dist);
      if((dist<3.0)&&(flip==TRACKINGRATELOG_RAflip[k]))
	{
	  array1[k1] = (double) TRACKINGRATELOG_TrateRA[k];
	  array2[k1] = (double) TRACKINGRATELOG_TrateDEC[k];
	  k1 ++;	  
	}
    }
  NBk1 = k1;

  //  printf("%ld data points selected out of %ld\n",NBk1,TRACKINGRATELOG_NBpt);

  quick_sort(array1, NBk1);
  quick_sort(array2, NBk1);

  lim1min = array1[(long) (0.35*NBk1)];
  lim1max = array1[(long) (0.65*NBk1)];

  lim2min = array2[(long) (0.35*NBk1)];
  lim2max = array2[(long) (0.65*NBk1)];

  coeffTra = 0.0;
  coeffTdec = 0.0;
  TrateRA = 0.0;
  TrateDEC = 0.0;
  n1 = 0;
  for(k=0;k<TRACKINGRATELOG_NBpt;k++)
    {
      tmp1 = mRA - TRACKINGRATELOG_mRA[k];
      tmp2 = mDEC - TRACKINGRATELOG_mDEC[k];
      dist = sqrt(tmp1*tmp1+tmp2*tmp2)/dlim;
      coeff = exp(-dist*dist);
      if((dist<3.0)||(flip==TRACKINGRATELOG_RAflip[k]))
	{
	  Tra = TRACKINGRATELOG_TrateRA[k];
	  Tdec = TRACKINGRATELOG_TrateDEC[k];
	  if((Tra>lim1min)&&(Tra<lim1max))
	    {
	      coeff = exp(-dist*dist);
	      coeffTra += coeff;
	      TrateRA += coeff*Tra;
	      n1++;
	    }
	  if((Tdec>lim2min)&&(Tdec<lim2max))
	    {
	      coeff = exp(-dist*dist);
	      coeffTdec += coeff;
	      TrateDEC += coeff*Tdec;
	    }	  
	}
    }
  
  if(coeffTra>5.0)
    {
      TrateRA /= coeffTra;
      TrateDEC /= coeffTdec;
      // convert to steps per sec
      TrateRA *= NBstepMountRot/360.0/3600.0;
      TrateDEC *= NBstepMountRot/360.0/3600.0;
    }
  else
    {
      TrateRA = 15.0*NBstepMountRot/360.0/3600.0;
      TrateDEC = 0.0;
    }

  free(array1);
  free(array2);


  // limits
  if(TrateRA<14.0*NBstepMountRot/360.0/3600.0)
    TrateRA = 14.0*NBstepMountRot/360.0/3600.0;
  if(TrateRA>16.0*NBstepMountRot/360.0/3600.0)
    TrateRA = 16.0*NBstepMountRot/360.0/3600.0;
  if(TrateDEC<-1.0*NBstepMountRot/360.0/3600.0)
    TrateDEC = -1.0*NBstepMountRot/360.0/3600.0;
  if(TrateDEC>1.0*NBstepMountRot/360.0/3600.0)
    TrateDEC = 1.0*NBstepMountRot/360.0/3600.0;

  TRACKrate_RA = TrateRA; 
  TRACKrate_DEC = TrateDEC; 

  n = snprintf(line,SBUFFERSIZE,"ESTIMATED BEST TRACKING RATE FOR %.3f %.3f %d: %.3f %.3f [%ld - %g %g]\n", mRA, mDEC, flip, TRACKrate_RA, TRACKrate_DEC, n1, coeffTra, coeffTdec);
  if(n >= SBUFFERSIZE) 
    {
      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
      exit(0);
    }	  
  SKYCAM_log();

  //  TRACKrate_RA = 1.0*((long) ((1.0*NBstepMountRot/360.0/3600.0*15.0)+0.5));
  // TRACKrate_DEC = 0.0;

  /*
  TrateRA = 15.0*NBstepMountRot/360.0/3600.0;
  TrateDEC = 0.0;

  TRACKrate_RA = TrateRA; 
  TRACKrate_DEC = TrateDEC; 
  */

  return(sqrt(coeffTra*coeffTdec));
}


//
// apply pointing offset during tracking
//
// dRA dDEC in sky arcsec
// dRA needs to be corrected for cosine DEC to yield mechanical mount angle
// tspan is the time span over which the offset is applied [sec]
//
// rates are in mechanical arcsec/sec, sky directions
//
int Apply_TrackingOffset(double dRA, double dDEC, double RArate, double DECrate, double tspan)
{
  //  FILE *fptest;
  double TRACKrate_RA_ref; // previous tracking rate
  double TRACKrate_DEC_ref;
  
  double tdelay = 0.47; // effective tracking off time due to command [sec]

  double dRA1, dDEC1; // corrected for cosine DEC effect

  if(cos(COORD_DEC)>0.2)
    dRA1 = dRA/cos(COORD_DEC);
  else
    dRA = dRA/0.2;

  dDEC1 = dDEC;

  //  fptest = fopen("trackratetest.txt","a");

  TRACKrate_RA_ref = TRACKrate_RA;
  TRACKrate_DEC_ref = TRACKrate_DEC;

  //  fprintf(fptest,"TRACK RATE %f %f   ->  ", TRACKrate_RA, TRACKrate_DEC);

  TRACKrate_RA = TRACKrate_RA + dRA1*NBstepMountRot/360.0/3600.0/tspan + TRACKrate_RA*tdelay/tspan;
  TRACKrate_DEC = TRACKrate_DEC + dDEC1*NBstepMountRot/360.0/3600.0/tspan*RAflip + TRACKrate_DEC*tdelay/tspan;

 
 // limits
  if(TRACKrate_RA<-120.0*NBstepMountRot/360.0/3600.0)
    TRACKrate_RA = -120.0*NBstepMountRot/360.0/3600.0;

  if(TRACKrate_RA>120.0*NBstepMountRot/360.0/3600.0)
    TRACKrate_RA = 120.0*NBstepMountRot/360.0/3600.0;

  if(TRACKrate_DEC<-120.0*NBstepMountRot/360.0/3600.0)
    TRACKrate_DEC = -120.0*NBstepMountRot/360.0/3600.0;

  if(TRACKrate_DEC>120.0*NBstepMountRot/360.0/3600.0)
    TRACKrate_DEC = 120.0*NBstepMountRot/360.0/3600.0;

  //  fprintf(fptest," %f %f   %f\n", TRACKrate_RA, TRACKrate_DEC, tspan);

  SKYCAM_command_MOUNTSTOP();
  track_mra_mdec(TRACKrate_RA, TRACKrate_DEC);

  printf("\n\n\n");
  printf("================== WAITING %f sec ==================\n", tspan);
  printf("\n\n\n");
  usleep((long) (tspan*1000000));

  printf("=================== DONE WAITING ===================\n");
  printf("\n\n\n");

  TRACKrate_RA = TRACKrate_RA_ref;
  TRACKrate_DEC = TRACKrate_DEC_ref;

  TRACKrate_RA = RArate*NBstepMountRot/360.0/3600.0;
  TRACKrate_DEC = DECrate*NBstepMountRot/360.0/3600.0*RAflip;


  SKYCAM_command_MOUNTSTOP();
  track_mra_mdec(TRACKrate_RA, TRACKrate_DEC);

  //  fclose(fptest);

  return 0;
}


int main(int argc, char **argv)
{
  long i;
  int cam;
  long cnt;
  char str1[SBUFFERSIZE];
  FILE *fp;
  double v; //,v1,v2,v1tot,v2tot;
  double lst;
  double u;
  int coordOK;
  double RA,DEC,ALT,AZ;
  int hr,min,sec;
  double etime;
  int imgdone;
  char command[SBUFFERSIZE];
  char commandline[SBUFFERSIZE];
  long NBiter;
  int TIME_LST_hr,TIME_LST_mn;
  double TIME_LST_sec;
  int targetcnt;
  int targetcntmax = 999;
  char targetfilename[SBUFFERSIZE];
  char fname[SBUFFERSIZE];
  //  char fnameimg[SBUFFERSIZE];
  char fname1[SBUFFERSIZE];
  char fnameout[SBUFFERSIZE];
  double RAnew,DECnew,RAold,DECold;
  double mRAnew,mDECnew,mRAold,mDECold;
  char w0[100];
  char w1[100];
  char w2[100];
  long isovalue;
  int ok;
  struct stat file_st;
  char dirname[SBUFFERSIZE];
  char content[SBUFFERSIZE];
  char entry[SBUFFERSIZE];

  double RArandom,DECrandom; // rad
  long RandomPointingCnt = -1;
  long RandomPointingCnt_limit = 8; // number of consecutive exposures on same random pointing

  //double Ttrack = 0.0;

  int printHELP = 0;
  int n;

  // variables for tracking analysis
  double RA0, RA1, DEC0, DEC1;
  double mRA0, mRA1, mDEC0, mDEC1;
  double UTtime0, UTtime1;
  double dtime, dRA, dDEC;
  int RAflip0, RAflip1;
  long IMG0index;
  int loopOK;
  long index;
  double trackingrateRA0, trackingrateDEC0;
  double trackingrateRA1, trackingrateDEC1;
  char fname0[SBUFFERSIZE];
  char fnameimg0[SBUFFERSIZE];
  char fnameimg1[SBUFFERSIZE];
  double dx, dy, RAoffset, DECoffset;
  char trackingfile[SBUFFERSIZE];

  int CHANGEtarget = 0;

  double f1,f2;
  int m;
  long IMGindex;
  float tmpf;


  TRACKINGRATELOG_NBpt = 0;

  RAold = -1.0;
  DECold = -1.0;



  FILE *fpGT;
  long gti;
  


  tzset();

  srand(time(NULL));


  if(argc<2)
    {
      printf("ERROR: %s needs at least one argument\n",argv[0]);
      printHELP = 1;
    }
  if(argc==2)
    if(strcmp(argv[1],"help")==0)
       printHELP = 1;

  
  if(printHELP == 1)
    {
      printf("\n");
      printf("\n");
      printf(" Version: %s\n",VERSION);
      printf("\n");
      printf("--------------- LIST OF COMMANDS -------------------\n");
      printf("\n");


      printf("      help                     : print list of commands and exit\n");
      printf("\n");

      printf("ASTRONOMICAL COMMANDS--------------------------------\n");
      printf("\n");
      printf("      utcnow                   : compute current UTC (->utc.txt)\n");
      printf("      lst yr mo day            : compute local sidearal time (->lst.txt)\n");
      printf("      lstnow                   : compute current local sidereal time (->lst.txt)\n");
      printf("      radec2altaz              : convert ra dec into alt az (deg)\n");
      printf("      altaz2radec              : convert alt az into ra dec (deg)\n");
      printf("      moonpos                  : get current Moon position\n");
      printf("      sunpos                   : get current Sun position\n");
      printf("\n");


      printf("COMMUNICATION SETUP-------------------------------------\n");
      printf("\n");
      printf("      resetusb <portname>      : reset usb port\n");
      printf("      scanusb                  : scan usb ports, write USBports.txt and exit\n");
      printf("\n");

      printf("LOW LEVEL COMMANDS--------------------------------------\n");
      printf("\n");
      printf("      readdio                  : read digital input\n");
      printf("      setdio ch x              : set TTL channel (C-..) ch to x (0/1)\n");	
      printf("      powercams x              : turn power on/off (0/1) for cameras\n");
      printf("      powerwebcam x            : turn power on/off (0/1) for webcams\n");
      printf("      powermount x             : turn aux power on/off (0/1) for mount\n");
      printf("      led x                    : turn on/off LED\n");
      printf("\n");


      printf("MOUNT COMMANDS----------------------------------------\n");
      printf("\n");
      printf("      home                     : home mount\n");
      printf("      tracksidN                : track sid rate, North hemisphere\n");
      printf("      mountstop                : stop tracking or slew\n");
      printf("      dispmc                   : display mount coordinates\n");
      printf("      dispmlsw                 : display mount limit switches\n");
      printf("      movpos mra mdec          : move to position 0.0 < maz < 1.0 and 0.0 < malt < 1.0\n");
      printf("      track az alt             : track (units = steps / sec)\n");
      printf("      park                     : park system into standby\n");
      printf("\n");

      printf("TEMPERATURE & HUMIDITY & LUMINOSITY COMMANDS---------------\n");
      printf("\n");
      printf("      gettemp1                 : get temperature for sensor 1\n");
      printf("      gettemp2                 : get temperature for sensor 2\n");
      printf("      gettemp12                : get temperature for sensors 1 & 2. Run for 100 samples, output to log file\n");
      printf("      gettemp12loop            : execute gettemp12 in loop\n");
      printf("      gethum                   : get humidity, run for 100 samples, output to log file\n");
      printf("      getacpowst               : get AC power status, run for 100 samples\n");
      printf("      getlum <cam>             : get luminosity of webcam <cam>\n");
      printf("\n");


      printf("CAMERA COMMANDS--------------------------------------\n");
      printf("\n");
      printf("      setcammode <cam> <mode>  : set camera mode (0=off, 1=USB, 2=TTL)\n");
      printf("      camlistfiles <cam>       : list files on camera <cam>\n");
      printf("      camloadfiles <cam>       : load files from camera <cam>\n");
      printf("      camrmfiles <cam>         : rm files from camera <cam>\n");
      printf("      camsetiso <cam> <isomode>: set ISO mode on camera (1=100 ..) <cam>\n");
      printf("\n");


      printf("HIGH LEVEL COMMANDS------------------------------------\n");
      printf("\n");
      printf("      startwebcamloop <cam>    : start webcam loop for cameras 1 to <cam>\n");
      printf("      stopwebcamloop <cam>     : stop webcam loop for cameras 1 to <cam>\n");
      printf("      init                     : init system (scanusb + home)\n");
      printf("      polaralign               : polar align sequence\n");
      printf("      test                     : test routine\n");
      printf("      observingstatus          : set observing status according to weather\n");
      printf("      takedark etime isomode   : take dark\n");
      printf("      taketestim etime isomode : take test image\n");
      printf("      mainloop                 : main loop\n");
      printf("      monitorloop              : monitor loop\n");
      printf("      mloweathermon            : monitor MLO observatory weather (infinite loop)\n");
      printf("\n");


      printf("DATA MANAGEMENT AND ANALYSIS---------------------------\n");
      printf("\n");
      printf("      compress yyyy-mm-dd frnb         : compress frame\n");
      printf("      raw2fits yyyy-mm-dd frnb         : convert from raw to fits, use compressed file if .CR2 does not exist\n");
      printf("      mkimlist                         : make/update image list file\n");
      printf("      trackinganalysis <YR> <MO> <DAY> : perform tracking analysis\n");
      printf("      mergetracking                    : merge all tracking log data\n");
      printf("      trackrate <mRA> <mDEC> <flip>    : compute tracking rate for mRA, mDEC, flip\n");
      printf("\n");
      exit(0);
    }
 

  SKYCAM_loadTrackingLog("GTable.txt");

  SKYCAM_read_STATUS();

 
  // OFFLINE FUNCTIONS START HERE


  if(strcmp(argv[1],"compress")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: compress needs 2 arguments\n");
	  exit(0);
	}
      n = snprintf(fname,SBUFFERSIZE,"%s/%s/%s/IMG_%04d.info",DATA_DIRECTORY,argv[2],DATA_HEADER_DIRECTORY2,atoi(argv[3]));
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("reading file %s\n",fname);
      
      ok = 0;
      if((fp = fopen(fname,"r") )==NULL)
	{
	  printf("ERROR: cannot read file \"%s\"\n",fname);
	  exit(0);
	}
      
      while(fgets(line,1000,fp)!=NULL)
	{
	  sscanf(line,"%s %s %s",w0,w1,w2);
	  if(strcmp(w0,"ISO")==0)
	    {
	      isovalue = atol(w2);
	      ok = 1;
	    }
	}
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      
      if(ok==0)
	{
	  printf("ERROR: parameter ISO does not exist in file \"%s\"\n",fname);
	  exit(0);
	}
      else
	printf("iso = %ld\n",isovalue);

      
      
      ok = 0;
      n = snprintf(fname,SBUFFERSIZE,"%s/%s/%s/IMG_%04d.CR2",DATA_DIRECTORY,argv[2],DATA_CR2_DIRECTORY2,atoi(argv[3]));
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      n = snprintf(fname1,SBUFFERSIZE,"%s.fitsc.rz",fname);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  

      if((fp = fopen(fname1,"r"))!=NULL)
	{
	  printf("ERROR: file %s already exists\n",fname1);
	  exit(0);
	}

      if(isovalue==100)
	{
	  ok = 1;
	  n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/CR2toFITSc_100 %s\n",fname);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	  system(commandline);
	}
      if(isovalue==200)
	{
	  ok = 1;
	  n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/CR2toFITSc_200 %s\n",fname);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	  system(commandline);
	}
      if(isovalue==400)
	{
	  ok = 1;
	  n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/CR2toFITSc_400 %s\n",fname);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	  system(commandline);
	}
      if(ok==0)
	{
	  printf("ERROR: iso value %ld not valid\n",isovalue);
	  exit(0);
	}

      exit(0);
    }



  if(strcmp(argv[1],"raw2fits")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: raw2fits needs 2 arguments\n");
	  exit(0);
	}
      n = snprintf(fname,SBUFFERSIZE,"%s/%s/%s/IMG_%04d.info",DATA_DIRECTORY,argv[2],DATA_HEADER_DIRECTORY2,atoi(argv[3]));
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("reading file %s\n",fname);
      
      ok = 0;
      if((fp = fopen(fname,"r") )==NULL)
	{
	  printf("ERROR: cannot read file \"%s\"\n",fname);
	  exit(0);
	}
      
      while(fgets(line,1000,fp)!=NULL)
	{
	  sscanf(line,"%s %s %s",w0,w1,w2);
	  if(strcmp(w0,"ISO")==0)
	    {
	      isovalue = atol(w2);
	      ok = 1;
	    }
	}
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}

      
      if(ok==0)
	{
	  printf("ERROR: parameter ISO does not exist in file \"%s\"\n",fname);
	  exit(0);
	}
      else
	printf("iso = %ld\n",isovalue);

      
      n = snprintf(commandline,SBUFFERSIZE,"mkdir -p %s/%s/%s",DATA_DIRECTORY,argv[2],DATA_FITS_DIRECTORY2);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
      system(commandline);

      
      ok = 0;
      n = snprintf(fname,SBUFFERSIZE,"%s/%s/%s/IMG_%04d.CR2",DATA_DIRECTORY,argv[2],DATA_CR2_DIRECTORY2,atoi(argv[3]));
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      n = snprintf(fnameout,SBUFFERSIZE,"%s/%s/%s/IMG_%04d.fits",DATA_DIRECTORY,argv[2],DATA_FITS_DIRECTORY2,atoi(argv[3]));
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      n = snprintf(fname1,SBUFFERSIZE,"%s.fitsc.rz",fname);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  

      if((fp = fopen(fname,"r"))!=NULL)
	{
	  ok = 1;
	  n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/CR2toFITS %s %s\n",fname,fnameout);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  
	  printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	  system(commandline);
	  exit(0);
	}

      if((fp = fopen(fname1,"r"))!=NULL)
	{
	  ok = 0;
	  if(isovalue==100)
	    {
	      ok = 1;
	      n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/FITSctoFITS_100 %s %s\n",fname1,fnameout);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	      system(commandline);
	    }
	  if(isovalue==200)
	    {
	      ok = 1;
	      n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/FITSctoFITS_200 %s %s\n",fname1,fnameout);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      printf("EXECUTING SYSTEM COMMAND: %s\n",commandline);
	      system(commandline);
	    }
	  if(ok==0)
	    {
	      printf("ERROR: iso value %ld not valid\n",isovalue);
	      exit(0);
	    }
	}
      else
	{
	  printf("ERROR: file %s and %s do not exist\n",fname,fname1);
	  exit(0);
	}

      exit(0);
    }


 

  // build a single file list of all images (1 image per line), with critical parameters
  if(strcmp(argv[1],"mkimlist")==0)
    {  
      if((fp = fopen("imlist.txt","w"))==NULL)
	{
	  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	  exit(0);
	}
      fclose(fp);

      for(TIME_UTC_YR=2000;TIME_UTC_YR<3000;TIME_UTC_YR++)
	for(TIME_UTC_MON=0;TIME_UTC_MON<13;TIME_UTC_MON++)
	  for(TIME_UTC_DAY=0;TIME_UTC_DAY<33;TIME_UTC_DAY++)
	    {
	      n = snprintf(dirname,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      // test if directory exists
	      if(stat(dirname,&file_st) == 0)
		{
		  printf("Directory \"%s\" is present\n",dirname);

		  for(cam=1; cam<3; cam++)
		    {
		      for(IMGindex=0;IMGindex<10000;IMGindex++)
			{
			  n = snprintf(fname,SBUFFERSIZE,"%s/IMG%d_%04ld.info",dirname,cam,IMGindex);
			  if(n >= SBUFFERSIZE) 
			    {
			      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
			      exit(0);
			    }	  
			  if(stat(fname,&file_st) == 0)
			    {
			      printf("   Writing entry IMG%d %04ld \n",cam,IMGindex);
			      SKYCAM_read_imgheader(TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, cam, IMGindex, 0);
			     
			      if((fp = fopen("imlist.txt","a"))==NULL)
				 {
			      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
			      exit(0);
			    }	  
			      fprintf(fp,"%d %02d %02d",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY); // 1 2 3
			      fprintf(fp," %07.4f", IMGHEADER_UT_START); // 4	  
			      fprintf(fp," %d %04ld  %10s", cam, IMGindex, IMGHEADER_IMTYPE); // 5 6 7
			      fprintf(fp," %5.1f",IMGHEADER_SHUTTER);  // 8
			      fprintf(fp," %3d",IMGHEADER_ISO); // 9

			      fprintf(fp," %8.4f",IMGHEADER_RA);  // 10
			      fprintf(fp," %8.4f",IMGHEADER_DEC); // 11
			      fprintf(fp," %8.4f",IMGHEADER_ALT); // 12
			      fprintf(fp," %8.4f",IMGHEADER_AZ);  // 13
			      fprintf(fp," %8.4f",IMGHEADER_MOUNTRA);  // 14
			      fprintf(fp," %8.4f",IMGHEADER_MOUNTDEC); // 15
			      fprintf(fp," %8.4f",IMGHEADER_MOUNT_TRACKrate_RA);  // 16
			      fprintf(fp," %8.4f",IMGHEADER_MOUNT_TRACKrate_DEC); // 17
			      fprintf(fp," %2d",IMGHEADER_MOUNT_RAflip);  // 18

			      fprintf(fp," %8.4f",IMGHEADER_TEMPERATURE1); // 19
			      fprintf(fp," %8.4f",IMGHEADER_TEMPERATURE2); // 20
			      fprintf(fp," %8.4f",IMGHEADER_HUMIDITY); // 21
			      fprintf(fp," %8.4f",IMGHEADER_SUN_ALT); // 22
			      fprintf(fp," %8.4f",IMGHEADER_SUN_AZ);  // 23
			      fprintf(fp," %8.4f",IMGHEADER_MOON_ALT); // 24
			      fprintf(fp," %8.4f",IMGHEADER_MOON_AZ);  // 25
			      fprintf(fp," %8.4f",IMGHEADER_MOON_MAGN);  // 26
			      
			      

			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE01); // 27
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE05); // 28
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE10); // 29
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE20); // 30
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE50); // 31
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE80); // 32
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE90); // 33
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE95); // 34
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE99); // 35
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE995); // 36
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE998); // 37
			      fprintf(fp," %5.0f",IMGHEADER_RED_PERCENTILE999); // 38

			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE01); // 39
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE05); // 40
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE10); // 41
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE20); // 42
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE50); // 43
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE80); // 44
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE90); // 45
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE95); // 46
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE99); // 47
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE995); // 48
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE998); // 49
			      fprintf(fp," %5.0f",IMGHEADER_GREEN1_PERCENTILE999); // 50

			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE01); // 51
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE05); // 52
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE10); // 53
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE20); // 54
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE50); // 55
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE80); // 56
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE90); // 57
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE95); // 58
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE99); // 59
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE995); // 60
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE998); // 61
			      fprintf(fp," %5.0f",IMGHEADER_GREEN2_PERCENTILE999); // 62

			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE01); // 63
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE05); // 64
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE10); // 65
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE20); // 66
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE50); // 67
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE80); // 68
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE90); // 69
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE95); // 70
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE99); // 71
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE995); // 72
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE998); // 73
			      fprintf(fp," %5.0f",IMGHEADER_BLUE_PERCENTILE999); // 74

			      /*			      fprintf(fp," %5.10f",IMGHEADER_NOISE_RED); // 75
			      fprintf(fp," %5.10f",IMGHEADER_NOISE_GREEN1); // 76
			      fprintf(fp," %5.10f",IMGHEADER_NOISE_GREEN2); // 77
			      fprintf(fp," %5.10f",IMGHEADER_NOISE_BLUE); // 78
			      

			      fprintf(fp," %8.4f",IMGHEADER_ASTROM_FITQUALITY);
			      fprintf(fp," %10ld",IMGHEADER_ASTROM_NBstarImage);
			      fprintf(fp," %8.4f",IMGHEADER_ASTROM_RA);
			      fprintf(fp," %8.4f",IMGHEADER_ASTROM_DEC);
			      fprintf(fp," %8.4f",IMGHEADER_ASTROM_PA);
			      fprintf(fp," %8.4f",IMGHEADER_ASTROM_PIXSCALE);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_Y);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_Y);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_YY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_YY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XXX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XXY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_YYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XXX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XXY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_YYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XXXX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XXXY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XXYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_XYYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTX_YYYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XXXX);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XXXY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XXYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_XYYY);
			      fprintf(fp," % .8f",IMGHEADER_ASTROM_DISTY_YYYY);
			      fprintf(fp," %4ld",IMGHEADER_ASTROM_XSIZE);
			      fprintf(fp," %4ld",IMGHEADER_ASTROM_YSIZE);
			      */

			      if(strlen(IMGHEADER_TARGETDESCRIPTION)>0)
				fprintf(fp," %20s",IMGHEADER_TARGETDESCRIPTION);
			      else
				fprintf(fp,"                    _"); 
				

			      fprintf(fp,"\n");
			      if(fclose(fp)!=0)
				{
				  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
				  exit(0);
				} 		      
			    }
			}
		    }
		}
	    }

      exit(0);
    }

  
  // tracking analysis for 1 night
  // output: ASCII file
  // list of mRA, mDEC, dt, driftRA, driftDEC, trackingrateRA, trackingrateDEC
  if(strcmp(argv[1],"trackinganalysis")==0)
    {
      if(argc!=5)
	{
	  printf("ERROR: need 3 argument after %s: YR, MON, DAY\n", argv[1]);
	  SKYCAM_exit();
	}
      TIME_UTC_YR = atoi(argv[2]);
      TIME_UTC_MON = atoi(argv[3]);
      TIME_UTC_DAY = atoi(argv[4]);
      n = snprintf(dirname,SBUFFERSIZE,"%s/%04d-%02d-%02d/%s",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,DATA_HEADER_DIRECTORY2);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      // test if directory exists
      if(stat(dirname,&file_st) == 0)
	{
	  printf("Directory \"%s\" is present\n",dirname);
	  
	  n = snprintf(trackingfile,SBUFFERSIZE,"%s/%04d-%02d-%02d/tracking.txt",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	  

	  for(IMG0index=0; IMG0index<10000;IMG0index++)
	    {
	      n = snprintf(fname0, SBUFFERSIZE,"%s/IMG0_%04ld.info", dirname, IMG0index);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      n = snprintf(fnameimg0, SBUFFERSIZE, "%s/%04d-%02d-%02d/CR2/IMG0_%04ld.CR2", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMG0index);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  

	      if((stat(fname0, &file_st) == 0)&&(stat(fnameimg0, &file_st) == 0))
		{
		  SKYCAM_read_imgheader(TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, 0, IMG0index, 0);
		  UTtime0 = IMGHEADER_UT_START*3600.0 + 0.5*IMGHEADER_SHUTTER;
		  mRA0 = IMGHEADER_MOUNTRA;
		  mDEC0 = IMGHEADER_MOUNTDEC;
		  RA0 = IMGHEADER_RA;
		  DEC0 = IMGHEADER_DEC;
		  RAflip0 = IMGHEADER_MOUNT_RAflip;
		  trackingrateRA0 = IMGHEADER_MOUNT_TRACKrate_RA; 
		  trackingrateDEC0 = IMGHEADER_MOUNT_TRACKrate_DEC; 

		  loopOK = 1;
		  index = 1;
		  while(loopOK==1)
		    {
		      n = snprintf(fname1, SBUFFERSIZE, "%s/IMG0_%04ld.info", dirname, IMG0index+index);
		      if(n >= SBUFFERSIZE) 
			{
			  printERROR(__FILE__,__func__,__LINE__, "Attempted to write string buffer with too many characters");
			  exit(0);
			}	  
		      n = snprintf(fnameimg1, SBUFFERSIZE, "%s/%04d-%02d-%02d/CR2/IMG0_%04ld.CR2",DATA_DIRECTORY,TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY, IMG0index+index);
		      if(n >= SBUFFERSIZE) 
			{
			  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
			  exit(0);
			}	  

		      if((stat(fname1,&file_st) == 0)&&(stat(fnameimg1,&file_st) == 0))
			{
			  SKYCAM_read_imgheader(TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, 0, IMG0index+index, 0);
			  UTtime1 = IMGHEADER_UT_START*3600.0 + 0.5*IMGHEADER_SHUTTER;
			  mRA1 = IMGHEADER_MOUNTRA;
			  mDEC1 = IMGHEADER_MOUNTDEC;
			  RA1 = IMGHEADER_RA;
			  DEC1 = IMGHEADER_DEC;

			  RAflip1 = IMGHEADER_MOUNT_RAflip;
			  trackingrateRA1 = IMGHEADER_MOUNT_TRACKrate_RA; 
			  trackingrateDEC1 = IMGHEADER_MOUNT_TRACKrate_DEC; 

			  

			  dtime = UTtime1-UTtime0; // sec
			  dRA = RA1-RA0;
			  dDEC = DEC1-DEC0;

			  
			  if((dtime<3600.0)&&(fabs(dRA)<2.0e-2)&&(fabs(dDEC)<2.0e-2)&&(RAflip0==RAflip1))
			    {
			      printf("------ IMAGE PAIR IDENTIFIED: %04ld - %04ld -------\n", IMG0index, IMG0index+index);

			      // check if entry already exists			      
			      n = snprintf(entry, SBUFFERSIZE, "%04d %02d %02d %04ld %04ld", TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMG0index, IMG0index+index);
			      if(n >= SBUFFERSIZE) 
				{
				  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
				  exit(0);
				}

			      if((fp = fopen(trackingfile,"a"))==NULL)
				{
				  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
				  exit(0);
				}
			      fclose(fp);
			      if(read_parameter(trackingfile, entry ,content) == 0)
				{			      
				  
				  printf("------ PERFORMING TRACKING ANALYSIS: %04ld - %04ld\n",IMG0index,IMG0index+index);
				  n = snprintf(fname0, SBUFFERSIZE, "%s/%04d-%02d-%02d/CR2/IMG0_%04ld.CR2", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMG0index);
				  if(n >= SBUFFERSIZE) 
				    {
				      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
				      exit(0);
				    }	  
				  n = snprintf(fname1, SBUFFERSIZE, "%s/%04d-%02d-%02d/CR2/IMG0_%04ld.CR2", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMG0index+index);
				  if(n >= SBUFFERSIZE) 
				    {
				      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
				      exit(0);
				    }	  
				  n = snprintf(commandline,SBUFFERSIZE,"./soft/measure_offset_cam1 %s %s", fname0, fname1);
				  if(n >= SBUFFERSIZE) 
				    {
				      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
				      exit(0);
				    }	  
				  printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
				  system(commandline);

				  if((fp = fopen("xcent.txt","r"))==NULL)
				    {
				      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
				      exit(0);
				    }
				  fscanf(fp,"%lf",&dx);
				  fclose(fp);

				  if((fp = fopen("ycent.txt","r"))==NULL)
				    {
				      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
				      exit(0);
				    }
				  fscanf(fp,"%lf",&dy);
				  fclose(fp);
				  
				  if(RAflip0==-1)
				    {
				      RAoffset = dy*PIXELSIZE_CAM[0]; // arcsec
				      DECoffset = dx*PIXELSIZE_CAM[0];
				    }
				  else
				    {
				      RAoffset = -dy*PIXELSIZE_CAM[0];
				      DECoffset = -dx*PIXELSIZE_CAM[0];
				    }
			
				  if((fp = fopen(trackingfile,"a"))==NULL)
				    {
				      printERROR(__FILE__,__func__,__LINE__,"fopen() error");
				      exit(0);
				    }
				  fprintf(fp,"%04d %02d %02d %04ld %04ld %f %f %f %f %f %d %f %f %f %f %.10f %.10f\n", TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, IMG0index, IMG0index+index, 0.5*(mRA0+mRA1), 0.5*(mDEC0+mDEC1), dtime, dx, dy, RAflip0, 0.5*(trackingrateRA0+trackingrateRA1), 0.5*(trackingrateDEC0+trackingrateDEC1), RAoffset, DECoffset, 0.5*(trackingrateRA0+trackingrateRA1) - (RAoffset*NBstepMountRot/3600.0/360.0)/dtime, 0.5*(trackingrateDEC0+trackingrateDEC1) - (1.0*RAflip)*(DECoffset*NBstepMountRot/3600.0/360.0)/dtime);
				  fclose(fp);
				}
			      else
				printf("------ TRACKING ANALYSIS EXITS: %04ld - %04ld\n", IMG0index, IMG0index+index);
			    }
			  else
			    {
			      printf("------ IMAGE PAIR DOES NOT MATCH: %04ld - %04ld -------\n", IMG0index, IMG0index+index);
			      loopOK = 0;
			    }
			}
		      else
			loopOK = 0;
		      index ++;
		    }
		}
	    }
	}
      exit(0);
    }

  // merge tracking log files into single tracking log file
  if(strcmp(argv[1],"mergetracking")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: %s needs no arguments\n",argv[1]);
	  exit(0);
	}

      n = snprintf(commandline,SBUFFERSIZE,"rm TRACKING.log");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
      system(commandline);
      
      n = snprintf(commandline,SBUFFERSIZE,"cat %s/*/tracking.txt | grep -Ev 'nan' | awk '{if(($16>410)&&($16<422)&&($17>-5.0)&&($17<5.0)) print $0}'  > TRACKING.log",DATA_DIRECTORY);
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	
      printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
      system(commandline);
      exit(0);
    }

  // estimate tracking rate
  if(strcmp(argv[1],"trackrate")==0)
    {
      if(argc!=5)
	{
	  printf("ERROR: %s needs 3 arguments\n",argv[1]);
	  exit(0);
	}
      SKYCAM_computeTrackingRate_sidN( atof(argv[2]), atof(argv[3]), atoi(argv[4]));
            
      exit(0);
    }
  








  // monitor MLO weather
  if(strcmp(argv[1],"mloweathermon")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: mloweathermon needs no arguments\n");
	  exit(0);
	}
      while(1)
	{
	  system("/skycam/soft/getMLOweatherFile");
	  sleep(120);
	}

      exit(0);
    }
  

  // start webcam loop
  if(strcmp(argv[1],"startwebcamloop")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      NOSTOP = 1;
      SKYCAM_start_webcamloop(atoi(argv[2]));
      exit(0);
    }
  // stop webcam loop
  if(strcmp(argv[1],"stopwebcamloop")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      n = snprintf(commandline,SBUFFERSIZE,"touch _webcamloop_stop");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	 
      printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
      system(commandline);
      exit(0);
    }







  // ONLINE FUNCTIONS START HERE
  sprintf(command, "gvfs-mount -u ~/.gvfs/gphoto2*");
  printf("SYSTEM COMMAND = %s\n", command);
  system(command);

  // monitor loop status, park mount if loop crashes
  if(strcmp(argv[1],"monitorloop")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: monitorloop needs no arguments\n");
	  exit(0);
	}
      SKYCAM_monitor_skycam();
      
      n = snprintf(line,SBUFFERSIZE,"SKYCAM LOOP CRASHED (DETECTED BY MONITOR) -> SAFE MODE\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
 
      system("rm /skycam/_SKYCAM_RUNNING");
      system("touch /skycam/STOP");
      sleep(2);
      SKYCAM_read_STATUS();
      
      if((MOUNTSTATUS!=4)&&(MOUNTSTATUS!=2))
	{
	  sleep(10);
	  printf("STOP TRACKING\n");
	  system("/skycam/skycam trackstop");
	  sleep(10);
	  printf("PARKING");
	  system("/skycam/skycam park");
	  sleep(10);
	}
      exit(0);
    }
  


  if((fp = fopen("/skycam/_SKYCAM_RUNNING","r"))!=NULL)
    {
      printf("ERROR: SKYCAM ALREADY RUNNING\n");
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      exit(0);
    }
  
  system("touch /skycam/_SKYCAM_RUNNING");
  

  // ASTRONOMICAL COMMANDS

  // compute current UTC
  if(strcmp(argv[1],"utcnow")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 arguments after utcnow\n");
	  SKYCAM_exit();
	}
      compute_UTCnow();
      hr = (int) TTIME_UTC;
      min = (int) (60.0*(TTIME_UTC-hr));
      sec = (int) (3600.0*(TTIME_UTC-1.0*hr-1.0/60.0*min));

      printf("%04d-%02d-%02d %02d:%02d:%02d\n",TIME_UTC_YR,TIME_UTC_MON,TIME_UTC_DAY,hr,min,sec);
      
      SKYCAM_exit();
    }

  // compute LST
  if(strcmp(argv[1],"lst")==0)
    {
      if(argc!=5)
	{
	  printf("ERROR: need 3 arguments after lst\n");
	  SKYCAM_exit();
	}
      lst = compute_LST(SITE_LONG, atoi(argv[2]), atoi(argv[3]), atof(argv[4]));
      printf("LST = %f\n",lst);
      if((fp = fopen("/skycam/lst.txt","w"))==NULL)
	{
	  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	  exit(0);
	}
      fprintf(fp,"%f\n",lst);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
      SKYCAM_exit();
    }
    
  // compute current current LST
  if(strcmp(argv[1],"lstnow")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 arguments after lstnow\n");
	  SKYCAM_exit();
	}
      compute_LSTnow();
      TIME_LST_hr = (int) TIME_LST;
      TIME_LST_mn = (int) (60.0*(TIME_LST-TIME_LST_hr));
      TIME_LST_sec = 60.0*(60.0*(TIME_LST-TIME_LST_hr)-TIME_LST_mn);
      printf("LST = %f hr  %02d:%02d:%05.2f\n",TIME_LST, TIME_LST_hr,TIME_LST_mn,TIME_LST_sec);
      if((fp = fopen("/skycam/lst.txt","w"))==NULL)
	{
	  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	  exit(0);
	}
      fprintf(fp,"%f\n",TIME_LST);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
      SKYCAM_exit();
    }
    
  // convert ra dec into alt az
  if(strcmp(argv[1],"radec2altaz")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after radec2altaz\n");
	  SKYCAM_exit();
	}
      if((fp = fopen("/skycam/lst.txt","r"))==NULL)
	{
	  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	  exit(0);
	}
      fscanf(fp,"%lf\n",&TIME_LST);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
      printf("LST = %f\n",TIME_LST);
      compute_coordinates_from_RA_DEC(atof(argv[2])/180.0*M_PI,atof(argv[3])/180.0*M_PI);

      printf("RA     = %6.2f deg\n",COORD_RA/M_PI*180.0);
      printf("DEC    = %6.2f deg\n",COORD_DEC/M_PI*180.0);
      printf("ALT    = %6.2f deg\n",COORD_ALT/M_PI*180.0);
      printf("AZ     = %6.2f deg\n",COORD_AZ/M_PI*180.0);
      SKYCAM_exit();
    }

  // convert alt az into ra dec
  if(strcmp(argv[1],"altaz2radec")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after altaz2radec\n");
	  SKYCAM_exit();
	}
      if((fp = fopen("/skycam/lst.txt","r"))==NULL)
	{
	  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	  exit(0);
	}
      fscanf(fp,"%lf\n",&TIME_LST);
      if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
      fp = NULL;
      printf("LST = %f\n",TIME_LST);
      compute_coordinates_from_ALT_AZ(atof(argv[2])/180.0*M_PI,atof(argv[3])/180.0*M_PI);
     
      printf("RA     = %6.2f deg\n",COORD_RA/M_PI*180.0);
      printf("DEC    = %6.2f deg\n",COORD_DEC/M_PI*180.0);
      printf("ALT    = %6.2f deg\n",COORD_ALT/M_PI*180.0);
      printf("AZ     = %6.2f deg\n",COORD_AZ/M_PI*180.0);
      SKYCAM_exit();
    }


  

  // get current Moon position
  if(strcmp(argv[1],"moonpos")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: needs no argument after moonpos\n");
	  SKYCAM_exit();
	}
      get_Moon_pos();
      printf("MOON:\n");
      printf("  RA = %+11.6f rad\n",MOON_RA);
      printf(" DEC = %+11.6f rad\n",MOON_DEC);
      printf("MAGN = %+11.6f\n",MOON_MAGN);
      printf(" ALT = %+11.6f deg\n",MOON_ALT/M_PI*180.0);
      SKYCAM_exit();
    }

  // get current Sun position
  if(strcmp(argv[1],"sunpos")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: needs no argument after sunpos\n");
	  SKYCAM_exit();
	}
      get_Sun_pos();
      printf("SUN:\n");
      printf("  RA = %+11.6f rad  ( %+11.6f deg )\n",SUN_RA,SUN_RA*180.0/M_PI);
      printf(" DEC = %+11.6f rad  ( %+11.6f deg )\n",SUN_DEC,SUN_DEC*180/M_PI);
      printf(" ALT = %+11.6f deg\n",SUN_ALT/M_PI*180.0);
      printf("  AZ = %+11.6f deg\n",SUN_AZ/M_PI*180.0);
      SKYCAM_exit();
    }




  // COMMUNICATION SETUP

  if(strcmp(argv[1],"scanusb")==0)
    {
      if((scanttyUSBports()) == -1)
	{
	  printf("ERROR: mount aio dio not found\n");
	  SKYCAM_exit();
	}
      SKYCAM_exit();
    }

  // read USB ports numbers
  if((fp = fopen("/skycam/USBports.txt","r"))==NULL)
    {
      n = snprintf(line,SBUFFERSIZE,"ERROR: cannot read file \"/skycam/USBports.txt\"\n");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_log();
      SKYCAM_exit();
    }
  
  fscanf(fp,"%s %d\n",str1,&mountUSBportNB);
  fscanf(fp,"%s %d\n",str1,&dioUSBportNB);
  fscanf(fp,"%s %d\n",str1,&aioUSBportNB);
  if(fclose(fp)!=0)
	{
	  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
	  exit(0);
	}
  fp = NULL;
  if(mountUSBportNB==-1)
    printf("mount was not detected on last scan\n");
  else
    printf("mount on /dev/ttyUSB%d\n",mountUSBportNB);
  if(dioUSBportNB==-1)
    printf("dio   was not detected on last scan\n");
  else
    printf("dio   on /dev/ttyUSB%d\n",dioUSBportNB);
  if(aioUSBportNB==-1)
    printf("aio   was not detected on last scan\n");
  else
    printf("aio   on /dev/ttyUSB%d\n",aioUSBportNB);
 
  // LOW LEVEL COMMANDS

  // test digital input
  if(strcmp(argv[1],"readdio")==0)
    {
      SKYCAM_command_readDIO();
      SKYCAM_exit();
    }


  // set dio output
  if(strcmp(argv[1],"setdio")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after setdio\n");
	  SKYCAM_exit();
	}	
      printf("chan = %c\n",argv[2][0]);
      printf("val  = %d\n",atoi(argv[3]));
      dio_setdigital_out(argv[2][0],atoi(argv[3]));
      
      SKYCAM_exit();
    }



  // turn power on/off
  if(strcmp(argv[1],"powercams")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after powercams\n");
	  SKYCAM_exit();
	}	

      printf("Power Cams = %d\n",atoi(argv[2]));
      DIO_power_Cams(atoi(argv[2]));
           
      SKYCAM_exit();
    }

  // turn power webcam relay on/off
  if(strcmp(argv[1],"powerwebcam")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after power webcam\n");
	  SKYCAM_exit();
	}	

      printf("Power webcam= %d\n",atoi(argv[2]));
      DIO_power_webcam(atoi(argv[2]));

      SKYCAM_exit();
    }

  // turn power aux relay on/off
  if(strcmp(argv[1],"powermount")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after power aux\n");
	  SKYCAM_exit();
	}	

      printf("Power Mount = %d\n",atoi(argv[2]));
      DIO_power_Mount(atoi(argv[2]));

      SKYCAM_exit();
    }

  // turn LED on/off
  if(strcmp(argv[1],"led")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after led\n");
	  SKYCAM_exit();
	}	

      printf("LED = %d\n",atoi(argv[2]));
      
      if(atoi(argv[2])==1)
	{
	  dio_setdigital_out('C',0);
	  dio_setdigital_out('D',1);
	}
      else
	{
	  dio_setdigital_out('C',0);
	  dio_setdigital_out('D',0);
	}
      SKYCAM_exit();
    }



  // MOUNT COMMANDS

  // home
  if(strcmp(argv[1],"home")==0)
    {
      openmountfd();      
      SKYCAM_command_home();
      closemountfd();
      SKYCAM_exit();
    }

  // track sidereal, North hemisphere
  if(strcmp(argv[1],"tracksidN")==0)
    {
      openmountfd();
      get_posmountradec();
      SKYCAM_command_tracksidN();
      closemountfd();
      SKYCAM_exit();
    }

  // stop tracking
  if(strcmp(argv[1],"mountstop")==0)
    {
      openmountfd();
      SKYCAM_command_MOUNTSTOP();
      closemountfd();
      SKYCAM_exit();
    }  

  // display alt az mount coordinates
  if(strcmp(argv[1],"dispmc")==0)
    {
      openmountfd();
      i = 0;
      while(i<1)
       {     
         get_posmountradec();

	 if(pos_mountdec<mDEC_NPOLE)
	   RAflip = -1;
	 else
	   RAflip = 1;

	 compute_LSTnow();
	 pos_RA = 2.0*M_PI*((24.0*(-pos_mountra-0.25*RAflip)+TIME_LST)/24.0 + mRA_MERIDIAN); // [rad]
	 pos_DEC = 2.0*M_PI*(90.0-360.0*fabs(pos_mountdec-mDEC_NPOLE))/360.0; // [rad]
	 
	 while(pos_RA<0.0)
	   pos_RA += 2.0*M_PI;
	 while(pos_RA>2.0*M_PI)
	   pos_RA -= 2.0*M_PI;
// RA  [hr]  = 24.0*(mRA+RAflip*0.25)+LST
// DEC [deg] = 90.0-360.0*fabs(mDEC-mDEC_NPOLE)
	 compute_coordinates_from_RA_DEC(pos_RA, pos_DEC);

	 printf("\r%5ld %g %g [%d %g hr] RA = %g hr DEC = %g deg  ALT = %g deg  AZ = %g deg         ", i, pos_mountra, pos_mountdec, RAflip, TIME_LST, 24.0*(pos_RA/2.0/M_PI), 360.0*(pos_DEC/2.0/M_PI), 180.0*COORD_ALT/M_PI, 180.0*COORD_AZ/M_PI);  
         fflush(stdout);

	 usleep(200000);

         i++;      
       }
      closemountfd();

      SKYCAM_exit();
    }


  // display limit switches
  if(strcmp(argv[1],"dispmlsw")==0)
    {
      openmountfd();
      i = 0;
      while(i<100)
       {     
         get_mountswitches();
	 usleep(200000);
         i++;      
       }
      
      closemountfd();
      SKYCAM_exit();
    }



  // move to position
  if(strcmp(argv[1],"movpos")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after movpos\n");
	  SKYCAM_exit();
	}
     
      printf("az  = %f\n",atof(argv[2]));
      printf("alt = %f\n",atof(argv[3]));
      openmountfd();
      SKYCAM_command_movpos(atof(argv[2]),atof(argv[3]));
      closemountfd();
      SKYCAM_exit();
    }

  // set position without moving
  if(strcmp(argv[1],"setpos")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after movpos\n");
	  SKYCAM_exit();
	}
     
      printf("az  = %f\n",atof(argv[2]));
      printf("alt = %f\n",atof(argv[3]));
      openmountfd();
      set_posmount_radec(atof(argv[2]),atof(argv[3]));
      closemountfd();
      SKYCAM_exit();
    }


  // track 
  if(strcmp(argv[1],"track")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after \"%s\"\n",argv[1]);
	  SKYCAM_exit();
	}
   
      openmountfd();
      track_mra_mdec(atof(argv[2]),atof(argv[3]));
      closemountfd();

      SKYCAM_exit();
    }


  // park
  if(strcmp(argv[1],"park")==0)
    {
      SKYCAM_command_park();
      SKYCAM_exit();
    }










  // TEMPERATURE & HUMIDITY COMMANDS

  // get temperature for sensor #1
  if(strcmp(argv[1],"gettemp1")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after gettemp1\n");
	  SKYCAM_exit();
	}
      if(!(diofd<0))
	{
	  printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
   
      aio_analogchannel_init('A',3,2);
      cnt = 0;
      while(1)
	{
	  v = aio_analogchannel_value('A');
	  printf("%ld %.5f %.4f\n",cnt,v,VtoTemp(v));
	  usleep(100000);
	  cnt ++;
	}
      if(close(diofd)!=0)
	{
	  printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      SKYCAM_exit();
    }

  // get temperature for sensor #2
  if(strcmp(argv[1],"gettemp2")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after gettemp1\n");
	  SKYCAM_exit();
	}
      if(!(diofd<0))
	{
	  printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  SKYCAM_exit();
	}
      diofd = open_ttyUSBport(dioUSBportNB);
   
      aio_analogchannel_init('B',3,2);
      cnt = 0;
      while(1)
	{
	  v = aio_analogchannel_value('B');
	  printf("%ld %.5f %.4f\n",cnt,v,VtoTemp(v));
	  usleep(100000);
	  cnt ++;
	}
      if(close(diofd)!=0)
	{
	  printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",diofd,__func__,__LINE__);
	  SKYCAM_exit();
	}
      diofd = -1;
      fdcnt--;
      SKYCAM_exit();
    }

  // get temperature for sensors #1 and #2
  if(strcmp(argv[1],"gettemp12")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after gettemp12\n");
	  SKYCAM_exit();
	}

      SKYCAM_command_gettemp12(100);
      SKYCAM_exit();
    }

  // get temperature for sensors #1 and #2, loop
  if(strcmp(argv[1],"gettemp12loop")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after gettemp12loop\n");
	  SKYCAM_exit();
	}
      while(1)
	{
	  SKYCAM_command_gettemp12(100);
	  sleep(10);
	}
      SKYCAM_exit();
    }

  // get humidity
  if(strcmp(argv[1],"gethum")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after gethum\n");
	  SKYCAM_exit();
	}

      SKYCAM_command_gethumidity(100);
      SKYCAM_exit();
    }

  // get AC power status
  if(strcmp(argv[1],"getacpowst")==0)
    {
      if(argc!=2)
	{
	  printf("ERROR: need 0 argument after getacpowst\n");
	  SKYCAM_exit();
	}

      SKYCAM_command_getACPowerStatus(100);
      SKYCAM_exit();
    }

  // get luminosity level
  if(strcmp(argv[1],"getlum")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after getlum\n");
	  SKYCAM_exit();
	}
      SKYCAM_command_getwebcamlum(atoi(argv[2]));
      SKYCAM_exit();
    }


  // CAMERA COMMANDS

  if(strcmp(argv[1],"setcammode")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      SKYCAM_command_setCAM_mode(atoi(argv[2]),atoi(argv[3]));
      SKYCAM_exit();    
    }

  if(strcmp(argv[1],"camlistfiles")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      SKYCAM_command_cam_listFILES(atoi(argv[2]));
      SKYCAM_exit();    
    }

  if(strcmp(argv[1],"camloadfiles")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      SKYCAM_command_cam_loadFILES(atoi(argv[2]));
      SKYCAM_exit();    
    }

  if(strcmp(argv[1],"camrmfiles")==0)
    {
      if(argc!=3)
	{
	  printf("ERROR: need 1 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}
      SKYCAM_command_cam_rmFILES(atoi(argv[2]));
      SKYCAM_exit();    
    }

  if(strcmp(argv[1],"camsetiso")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 argument after %s\n",argv[1]);
	  SKYCAM_exit();
	}     
      SKYCAM_command_camSetISO(atoi(argv[2]), atoi(argv[3]));
      SKYCAM_exit();
    }


  // HIGH LEVEL COMMANDS


  // initialization, boot up system 
  if(strcmp(argv[1],"init")==0)
    {
      SKYCAM_command_init();
      SKYCAM_exit();
    }    

  // polar align
  if(strcmp(argv[1],"polaralign")==0)
    {
      SKYCAM_command_polaralign(20.0,4,40.0);
      SKYCAM_exit();
      if(0==0)
	{
	  etime = 60.0*1.0;
	  n = snprintf(commandline,SBUFFERSIZE,"/skycam/soft/takeimageUSB %.2f 1 polaralign_sky.CR2",etime);
	  if(n >= SBUFFERSIZE) 
	    {
	      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	      exit(0);
	    }	 
	  printf("EXECUTING SYSTEM COMMAND: %s\n", commandline);
	  system(commandline);
	  SKYCAM_exit();

	  // 2644 1445
	  // 
	  // 2675 1329
	  // 31 x 116 pix = 1200 arcsec ~0.5 deg

	  printf("Waiting for image completion  ");
	  imgdone = 0;
	  cnt = 0;
	  while(imgdone == 0)
	    {
	      fflush(stdout);
	      if((fp = fopen("polaralign_sky.CR2","r"))==NULL)
		{
		  printERROR(__FILE__,__func__,__LINE__,"fopen() error");
		  exit(0);
		}
	      if(fp != NULL)
		{
		  imgdone = 1;
		  if(fclose(fp)!=0)
		    {
		      printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		      exit(0);
		    }
		  fp = NULL;
		}
	      usleep(100000);
	      if(cnt > (long) (1.2*(etime+20.0)/0.1))
		{
		  printf("ERROR: IMAGE NOT AVAILABLE AFTER TIMEOUT\n");
		  SKYCAM_ABORT();
		}
	      cnt++;
	    }
	  printf("IMAGE WRITTEN ON DISK\n");
	}
      SKYCAM_exit();
    }

  if(strcmp(argv[1],"test")==0)
    {
     if(argc!=5)
	{
	  printf("ERROR: need 3 arguments after test\n");
	  SKYCAM_exit();
	}	

     NBiter = atol(argv[2]);
      printf("NBiter = %ld\n",NBiter);

      for(i=0;i<NBiter;i++)
	{
	  printf("----------------- Iteration %ld/%ld\n", i, NBiter);
	  SKYCAM_command_tracksidN();
	  sleep((unsigned int) atof(argv[3]));
	  SKYCAM_command_MOUNTSTOP();
	  sleep((unsigned int) atof(argv[4]));
	}
      SKYCAM_exit();
    }


  if(strcmp(argv[1],"observingstatus")==0)
    {
      SKYCAM_command_observingstatus();
      SKYCAM_exit();
    }


  if(strcmp(argv[1],"takedark")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after test: exp time and iso mode\n");
	  SKYCAM_exit();
	}	
      if(MOUNTSTATUS != 3) // not parked
	SKYCAM_command_park();	       
      //      SKYCAM_command_observingstatus();
      n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"DARK");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  
      SKYCAM_command_ACQUIREimage(atof(argv[2]),atoi(argv[3]));
      SKYCAM_exit();
    }


  if(strcmp(argv[1],"taketestim")==0)
    {
      if(argc!=4)
	{
	  printf("ERROR: need 2 arguments after %s: exp time and iso mode\n",argv[1]);
	  SKYCAM_exit();
	}	
      n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"TEST");
      if(n >= SBUFFERSIZE) 
	{
	  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
	  exit(0);
	}	  

      SKYCAM_command_ACQUIREimage(atof(argv[2]),atoi(argv[3]));
      SKYCAM_exit();
    }



  // main loop  
  if(strcmp(argv[1],"mainloop")==0) 
    { 
      printf("Check observing status ...\n");
      fflush(stdout);
      SKYCAM_command_observingstatus();
      printf(" done\n"); 
      fflush(stdout); 
      
      SKYCAM_command_getACPowerStatus(10);
      if(ACPOWERSTATUS<ACPOWERLIMIT)
	{
	  while(ACPOWERSTATUS<ACPOWERLIMIT)
	    {
	      SKYCAM_SLEEP(10.0);
	      SKYCAM_command_getACPowerStatus(10);
	    }
	  SKYCAM_command_init(); // wake up
	}


      etime = 120.0;
      while(1==1) // start main loop
	{
	  SKYCAM_command_getACPowerStatus(2); // WAIT FOR AC POWER READ
	  if(ACPOWERSTATUS<ACPOWERLIMIT)
	    {
	      while(ACPOWERSTATUS<ACPOWERLIMIT)
		{
		  SKYCAM_SLEEP(10.0);
		  SKYCAM_command_getACPowerStatus(10);
		}
	      SKYCAM_command_init(); // wake up
	    }
	  
	  
	  // TAKE IMAGE OF MAUNA KEA
	  if((fp = fopen("shootSTATIC.txt","r"))==NULL)
	    {
	      printf("No static shoot request\n");
	      // printERROR(__FILE__,__func__,__LINE__,"fopen() error");
	      // exit(0);
	    }
	  if(fp!=NULL)
	    {	      
	      n = snprintf(line, SBUFFERSIZE, "STATIC SHOOT REQUEST FILE FOUND\n");
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  

	      SKYCAM_log();

	      m = fscanf(fp,"%lf %lf",&f1,&f2);
	      if(m==2)
		{
		  n = snprintf(line,SBUFFERSIZE,"STATIC SHOOT COORD = %lf %lf\n",f1,f2);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  SKYCAM_command_movpos(f1,f2);
		  n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"STATIC");
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_command_ACQUIREimage(120.0, CAMERA_MODE_ISO);
		}		  
	      else
		{
		  n = snprintf(line,SBUFFERSIZE,"STATIC SHOOT FILE READ ERROR, return = %d\n",m);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();		  
		}
		
	      fclose(fp);
	      system("rm shootSTATIC.txt");
	      CHANGEtarget = 1;
	    }

	  if((fp=fopen("/skycam/config/_etimemin.txt","r"))!=NULL)
	    {
	      fscanf(fp,"%f",&tmpf);
	      ETIME_MIN_SEC = tmpf;
	      if(fclose(fp)!=0)
		{
		  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		  exit(0);
		}
	      fp = NULL;
	    }

	  if((fp=fopen("/skycam/config/_etimemax.txt","r"))!=NULL)
	    {
	      fscanf(fp,"%f",&tmpf);
	      ETIME_MAX_SEC = tmpf;
	      if(fclose(fp)!=0)
		{
		  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
		  exit(0);
		}
	      fp = NULL;
	    }



	  if(OBSERVINGSTATUS == 0)
	    {
	      // DO NOT OBSERVE
	      // park mount if not already parked
	      if(MOUNTSTATUS != 3) // not parked		
		SKYCAM_command_park();	       
	      else
		{
		  for(i=0;i<30;i++)
		    {
		      printf(".");
		      fflush(stdout);
		      sleep(1);
		    }
		}

	      // load images from cameras if necessary
	      for(cam=0; cam<4; cam++)
		if((CAM_TO_LOAD[cam]>0)&&(CAMMODE[cam]!=0))
		  {
		    SKYCAM_command_cam_loadFILES(cam);
		    SKYCAM_command_archiveCAM(cam);
		    CAM_TO_LOAD[cam] = 0;
		    SKYCAM_write_STATUS();		  
		  }

	      mDECold = -100.0;
	      mRAold = -100.0;
	      SKYCAM_command_observingstatus();	      
	    }
	  else if(OBSERVINGSTATUS == 1) // TAKE DARK
	    {
	      n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"DARK");
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      // conditions are not good for observing
	      // park mount if not already parked
	      if(MOUNTSTATUS != 3) // not parked
		SKYCAM_command_park();	       
	      if(NBdark < NBdarkMAX)
		{
		  // TAKE DARK HERE
		  etime = 120.0;
		  SKYCAM_command_ACQUIREimage(etime,CAMERA_MODE_ISO);
		}
	      else
		{
		  sleep(30);
		  SKYCAM_command_observingstatus();
		}
	      mDECold = -100.0;
	      mRAold = -100.0;
	      NBdark ++;
	    }
	  else if(OBSERVINGSTATUS == 2) // TAKE FLATS
	    {
	      n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE,"FLAT");
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
// TODO: MOVE MOUNT INTO FLAT POINTING
	      // TODO: TAKE FLATS	      
	      if(MOUNTSTATUS!=6)
		SKYCAM_command_mvposFLATFIELDpos();
	      if(NBflatfield < NBflatfieldMAX)
		{
		  SKYCAM_command_ACQUIREimage(etimeflat,isoflat);
		  if(IMAGE_perc90>0)
		    {
		      if(IMAGE_perc90>10000.0)
			etimeflat *= 0.4;
		      if(IMAGE_perc90<8000.0)
			etimeflat *= 1.2;
		    }
		  if(etimeflat < 1.0)
		    {
		      etimeflat = 1.0;
		      isoflat++;
		    }
		  if(etimeflat > 60.0)
		    {
		      etimeflat = 60.0;
		      isoflat--;
		    }
		  if(isoflat<1)
		    isoflat = 1;
		  if(isoflat>5)
		    isoflat = 5;
		}
	      else
		{
		  sleep(30);
		  SKYCAM_command_observingstatus();
		}
	      mDECold = -100.0;
	      mRAold = -100.0;
	    }
	  else if(OBSERVINGSTATUS == 3) // OK TO OBSERVE
	    {	      
	      //	      MOUNTSTATUS = 4;
	      //	      SKYCAM_write_STATUS();
	      printf("image type =  OBJECT\n");
	      fflush(stdout);
	      n = snprintf(IMGHEADER_IMTYPE,SBUFFERSIZE, "OBJECT");	     
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      printf("OK to observe\n");
	      fflush(stdout);
	      compute_LSTnow();
	      printf("Computing Moon position\n");
	      fflush(stdout);
	      get_Moon_pos();
	      	      
	      printf("Searching for next pointing ...");
	      fflush(stdout);
	      coordOK = 0;
	      cnt = 0;
	      
	      // first, see if pointing request file exists
	      targetcnt = 0;
	      while((coordOK==0)&&(targetcnt<targetcntmax))
		{
		  n = snprintf(targetfilename,SBUFFERSIZE, "targetRADEC.%04d.txt", targetcnt);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__, "Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  

		  fp = fopen(targetfilename, "r");
		  if(fp != NULL)
		    {
		      printf("Found file %s\n",targetfilename);
		      fscanf(fp,"%lf %lf\n",&RA,&DEC);
		      fgets(IMGHEADER_TARGETDESCRIPTION,999,fp);
		      if(fclose(fp)!=0)
			{
			  printERROR( __FILE__, __func__, __LINE__,"fclose() error");
			  exit(0);
			}
		      RA = RA/180.0*M_PI;
		      DEC = DEC/180.0*M_PI;
		      printf("READ FROM FILE: RA = %f DEC = %f\n",RA,DEC);
		      RAnew = RA; // rad
		      DECnew = DEC; // rad
		      compute_coordinates_from_RA_DEC(RA,DEC);
		      compute_mount_radec_from_radec();
		      coordOK = SKYCAM_testcoord();
		      if(test_posmount(COORD_mRA,COORD_mDEC,0)==0)
			coordOK = 0;
		      if(coordOK == 0)
			printf("COORDINATE NOT OK\n");
		      else
			{
			  printf("COORDINATE OK\n");
			  RandomPointingCnt = -1;
			}
		      mRAnew = COORD_mRA;
		      mDECnew = COORD_mDEC;
		    }
		  //		  else
		  //  printf("No file %s\n",targetfilename);
		  targetcnt ++;
		}

	      if(coordOK == 0)
		{
		  printf("PICKING RANDOM TARGET\n");
		  n = snprintf(IMGHEADER_TARGETDESCRIPTION,SBUFFERSIZE,"random");
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  		  
		}	      


	      // SHOULD WE STAY ON LAST RANDOM POINTING ?
	      if((coordOK == 0)&&(RandomPointingCnt<RandomPointingCnt_limit)&&(RandomPointingCnt>-1)&&(CHANGEtarget == 0))
		{
		  //		  AZ = AZrandom;
		  // ALT = ALTrandom;

		  RA = RArandom; // rad
		  DEC = DECrandom; // rad
		  
		  compute_coordinates_from_RA_DEC(RA,DEC);
		  compute_mount_radec_from_radec();

		  //coordOK = SKYCAM_testcoord();		  
		  //		  compute_coordinates_from_ALT_AZ(ALT,AZ);
		  //compute_mount_radec_from_radec();

		  RAnew = COORD_RA;  // rad
		  DECnew = COORD_DEC; // rad
		  printf("Checking if last random pointing still OK (ALT/AZ= %f %f) (RA/DEC = %f %f)\n",COORD_ALT,COORD_AZ,COORD_RA,COORD_DEC);
		  fflush(stdout);
		  coordOK = SKYCAM_testcoord();
		  if(test_posmount(COORD_mRA,COORD_mDEC,0)==0)
		    coordOK = 0;
		  mRAnew = COORD_mRA;
		  mDECnew = COORD_mDEC;
		  printf("coordOK = %d\n",coordOK);
		  if(coordOK != 0)
		    RandomPointingCnt++;		  
		}

	      // SELECT RANDOM POINTING
	      while(coordOK == 0)
		{		  
		  u = ran1();
		  v = ran1()*(cos(observation_minelevation_random/180.0*M_PI+M_PI/2)+1.0)/2.0;
		  AZ = 2.0*M_PI*u;
		  ALT = acos(2.0*v-1.0)-M_PI/2.0;
		  //  AZrandom = AZ;
		  // ALTrandom = ALT;
		  // printf("%f %f %f\n",v,(cos(observation_minelevation/180.0*M_PI+M_PI/2)+1.0)/2.0,ALT);
		  compute_coordinates_from_ALT_AZ(ALT,AZ);
		  
		  compute_mount_radec_from_radec();
		  RAnew = COORD_RA;  // rad
		  DECnew = COORD_DEC; // rad
		  printf("Picked %f %f\n",COORD_ALT,COORD_AZ);
		  fflush(stdout);
		  coordOK = SKYCAM_testcoord();
		  if(test_posmount(COORD_mRA,COORD_mDEC,0)==0)
		    coordOK = 0;
		  mRAnew = COORD_mRA;
		  mDECnew = COORD_mDEC;
		  printf("coordOK = %d\n",coordOK);

		  RArandom = COORD_RA;
		  DECrandom = COORD_DEC;

		  RandomPointingCnt = 0;
		}
	      CHANGEtarget = 0;
	      
	      // is it the same pointing as last ?
	      if(((sqrt((RAold-RAnew)*(RAold-RAnew)+(DECold-DECnew)*(DECold-DECnew)))<60.0*POINTING_ACCURACY)&&(sqrt((mRAold-mRAnew)*(mRAold-mRAnew)+(mDECold-mDECnew)*(mDECold-mDECnew))<0.3)) // second test needed for RA flip
		{		  
		  n = snprintf(line,SBUFFERSIZE,"------ SAME POINTING - NO NEED TO MOVE\n");
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  pos_RA = COORD_RA;
		  pos_DEC = COORD_DEC;
		  pos_ALT = COORD_ALT;
		  pos_AZ = COORD_AZ;		  
		  
		  if(GTindex==0)
		    SKYCAM_command_tracksidN();
		  //		  GETPOSMOUNT = 0;
		 


		  GTindex ++;
		 
		  // solve GT table to compute new tracking rate
		  


		  // apply mount offset for next exposure
		  GTlim = 100.0; // [arcsec]
		  GTrlim = 2.0; // [arcsec/sec]
		  if( ( (fabs(GT_dRA[GTindex-1])>0.01) || (fabs(GT_dDEC[GTindex-1])>0.01) ) && (GTindex > 1) )
		    {
		      GT_RAoffset[GTindex] = -1.0*GT_dRA[GTindex-1]; // sky arcsec
		      GT_DECoffset[GTindex] = -1.0*GT_dDEC[GTindex-1]; // sky arcsec
		      
		      if(GT_RAoffset[GTindex]>GTlim)
			GT_RAoffset[GTindex] = GTlim;
		      if(GT_RAoffset[GTindex]<-GTlim)
			GT_RAoffset[GTindex] = -GTlim;
		      
		      if(GT_DECoffset[GTindex]>GTlim)
			GT_DECoffset[GTindex] = GTlim;
		      if(GT_DECoffset[GTindex]<-GTlim)
			GT_DECoffset[GTindex] = -GTlim;
		      
		      if(GTindex>1)
			{
			  // measured rates
			  if(cos(COORD_DEC)>0.2)
			    GT_RArate_meas[GTindex] = GT_RArate[GTindex-1] - ((GT_dRA[GTindex-1]-GT_RAoffset[GTindex-1])-GT_dRA[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2])/cos(COORD_DEC); // mechanical arcsec/sec, sky directions
			  else
			    GT_RArate_meas[GTindex] = GT_RArate[GTindex-1] - ((GT_dRA[GTindex-1]-GT_RAoffset[GTindex-1])-GT_dRA[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2])/0.2; // mechanical arcsec/sec, sky directions
			  
			  GT_DECrate_meas[GTindex] = GT_DECrate[GTindex-1] - ((GT_dDEC[GTindex-1]-GT_DECoffset[GTindex-1])-GT_dDEC[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2]); // mechanical arcsec/sec, sky directions
			  

			  // applied rates
			  if(cos(COORD_DEC)>0.2)
			    GT_RArate[GTindex] = GT_RArate[GTindex-1] - 0.2*((GT_dRA[GTindex-1]-GT_RAoffset[GTindex-1])-GT_dRA[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2])/cos(COORD_DEC); // mechanical arcsec/sec, sky directions
			  else
			    GT_RArate[GTindex] = GT_RArate[GTindex-1] - 0.2*((GT_dRA[GTindex-1]-GT_RAoffset[GTindex-1])-GT_dRA[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2])/0.2; // mechanical arcsec/sec, sky directions
			  
			  GT_DECrate[GTindex] = GT_DECrate[GTindex-1] - 0.2*((GT_dDEC[GTindex-1]-GT_DECoffset[GTindex-1])-GT_dDEC[GTindex-2])/(GT_tstart[GTindex-1]-GT_tstart[GTindex-2]); // mechanical arcsec/sec, sky directions
			}
		      else
			{
			  GT_RArate_meas[GTindex] = GT_RArate[GTindex-1]; // mechanical arcsec/sec, sky directions
			  GT_DECrate_meas[GTindex] = GT_DECrate[GTindex-1]; // mechanical arcsec/sec, sky directions

			  GT_RArate[GTindex] = GT_RArate[GTindex-1]; // mechanical arcsec/sec, sky directions
			  GT_DECrate[GTindex] = GT_DECrate[GTindex-1]; // mechanical arcsec/sec, sky directions
			}


		      if(GT_RArate[GTindex] > 15.0+GTrlim)
			GT_RArate[GTindex] = 15.0+GTrlim;
		      if(GT_RArate[GTindex] < 15.0-GTrlim)
			GT_RArate[GTindex] = 15.0-GTrlim;

		      if(GT_DECrate[GTindex] > GTrlim)
			GT_DECrate[GTindex] = GTrlim;
		      if(GT_DECrate[GTindex] < -GTrlim)
			GT_DECrate[GTindex] = -GTrlim;
		      

		      Apply_TrackingOffset(GT_RAoffset[GTindex], GT_DECoffset[GTindex], GT_RArate[GTindex], GT_DECrate[GTindex], 2.0);
		      
		      // modify tracking rate
		      
		    }
		  else
		    {
		      
		      GT_RAoffset[GTindex] = 0.0;
		      GT_DECoffset[GTindex] = 0.0;		      
		    }

		  /*		  if(GTindex>3)
		    {
		      GT_RAoffset[GTindex] = 20.0;
		      GT_DECoffset[GTindex] = 0.0;
		      Apply_TrackingOffset(GT_RAoffset[GTindex], GT_DECoffset[GTindex], 5.0);		   
		      }*/

		}
	      else   // if new pointing
		{
		  if(GTindex!=0)
		    {
		      GTtablenb++;
		      GTindex = 0;
		      for(gti=0;gti<GTsize;gti++)
			{
			  GT_imnb[gti] = 0; //updated by acquire image
			  GT_posmountRA[gti] = 0.0;
			  GT_posmountDEC[gti] = 0.0;
			  GT_tstart[gti] = 0.0; 
			  GT_etime[gti] = 0.0; // [sec]
			  GT_dRA[gti] = 0.0;
			  GT_dDEC[gti] = 0.0;
			  GT_RArate[gti] = 0.0;
			  GT_DECrate[gti] = 0.0;
			  GT_RArate_meas[gti] = 0.0;
			  GT_DECrate_meas[gti] = 0.0;
			  GT_RAoffset[gti] = 0.0;
			  GT_DECoffset[gti] = 0.0;		      
			}
		    }
		 

		  GETPOSMOUNT = 1;		  
		  n = snprintf(line,SBUFFERSIZE,"MOVING TO: [%f %f] RA = %f deg, DEC = %f deg, ALT = %f deg, AZ = %f deg\n",COORD_mRA,COORD_mDEC,COORD_RA/M_PI*180.0,COORD_DEC/M_PI*180.0,COORD_ALT/M_PI*180.0,COORD_AZ/M_PI*180.0);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  n = snprintf(line,SBUFFERSIZE,"DISTANCE TO MOON = %f deg\n",DISTMOON/M_PI*180.0);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  n = snprintf(line,SBUFFERSIZE,"[%ld] Moving by %f deg\n",MOUNT_NBMOVE,DISTMOVE/M_PI*180.0);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
	      
		  // move & start tracking
		  pos_RA = COORD_RA;
		  pos_DEC = COORD_DEC;
		  pos_ALT = COORD_ALT;
		  pos_AZ = COORD_AZ;
		  initpos = 1;
	      	      
		  RAold = RAnew;
		  DECold = DECnew;
		  mRAold = mRAnew;
		  mDECold = mDECnew;
		  
		  SKYCAM_command_MOUNTSTOP();
		  sleep(1);
		  
		  if(coordOK == 0)
		    {
		      printf("Unable to find pointing\n");
		      SKYCAM_exit();
		    }
		  
		  if(!(mountfd<0))
		    {
		      printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
		      SKYCAM_exit();
		    }
		  mountfd = open_ttyUSBport(mountUSBportNB);
		  get_posmountradec();
		  if(close(mountfd)!=0)
		    {
		      printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
		      SKYCAM_exit();
		    }
		  mountfd = -1;
		  fdcnt--;
		  
		  n = snprintf(line,SBUFFERSIZE,"POSITION BEFORE MOVE : %f %f\n",pos_mountra,pos_mountdec);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  
		  if(SKYCAM_command_movpos(COORD_mRA,COORD_mDEC)>POINTING_ACCURACY/2.0/M_PI)
		    SKYCAM_command_movpos(COORD_mRA,COORD_mDEC);
		  
		  
		  if(!(mountfd<0))
		    {
		      printf("FATAL ERROR: attempting to open file which is already open: file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
		      SKYCAM_exit();
		    }
		  mountfd = open_ttyUSBport(mountUSBportNB);
		  get_posmountradec();
		  if(close(mountfd)!=0)
		    {
		      printf("FATAL ERROR: could not close file descriptor %d [%s %d]\n",mountfd,__func__,__LINE__);
		      SKYCAM_exit();
		    }
		  mountfd = -1;
		  fdcnt --;

		  n = snprintf(line,SBUFFERSIZE,"POSITION AFTER MOVE : %f %f\n",pos_mountra,pos_mountdec);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
		  
	      
		  //		  Ttrack = 
		  SKYCAM_command_tracksidN();
		  sleep(TIME_WAIT_TRACKING_ENGAGE);
		}  // end if new pointing
	    
	      
	      // take picture here and check conditions 
	      compute_UTCnow();
	      n = snprintf(line,SBUFFERSIZE,"TIME_UTC_TRACK_STOP : %.5f\n",TIME_UTC_TRACK_STOP);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();
	      n = snprintf(line,SBUFFERSIZE,"TTIME_UTC + (etime+ 10.0)/3600.0 : %.5f\n",TTIME_UTC+(etime+10.0)/3600.0);
	      if(n >= SBUFFERSIZE) 
		{
		  printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		  exit(0);
		}	  
	      SKYCAM_log();

	      if(TTIME_UTC + (etime+10.0)/3600.0 < TIME_UTC_TRACK_STOP)
		{
		  if(etime < ETIME_MIN_SEC)
		    etime = ETIME_MIN_SEC;
		  if(etime > ETIME_MAX_SEC)
		    etime = ETIME_MAX_SEC;
		  printf("ACQUIRE IMAGE(S), etime = %f\n",etime);



		  n = snprintf(line,SBUFFERSIZE,"=============== ACQUIRE IMAGE : START ==================\n");
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();
	
	
		  SKYCAM_command_ACQUIREimage(etime, CAMERA_MODE_ISO);

	
		  n = snprintf(line,SBUFFERSIZE,"=============== ACQUIRE IMAGE : STOP ===================\n");
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }	  
		  SKYCAM_log();


		  GT_etime[GTindex] = etime;
		  GT_RArate[GTindex] = TRACKrate_RA/NBstepMountRot*360.0*3600.0;
		  GT_DECrate[GTindex] = TRACKrate_DEC/NBstepMountRot*360.0*3600.0*RAflip;


		  // write GT table to disk
		  n = snprintf(GTtablename, SBUFFERSIZE, "%s/%04d-%02d-%02d/GTtable.log", DATA_DIRECTORY, TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY);
		  if(n >= SBUFFERSIZE) 
		    {
		      printERROR(__FILE__,__func__,__LINE__,"Attempted to write string buffer with too many characters");
		      exit(0);
		    }

		  //		  sprintf(GTtablename, "GTtable_%04ld.txt", GTtablenb);
		  fpGT = fopen(GTtablename, "a");
		  //		  for(gti=0;gti<GTindex+1;gti++)
		  gti = GTindex;
		  fprintf(fpGT,"%4ld %5ld %5ld %2d %6.4lf %6.4lf %20.3lf %6.1lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf\n", GTtablenb, gti, GT_imnb[gti], RAflip, GT_posmountRA[gti], GT_posmountDEC[gti], GT_tstart[gti], GT_etime[gti], GT_dRA[gti], GT_dDEC[gti], GT_RArate[gti], GT_DECrate[gti], GT_RAoffset[gti], GT_DECoffset[gti], GT_RArate_meas[gti], GT_DECrate_meas[gti]);
		  fclose(fpGT);

		  fpGT = fopen("GTable.txt", "a");
		  //		  for(gti=0;gti<GTindex+1;gti++)
		  gti = GTindex;
		  fprintf(fpGT,"%04d-%02d-%02d %4ld %5ld %5ld %2d %6.4lf %6.4lf %20.3lf %6.1lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf %7.3lf\n", TIME_UTC_YR, TIME_UTC_MON, TIME_UTC_DAY, GTtablenb, gti, GT_imnb[gti], RAflip, GT_posmountRA[gti], GT_posmountDEC[gti], GT_tstart[gti], GT_etime[gti], GT_dRA[gti], GT_dDEC[gti], GT_RArate[gti], GT_DECrate[gti], GT_RAoffset[gti], GT_DECoffset[gti], GT_RArate_meas[gti], GT_DECrate_meas[gti]);
		  fclose(fpGT);
		  

		  if(IMAGE_percB50>0.0)
		    {
		      printf("Old etime = %f sec   IMAGE_percB50-CAMERABIAS = %f   ->",etime,IMAGE_percB50-CAMERABIAS);
		      if(IMAGE_percB50-CAMERABIAS<1000.0)
			etime *= 1.2;
		      if(IMAGE_percB50-CAMERABIAS<500.0)
			etime *= 1.2;
		      if(IMAGE_percB50-CAMERABIAS<200.0)
			etime *= 1.2;

		      if(IMAGE_percB50-CAMERABIAS>2000.0)
			etime *= 0.8;
		      if(IMAGE_percB50-CAMERABIAS>4000.0)
			etime *= 0.8;
		      if(IMAGE_percB50-CAMERABIAS>8000.0)
			etime *= 0.8;
		    }
		  if(etime<ETIME_MIN_SEC)
		    etime = ETIME_MIN_SEC;
		  if(etime>ETIME_MAX_SEC)
		    etime = ETIME_MAX_SEC;
		  
		  printf("New etime = %f sec [%f - %f]\n", etime, ETIME_MIN_SEC, ETIME_MAX_SEC);
		   
		}
	      else
		CHANGEtarget = 1;

		
	      if(OBSERVINGSTATUS != 3)
		 SKYCAM_command_MOUNTSTOP();
	    }
	}

      SKYCAM_command_MOUNTSTOP();
 
      SKYCAM_exit();
    }



  printf("ERROR: command not recognized. Try: %s help\n",argv[0]);
  system("rm /skycam/_SKYCAM_RUNNING");

  return(0);
}






// NORMAL CLEAN SKYCAM EXIT

int SKYCAM_exit() 
{  
  int cam;
  printf("---- SKYCAM EXIT -----\n");
  
  for(cam=0;cam<4;cam++)
    if(CAMTTLON[cam]==1)
      dio_setdigital_out(DIOCHAN_CAM_TTL[cam], 1);


  // load images from cameras if necessary
  for(cam=0; cam<4; cam++)
    if((CAM_TO_LOAD[cam]>0)&&(CAMMODE[cam]!=0))
      {
	SKYCAM_command_cam_loadFILES(cam);
	SKYCAM_command_archiveCAM(cam);
	CAM_TO_LOAD[cam] = 0;
	SKYCAM_write_STATUS();		  
      }
  
  
  SKYCAM_write_STATUS(); 


  printf("fdcnt   = %d\n",fdcnt);
  printf("mountfd = %d\n",mountfd);
  printf("diofd   = %d\n",diofd);
  system("rm /skycam/_SKYCAM_RUNNING");

  exit(0);

  return(0);
}
