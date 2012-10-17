
#ifndef fixed
#define fixed short
#endif

extern int fix_fft (fixed *, fixed *, int, int);
extern int iscale (int, int, int);
extern void window (fixed *, int);
extern void fix_loud (fixed loud[], fixed fr[], fixed fi[], int n, int scale_shift);

