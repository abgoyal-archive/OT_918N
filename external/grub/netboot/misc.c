
/* Based on "src/misc.c" in etherboot-5.0.5.  */

#define GRUB	1
#include <etherboot.h>

void
sleep (int secs)
{
  unsigned long tmo = currticks () + secs;

  while (currticks () < tmo)
    ;
}

void
twiddle (void)
{
  static unsigned long lastticks = 0;
  static int count = 0;
  static const char tiddles[]="-\\|/";
  unsigned long ticks;

  if (debug)
    {
      if ((ticks = currticks ()) == lastticks)
	return;
      
      lastticks = ticks;
      grub_putchar (tiddles[(count++) & 3]);
      grub_putchar ('\b');
    }
}

static int
etherboot_vsprintf (char *buf, const char *fmt, const int *dp)
{
  char *p, *s;
  
  s = buf;
  for ( ; *fmt != '\0'; ++fmt)
    {
      if (*fmt != '%')
	{
	  buf ? *s++ = *fmt : grub_putchar (*fmt);
	  continue;
	}
      
      if (*++fmt == 's')
	{
	  for (p = (char *) *dp++; *p != '\0'; p++)
	    buf ? *s++ = *p : grub_putchar (*p);
	}
      else
	{
	  /* Length of item is bounded */
	  char tmp[20], *q = tmp;
	  int alt = 0;
	  int shift = 28;
	  
	  if (*fmt == '#')
	    {
	      alt = 1;
	      fmt++;
	    }
	  
	  if (*fmt == 'h')
	    {
	      shift = 12;
	      fmt++;
	    }
	  
	  if (*fmt == 'h')
	    {
	      shift = 4;
	      fmt++;
	    }
	  
	  /*
	   * Before each format q points to tmp buffer
	   * After each format q points past end of item
	   */
	  if ((*fmt | 0x20) == 'x')
	    {
	      /* With x86 gcc, sizeof(long) == sizeof(int) */
	      const long *lp = (const long *) dp;
	      long h = *lp++;
	      int ncase = (*fmt & 0x20);
	      
	      dp = (const int *) lp;
	      if (alt)
		{
		  *q++ = '0';
		  *q++ = 'X' | ncase;
		}
	      for (; shift >= 0; shift -= 4)
		*q++ = "0123456789ABCDEF"[(h >> shift) & 0xF] | ncase;
	    }
	  else if (*fmt == 'd')
	    {
	      int i = *dp++;
	      char *r;
	      
	      if (i < 0)
		{
		  *q++ = '-';
		  i = -i;
		}
	      
	      p = q;		/* save beginning of digits */
	      do
		{
		  *q++ = '0' + (i % 10);
		  i /= 10;
		}
	      while (i);
	      
	      /* reverse digits, stop in middle */
	      r = q;		/* don't alter q */
	      while (--r > p)
		{
		  i = *r;
		  *r = *p;
		  *p++ = i;
		}
	    }
	  else if (*fmt == '@')
	    {
	      unsigned char *r;
	      union
	      {
		long		l;
		unsigned char	c[4];
	      }
	      u;
	      const long *lp = (const long *) dp;
	      
	      u.l = *lp++;
	      dp = (const int *) lp;
	      
	      for (r = &u.c[0]; r < &u.c[4]; ++r)
		q += etherboot_sprintf (q, "%d.", *r);
	      
	      --q;
	    }
	  else if (*fmt == '!')
	    {
	      char *r;
	      p = (char *) *dp++;
	      
	      for (r = p + ETH_ALEN; p < r; ++p)
		q += etherboot_sprintf (q, "%hhX:", *p);
	      
	      --q;
	    }
	  else if (*fmt == 'c')
	    *q++ = *dp++;
	  else
	    *q++ = *fmt;
	  
	  /* now output the saved string */
	  for (p = tmp; p < q; ++p)
	    buf ? *s++ = *p : grub_putchar (*p);
	}
    }
  
  if (buf)
    *s = '\0';
  
  return (s - buf);
}

int
etherboot_sprintf (char *buf, const char *fmt, ...)
{
  return etherboot_vsprintf (buf, fmt, ((const int *) &fmt) + 1);
}

void
etherboot_printf (const char *fmt, ...)
{
  (void) etherboot_vsprintf (0, fmt, ((const int *) &fmt) + 1);
}

int
inet_aton (char *p, in_addr *addr)
{
  unsigned long ip = 0;
  int val;
  int i;
  
  for (i = 0; i < 4; i++)
    {
      val = getdec (&p);
      
      if (val < 0 || val > 255)
	return 0;
      
      if (i != 3 && *p++ != '.')
	return 0;
      
      ip = (ip << 8) | val;
    }

  addr->s_addr = htonl (ip);

  return 1;
}

int
getdec (char **ptr)
{
  char *p = *ptr;
  int ret = 0;
  
  if (*p < '0' || *p > '9')
    return -1;
  
  while (*p >= '0' && *p <= '9')
    {
      ret = ret * 10 + (*p - '0');
      p++;
    }
  
  *ptr = p;
  
  return ret;
}
