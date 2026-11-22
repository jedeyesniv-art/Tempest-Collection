#ifndef timerH
#define timerH

class MPE;

struct _currenttime {
    int32 sec;
    int32 min;
    int32 hour;
    int32 wday;
    int32 mday;
    int32 month;
    int32 year;
    int32 isdst;     /* flag: for definitions, see below */
    int32 timezone;  /* minutes west of Greenwich */
    int32 reserved[3]; /* reserved for future expansion */
};

void InitializeTimingMethod();
void DeInitTimingMethod();

uint64 useconds_since_start();       // the emulated machine's clock (g_emu_time: emulated, else the PC's)
uint64 real_useconds_since_start();  // always the PC's clock
extern bool g_emu_time;
extern uint64 g_emu_cycles;
extern double g_emu_mhz;

void TimeElapsed(MPE &mpe);
void TimeOfDay(MPE &mpe);
void TimerInit(const uint32 whichTimer, const uint32 rate);
void TimerInit(MPE &mpe);

#endif
