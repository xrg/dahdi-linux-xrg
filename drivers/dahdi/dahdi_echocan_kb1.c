/*
 * ECHO_CAN_KB1
 *
 * by Kris Boutilier
 *
 * Based upon mec2.h
 * 
 * Copyright (C) 2002, Digium, Inc.
 *
 * Additional background on the techniques used in this code can be found in:
 *
 *  Messerschmitt, David; Hedberg, David; Cole, Christopher; Haoui, Amine; 
 *  Winship, Peter; "Digital Voice Echo Canceller with a TMS32020," 
 *  in Digital Signal Processing Applications with the TMS320 Family, 
 *  pp. 415-437, Texas Instruments, Inc., 1986. 
 *
 * A pdf of which is available by searching on the document title at http://www.ti.com/
 *
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>

#include <dahdi/kernel.h>

static int debug;
static int aggressive;

#define module_printk(level, fmt, args...) printk(level "%s: " fmt, THIS_MODULE->name, ## args)
#define debug_printk(level, fmt, args...) if (debug >= level) printk(KERN_DEBUG "%s (%s): " fmt, THIS_MODULE->name, __FUNCTION__, ## args)

/* Uncomment to provide summary statistics for overall echo can performance every 4000 samples */ 
/* #define MEC2_STATS 4000 */

/* Uncomment to generate per-sample statistics - this will severely degrade system performance and audio quality */
/* #define MEC2_STATS_DETAILED */

/* Get optimized routines for math */
#include "arith.h"

/*
   Important constants for tuning kb1 echo can
 */

/* Convergence (aka. adaptation) speed -- higher means slower */
#define DEFAULT_BETA1_I 2048

/* Constants for various power computations */
#define DEFAULT_SIGMA_LY_I 7
#define DEFAULT_SIGMA_LU_I 7
#define DEFAULT_ALPHA_ST_I 5 		/* near-end speech detection sensitivity factor */
#define DEFAULT_ALPHA_YT_I 5

#define DEFAULT_CUTOFF_I 128

/* Define the near-end speech hangover counter: if near-end speech 
 *  is declared, hcntr is set equal to hangt (see pg. 432)
 */
#define DEFAULT_HANGT 600  		/* in samples, so 600 samples = 75ms */

/* define the residual error suppression threshold */
#define DEFAULT_SUPPR_I 16		/* 16 = -24db */

/* This is the minimum reference signal power estimate level 
 *  that will result in filter adaptation.
 * If this is too low then background noise will cause the filter 
 *  coefficients to constantly be updated.
 */
#define MIN_UPDATE_THRESH_I 4096

/* The number of samples used to update coefficients using the
 *  the block update method (M). It should be related back to the 
 *  length of the echo can.
 * ie. it only updates coefficients when (sample number MOD default_m) = 0
 *
 *  Getting this wrong may cause an oops. Consider yourself warned!
 */
#define DEFAULT_M 16		  	/* every 16th sample */

/* If AGGRESSIVE supression is enabled, then we start cancelling residual 
 * echos again even while there is potentially the very end of a near-side 
 *  signal present.
 * This defines how many samples of DEFAULT_HANGT can remain before we
 *  kick back in
 */
#define AGGRESSIVE_HCNTR 160		/* in samples, so 160 samples = 20ms */


/***************************************************************/
/* The following knobs are not implemented in the current code */

/* we need a dynamic level of suppression varying with the ratio of the 
   power of the echo to the power of the reference signal this is 
   done so that we have a  smoother background. 		
   we have a higher suppression when the power ratio is closer to
   suppr_ceil and reduces logarithmically as we approach suppr_floor.
 */
#define SUPPR_FLOOR -64
#define SUPPR_CEIL -24

/* in a second departure, we calculate the residual error suppression
 * as a percentage of the reference signal energy level. The threshold
 * is defined in terms of dB below the reference signal.
 */
#define RES_SUPR_FACTOR -20

#ifndef NULL
#define NULL 0
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif

/* Generic circular buffer definition */
typedef struct {
	/* Pointer to the relative 'start' of the buffer */
	int idx_d;
	/* The absolute size of the buffer */
	int size_d;			 
	/* The actual sample -  twice as large as we need, however we do store values at idx_d and idx_d+size_d */
	short *buf_d;			
} echo_can_cb_s;

/* Echo canceller definition */
struct echo_can_state {
	/* an arbitrary ID for this echo can - this really should be settable from the calling channel... */
	int id;

	/* absolute time - aka. sample number index - essentially the number of samples since this can was init'ed */
	int i_d;
  
	/* Pre-computed constants */
	/* ---------------------- */
	/* Number of filter coefficents */
	int N_d;
	/* Rate of adaptation of filter */
	int beta2_i;

	/* Accumulators for power computations */
	/* ----------------------------------- */
	/* reference signal power estimate - aka. Average absolute value of y(k) */
	int Ly_i;			
	/* ... */
	int Lu_i;

	/* Accumulators for signal detectors */
	/* --------------------------------- */
	/* Power estimate of the recent past of the near-end hybrid signal - aka. Short-time average of: 2 x |s(i)| */
	int s_tilde_i;		
	/* Power estimate of the recent past of the far-end receive signal - aka. Short-time average of:     |y(i)| */
	int y_tilde_i;

	/* Near end speech detection counter - stores Hangover counter time remaining, in samples */
	int HCNTR_d;			
  
	/* Circular buffers and coefficients */
	/* --------------------------------- */
	/* ... */
	int *a_i;
	/* ... */
	short *a_s;
	/* Reference samples of far-end receive signal */
	echo_can_cb_s y_s;
	/* Reference samples of near-end signal */
	echo_can_cb_s s_s;
	/* Reference samples of near-end signal minus echo estimate */
	echo_can_cb_s u_s;
	/* Reference samples of far-end receive signal used to calculate short-time average */
	echo_can_cb_s y_tilde_s;

	/* Peak far-end receive signal */
	/* --------------------------- */
	/* Highest y_tilde value in the sample buffer */
	short max_y_tilde;
	/* Index of the sample containing the max_y_tilde value */
	int max_y_tilde_pos;

#ifdef MEC2_STATS
	/* Storage for performance statistics */
	int cntr_nearend_speech_frames;
	int cntr_residualcorrected_frames;
	int cntr_residualcorrected_framesskipped;
	int cntr_coeff_updates;
	int cntr_coeff_missedupdates;
 
	int avg_Lu_i_toolow; 
	int avg_Lu_i_ok;
#endif 
	unsigned int aggressive:1;
};

static inline void init_cb_s(echo_can_cb_s *cb, int len, void *where)
{
	cb->buf_d = (short *)where;
	cb->idx_d = 0;
	cb->size_d = len;
}

static inline void add_cc_s(echo_can_cb_s *cb, short newval)
{
	/* Can't use modulus because N+M isn't a power of two (generally) */
	cb->idx_d--;
	if (cb->idx_d < (int)0) 
		/* Whoops - the pointer to the 'start' wrapped around so reset it to the top of the buffer */
	 	cb->idx_d += cb->size_d;
  	
	/* Load two copies into memory */
	cb->buf_d[cb->idx_d] = newval;
	cb->buf_d[cb->idx_d + cb->size_d] = newval;
}

static inline short get_cc_s(echo_can_cb_s *cb, int pos)
{
	/* Load two copies into memory */
	return cb->buf_d[cb->idx_d + pos];
}

static inline void init_cc(struct echo_can_state *ec, int N, int maxy, int maxu) 
{

	void *ptr = ec;
	unsigned long tmp;
	/* Double-word align past end of state */
	ptr += sizeof(struct echo_can_state);
	tmp = (unsigned long)ptr;
	tmp += 3;
	tmp &= ~3L;
	ptr = (void *)tmp;

	/* Reset parameters */
	ec->N_d = N;
	ec->beta2_i = DEFAULT_BETA1_I;
  
	/* Allocate coefficient memory */
	ec->a_i = ptr;
	ptr += (sizeof(int) * ec->N_d);
	ec->a_s = ptr;
	ptr += (sizeof(short) * ec->N_d);

	/* Reset Y circular buffer (short version) */
	init_cb_s(&ec->y_s, maxy, ptr);
	ptr += (sizeof(short) * (maxy) * 2);
  
	/* Reset Sigma circular buffer (short version for FIR filter) */
	init_cb_s(&ec->s_s, (1 << DEFAULT_ALPHA_ST_I), ptr);
	ptr += (sizeof(short) * (1 << DEFAULT_ALPHA_ST_I) * 2);

	init_cb_s(&ec->u_s, maxu, ptr);
	ptr += (sizeof(short) * maxu * 2);

	/* Allocate a buffer for the reference signal power computation */
	init_cb_s(&ec->y_tilde_s, ec->N_d, ptr);

	/* Reset the absolute time index */
	ec->i_d = (int)0;
  
	/* Reset the power computations (for y and u) */
	ec->Ly_i = DEFAULT_CUTOFF_I;
	ec->Lu_i = DEFAULT_CUTOFF_I;

#ifdef MEC2_STATS
	/* set the identity */
	ec->id = (int)&ptr;
  
	/* Reset performance stats */
	ec->cntr_nearend_speech_frames = (int)0;
	ec->cntr_residualcorrected_frames = (int)0;
	ec->cntr_residualcorrected_framesskipped = (int)0;
	ec->cntr_coeff_updates = (int)0;
	ec->cntr_coeff_missedupdates = (int)0;

	ec->avg_Lu_i_toolow = (int)0;
	ec->avg_Lu_i_ok = (int)0;
#endif

	/* Reset the near-end speech detector */
	ec->s_tilde_i = (int)0;
	ec->y_tilde_i = (int)0;
	ec->HCNTR_d = (int)0;

}

static void echo_can_free(struct echo_can_state *ec)
{
	kfree(ec);
}

static inline short sample_update(struct echo_can_state *ec, short iref, short isig) 
{
	/* Declare local variables that are used more than once */
	/* ... */
	int k;
	/* ... */
	int rs;
	/* ... */
	short u;
	/* ... */
	int Py_i;
	/* ... */
	int two_beta_i;
	
	/* flow A on pg. 428 */
	/* eq. (16): high-pass filter the input to generate the next value;
	 *           push the current value into the circular buffer
	 *
	 * sdc_im1_d = sdc_d;
	 *     sdc_d = sig;
	 *     s_i_d = sdc_d;
	 *       s_d = s_i_d;
	 *     s_i_d = (float)(1.0 - gamma_d) * s_i_d
	 *	+ (float)(0.5 * (1.0 - gamma_d)) * (sdc_d - sdc_im1_d); 
	 */

	/* Update the Far-end receive signal circular buffers and accumulators */
	/* ------------------------------------------------------------------- */
	/* Delete the oldest sample from the power estimate accumulator */
	ec->y_tilde_i -= abs(get_cc_s(&ec->y_s, (1 << DEFAULT_ALPHA_YT_I) - 1 )) >> DEFAULT_ALPHA_YT_I;
	/* Add the new sample to the power estimate accumulator */
	ec->y_tilde_i += abs(iref) >> DEFAULT_ALPHA_ST_I;
	/* Push a copy of the new sample into its circular buffer */
	add_cc_s(&ec->y_s, iref);
 

	/* eq. (2): compute r in fixed-point */
	rs = CONVOLVE2(ec->a_s, 
  			ec->y_s.buf_d + ec->y_s.idx_d, 
  			ec->N_d);
	rs >>= 15;

	/* eq. (3): compute the output value (see figure 3) and the error
	 * note: the error is the same as the output signal when near-end
	 * speech is not present
	 */
	u = isig - rs;
  
	/* Push a copy of the output value sample into its circular buffer */
	add_cc_s(&ec->u_s, u);


	/* Update the Near-end hybrid signal circular buffers and accumulators */
	/* ------------------------------------------------------------------- */
	/* Delete the oldest sample from the power estimate accumulator */
	ec->s_tilde_i -= abs(get_cc_s(&ec->s_s, (1 << DEFAULT_ALPHA_ST_I) - 1 ));
	/* Add the new sample to the power estimate accumulator */
	ec->s_tilde_i += abs(isig);
	/* Push a copy of the new sample into it's circular buffer */
	add_cc_s(&ec->s_s, isig);


	/* Push a copy of the current short-time average of the far-end receive signal into it's circular buffer */
	add_cc_s(&ec->y_tilde_s, ec->y_tilde_i);		

	/* flow B on pg. 428 */
  
	/* If the hangover timer isn't running then compute the new convergence factor, otherwise set Py_i to 32768 */
	if (!ec->HCNTR_d) {
		Py_i = (ec->Ly_i >> DEFAULT_SIGMA_LY_I) * (ec->Ly_i >> DEFAULT_SIGMA_LY_I);
		Py_i >>= 15;
	} else {
	  	Py_i = (1 << 15);
	}
  
#if 0
	/* Vary rate of adaptation depending on position in the file
	 *  Do not do this for the first (DEFAULT_UPDATE_TIME) secs after speech
	 *  has begun of the file to allow the echo cancellor to estimate the
	 *  channel accurately
	 * Still needs conversion!
	 */

	if (ec->start_speech_d != 0 ){
		if ( ec->i_d > (DEFAULT_T0 + ec->start_speech_d)*(SAMPLE_FREQ) ){
			ec->beta2_d = max_cc_float(MIN_BETA, DEFAULT_BETA1 * exp((-1/DEFAULT_TAU)*((ec->i_d/(float)SAMPLE_FREQ) - DEFAULT_T0 - ec->start_speech_d)));
		}
	} else {
		ec->beta2_d = DEFAULT_BETA1;
	}
#endif
  
	/* Fixed point, inverted */
	ec->beta2_i = DEFAULT_BETA1_I;	
  
	/* Fixed point version, inverted */
	two_beta_i = (ec->beta2_i * Py_i) >> 15;	
	if (!two_beta_i)
		two_beta_i++;

	/* Update the Suppressed signal power estimate accumulator */
        /* ------------------------------------------------------- */
        /* Delete the oldest sample from the power estimate accumulator */
	ec->Lu_i -= abs(get_cc_s(&ec->u_s, (1 << DEFAULT_SIGMA_LU_I) - 1 )) ;
        /* Add the new sample to the power estimate accumulator */
	ec->Lu_i += abs(u);

	/* Update the Far-end reference signal power estimate accumulator */
        /* -------------------------------------------------------------- */
	/* eq. (10): update power estimate of the reference */
        /* Delete the oldest sample from the power estimate accumulator */
	ec->Ly_i -= abs(get_cc_s(&ec->y_s, (1 << DEFAULT_SIGMA_LY_I) - 1)) ;
        /* Add the new sample to the power estimate accumulator */
	ec->Ly_i += abs(iref);

	if (ec->Ly_i < DEFAULT_CUTOFF_I)
		ec->Ly_i = DEFAULT_CUTOFF_I;


	/* Update the Peak far-end receive signal detected */
        /* ----------------------------------------------- */
	if (ec->y_tilde_i > ec->max_y_tilde) {
		/* New highest y_tilde with full life */
		ec->max_y_tilde = ec->y_tilde_i;
		ec->max_y_tilde_pos = ec->N_d - 1;
	} else if (--ec->max_y_tilde_pos < 0) {
		/* Time to find new max y tilde... */
		ec->max_y_tilde = MAX16(ec->y_tilde_s.buf_d + ec->y_tilde_s.idx_d, ec->N_d, &ec->max_y_tilde_pos);
	}

	/* Determine if near end speech was detected in this sample */
	/* -------------------------------------------------------- */
	if (((ec->s_tilde_i >> (DEFAULT_ALPHA_ST_I - 1)) > ec->max_y_tilde) 
	    && (ec->max_y_tilde > 0))  {
		/* Then start the Hangover counter */
		ec->HCNTR_d = DEFAULT_HANGT;
#ifdef MEC2_STATS_DETAILED
		printk(KERN_INFO "Reset near end speech timer with: s_tilde_i %d, stmnt %d, max_y_tilde %d\n", ec->s_tilde_i, (ec->s_tilde_i >> (DEFAULT_ALPHA_ST_I - 1)), ec->max_y_tilde);
#endif
#ifdef MEC2_STATS
		++ec->cntr_nearend_speech_frames;
#endif
	} else if (ec->HCNTR_d > (int)0) {
  		/* otherwise, if it's still non-zero, decrement the Hangover counter by one sample */
#ifdef MEC2_STATS
		++ec->cntr_nearend_speech_frames;
#endif
		ec->HCNTR_d--;
	} 

	/* Update coefficients if no near-end speech in this sample (ie. HCNTR_d = 0)
	 * and we have enough signal to bother trying to update.
	 * --------------------------------------------------------------------------
	 */
	if (!ec->HCNTR_d && 				/* no near-end speech present */
		!(ec->i_d % DEFAULT_M)) {		/* we only update on every DEFAULM_M'th sample from the stream */
  			if (ec->Lu_i > MIN_UPDATE_THRESH_I) {	/* there is sufficient energy above the noise floor to contain meaningful data */
  							/* so loop over all the filter coefficients */
#ifdef MEC2_STATS_DETAILED
				printk( KERN_INFO "updating coefficients with: ec->Lu_i %9d\n", ec->Lu_i);
#endif
#ifdef MEC2_STATS
				ec->avg_Lu_i_ok = ec->avg_Lu_i_ok + ec->Lu_i;  
				++ec->cntr_coeff_updates;
#endif
				for (k=0; k < ec->N_d; k++) {
					/* eq. (7): compute an expectation over M_d samples */
					int grad2;
					grad2 = CONVOLVE2(ec->u_s.buf_d + ec->u_s.idx_d,
							  ec->y_s.buf_d + ec->y_s.idx_d + k,
							  DEFAULT_M);
					/* eq. (7): update the coefficient */
					ec->a_i[k] += grad2 / two_beta_i;
					ec->a_s[k] = ec->a_i[k] >> 16;
				}
		 	 } else { 
#ifdef MEC2_STATS_DETAILED
				printk( KERN_INFO "insufficient signal to update coefficients ec->Lu_i %5d < %5d\n", ec->Lu_i, MIN_UPDATE_THRESH_I);
#endif
#ifdef MEC2_STATS
				ec->avg_Lu_i_toolow = ec->avg_Lu_i_toolow + ec->Lu_i;  
				++ec->cntr_coeff_missedupdates;
#endif
			}
	}
  
	/* paragraph below eq. (15): if no near-end speech in the sample and 
	 * the reference signal power estimate > cutoff threshold
	 * then perform residual error suppression
	 */
#ifdef MEC2_STATS_DETAILED
	if (ec->HCNTR_d == 0)
		printk( KERN_INFO "possibily correcting frame with ec->Ly_i %9d ec->Lu_i %9d and expression %d\n", ec->Ly_i, ec->Lu_i, (ec->Ly_i/(ec->Lu_i + 1)));
#endif

#ifndef NO_ECHO_SUPPRESSOR
	if (ec->aggressive) {
		if ((ec->HCNTR_d < AGGRESSIVE_HCNTR) && (ec->Ly_i > (ec->Lu_i << 1))) {
			for (k=0; k < 2; k++) {
				u = u * (ec->Lu_i >> DEFAULT_SIGMA_LU_I) / ((ec->Ly_i >> (DEFAULT_SIGMA_LY_I)) + 1);
			}
#ifdef MEC2_STATS_DETAILED
			printk( KERN_INFO "aggresively correcting frame with ec->Ly_i %9d ec->Lu_i %9d expression %d\n", ec->Ly_i, ec->Lu_i, (ec->Ly_i/(ec->Lu_i + 1)));
#endif
#ifdef MEC2_STATS
			++ec->cntr_residualcorrected_frames;
#endif
		}
	} else {
		if (ec->HCNTR_d == 0) { 
			if ((ec->Ly_i/(ec->Lu_i + 1)) > DEFAULT_SUPPR_I) {
				for (k=0; k < 1; k++) {
					u = u * (ec->Lu_i >> DEFAULT_SIGMA_LU_I) / ((ec->Ly_i >> (DEFAULT_SIGMA_LY_I + 2)) + 1);
				}
#ifdef MEC2_STATS_DETAILED
				printk( KERN_INFO "correcting frame with ec->Ly_i %9d ec->Lu_i %9d expression %d\n", ec->Ly_i, ec->Lu_i, (ec->Ly_i/(ec->Lu_i + 1)));
#endif
#ifdef MEC2_STATS
				++ec->cntr_residualcorrected_frames;
#endif
			}
#ifdef MEC2_STATS
			else {
				++ec->cntr_residualcorrected_framesskipped;
			}
#endif
		}
	}
#endif  

#if 0
	/* This will generate a non-linear supression factor, once converted */
	if ((ec->HCNTR_d == 0) && 
	   ((ec->Lu_d/ec->Ly_d) < DEFAULT_SUPPR) &&
	    (ec->Lu_d/ec->Ly_d > EC_MIN_DB_VALUE)) { 
	    	suppr_factor = (10 / (float)(SUPPR_FLOOR - SUPPR_CEIL)) * log(ec->Lu_d/ec->Ly_d)
	    			- SUPPR_CEIL / (float)(SUPPR_FLOOR - SUPPR_CEIL);
		u_suppr = pow(10.0, (suppr_factor) * RES_SUPR_FACTOR / 10.0) * u_suppr;
	}
#endif  

#ifdef MEC2_STATS
	/* Periodically dump performance stats */
	if ((ec->i_d % MEC2_STATS) == 0) {
		/* make sure to avoid div0's! */
		if (ec->cntr_coeff_missedupdates > 0)
			ec->avg_Lu_i_toolow = (int)(ec->avg_Lu_i_toolow / ec->cntr_coeff_missedupdates);
		else
			ec->avg_Lu_i_toolow = -1;

		if (ec->cntr_coeff_updates > 0)
			ec->avg_Lu_i_ok = (ec->avg_Lu_i_ok / ec->cntr_coeff_updates);
		else
			ec->avg_Lu_i_ok = -1;

		printk( KERN_INFO "%d: Near end speech: %5d Residuals corrected/skipped: %5d/%5d Coefficients updated ok/low sig: %3d/%3d Lu_i avg ok/low sig %6d/%5d\n", 
			ec->id,
			ec->cntr_nearend_speech_frames, 
			ec->cntr_residualcorrected_frames, ec->cntr_residualcorrected_framesskipped, 
			ec->cntr_coeff_updates, ec->cntr_coeff_missedupdates, 
			ec->avg_Lu_i_ok, ec->avg_Lu_i_toolow);

		ec->cntr_nearend_speech_frames = 0;
		ec->cntr_residualcorrected_frames = 0;
		ec->cntr_residualcorrected_framesskipped = 0;
		ec->cntr_coeff_updates = 0;
		ec->cntr_coeff_missedupdates = 0;
		ec->avg_Lu_i_ok = 0;
		ec->avg_Lu_i_toolow = 0;
	}
#endif

	/* Increment the sample index and return the corrected sample */
	ec->i_d++;
	return u;
}

static void echo_can_update(struct echo_can_state *ec, short *iref, short *isig)
{
	unsigned int x;
	short result;

	for (x = 0; x < DAHDI_CHUNKSIZE; x++) {
		result = sample_update(ec, *iref, *isig);
		*isig++ = result;
		++iref;
	}
}

static int echo_can_create(struct dahdi_echocanparams *ecp, struct dahdi_echocanparam *p,
			   struct echo_can_state **ec)
{
	int maxy;
	int maxu;
	size_t size;
	unsigned int x;
	char *c;

	maxy = ecp->tap_length + DEFAULT_M;
	maxu = DEFAULT_M;
	if (maxy < (1 << DEFAULT_ALPHA_YT_I))
		maxy = (1 << DEFAULT_ALPHA_YT_I);
	if (maxy < (1 << DEFAULT_SIGMA_LY_I))
		maxy = (1 << DEFAULT_SIGMA_LY_I);
	if (maxu < (1 << DEFAULT_SIGMA_LU_I))
		maxu = (1 << DEFAULT_SIGMA_LU_I);

	size = sizeof(*ec) +
		4 + 						/* align */
		sizeof(int) * ecp->tap_length +			/* a_i */
		sizeof(short) * ecp->tap_length + 		/* a_s */
		2 * sizeof(short) * (maxy) +			/* y_s */
		2 * sizeof(short) * (1 << DEFAULT_ALPHA_ST_I) + /* s_s */
		2 * sizeof(short) * (maxu) +			/* u_s */
		2 * sizeof(short) * ecp->tap_length;		/* y_tilde_s */

	if (!(*ec = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;

	memset(*ec, 0, size);

	(*ec)->aggressive = aggressive;

	for (x = 0; x < ecp->param_count; x++) {
		for (c = p[x].name; *c; c++)
			*c = tolower(*c);
		if (!strcmp(p[x].name, "aggressive")) {
			(*ec)->aggressive = p[x].value ? 1 : 0;
		} else {
			printk(KERN_WARNING "Unknown parameter supplied to KB1 echo canceler: '%s'\n", p[x].name);
			kfree(*ec);

			return -EINVAL;
		}
	}

	init_cc(*ec, ecp->tap_length, maxy, maxu);
	
	return 0;
}

static int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	/* Set the hangover counter to the length of the can to 
	 * avoid adjustments occuring immediately after initial forced training 
	 */
	ec->HCNTR_d = ec->N_d << 1;

	if (pos >= ec->N_d)
		return 1;

	ec->a_i[pos] = val << 17;
	ec->a_s[pos] = val << 1;

	if (++pos >= ec->N_d)
		return 1;

	return 0;
}

static const struct dahdi_echocan me = {
	.name = "KB1",
	.owner = THIS_MODULE,
	.echo_can_create = echo_can_create,
	.echo_can_free = echo_can_free,
	.echo_can_array_update = echo_can_update,
	.echo_can_traintap = echo_can_traintap,
};

static int __init mod_init(void)
{
	if (dahdi_register_echocan(&me)) {
		module_printk(KERN_ERR, "could not register with DAHDI core\n");

		return -EPERM;
	}

	module_printk(KERN_NOTICE, "Registered echo canceler '%s'\n", me.name);

	return 0;
}

static void __exit mod_exit(void)
{
	dahdi_unregister_echocan(&me);
}

module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(aggressive, int, S_IRUGO | S_IWUSR);

MODULE_DESCRIPTION("DAHDI 'KB1' Echo Canceler");
MODULE_AUTHOR("Kris Boutilier");
MODULE_LICENSE("GPL v2");

module_init(mod_init);
module_exit(mod_exit);
