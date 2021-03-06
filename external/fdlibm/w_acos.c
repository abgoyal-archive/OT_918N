
/* @(#)w_acos.c 1.3 95/01/18 */


#include "fdlibm.h"


#ifdef __STDC__
	double ieee_acos(double x)		/* wrapper acos */
#else
	double ieee_acos(x)			/* wrapper acos */
	double x;
#endif
{
#ifdef _IEEE_LIBM
	return __ieee754_acos(x);
#else
	double z;
	z = __ieee754_acos(x);
	if(_LIB_VERSION == _IEEE_ || ieee_isnan(x)) return z;
	if(ieee_fabs(x)>1.0) {
	        return __kernel_standard(x,x,1); /* ieee_acos(|x|>1) */
	} else
	    return z;
#endif
}
