#include <tommath.h>
#ifdef BN_MP_READ_RADIX_C

/* read a string [ASCII] in a given radix */
int mp_read_radix (mp_int * a, const char *str, int radix)
{
  int     y, res, neg;
  char    ch;

  /* zero the digit bignum */
  mp_zero(a);

  /* make sure the radix is ok */
  if (radix < 2 || radix > 64) {
    return MP_VAL;
  }

  /* if the leading digit is a 
   * minus set the sign to negative. 
   */
  if (*str == '-') {
    ++str;
    neg = MP_NEG;
  } else {
    neg = MP_ZPOS;
  }

  /* set the integer to the default of zero */
  mp_zero (a);
  
  /* process each digit of the string */
  while (*str) {
    /* if the radix < 36 the conversion is case insensitive
     * this allows numbers like 1AB and 1ab to represent the same  value
     * [e.g. in hex]
     */
    ch = (char) ((radix < 36) ? toupper (*str) : *str);
    for (y = 0; y < 64; y++) {
      if (ch == mp_s_rmap[y]) {
         break;
      }
    }

    /* if the char was found in the map 
     * and is less than the given radix add it
     * to the number, otherwise exit the loop. 
     */
    if (y < radix) {
      if ((res = mp_mul_d (a, (mp_digit) radix, a)) != MP_OKAY) {
         return res;
      }
      if ((res = mp_add_d (a, (mp_digit) y, a)) != MP_OKAY) {
         return res;
      }
    } else {
      break;
    }
    ++str;
  }
  
  /* set the sign only if a != 0 */
  if (mp_iszero(a) != 1) {
     a->sign = neg;
  }
  return MP_OKAY;
}
#endif

/* $Source: /cvs/libtom/libtommath/bn_mp_read_radix.c,v $ */
/* $Revision: 1.4 $ */
/* $Date: 2006/03/31 14:18:44 $ */
